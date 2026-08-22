"""Serialized BLE lifecycle and the menu application's thread controller."""

from __future__ import annotations

import asyncio
import dataclasses
import enum
import logging
import re
import secrets
import subprocess
import threading
import time
from typing import Callable, List, Optional, Sequence

from agent import (
    STATUS_INTERVAL_SECONDS,
    BleStatusTransport,
    CodexStatusAgent,
    WatchDiscovery,
)
from agent_identity import AgentIdentityStore
from app_state import (
    AgentSnapshot,
    CodexSnapshot,
    ConnectionState,
    PairingState,
    Preferences,
    TraeSnapshot,
    UserDefaultsPreferences,
)
from codex_adapter import CodexStateCollector
from codex_task_composer import CodexTaskComposerClient
from combined_collector import CompositeStateCollector
from pairing_client import PairingClient, WatchCredential
from codex_submit import CodexSubmitController, MacOSCodexAccessibility
from input_action_protocol import (
    ActionReplayGuard,
    decode_action,
    encode_ready as encode_input_ready,
)
from task_alerts import TaskAlertTracker
from task_routes import TaskRouteRegistry, TaskRouteTarget, route_id_for_thread
from task_voice_submitter import TaskVoiceSubmitter
from trae_adapter import TraeStateCollector
from typeless_transcripts import TypelessTranscriptSource
from watch_credentials import WatchCredentialStore
from voice_audio import (
    VoiceAudioBridge,
    VoiceAudioDeviceNotFound,
    VoiceAudioRoute,
)
from voice_driver import (
    BLACKHOLE_DEVICE_NAME,
    BLACKHOLE_RECEIPT,
    DriverState,
    detect_driver_state,
)
from voice_input_protocol import (
    ACCESSIBILITY_REQUIRED,
    DRIVER_REQUIRED,
    INITIALIZATION_FAILED,
    RESTART_REQUIRED,
    TYPELESS_NOT_RUNNING,
    EXTERNAL_MICROPHONE_CAPABILITY,
    PUSH_TO_TALK_CAPABILITY,
    encode_ready,
    encode_unavailable,
)
from voice_session import (
    TaskVoiceSubmissionError,
    VoiceSessionService,
    VoiceState,
)
from voice_shortcut import (
    FnShortcutController,
    _is_accessibility_trusted,
    _is_typeless_running,
)


LOGGER = logging.getLogger(__name__)
DEFAULT_RETRY_DELAYS = (1.0, 2.0, 4.0, 8.0, 16.0, 30.0)
VOICE_TARGET_BPS = 8192
VOICE_KEY_EPOCH = 1
_VOICE_SECRET_HEX = re.compile(
    r"(?i)(?<![0-9a-f])[0-9a-f]{32,}(?![0-9a-f])"
)
_VOICE_SECRET_BASE64 = re.compile(
    r"(?<![A-Za-z0-9+/])[A-Za-z0-9+/]{22}==(?=$|[^A-Za-z0-9+/=])"
)

_VOICE_UNAVAILABLE_REASONS = {
    VoiceState.DRIVER_REQUIRED: DRIVER_REQUIRED,
    VoiceState.RESTART_REQUIRED: RESTART_REQUIRED,
    VoiceState.ACCESSIBILITY_REQUIRED: ACCESSIBILITY_REQUIRED,
    VoiceState.TYPELESS_NOT_RUNNING: TYPELESS_NOT_RUNNING,
    VoiceState.ERROR: INITIALIZATION_FAILED,
}


def _status_publication_view(status):
    return {
        key: value
        for key, value in status.items()
        if not str(key).endswith("_collected_at")
    }


class ActiveRekeyError(RuntimeError):
    """The watch confirmed a new epoch but active rekey did not complete."""


@dataclasses.dataclass(frozen=True)
class SecureCredentialRefresh:
    """Credential retained for voice plus its task-detail epoch confidence."""

    credential: WatchCredential
    verified: bool


@dataclasses.dataclass
class _BleSessionContext:
    transport: object
    credential: Optional[WatchCredential] = None
    voice_key: Optional[bytes] = None
    voice_key_epoch: int = VOICE_KEY_EPOCH
    voice_service: Optional[object] = None
    voice_failed: bool = False
    input_nonce: Optional[bytes] = None
    input_replay_guard: Optional[ActionReplayGuard] = None
    input_submit_controller: Optional[object] = None
    input_active: bool = False
    accessibility_available: bool = True
    shutdown_task: Optional[asyncio.Task] = None


@dataclasses.dataclass(frozen=True)
class VoiceSetupToken:
    revision: int


