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
from typing import Callable, List, Optional, Sequence

from agent import (
    STATUS_INTERVAL_SECONDS,
    BleStatusTransport,
    CodexStatusAgent,
)
from app_state import (
    AgentSnapshot,
    CodexSnapshot,
    ConnectionState,
    Preferences,
    UserDefaultsPreferences,
)
from codex_adapter import CodexStateCollector
from task_alerts import TaskAlertTracker
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
from voice_input_crypto import load_key_from_keychain
from voice_input_protocol import (
    ACCESSIBILITY_REQUIRED,
    DRIVER_REQUIRED,
    INITIALIZATION_FAILED,
    RESTART_REQUIRED,
    TYPELESS_NOT_RUNNING,
    encode_ready,
    encode_unavailable,
)
from voice_session import VoiceSessionService, VoiceState
from voice_shortcut import (
    FnShortcutController,
    _is_accessibility_trusted,
    _is_typeless_running,
)


LOGGER = logging.getLogger(__name__)
DEFAULT_RETRY_DELAYS = (1.0, 2.0, 4.0, 8.0, 16.0, 30.0)
VOICE_TARGET_BPS = 8192
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


@dataclasses.dataclass
class _BleSessionContext:
    transport: object
    voice_key: Optional[bytes] = None
    voice_service: Optional[object] = None
    voice_failed: bool = False
    shutdown_task: Optional[asyncio.Task] = None


def _new_voice_nonce() -> bytes:
    nonce = bytes(8)
    while not any(nonce):
        nonce = secrets.token_bytes(8)
    return nonce