class SecureConnectionAuthorizer:
    """Authenticate a known watch or enroll an unknown watch before use."""

    def __init__(
        self,
        identity_store=None,
        credential_store=None,
        pairing_client=None,
    ):
        self._identity_store = identity_store or AgentIdentityStore()
        self._credential_store = credential_store or WatchCredentialStore()
        self._pairing_client = pairing_client or PairingClient(
            self._credential_store
        )
        self._state_lock = threading.Lock()
        self._current_watch_id: Optional[bytes] = None

    async def authorize(self, transport, publish) -> WatchCredential:
        identity = await _run_in_daemon_thread(
            self._identity_store.load_or_create
        )
        discovery = await transport.discover_watch()
        if (
            not isinstance(discovery, WatchDiscovery)
            or not isinstance(discovery.watch_id, bytes)
            or len(discovery.watch_id) != 16
            or not any(discovery.watch_id)
            or not isinstance(discovery.key_epoch, int)
            or isinstance(discovery.key_epoch, bool)
            or discovery.key_epoch <= 0
            or not isinstance(discovery.watch_identity_public, bytes)
            or len(discovery.watch_identity_public) != 33
            or not any(discovery.watch_identity_public)
        ):
            raise ValueError("watch discovery response is invalid")
        with self._state_lock:
            self._current_watch_id = discovery.watch_id
        watch_suffix = discovery.watch_id.hex()[-4:].upper()
        try:
            credential = await _run_in_daemon_thread(
                lambda: self._credential_store.load(discovery.watch_id)
            )
            if credential is None:
                publish(
                    PairingState.REQUESTING,
                    "",
                    watch_suffix,
                    identity.display_name,
                )
                presentation = await self._pairing_client.request_pairing(
                    transport,
                    identity.private_key,
                    identity.display_name,
                )
                if presentation.watch_id != discovery.watch_id:
                    raise ValueError("pairing response belongs to another watch")
                publish(
                    PairingState.AWAITING_CONFIRMATION,
                    presentation.verification_code,
                    watch_suffix,
                    identity.display_name,
                )
                result = await self._pairing_client.confirm_pairing(True)
                if not result.paired or result.credential is None:
                    raise PermissionError("pairing was rejected on watch")
                credential = result.credential
            else:
                if (
                    credential.watch_identity_public
                    != discovery.watch_identity_public
                ):
                    raise PermissionError(
                        "watch identity does not match Keychain"
                    )
                state = (
                    PairingState.REKEYING
                    if credential.key_epoch != discovery.key_epoch
                    else PairingState.AUTHENTICATING
                )
                publish(state, "", watch_suffix, identity.display_name)
                result = await self._pairing_client.authenticate(
                    transport,
                    identity.private_key,
                    credential,
                    identity.display_name,
                )
                if not result.authenticated:
                    raise PermissionError("Agent authentication was rejected")
                credential = result.credential

            if (
                credential.watch_id != discovery.watch_id
                or credential.key_epoch != discovery.key_epoch
                or credential.watch_identity_public
                != discovery.watch_identity_public
            ):
                raise ValueError("watch credential epoch is not current")
        except Exception:
            publish(
                PairingState.ERROR,
                "",
                watch_suffix,
                identity.display_name,
            )
            raise
        publish(
            PairingState.PAIRED,
            "",
            watch_suffix,
            identity.display_name,
        )
        return credential

    async def refresh_if_needed(
        self,
        transport,
        credential: WatchCredential,
        publish,
    ) -> SecureCredentialRefresh:
        watch_suffix = credential.watch_id.hex()[-4:].upper()
        try:
            discovery = await transport.discover_watch()
        except Exception as error:
            LOGGER.warning("Active watch epoch check failed: %s", error)
            return SecureCredentialRefresh(credential, verified=False)
        identity = await _run_in_daemon_thread(
            self._identity_store.load_or_create
        )
        try:
            if (
                not isinstance(discovery, WatchDiscovery)
                or discovery.watch_id != credential.watch_id
                or discovery.watch_identity_public
                != credential.watch_identity_public
                or not isinstance(discovery.key_epoch, int)
                or isinstance(discovery.key_epoch, bool)
                or discovery.key_epoch <= 0
            ):
                raise PermissionError(
                    "active watch identity does not match Keychain"
                )
            if discovery.key_epoch == credential.key_epoch:
                return SecureCredentialRefresh(credential, verified=True)
            if discovery.key_epoch < credential.key_epoch:
                raise PermissionError("watch credential epoch rolled back")
            publish(
                PairingState.REKEYING,
                "",
                watch_suffix,
                identity.display_name,
            )
            result = await self._pairing_client.authenticate(
                transport,
                identity.private_key,
                credential,
                identity.display_name,
            )
            updated = result.credential
            if (
                not result.authenticated
                or not result.rekeyed
                or updated.watch_id != credential.watch_id
                or updated.key_epoch != discovery.key_epoch
                or updated.watch_identity_public
                != credential.watch_identity_public
            ):
                raise PermissionError("active Agent rekey was rejected")
            publish(
                PairingState.PAIRED,
                "",
                watch_suffix,
                identity.display_name,
            )
            return SecureCredentialRefresh(updated, verified=True)
        except Exception as error:
            publish(
                PairingState.ERROR,
                "",
                watch_suffix,
                identity.display_name,
            )
            raise ActiveRekeyError(str(error)) from error

    def forget_current_watch(self) -> bool:
        with self._state_lock:
            watch_id = self._current_watch_id
        if watch_id is None:
            return False
        self._credential_store.forget(watch_id)
        return True


def _new_voice_nonce() -> bytes:
    nonce = bytes(8)
    while not any(nonce):
        nonce = secrets.token_bytes(8)
    return nonce


def _create_submit_controller() -> CodexSubmitController:
    return CodexSubmitController(MacOSCodexAccessibility())