def _blackhole_receipt_present() -> bool:
    try:
        result = subprocess.run(
            ["/usr/sbin/pkgutil", "--pkg-info", BLACKHOLE_RECEIPT],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except OSError:
        return False
    return result.returncode == 0


def _create_voice_session(key: bytes) -> VoiceSessionService:
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
        if not _is_accessibility_trusted():
            return VoiceState.ACCESSIBILITY_REQUIRED
        if not _is_typeless_running():
            return VoiceState.TYPELESS_NOT_RUNNING
        return VoiceState.READY

    return VoiceSessionService(
        route,
        shortcut,
        key,
        readiness_checker=readiness,
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
    ):
        self._collector = collector
        self._transport_factory = transport_factory
        self._connection_enabled = connection_enabled
        self._status_interval = status_interval
        self._retry_delays = tuple(retry_delays) or (1.0,)
        self._disconnect_timeout = disconnect_timeout
        self._voice_key_loader = voice_key_loader or load_key_from_keychain
        self._voice_session_factory = voice_session_factory or _create_voice_session
        self._voice_nonce_factory = voice_nonce_factory or _new_voice_nonce
        self._state_lock = threading.Lock()
        self._snapshot = AgentSnapshot(
            ConnectionState.SCANNING
            if connection_enabled
            else ConnectionState.DISABLED,
            connection_enabled,
        )
        self._command_lock = threading.Lock()
        self._pending_commands: List[AgentCommand] = []
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._commands: Optional[asyncio.Queue] = None
        self._latest_status = {}
        self._status_ready: Optional[asyncio.Event] = None

    def snapshot(self) -> AgentSnapshot:
        with self._state_lock:
            return self._snapshot

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

    def _transition(self, state: ConnectionState, detail: str = "") -> None:
        with self._state_lock:
            previous = self._snapshot
            snapshot = AgentSnapshot(
                state,
                self._connection_enabled,
                detail,
                voice_state=previous.voice_state,
                voice_detail=previous.voice_detail,
                codex=previous.codex,
            )
            self._snapshot = snapshot
        if previous.state is not state or previous.detail != detail:
            LOGGER.info("Bluetooth state: %s%s", state.value, ": " + detail if detail else "")

    def _transition_voice(self, state: VoiceState, detail: str = "") -> None:
        detail = _redact_voice_detail(detail)
        with self._state_lock:
            previous = self._snapshot
            self._snapshot = AgentSnapshot(
                previous.state,
                previous.connection_enabled,
                previous.detail,
                voice_state=state,
                voice_detail=detail,
                codex=previous.codex,
            )
        if previous.voice_state is not state or previous.voice_detail != detail:
            LOGGER.info(
                "Voice state: %s%s",
                state.value,
                ": " + detail if detail else "",
            )

    async def run(self) -> None:
        loop = asyncio.get_running_loop()
        commands: asyncio.Queue = asyncio.Queue()
        self._status_ready = asyncio.Event()
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
            with self._command_lock:
                self._loop = None
                self._commands = None

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
                first_status = not self._latest_status
                codex = CodexSnapshot.from_status(status)
                with self._state_lock:
                    previous = self._snapshot
                    self._snapshot = AgentSnapshot(
                        previous.state,
                        previous.connection_enabled,
                        previous.detail,
                        voice_state=previous.voice_state,
                        voice_detail=previous.voice_detail,
                        codex=codex,
                    )
                self._latest_status = status
                if first_status:
                    LOGGER.info(
                        "Codex status ready: model=%s effort=%s",
                        codex.model or "--",
                        codex.effort or "--",
                    )
                if self._status_ready is not None:
                    self._status_ready.set()
            await asyncio.sleep(self._status_interval)

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
            self._transition(ConnectionState.CONNECTED)
            status_agent = CodexStatusAgent(self._collector, transport)
            await status_agent.sync_time()
            await self._prepare_voice(context)
            if self._status_ready is not None:
                await self._status_ready.wait()
            while transport.connected:
                failure = self._voice_failure(context)
                if failure is not None:
                    await self._isolate_voice_failure(context, failure)
                await status_agent.tick(status=dict(self._latest_status))
                failure = await self._wait_for_status_interval(
                    transport, ignore_voice_failure=context.voice_failed
                )
                if failure is not None:
                    await self._isolate_voice_failure(context, failure)
            return "disconnected"
        finally:
            voice_was_recording = (
                self.snapshot().voice_state is VoiceState.RECORDING
            )
            await self._await_session_shutdown(context, voice_was_recording)

    async def _prepare_voice(self, context: _BleSessionContext) -> None:
        transport = context.transport
        try:
            key = await asyncio.to_thread(self._voice_key_loader)
        except Exception as error:
            self._transition_voice(VoiceState.ERROR, str(error))
            return
        context.voice_key = key

        try:
            service = self._voice_session_factory(key)
        except Exception as error:
            await self._isolate_voice_failure(context, error)
            return
        context.voice_service = service

        try:
            state = await service.prepare()
        except Exception as error:
            await self._isolate_voice_failure(context, error)
            return

        self._transition_voice(state)
        if state is not VoiceState.READY:
            reason = _VOICE_UNAVAILABLE_REASONS.get(
                state, INITIALIZATION_FAILED
            )
            payload = self._encode_unavailable_payload(key, reason)
            await self._disable_voice(transport, payload)
            return

        async def handle_notification(payload: bytes) -> None:
            try:
                await service.handle(payload)
            except Exception as error:
                self._transition_voice(service.state, str(error))
                raise
            else:
                self._transition_voice(service.state)

        try:
            ready_payload = encode_ready(
                key, self._voice_nonce_factory(), VOICE_TARGET_BPS
            )
        except Exception as error:
            await self._isolate_voice_failure(context, error)
            return
        try:
            await transport.enable_voice(ready_payload, handle_notification)
        except Exception as error:
            await self._isolate_voice_failure(context, error)

    def _encode_unavailable_payload(self, key: bytes, reason: int):
        try:
            return encode_unavailable(
                key, self._voice_nonce_factory(), reason
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
        self, transport, ignore_voice_failure: bool = False
    ):
        failure = getattr(transport, "voice_failure", None)
        if failure is None or ignore_voice_failure:
            await asyncio.sleep(self._status_interval)
            return None
        sleep_task = asyncio.create_task(asyncio.sleep(self._status_interval))
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

    async def _isolate_voice_failure(
        self, context: _BleSessionContext, error: Exception
    ) -> None:
        context.voice_failed = True
        self._transition_voice(VoiceState.ERROR, str(error))
        unavailable_payload = None
        if context.voice_key is not None:
            unavailable_payload = self._encode_unavailable_payload(
                context.voice_key, INITIALIZATION_FAILED
            )
        await self._disable_voice(context.transport, unavailable_payload)
        if context.voice_service is not None:
            cleaned = await self._bounded_cleanup(
                context.voice_service.disconnect(),
                "Voice session cleanup",
            )
            if cleaned:
                context.voice_service = None

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
        unavailable_payload = None
        if context.voice_key is not None:
            unavailable_payload = self._encode_unavailable_payload(
                context.voice_key, INITIALIZATION_FAILED
            )

        has_service = context.voice_service is not None
        future_steps = 2 if has_service else 1
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
            cleaned = await self._cleanup_before_deadline(
                service.disconnect,
                "Voice session cleanup",
                deadline,
                2,
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

    async def _bounded_cleanup(self, awaitable, label: str) -> bool:
        try:
            await asyncio.wait_for(awaitable, timeout=self._disconnect_timeout)
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


async def _await_cancelled(task: asyncio.Task) -> None:
    try:
        await task
    except asyncio.CancelledError:
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
    ):
        self._preferences = preferences or UserDefaultsPreferences()
        self._collector = None
        if runtime is None:
            self._collector = CodexStateCollector(alert_tracker=TaskAlertTracker())
            runtime = AgentRuntime(
                collector=self._collector,
                connection_enabled=self._preferences.connection_enabled(),
            )
        self._runtime = runtime
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

    def stop(self, timeout: float = 5.0) -> bool:
        with self._stop_lock:
            thread = self._thread
            if thread is None or not thread.is_alive():
                return True
            self._runtime.submit(AgentCommand.STOP)
            thread.join(timeout)
            return not thread.is_alive()