def _blackhole_receipt_present() -> bool:
    try:
        result = subprocess.run(
            ["/usr/sbin/pkgutil", "--pkg-info", BLACKHOLE_RECEIPT],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=2.0,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


def _create_voice_session(
    key: bytes,
    key_epoch: int = VOICE_KEY_EPOCH,
    *,
    transcript_source=None,
    task_submitter=None,
) -> VoiceSessionService:
    bridge = VoiceAudioBridge()
    route = VoiceAudioRoute(bridge)
    shortcut = FnShortcutController()

    def readiness() -> VoiceState:
        try:
            bridge.find_device_uid(BLACKHOLE_DEVICE_NAME)
            device_visible = True
        except VoiceAudioDeviceNotFound:
            device_visible = False
        driver_state = detect_driver_state(
            _blackhole_receipt_present(), device_visible
        )
        if driver_state is DriverState.DRIVER_REQUIRED:
            return VoiceState.DRIVER_REQUIRED
        if driver_state is DriverState.RESTART_REQUIRED:
            return VoiceState.RESTART_REQUIRED
        return VoiceState.READY

    return VoiceSessionService(
        route,
        shortcut,
        key,
        key_epoch=key_epoch,
        readiness_checker=readiness,
        transcript_source=transcript_source,
        task_submitter=task_submitter,
    )


class AgentCommand(enum.Enum):
    ENABLE = "enable"
    DISABLE = "disable"
    RECONNECT = "reconnect"
    STOP = "stop"


class AgentRuntime:
    def __init__(
        self,
        collector,
        transport_factory: Callable[[], object] = BleStatusTransport,
        connection_enabled: bool = True,
        status_interval: float = STATUS_INTERVAL_SECONDS,
        retry_delays: Sequence[float] = DEFAULT_RETRY_DELAYS,
        disconnect_timeout: float = 2.0,
        voice_key_loader=None,
        voice_session_factory=None,
        voice_nonce_factory=None,
        secure_authorizer=None,
        input_nonce_factory=None,
        submit_controller_factory=None,
        accessibility_checker=None,
        typeless_checker=None,
        task_route_registry=None,
        app_server_client=None,
        transcript_source=None,
        task_submitter=None,
    ):
        self._collector = collector
        self._transport_factory = transport_factory
        self._connection_enabled = connection_enabled
        self._status_interval = status_interval
        self._retry_delays = tuple(retry_delays) or (1.0,)
        self._disconnect_timeout = disconnect_timeout
        # Kept only as an explicit test/development injection. Production must
        # receive the voice key from the authenticated watch credential.
        self._voice_key_loader = voice_key_loader
        self._voice_session_factory = voice_session_factory or _create_voice_session
        self._voice_nonce_factory = voice_nonce_factory or _new_voice_nonce
        self._secure_authorizer = secure_authorizer
        self._input_nonce_factory = input_nonce_factory or _new_voice_nonce
        self._submit_controller_factory = (
            submit_controller_factory or _create_submit_controller
        )
        self._accessibility_checker = (
            accessibility_checker or _is_accessibility_trusted
        )
        self._typeless_checker = typeless_checker or _is_typeless_running
        self._task_route_registry = task_route_registry or TaskRouteRegistry()
        self._app_server_client = (
            app_server_client or CodexTaskComposerClient()
        )
        self._transcript_source = transcript_source or TypelessTranscriptSource()
        self._task_submitter = task_submitter or TaskVoiceSubmitter(
            self._task_route_registry, self._app_server_client
        )
        self._state_lock = threading.Lock()
        self._snapshot = AgentSnapshot(
            ConnectionState.SCANNING
            if connection_enabled
            else ConnectionState.DISABLED,
            connection_enabled,
        )
        self._voice_revision = 0
        self._command_lock = threading.Lock()
        self._pending_commands: List[AgentCommand] = []
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._commands: Optional[asyncio.Queue] = None
        self._latest_status = {}
        self._latest_status_publication_view = None
        self._status_generation = 0
        self._status_ready: Optional[asyncio.Event] = None
        self._status_changed: Optional[asyncio.Event] = None
        self._voice_refresh_requested = threading.Event()

    def snapshot(self) -> AgentSnapshot:
        with self._state_lock:
            return self._snapshot

    def set_agent_name(self, display_name: str) -> None:
        with self._state_lock:
            self._snapshot = dataclasses.replace(
                self._snapshot,
                agent_name=display_name,
                changed_at=time.time(),
            )

    def submit(self, command: AgentCommand) -> None:
        with self._command_lock:
            loop = self._loop
            commands = self._commands
            if loop is None or commands is None:
                self._pending_commands.append(command)
                return
            try:
                loop.call_soon_threadsafe(commands.put_nowait, command)
            except RuntimeError:
                self._pending_commands.append(command)

    def refresh_voice_readiness(self) -> None:
        self._voice_refresh_requested.set()

    def begin_voice_setup(self) -> Optional[VoiceSetupToken]:
        with self._state_lock:
            if self._snapshot.voice_state is not VoiceState.DRIVER_REQUIRED:
                return None
            return VoiceSetupToken(self._voice_revision)

    def report_voice_setup_failure(
        self, token: VoiceSetupToken, detail: str
    ) -> bool:
        return self._compare_and_transition_voice(
            token,
            VoiceState.DRIVER_REQUIRED,
            _sanitize_voice_setup_detail(detail),
        )

    def report_voice_setup_result(
        self, token: VoiceSetupToken, state: DriverState
    ) -> bool:
        voice_state = {
            DriverState.READY: VoiceState.READY,
            DriverState.RESTART_REQUIRED: VoiceState.RESTART_REQUIRED,
        }.get(state)
        if voice_state is None:
            raise ValueError("setup result must be ready or restart required")
        return self._compare_and_transition_voice(token, voice_state)

    def _transition(self, state: ConnectionState, detail: str = "") -> None:
        with self._state_lock:
            previous = self._snapshot
            snapshot = dataclasses.replace(
                previous,
                state=state,
                connection_enabled=self._connection_enabled,
                detail=detail,
                changed_at=time.time(),
            )
            self._snapshot = snapshot
        if previous.state is not state or previous.detail != detail:
            LOGGER.info("Bluetooth state: %s%s", state.value, ": " + detail if detail else "")

    def _transition_voice(self, state: VoiceState, detail: str = "") -> None:
        detail = _redact_voice_detail(detail)
        with self._state_lock:
            previous = self._snapshot
            self._voice_revision += 1
            self._snapshot = dataclasses.replace(
                previous,
                voice_state=state,
                voice_detail=detail,
                changed_at=time.time(),
            )
        if previous.voice_state is not state or previous.voice_detail != detail:
            LOGGER.info(
                "Voice state: %s%s",
                state.value,
                ": " + detail if detail else "",
            )

    def _compare_and_transition_voice(
        self,
        token: VoiceSetupToken,
        state: VoiceState,
        detail: str = "",
    ) -> bool:
        detail = _redact_voice_detail(detail)
        with self._state_lock:
            previous = self._snapshot
            if (
                token.revision != self._voice_revision
                or previous.voice_state is not VoiceState.DRIVER_REQUIRED
            ):
                return False
            self._voice_revision += 1
            self._snapshot = dataclasses.replace(
                previous,
                voice_state=state,
                voice_detail=detail,
                changed_at=time.time(),
            )
        LOGGER.info(
            "Voice state: %s%s",
            state.value,
            ": " + detail if detail else "",
        )
        return True

    def _transition_pairing(
        self,
        state: PairingState,
        verification_code: str = "",
        watch_identity_suffix: str = "",
        agent_name: str = "",
    ) -> None:
        with self._state_lock:
            previous = self._snapshot
            self._snapshot = dataclasses.replace(
                previous,
                pairing_state=state,
                verification_code=(
                    verification_code
                    if len(verification_code) == 6
                    and verification_code.isascii()
                    and verification_code.isdigit()
                    else ""
                ),
                watch_identity_suffix=watch_identity_suffix,
                agent_name=agent_name or previous.agent_name,
                changed_at=time.time(),
            )

    async def run(self) -> None:
        loop = asyncio.get_running_loop()
        commands: asyncio.Queue = asyncio.Queue()
        self._status_ready = asyncio.Event()
        self._status_changed = asyncio.Event()
        collector_task = asyncio.create_task(self._collect_status())
        with self._command_lock:
            self._loop = loop
            self._commands = commands
            pending = self._pending_commands
            self._pending_commands = []
        for command in pending:
            commands.put_nowait(command)

        retry_index = 0
        try:
            while True:
                if not self._connection_enabled:
                    self._transition(ConnectionState.DISABLED)
                    command = await commands.get()
                    if command is AgentCommand.STOP:
                        return
                    if command in (AgentCommand.ENABLE, AgentCommand.RECONNECT):
                        self._connection_enabled = True
                        retry_index = 0
                    continue

                session_task = asyncio.create_task(self._run_session())
                command_task = asyncio.create_task(commands.get())
                done, _ = await asyncio.wait(
                    (session_task, command_task),
                    return_when=asyncio.FIRST_COMPLETED,
                )

                if command_task in done:
                    command = command_task.result()
                    if command in (
                        AgentCommand.ENABLE,
                        AgentCommand.DISABLE,
                        AgentCommand.RECONNECT,
                        AgentCommand.STOP,
                    ):
                        if self.snapshot().state in (
                            ConnectionState.CONNECTING,
                            ConnectionState.CONNECTED,
                        ):
                            self._transition(ConnectionState.DISCONNECTING)
                        session_task.cancel()
                        await _await_cancelled(session_task)

                    if command is AgentCommand.STOP:
                        return
                    if command is AgentCommand.DISABLE:
                        self._connection_enabled = False
                    elif command in (AgentCommand.ENABLE, AgentCommand.RECONNECT):
                        self._connection_enabled = True
                        retry_index = 0
                    continue

                command_task.cancel()
                await _await_cancelled(command_task)
                try:
                    result = session_task.result()
                    detail = (
                        "StopWatch-Codex not found"
                        if result == "not_found"
                        else "StopWatch-Codex disconnected"
                    )
                    self._transition(ConnectionState.WAITING, detail)
                except Exception as error:
                    state = _state_for_error(error)
                    self._transition(state, str(error))
                    if self.snapshot().pairing_state in (
                        PairingState.REQUESTING,
                        PairingState.AWAITING_CONFIRMATION,
                        PairingState.AUTHENTICATING,
                        PairingState.REKEYING,
                    ):
                        self._transition_pairing(PairingState.ERROR)
                    LOGGER.exception("BLE session failed")

                delay = self._retry_delays[min(retry_index, len(self._retry_delays) - 1)]
                try:
                    command = await asyncio.wait_for(commands.get(), timeout=delay)
                except asyncio.TimeoutError:
                    retry_index += 1
                    continue

                if command is AgentCommand.STOP:
                    return
                if command is AgentCommand.DISABLE:
                    self._connection_enabled = False
                elif command in (AgentCommand.ENABLE, AgentCommand.RECONNECT):
                    self._connection_enabled = True
                    retry_index = 0
        finally:
            collector_task.cancel()
            await _await_cancelled(collector_task)
            self._status_ready = None
            self._status_changed = None
            with self._command_lock:
                self._loop = None
                self._commands = None
            await self._app_server_client.close()

    async def _collect_status(self) -> None:
        while True:
            try:
                status = await asyncio.to_thread(self._collector.get_state)
                status = dict(status)
            except asyncio.CancelledError:
                raise
            except Exception:
                LOGGER.exception("Codex status collection failed")
            else:
                publication_view = _status_publication_view(status)
                first_status = self._latest_status_publication_view is None
                status_changed = (
                    first_status
                    or publication_view
                    != self._latest_status_publication_view
                )
                self._latest_status = status
                self._latest_status_publication_view = publication_view
                self._update_task_routes(status)
                if status_changed:
                    codex = CodexSnapshot.from_status(status)
                    trae = TraeSnapshot.from_status(status)
                    with self._state_lock:
                        previous = self._snapshot
                        self._snapshot = dataclasses.replace(
                            previous,
                            voice_state=previous.voice_state,
                            voice_detail=previous.voice_detail,
                            codex=codex,
                            trae=trae,
                            changed_at=time.time(),
                        )
                    self._status_generation += 1
                    if self._status_changed is not None:
                        self._status_changed.set()
                    if first_status:
                        LOGGER.info(
                            "Codex status ready: model=%s effort=%s",
                            codex.model or "--",
                            codex.effort or "--",
                        )
                if self._status_ready is not None:
                    self._status_ready.set()
            wait_for_change = getattr(self._collector, "wait_for_change", None)
            if wait_for_change is None:
                await asyncio.sleep(self._status_interval)
            else:
                try:
                    await asyncio.to_thread(
                        wait_for_change,
                        self._status_interval,
                    )
                except asyncio.CancelledError:
                    raise
                except Exception:
                    LOGGER.exception(
                        "Codex rollout watch failed; using polling fallback"
                    )
                    await asyncio.sleep(self._status_interval)

    def _update_task_routes(self, status) -> None:
        alerts = status.get("codex_alerts")
        revision = status.get("codex_alert_revision", 0)
        if isinstance(revision, bool) or not isinstance(revision, int) or revision < 0:
            revision = 0
        targets = []
        observed_routes = []
        if isinstance(alerts, list):
            for alert in alerts:
                if not isinstance(alert, dict):
                    continue
                route = alert.get("route")
                thread_id = alert.get("thread_id")
                state = alert.get("state")
                if isinstance(route, str):
                    observed_routes.append(route)
                if (
                    not isinstance(thread_id, str)
                    or not thread_id
                    or state not in ("pending", "running", "completed")
                ):
                    continue
                derived_route = route_id_for_thread(thread_id)
                if route != derived_route:
                    continue
                targets.append(
                    TaskRouteTarget(
                        route=derived_route,
                        thread_id=thread_id,
                        state=state,
                        expected_turn_id=(
                            alert.get("turn_id") if state == "running" else None
                        ),
                        revision=revision,
                    )
                )
        try:
            self._task_route_registry.update(
                targets, observed_routes=observed_routes
            )
        except ValueError:
            self._task_route_registry.update(())

    async def _run_session(self) -> str:
        context = _BleSessionContext(self._transport_factory())
        transport = context.transport
        self._transition(ConnectionState.SCANNING)
        try:
            connected = await transport.connect(
                lambda: self._transition(ConnectionState.CONNECTING)
            )
            if not connected:
                return "not_found"
            # The submitter and status publisher share this registry. Sessions
            # run serially, so resetting here gives each connection a clean
            # collision domain without rebuilding the submitter stack.
            self._task_route_registry.reset()
            self._update_task_routes(self._latest_status)
            if self._secure_authorizer is not None:
                credential = await self._secure_authorizer.authorize(
                    transport, self._transition_pairing
                )
                context.credential = credential
                context.voice_key = credential.master_key
                context.voice_key_epoch = credential.key_epoch
            self._transition(ConnectionState.CONNECTED)
            status_agent = CodexStatusAgent(
                self._collector,
                transport,
                detail_key=context.voice_key,
                detail_key_epoch=context.voice_key_epoch,
                task_route_registry=self._task_route_registry,
            )
            await status_agent.sync_time()
            await self._prepare_voice(context)
            if self._status_ready is not None:
                await self._status_ready.wait()
            task_details_enabled = context.voice_key is not None
            status_write_timed_out = False
            while transport.connected:
                if self._voice_refresh_requested.is_set():
                    self._voice_refresh_requested.clear()
                    await self._refresh_voice_readiness(context)
                failure = self._voice_failure(context)
                if failure is not None:
                    await self._isolate_voice_failure(context, failure)
                sent_generation = self._status_generation
                detail_credential = (
                    context.voice_key,
                    context.voice_key_epoch,
                )
                detail_safe = await self._refresh_secure_credential(context)
                if detail_safe is False:
                    status_agent.disable_detail_credential()
                    task_details_enabled = False
                elif (
                    not task_details_enabled
                    or detail_credential
                    != (context.voice_key, context.voice_key_epoch)
                ):
                    status_agent.update_detail_credential(
                        context.voice_key,
                        context.voice_key_epoch,
                    )
                    task_details_enabled = context.voice_key is not None
                try:
                    await status_agent.tick(status=dict(self._latest_status))
                except asyncio.TimeoutError:
                    if status_write_timed_out or not transport.connected:
                        raise
                    status_write_timed_out = True
                    LOGGER.warning(
                        "Status update timed out; keeping BLE session alive"
                    )
                else:
                    status_write_timed_out = False
                failure = await self._wait_for_status_interval(
                    transport,
                    ignore_voice_failure=context.voice_failed,
                    sent_generation=sent_generation,
                )
                if failure is not None:
                    await self._isolate_voice_failure(context, failure)
            return "disconnected"
        finally:
            voice_was_recording = (
                self.snapshot().voice_state is VoiceState.RECORDING
            )
            await self._await_session_shutdown(context, voice_was_recording)

    async def _refresh_secure_credential(
        self, context: _BleSessionContext
    ) -> bool:
        if self._secure_authorizer is None or context.credential is None:
            return True
        refresh = getattr(self._secure_authorizer, "refresh_if_needed", None)
        if refresh is None:
            return True
        try:
            refresh_result = await refresh(
                context.transport,
                context.credential,
                self._transition_pairing,
            )
        except ActiveRekeyError as error:
            if not context.voice_failed:
                await self._isolate_voice_failure(context, error)
            LOGGER.exception("Active Agent rekey failed; status remains active")
            return False
        except Exception as error:
            LOGGER.warning(
                "Active Agent epoch check unverified; task details paused: %s",
                error,
            )
            return False
        if isinstance(refresh_result, SecureCredentialRefresh):
            if not refresh_result.verified:
                return False
            updated = refresh_result.credential
        else:
            # Compatibility for injected test/development authorizers. The
            # production authorizer always returns SecureCredentialRefresh.
            updated = refresh_result
        if updated == context.credential:
            return True
        return await self._replace_voice_credential(context, updated)

    async def _replace_voice_credential(
        self,
        context: _BleSessionContext,
        credential: WatchCredential,
    ) -> bool:
        context.input_active = False
        await self._bounded_cleanup(
            context.transport.disable_input_actions(),
            "Input action rekey cleanup",
        )
        context.input_nonce = None
        context.input_replay_guard = None
        context.input_submit_controller = None
        retired_consumer = self._retire_voice_generation(context.transport)
        await self._disable_voice(context.transport, None)
        if context.voice_service is not None:
            cleaned = await self._cleanup_voice_service(
                context.voice_service,
                self._disconnect_timeout,
                "Voice session rekey cleanup",
            )
            if not cleaned:
                context.voice_failed = True
                self._transition_voice(
                    VoiceState.ERROR,
                    "Voice session could not restart after rekey",
                )
                return False
            context.voice_service = None
        if retired_consumer is not None:
            await self._bounded_cleanup(
                _await_voice_consumer(retired_consumer),
                "Voice rekey consumer cleanup",
            )
        context.credential = credential
        context.voice_key = credential.master_key
        context.voice_key_epoch = credential.key_epoch
        context.voice_failed = False
        await self._prepare_voice(context)
        return True

    async def _prepare_voice(self, context: _BleSessionContext) -> None:
        key = context.voice_key
        if key is None:
            if self._voice_key_loader is None:
                self._transition_voice(
                    VoiceState.ERROR,
                    "Secure watch credential is required",
                )
                return
            try:
                key = await _run_in_daemon_thread(self._voice_key_loader)
            except Exception as error:
                self._transition_voice(VoiceState.ERROR, str(error))
                return
            context.voice_key = key

        context.accessibility_available = self._accessibility_is_available()

        await self._prepare_input_actions(context)

        try:
            if self._secure_authorizer is None:
                try:
                    service = self._voice_session_factory(
                        key,
                        transcript_source=self._transcript_source,
                        task_submitter=self._task_submitter,
                    )
                except TypeError:
                    service = self._voice_session_factory(key)
            else:
                try:
                    service = self._voice_session_factory(
                        key,
                        key_epoch=context.voice_key_epoch,
                        transcript_source=self._transcript_source,
                        task_submitter=self._task_submitter,
                    )
                except TypeError:
                    service = self._voice_session_factory(
                        key, key_epoch=context.voice_key_epoch
                    )
        except Exception as error:
            await self._isolate_voice_failure(context, error)
            return
        context.voice_service = service

        await self._publish_voice_readiness(context)

    async def _prepare_input_actions(
        self, context: _BleSessionContext
    ) -> None:
        key = context.voice_key
        if key is None:
            return
        try:
            nonce = self._input_nonce_factory()
            ready_payload = encode_input_ready(key, nonce)
            replay_guard = ActionReplayGuard()
            replay_guard.set_active_nonce(nonce)
            controller = self._submit_controller_factory()
        except Exception:
            LOGGER.exception("Could not prepare authenticated input actions")
            return

        context.input_nonce = nonce
        context.input_replay_guard = replay_guard
        context.input_submit_controller = controller
        context.input_active = True

        async def handle_notification(payload: bytes) -> None:
            if not context.input_active:
                return
            try:
                packet = decode_action(key, payload, nonce)
            except Exception as error:
                LOGGER.warning("Ignoring invalid input action: %s", error)
                return
            try:
                accepted_sequence = replay_guard.accept(
                    nonce, packet.sequence
                )
            except Exception as error:
                LOGGER.warning("Ignoring invalid input sequence: %s", error)
                return
            if not accepted_sequence or not context.input_active:
                LOGGER.info(
                    "Ignoring replayed or stale input action sequence %d",
                    packet.sequence,
                )
                return
            context.accessibility_available = (
                self._accessibility_is_available()
            )
            if not context.accessibility_available:
                self._transition_voice(VoiceState.ACCESSIBILITY_REQUIRED)
                LOGGER.info(
                    "Ignoring input action: Accessibility permission is required"
                )
                return
            try:
                accepted = await asyncio.to_thread(
                    controller.handle, packet.action
                )
            except Exception:
                LOGGER.exception("Codex input action dispatch failed")
                return
            LOGGER.info(
                "Codex input action %s: %s",
                packet.action.name.lower(),
                "accepted" if accepted else "ignored",
            )

        try:
            await context.transport.enable_input_actions(
                ready_payload, handle_notification
            )
        except Exception:
            context.input_active = False
            context.input_nonce = None
            context.input_replay_guard = None
            context.input_submit_controller = None
            LOGGER.exception("Could not enable authenticated input actions")

    async def _refresh_voice_readiness(self, context: _BleSessionContext) -> None:
        service_state = getattr(
            context.voice_service,
            "state",
            self.snapshot().voice_state,
        )
        if (
            context.voice_failed
            or context.voice_key is None
            or context.voice_service is None
            or (
                service_state
                not in (
                    VoiceState.DRIVER_REQUIRED,
                    VoiceState.RESTART_REQUIRED,
                )
                and self.snapshot().voice_state not in (
                    VoiceState.ACCESSIBILITY_REQUIRED,
                    VoiceState.TYPELESS_NOT_RUNNING,
                )
            )
        ):
            return
        await self._publish_voice_readiness(context)

    async def _publish_voice_readiness(self, context: _BleSessionContext) -> None:
        transport = context.transport
        key = context.voice_key
        service = context.voice_service

        try:
            state = await service.prepare()
        except Exception as error:
            await self._isolate_voice_failure(context, error)
            return

        context.accessibility_available = self._accessibility_is_available()
        if state in (VoiceState.DRIVER_REQUIRED, VoiceState.RESTART_REQUIRED):
            public_state = (
                state
                if context.accessibility_available
                else VoiceState.ACCESSIBILITY_REQUIRED
            )
            self._transition_voice(public_state)
            reason = _VOICE_UNAVAILABLE_REASONS.get(
                public_state, INITIALIZATION_FAILED
            )
            payload = self._encode_unavailable_payload(
                key,
                reason,
                context.voice_key_epoch,
            )
            await self._disable_voice(transport, payload)
            return

        if state not in (
            VoiceState.READY,
            VoiceState.ACCESSIBILITY_REQUIRED,
            VoiceState.TYPELESS_NOT_RUNNING,
        ):
            self._transition_voice(state)
            payload = self._encode_unavailable_payload(
                key, INITIALIZATION_FAILED, context.voice_key_epoch
            )
            await self._disable_voice(transport, payload)
            return

        capabilities = EXTERNAL_MICROPHONE_CAPABILITY
        typeless_available = self._typeless_is_available()
        if (
            state is VoiceState.READY
            and context.accessibility_available
            and typeless_available
        ):
            capabilities |= PUSH_TO_TALK_CAPABILITY
            public_state = VoiceState.READY
        elif (
            state is VoiceState.ACCESSIBILITY_REQUIRED
            or not context.accessibility_available
        ):
            public_state = VoiceState.ACCESSIBILITY_REQUIRED
        else:
            public_state = VoiceState.TYPELESS_NOT_RUNNING
        self._transition_voice(public_state)

        async def handle_notification(payload: bytes) -> None:
            try:
                await service.handle(payload)
            except TaskVoiceSubmissionError:
                LOGGER.warning(
                    "Task voice submission failed; voice remains ready"
                )
                self._transition_voice(service.state)
            except Exception as error:
                self._transition_voice(service.state, str(error))
                raise
            else:
                self._transition_voice(service.state)

        try:
            ready_payload = encode_ready(
                key,
                self._voice_nonce_factory(),
                VOICE_TARGET_BPS,
                key_epoch=context.voice_key_epoch,
                capabilities=capabilities,
            )
        except Exception as error:
            await self._isolate_voice_failure(context, error)
            return
        try:
            await transport.enable_voice(ready_payload, handle_notification)
        except Exception as error:
            await self._isolate_voice_failure(context, error)

    def _accessibility_is_available(self) -> bool:
        try:
            return bool(self._accessibility_checker())
        except Exception:
            LOGGER.exception("Could not inspect Accessibility permission")
            return False

    def _typeless_is_available(self) -> bool:
        try:
            return bool(self._typeless_checker())
        except Exception:
            LOGGER.exception("Could not inspect Typeless process")
            return False

    def _encode_unavailable_payload(
        self,
        key: bytes,
        reason: int,
        key_epoch: int = VOICE_KEY_EPOCH,
    ):
        try:
            return encode_unavailable(
                key,
                self._voice_nonce_factory(),
                reason,
                key_epoch=key_epoch,
            )
        except Exception as error:
            self._transition_voice(VoiceState.ERROR, str(error))
            LOGGER.warning("Could not encode voice UNAVAILABLE control")
            return None

    async def _disable_voice(self, transport, unavailable_payload) -> None:
        cleaned = await self._bounded_cleanup(
            transport.disable_voice(unavailable_payload),
            "Voice notification cleanup",
        )
        if not cleaned:
            await self._bounded_cleanup(
                transport.disable_voice(None),
                "Voice notification stop",
            )

    @staticmethod
    def _voice_failure(context: _BleSessionContext):
        if context.voice_failed:
            return None
        failure = getattr(context.transport, "voice_failure", None)
        if failure is not None and failure.done():
            return failure.result()
        return None

    async def _wait_for_status_interval(
        self,
        transport,
        ignore_voice_failure: bool = False,
        sent_generation: Optional[int] = None,
    ):
        if sent_generation is not None and self._status_changed is not None:
            status_wait = self._wait_for_status_change(sent_generation)
        else:
            status_wait = asyncio.sleep(self._status_interval)
        failure = getattr(transport, "voice_failure", None)
        if failure is None or ignore_voice_failure:
            await status_wait
            return None
        sleep_task = asyncio.create_task(status_wait)
        try:
            done, _ = await asyncio.wait(
                (sleep_task, failure), return_when=asyncio.FIRST_COMPLETED
            )
            if failure in done:
                return failure.result()
            return None
        finally:
            if not sleep_task.done():
                sleep_task.cancel()
            await _await_cancelled(sleep_task)

    async def _wait_for_status_change(self, sent_generation: int) -> None:
        event = self._status_changed
        if event is None:
            await asyncio.sleep(self._status_interval)
            return
        deadline = asyncio.get_running_loop().time() + self._status_interval
        while self._status_generation <= sent_generation:
            event.clear()
            if self._status_generation > sent_generation:
                return
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                return
            timed_out = False

            def wake_for_timeout() -> None:
                nonlocal timed_out
                timed_out = True
                event.set()

            timeout_handle = asyncio.get_running_loop().call_later(
                remaining,
                wake_for_timeout,
            )
            try:
                await event.wait()
                if timed_out and self._status_generation <= sent_generation:
                    return
            finally:
                timeout_handle.cancel()

    async def _isolate_voice_failure(
        self, context: _BleSessionContext, error: Exception
    ) -> None:
        context.voice_failed = True
        self._transition_voice(VoiceState.ERROR, str(error))
        retired_consumer = self._retire_voice_generation(context.transport)
        unavailable_payload = None
        if context.voice_key is not None:
            unavailable_payload = self._encode_unavailable_payload(
                context.voice_key,
                INITIALIZATION_FAILED,
                context.voice_key_epoch,
            )
        await self._disable_voice(context.transport, unavailable_payload)
        if context.voice_service is not None:
            cleaned = await self._cleanup_voice_service(
                context.voice_service,
                self._disconnect_timeout,
                "Voice session cleanup",
            )
            if cleaned:
                context.voice_service = None
        if retired_consumer is not None:
            await self._bounded_cleanup(
                _await_voice_consumer(retired_consumer),
                "Voice consumer cleanup",
            )

    async def _await_session_shutdown(
        self, context: _BleSessionContext, voice_was_recording: bool
    ) -> None:
        if context.shutdown_task is None:
            context.shutdown_task = asyncio.create_task(
                self._shutdown_session(context, voice_was_recording)
            )
        interrupted = False
        while not context.shutdown_task.done():
            try:
                await asyncio.shield(context.shutdown_task)
            except asyncio.CancelledError:
                interrupted = True
        await context.shutdown_task
        if interrupted:
            raise asyncio.CancelledError

    async def _shutdown_session(
        self, context: _BleSessionContext, voice_was_recording: bool
    ) -> None:
        deadline = asyncio.get_running_loop().time() + self._disconnect_timeout
        context.input_active = False
        unavailable_payload = None
        if context.voice_key is not None:
            unavailable_payload = self._encode_unavailable_payload(
                context.voice_key,
                INITIALIZATION_FAILED,
                context.voice_key_epoch,
            )

        retired_consumer = self._retire_voice_generation(context.transport)
        has_service = context.voice_service is not None
        future_steps = (
            int(has_service) + int(retired_consumer is not None) + 2
        )
        await self._cleanup_before_deadline(
            context.transport.disable_input_actions,
            "Input action notification cleanup",
            deadline,
            future_steps + 1,
        )
        context.input_nonce = None
        context.input_replay_guard = None
        context.input_submit_controller = None
        voice_cleaned = await self._cleanup_before_deadline(
            lambda: context.transport.disable_voice(unavailable_payload),
            "Voice notification cleanup",
            deadline,
            future_steps + 1,
        )
        if not voice_cleaned:
            await self._cleanup_before_deadline(
                lambda: context.transport.disable_voice(None),
                "Voice notification stop",
                deadline,
                future_steps + 1,
            )

        if context.voice_service is not None:
            service = context.voice_service
            remaining = max(
                0.0, deadline - asyncio.get_running_loop().time()
            )
            cleaned = await self._cleanup_voice_service(
                service,
                remaining
                / (3 if retired_consumer is not None else 2),
                "Voice session cleanup",
            )
            if cleaned:
                context.voice_service = None
            if voice_was_recording:
                if cleaned:
                    self._transition_voice(service.state)
                else:
                    self._transition_voice(
                        VoiceState.ERROR,
                        "Voice session cleanup failed",
                    )

        if retired_consumer is not None:
            await self._cleanup_before_deadline(
                lambda: _await_voice_consumer(retired_consumer),
                "Voice consumer cleanup",
                deadline,
                2,
            )

        await self._cleanup_before_deadline(
            context.transport.disconnect,
            "Bluetooth disconnect",
            deadline,
            1,
        )

    async def _cleanup_before_deadline(
        self, operation, label: str, deadline: float, remaining_steps: int
    ) -> bool:
        try:
            task = asyncio.create_task(operation())
            await asyncio.sleep(0)
            if task.done():
                task.result()
                return True
            remaining = max(
                0.0, deadline - asyncio.get_running_loop().time()
            )
            timeout = remaining / max(1, remaining_steps)
            await asyncio.wait_for(task, timeout=timeout)
        except asyncio.TimeoutError:
            LOGGER.warning("%s timed out", label)
            return False
        except Exception:
            LOGGER.warning("%s failed", label)
            return False
        return True

    @staticmethod
    def _retire_voice_generation(transport):
        retire = getattr(transport, "retire_voice_generation", None)
        return retire() if retire is not None else None

    async def _cleanup_voice_service(
        self, service, timeout: float, label: str
    ) -> bool:
        bounded_disconnect = getattr(service, "disconnect_bounded", None)
        if bounded_disconnect is None:
            return await self._bounded_cleanup(
                service.disconnect(), label, timeout=timeout
            )
        try:
            cleaned = await bounded_disconnect(timeout)
        except asyncio.CancelledError:
            raise
        except Exception:
            LOGGER.warning("%s failed", label)
            return False
        if not cleaned:
            LOGGER.warning("%s timed out", label)
        return cleaned

    async def _bounded_cleanup(
        self, awaitable, label: str, timeout: Optional[float] = None
    ) -> bool:
        if timeout is None:
            timeout = self._disconnect_timeout
        try:
            await asyncio.wait_for(awaitable, timeout=timeout)
        except asyncio.TimeoutError:
            LOGGER.warning("%s timed out", label)
            return False
        except Exception:
            LOGGER.warning("%s failed", label)
            return False
        return True


def _redact_voice_detail(detail: str) -> str:
    text = str(detail)
    text = _VOICE_SECRET_HEX.sub("[redacted]", text)
    return _VOICE_SECRET_BASE64.sub("[redacted]", text)


def _sanitize_voice_setup_detail(detail: str) -> str:
    concise = " ".join(_redact_voice_detail(detail).split())
    return (concise or "Driver setup failed")[:120]


async def _run_in_daemon_thread(operation):
    loop = asyncio.get_running_loop()
    result_future = loop.create_future()
    abandoned = threading.Event()

    def deliver(succeeded, value) -> None:
        if result_future.done():
            return
        if succeeded:
            result_future.set_result(value)
        else:
            result_future.set_exception(value)

    def worker() -> None:
        try:
            value = operation()
        except Exception as error:
            outcome = (False, error)
        else:
            outcome = (True, value)
        if abandoned.is_set():
            return
        try:
            loop.call_soon_threadsafe(deliver, *outcome)
        except RuntimeError:
            pass

    threading.Thread(
        target=worker,
        name="voice-key-loader",
        daemon=True,
    ).start()
    try:
        return await result_future
    except asyncio.CancelledError:
        abandoned.set()
        result_future.cancel()
        raise


async def _await_cancelled(task: asyncio.Task) -> None:
    try:
        await task
    except asyncio.CancelledError:
        pass


async def _await_voice_consumer(task: asyncio.Task) -> None:
    try:
        await task
    except asyncio.CancelledError:
        pass
    except Exception:
        pass


def _state_for_error(error: Exception) -> ConnectionState:
    message = str(error).lower()
    if "not authorized" in message or "permission" in message:
        return ConnectionState.UNAUTHORIZED
    if "powered off" in message or "turned off" in message or "unavailable" in message:
        return ConnectionState.UNAVAILABLE
    return ConnectionState.ERROR


class AgentController:
    def __init__(
        self,
        preferences: Optional[Preferences] = None,
        runtime: Optional[AgentRuntime] = None,
        identity_store=None,
        credential_store=None,
        secure_authorizer=None,
    ):
        self._preferences = preferences or UserDefaultsPreferences()
        self._collector = None
        self._identity_store = identity_store
        self._secure_authorizer = secure_authorizer
        if runtime is None:
            self._identity_store = self._identity_store or AgentIdentityStore()
            credential_store = credential_store or WatchCredentialStore()
            self._secure_authorizer = self._secure_authorizer or (
                SecureConnectionAuthorizer(
                    identity_store=self._identity_store,
                    credential_store=credential_store,
                )
            )
            self._collector = CompositeStateCollector(
                CodexStateCollector(alert_tracker=TaskAlertTracker()),
                TraeStateCollector(),
            )
            runtime = AgentRuntime(
                collector=self._collector,
                connection_enabled=self._preferences.connection_enabled(),
                secure_authorizer=self._secure_authorizer,
            )
        self._runtime = runtime
        if self._identity_store is not None:
            try:
                self._runtime.set_agent_name(
                    self._identity_store.load_or_create().display_name
                )
            except Exception:
                LOGGER.warning("Agent name is unavailable from Keychain")
        self._thread: Optional[threading.Thread] = None
        self._stop_lock = threading.Lock()

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._thread = threading.Thread(
            target=self._thread_main,
            name="codex-agent",
            daemon=False,
        )
        self._thread.start()

    def _thread_main(self) -> None:
        asyncio.run(self._runtime.run())

    def snapshot(self) -> AgentSnapshot:
        return self._runtime.snapshot()

    def set_connection_enabled(self, enabled: bool) -> None:
        self._preferences.set_connection_enabled(enabled)
        self._runtime.submit(AgentCommand.ENABLE if enabled else AgentCommand.DISABLE)

    def reconnect(self) -> None:
        self._runtime.submit(AgentCommand.RECONNECT)

    def rename_agent(self, value: str) -> str:
        if self._identity_store is None:
            raise RuntimeError("Agent identity store is unavailable")
        display_name = self._identity_store.rename(value)
        self._runtime.set_agent_name(display_name)
        self._runtime.submit(AgentCommand.RECONNECT)
        return display_name

    def forget_current_watch(self) -> bool:
        if self._secure_authorizer is None:
            return False
        forgotten = self._secure_authorizer.forget_current_watch()
        if forgotten:
            self._runtime.submit(AgentCommand.RECONNECT)
        return forgotten

    def refresh_voice_readiness(self) -> None:
        self._runtime.refresh_voice_readiness()

    def begin_voice_setup(self) -> Optional[VoiceSetupToken]:
        return self._runtime.begin_voice_setup()

    def report_voice_setup_failure(
        self, token: VoiceSetupToken, detail: str
    ) -> bool:
        return self._runtime.report_voice_setup_failure(token, detail)

    def report_voice_setup_result(
        self, token: VoiceSetupToken, state: DriverState
    ) -> bool:
        return self._runtime.report_voice_setup_result(token, state)

    def stop(self, timeout: float = 5.0) -> bool:
        with self._stop_lock:
            thread = self._thread
            if thread is None or not thread.is_alive():
                return True
            self._runtime.submit(AgentCommand.STOP)
            thread.join(timeout)
            return not thread.is_alive()
