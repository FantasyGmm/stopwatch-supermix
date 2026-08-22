import asyncio
import dataclasses
import json
import pathlib
import sys
import threading
import time
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives.asymmetric import ec


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from app_state import ConnectionState, MemoryPreferences, PairingState
from agent import VOICE_CONTROL_UUID, BleStatusTransport, WatchDiscovery
from combined_collector import CompositeStateCollector
from pairing_client import (
    AuthenticationResult,
    PairingPresentation,
    PairingResult,
    WatchCredential,
)
from secure_pairing_crypto import public_key_bytes
from input_action_protocol import (
    InputAction,
    decode_ready as decode_input_ready,
    encode_action,
)
from protocol import EVENT_MESSAGE, STATUS_MESSAGE
from runtime import (
    ActiveRekeyError,
    VOICE_KEY_EPOCH,
    AgentCommand,
    AgentController,
    AgentRuntime,
    SecureCredentialRefresh,
    SecureConnectionAuthorizer,
    _BleSessionContext,
    _create_voice_session,
)
from pet_package import HEIGHT, WIDTH, PetFrame, encode_package
from pet_upload import (
    PetCapability,
    PetOperation,
    PetProgress,
    PetRequest,
    PetUnsupportedError,
    PetUploadError,
    PetWatchState,
)
from codex_task_composer import CodexTaskComposerClient
from task_routes import route_id_for_thread
from voice_audio import VoiceAudioDeviceNotFound, VoiceAudioError
from voice_driver import DriverState
from voice_input_crypto import VoiceKeyError
from voice_input_protocol import (
    ACCESSIBILITY_REQUIRED,
    BEGIN_BODY,
    DRIVER_REQUIRED,
    IMA_ADPCM,
    EXTERNAL_MICROPHONE_CAPABILITY,
    PUSH_TO_TALK,
    PTT_BEGIN,
    PTT_END,
    READY,
    RESTART_REQUIRED,
    TYPELESS_NOT_RUNNING,
    UNAVAILABLE,
    decode_control as _decode_control,
    pack_notification as _pack_notification,
    EndSummary,
)
from voice_session import VoiceSessionService as _VoiceSessionService, VoiceState


TEST_TASK_ROUTE = 0x0123456789ABCDEF


def decode_control(key, raw, *, expected_epoch=VOICE_KEY_EPOCH):
    return _decode_control(key, raw, expected_epoch=expected_epoch)


def pack_notification(
    key,
    kind,
    session,
    sequence,
    watch_ms,
    payload,
    session_nonce,
    *,
    key_epoch=VOICE_KEY_EPOCH,
):
    return _pack_notification(
        key,
        kind,
        session,
        sequence,
        watch_ms,
        payload,
        session_nonce,
        key_epoch=key_epoch,
    )


def VoiceSessionService(route, shortcut, key, **kwargs):
    kwargs.setdefault("key_epoch", VOICE_KEY_EPOCH)
    return _VoiceSessionService(route, shortcut, key, **kwargs)


class FakeCollector:
    def __init__(self, statuses=None):
        self.statuses = list(statuses or [{"codex_model": "gpt-5.3"}])
        self.calls = 0

    def get_state(self):
        self.calls += 1
        item = self.statuses.pop(0) if len(self.statuses) > 1 else self.statuses[0]
        if isinstance(item, Exception):
            raise item
        return dict(item)

    def pending_watch_notification_ids(self):
        return ()

    def mark_watch_notified(self, alert_ids):
        pass


class FakeTransport:
    def __init__(
        self,
        connect_result=True,
        connect_error=None,
        block_connect=False,
        block_disconnect=False,
        send_error=None,
        events=None,
        disconnect_after_voice=False,
        disconnect_after_messages=None,
        voice_enable_error=None,
        voice_failure_error=None,
        block_voice_disable=False,
        block_voice_enable=False,
        voice_notification=None,
        voice_notifications=None,
        input_enable_error=None,
        input_notifications=None,
        block_input_disable=False,
    ):
        self.connect_result = connect_result
        self.connect_error = connect_error
        self.block_connect = block_connect
        self.block_disconnect = block_disconnect
        self.send_error = send_error
        self.events = events
        self.disconnect_after_voice = disconnect_after_voice
        self.disconnect_after_messages = disconnect_after_messages
        self.voice_enable_error = voice_enable_error
        self.voice_failure_error = voice_failure_error
        self.block_voice_disable = block_voice_disable
        self.block_voice_enable = block_voice_enable
        self.voice_notification = voice_notification
        self.voice_notifications = list(voice_notifications or [])
        self.input_enable_error = input_enable_error
        self.input_notifications = list(input_notifications or [])
        self.block_input_disable = block_input_disable
        self.connect_started = asyncio.Event()
        self.connected_event = asyncio.Event()
        self.disconnected_event = asyncio.Event()
        self.voice_enable_started = asyncio.Event()
        self.voice_notification_handled = asyncio.Event()
        self.input_enable_started = asyncio.Event()
        self.input_notifications_handled = asyncio.Event()
        self._connect_release = asyncio.Event()
        self._connected = False
        self.disconnect_count = 0
        self.messages = []
        self.voice_failure = None
        self.voice_payloads = []
        self._voice_disabled = False
        self.input_payloads = []
        self.input_disable_count = 0
        self._input_disabled = True
        self.input_handler = None

    @property
    def connected(self):
        return self._connected

    async def connect(self, on_device_found=None):
        self.connect_started.set()
        if self.block_connect:
            await self._connect_release.wait()
        if self.connect_error:
            raise self.connect_error
        if not self.connect_result:
            return False
        if on_device_found:
            on_device_found()
        self._connected = True
        self.connected_event.set()
        return True

    async def disconnect(self):
        self.disconnect_count += 1
        self._connected = False
        self.disconnected_event.set()
        if self.events is not None:
            self.events.append("ble.disconnect")
        if self.block_disconnect:
            await asyncio.Event().wait()

    async def send_json(self, payload, message_type):
        if self.send_error is not None:
            raise self.send_error
        self.messages.append((message_type, payload))
        if (
            self.disconnect_after_messages is not None
            and len(self.messages) >= self.disconnect_after_messages
        ):
            self._connected = False

    async def enable_voice(self, ready_payload, handler):
        self.voice_enable_started.set()
        self._voice_disabled = False
        self.voice_payloads.append(("ready", ready_payload, handler))
        if self.events is not None:
            self.events.append("voice.enable")
        if self.voice_enable_error is not None:
            raise self.voice_enable_error
        if self.voice_failure_error is not None:
            self.voice_failure = asyncio.get_running_loop().create_future()
            self.voice_failure.set_result(self.voice_failure_error)
        if self.voice_notification is not None:
            await handler(self.voice_notification)
            self.voice_notification_handled.set()
        for notification in self.voice_notifications:
            await handler(notification)
        if self.voice_notifications:
            self.voice_notification_handled.set()
        if self.block_voice_enable:
            await asyncio.Event().wait()
        if self.disconnect_after_voice:
            self._connected = False

    async def disable_voice(self, unavailable_payload):
        if self._voice_disabled:
            return
        self._voice_disabled = True
        self.voice_payloads.append(("unavailable", unavailable_payload))
        if self.events is not None:
            self.events.append("voice.disable")
        if self.block_voice_disable:
            await asyncio.Event().wait()
        if self.disconnect_after_voice:
            self._connected = False

    async def enable_input_actions(self, ready_payload, handler):
        self.input_enable_started.set()
        self._input_disabled = False
        self.input_handler = handler
        self.input_payloads.append(("ready", ready_payload, handler))
        if self.events is not None:
            self.events.append("input.enable")
        if self.input_enable_error is not None:
            raise self.input_enable_error
        for payload in self.input_notifications:
            await handler(payload)
        if self.input_notifications:
            self.input_notifications_handled.set()

    async def disable_input_actions(self):
        if self._input_disabled:
            return
        self._input_disabled = True
        self.input_handler = None
        self.input_disable_count += 1
        self.input_payloads.append(("disabled",))
        if self.events is not None:
            self.events.append("input.disable")
        if self.block_input_disable:
            await asyncio.Event().wait()


class FakeTransportFactory:
    def __init__(self, configurations):
        self.configurations = list(configurations)
        self.instances = []

    def __call__(self):
        configuration = self.configurations.pop(0) if self.configurations else {}
        transport = FakeTransport(**configuration)
        self.instances.append(transport)
        return transport

    async def wait_for_instances(self, count):
        for _ in range(100):
            if len(self.instances) >= count:
                return
            await asyncio.sleep(0.01)
        raise AssertionError("transport count did not reach {}".format(count))


async def wait_for_state(runtime, expected):
    for _ in range(100):
        if runtime.snapshot().state is expected:
            return
        await asyncio.sleep(0.01)
    raise AssertionError(
        "state did not become {}; current={}".format(expected, runtime.snapshot())
    )


async def wait_for_codex_model(runtime, expected):
    for _ in range(100):
        if runtime.snapshot().codex.model == expected:
            return
        await asyncio.sleep(0.01)
    raise AssertionError(
        "Codex model did not become {}; current={}".format(
            expected, runtime.snapshot()
        )
    )


async def wait_for_trae_model(runtime, expected):
    for _ in range(100):
        if runtime.snapshot().trae.model == expected:
            return
        await asyncio.sleep(0.01)
    raise AssertionError(
        "Trae model did not become {}; current={}".format(
            expected, runtime.snapshot()
        )
    )


async def wait_for_messages(transport, count):
    for _ in range(100):
        if len(transport.messages) >= count:
            return
        await asyncio.sleep(0.01)
    raise AssertionError("message count did not reach {}".format(count))


class AgentRuntimeTests(unittest.IsolatedAsyncioTestCase):
    async def test_route_registry_is_reset_and_shared_at_session_start(self):
        captured = []

        class StatusAgentSpy:
            def __init__(self, collector, transport, **kwargs):
                captured.append(kwargs["task_route_registry"])
                self.transport = transport

            async def sync_time(self):
                pass

            async def tick(self, status=None):
                self.transport._connected = False

            def update_detail_credential(self, key, epoch):
                pass

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=FakeTransportFactory([{}]),
        )
        route = route_id_for_thread("thread-collision")
        runtime._task_route_registry.update(
            [], observed_routes=[route, route]
        )
        self.assertTrue(runtime._task_route_registry.is_tombstoned(route))
        current_thread = "thread-current-session"
        current_route = route_id_for_thread(current_thread)
        runtime._latest_status = {
            "codex_alert_revision": 12,
            "codex_alerts": [
                {
                    "route": current_route,
                    "thread_id": current_thread,
                    "state": "pending",
                }
            ],
        }

        with patch("runtime.CodexStatusAgent", StatusAgentSpy):
            self.assertEqual(await runtime._run_session(), "disconnected")

        self.assertEqual(captured, [runtime._task_route_registry])
        self.assertIs(
            runtime._task_submitter._registry,
            runtime._task_route_registry,
        )
        self.assertFalse(runtime._task_route_registry.is_tombstoned(route))
        self.assertEqual(
            runtime._task_submitter.snapshot(int(current_route, 16)).thread_id,
            current_thread,
        )

    async def test_single_status_write_timeout_keeps_voice_session_alive(self):
        key = bytes(range(16))

        class TimeoutOnceTransport(FakeTransport):
            def __init__(self):
                super().__init__()
                self.send_attempts = 0

            async def send_json(self, payload, message_type):
                self.send_attempts += 1
                if self.send_attempts == 2:
                    raise asyncio.TimeoutError()
                await super().send_json(payload, message_type)
                if self.send_attempts == 3:
                    self._connected = False

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                pass

        transport = TimeoutOnceTransport()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=lambda: transport,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"12345678",
            status_interval=0.01,
        )

        result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(transport.send_attempts, 3)
        self.assertIs(runtime.snapshot().voice_state, VoiceState.READY)

    async def test_production_voice_uses_the_active_codex_task_composer(self):
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transcript_source=object(),
        )

        self.assertIsInstance(
            runtime._app_server_client, CodexTaskComposerClient
        )

    async def test_latest_internal_alerts_refresh_shared_voice_routes(self):
        class Client:
            async def close(self):
                pass

        runtime = AgentRuntime(
            collector=FakeCollector(),
            app_server_client=Client(),
            transcript_source=object(),
        )
        thread_id = "thread-selected"
        runtime._update_task_routes(
            {
                "codex_alert_revision": 8,
                "codex_alerts": [
                    {
                        "route": route_id_for_thread(thread_id),
                        "thread_id": thread_id,
                        "state": "running",
                        "turn_id": "turn-active",
                    }
                ],
            }
        )

        target = runtime._task_submitter.snapshot(
            int(route_id_for_thread(thread_id), 16)
        )
        self.assertEqual(target.thread_id, thread_id)
        self.assertEqual(target.expected_turn_id, "turn-active")
        self.assertEqual(target.revision, 8)

    async def test_completed_alert_remains_available_for_inflight_voice_revalidation(self):
        class Client:
            async def close(self):
                pass

        runtime = AgentRuntime(
            collector=FakeCollector(),
            app_server_client=Client(),
            transcript_source=object(),
        )
        thread_id = "thread-just-completed"
        route = route_id_for_thread(thread_id)
        runtime._update_task_routes(
            {
                "codex_alert_revision": 9,
                "codex_alerts": [
                    {
                        "route": route,
                        "thread_id": thread_id,
                        "state": "completed",
                        "turn_id": "turn-complete",
                    }
                ],
            }
        )

        target = runtime._task_route_registry.resolve(route)
        self.assertIsNotNone(target)
        self.assertEqual(target.state, "completed")
        self.assertIsNone(target.expected_turn_id)

    async def test_authenticated_credential_is_injected_into_status_agent(self):
        credential = WatchCredential(
            bytes(range(16)),
            b"K" * 16,
            9,
            b"F" * 16,
            b"\x02" + b"W" * 32,
        )

        class Authorizer:
            async def authorize(self, transport, publish):
                return credential

        captured = []

        class StatusAgentSpy:
            def __init__(self, collector, transport, **kwargs):
                captured.append(kwargs)
                self.transport = transport

            async def sync_time(self):
                pass

            async def tick(self, status=None):
                self.transport._connected = False

        voice_service = type(
            "VoiceSession",
            (),
            {
                "state": VoiceState.ERROR,
                "prepare": lambda self: asyncio.sleep(0, result=VoiceState.ERROR),
                "disconnect": lambda self: asyncio.sleep(0),
            },
        )()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=FakeTransportFactory([{}]),
            secure_authorizer=Authorizer(),
            voice_session_factory=lambda key, key_epoch: voice_service,
        )

        with patch("runtime.CodexStatusAgent", StatusAgentSpy):
            result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(
            captured,
            [
                {
                    "detail_key": credential.master_key,
                    "detail_key_epoch": 9,
                    "task_route_registry": runtime._task_route_registry,
                }
            ],
        )

    async def test_active_rekey_rotates_status_agent_detail_credential(self):
        original = WatchCredential(
            bytes(range(16)),
            b"K" * 16,
            9,
            b"F" * 16,
            b"\x02" + b"W" * 32,
        )
        updated = WatchCredential(
            original.watch_id,
            b"N" * 16,
            10,
            original.fingerprint,
            original.watch_identity_public,
        )

        class Authorizer:
            async def authorize(self, transport, publish):
                return original

        rotations = []

        class StatusAgentSpy:
            def __init__(self, collector, transport, **kwargs):
                self.transport = transport

            async def sync_time(self):
                pass

            async def tick(self, status=None):
                pass

            def update_detail_credential(self, key, epoch):
                rotations.append((key, epoch))

        class RekeyRuntime(AgentRuntime):
            async def _refresh_secure_credential(self, context):
                context.credential = updated
                context.voice_key = updated.master_key
                context.voice_key_epoch = updated.key_epoch
                context.transport._connected = False
                return True

        voice_service = type(
            "VoiceSession",
            (),
            {
                "state": VoiceState.ERROR,
                "prepare": lambda self: asyncio.sleep(0, result=VoiceState.ERROR),
                "disconnect": lambda self: asyncio.sleep(0),
            },
        )()
        runtime = RekeyRuntime(
            collector=FakeCollector(),
            transport_factory=FakeTransportFactory([{}]),
            secure_authorizer=Authorizer(),
            voice_session_factory=lambda key, key_epoch: voice_service,
            status_interval=0,
        )

        with patch("runtime.CodexStatusAgent", StatusAgentSpy):
            result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(rotations, [(updated.master_key, updated.key_epoch)])

    async def test_secure_credential_refresh_precedes_every_status_tick(self):
        credential = WatchCredential(
            bytes(range(16)), b"K" * 16, 9, b"F" * 16, b"\x02" + b"W" * 32
        )
        events = []

        class Authorizer:
            async def authorize(self, transport, publish):
                return credential

            async def refresh_if_needed(self, transport, current, publish):
                events.append("refresh")
                return current

        class StatusAgentSpy:
            def __init__(self, collector, transport, **kwargs):
                self.transport = transport
                self.detail_enabled = True

            async def sync_time(self):
                pass

            async def tick(self, status=None):
                events.append("detail" if self.detail_enabled else "compact")
                self.transport._connected = False

            def update_detail_credential(self, key, epoch):
                events.append("rotate")

            def disable_detail_credential(self):
                events.append("disable")
                self.detail_enabled = False

        voice_service = type(
            "VoiceSession",
            (),
            {
                "state": VoiceState.ERROR,
                "prepare": lambda self: asyncio.sleep(0, result=VoiceState.ERROR),
                "disconnect": lambda self: asyncio.sleep(0),
            },
        )()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=FakeTransportFactory([{}]),
            secure_authorizer=Authorizer(),
            voice_session_factory=lambda key, key_epoch: voice_service,
            status_interval=0,
        )

        with patch("runtime.CodexStatusAgent", StatusAgentSpy):
            await runtime._run_session()

        self.assertEqual(events[:2], ["refresh", "detail"])

    async def test_rekey_cleanup_failure_disables_detail_before_next_tick(self):
        original = WatchCredential(
            bytes(range(16)), b"K" * 16, 9, b"F" * 16, b"\x02" + b"W" * 32
        )
        updated = WatchCredential(
            original.watch_id,
            b"N" * 16,
            10,
            original.fingerprint,
            original.watch_identity_public,
        )
        events = []

        class Authorizer:
            async def authorize(self, transport, publish):
                return original

            async def refresh_if_needed(self, transport, current, publish):
                return updated

        class StatusAgentSpy:
            def __init__(self, collector, transport, **kwargs):
                self.transport = transport
                self.detail_enabled = True

            async def sync_time(self):
                pass

            async def tick(self, status=None):
                events.append("detail" if self.detail_enabled else "compact")
                self.transport._connected = False

            def update_detail_credential(self, key, epoch):
                events.append(("rotate", key, epoch))

            def disable_detail_credential(self):
                events.append("disable")
                self.detail_enabled = False

        class VoiceService:
            state = VoiceState.ERROR

            async def prepare(self):
                return VoiceState.ERROR

            async def disconnect_bounded(self, timeout):
                return False

            async def disconnect(self):
                pass

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=FakeTransportFactory([{}]),
            secure_authorizer=Authorizer(),
            voice_session_factory=lambda key, key_epoch: VoiceService(),
            status_interval=0,
        )

        with patch("runtime.CodexStatusAgent", StatusAgentSpy):
            await runtime._run_session()

        self.assertEqual(events, ["disable", "compact"])

    async def test_active_rekey_failure_disables_detail_before_next_tick(self):
        credential = WatchCredential(
            bytes(range(16)), b"K" * 16, 9, b"F" * 16, b"\x02" + b"W" * 32
        )
        events = []

        class Authorizer:
            async def authorize(self, transport, publish):
                return credential

            async def refresh_if_needed(self, transport, current, publish):
                raise ActiveRekeyError("retired")

        class StatusAgentSpy:
            def __init__(self, collector, transport, **kwargs):
                self.transport = transport
                self.detail_enabled = True

            async def sync_time(self):
                pass

            async def tick(self, status=None):
                events.append("detail" if self.detail_enabled else "compact")
                self.transport._connected = False

            def update_detail_credential(self, key, epoch):
                events.append("rotate")

            def disable_detail_credential(self):
                events.append("disable")
                self.detail_enabled = False

        voice_service = type(
            "VoiceSession",
            (),
            {
                "state": VoiceState.ERROR,
                "prepare": lambda self: asyncio.sleep(0, result=VoiceState.ERROR),
                "disconnect": lambda self: asyncio.sleep(0),
            },
        )()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=FakeTransportFactory([{}]),
            secure_authorizer=Authorizer(),
            voice_session_factory=lambda key, key_epoch: voice_service,
            status_interval=0,
        )

        with patch("runtime.CodexStatusAgent", StatusAgentSpy):
            await runtime._run_session()

        self.assertEqual(events, ["disable", "compact"])

    async def test_uncertain_ticks_suppress_details_until_verified_unchanged(self):
        credential = WatchCredential(
            bytes(range(16)), b"K" * 16, 9, b"F" * 16, b"\x02" + b"W" * 32
        )
        refreshes = iter(
            (
                SecureCredentialRefresh(credential, verified=False),
                RuntimeError("temporary discovery failure"),
                SecureCredentialRefresh(credential, verified=True),
            )
        )
        events = []

        class Authorizer:
            async def authorize(self, transport, publish):
                return credential

            async def refresh_if_needed(self, transport, current, publish):
                result = next(refreshes)
                if isinstance(result, Exception):
                    raise result
                return result

        class StatusAgentSpy:
            def __init__(self, collector, transport, **kwargs):
                self.transport = transport
                self.detail_enabled = True

            async def sync_time(self):
                pass

            async def tick(self, status=None):
                events.append("detail" if self.detail_enabled else "compact")
                ticks = [
                    item
                    for item in events
                    if item in ("detail", "compact")
                ]
                if len(ticks) == 3:
                    self.transport._connected = False

            def update_detail_credential(self, key, epoch):
                events.append(("rotate", key, epoch))
                self.detail_enabled = True

            def disable_detail_credential(self):
                events.append("disable")
                self.detail_enabled = False

        voice_service = type(
            "VoiceSession",
            (),
            {
                "state": VoiceState.ERROR,
                "prepare": lambda self: asyncio.sleep(0, result=VoiceState.ERROR),
                "disconnect": lambda self: asyncio.sleep(0),
            },
        )()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=FakeTransportFactory([{}]),
            secure_authorizer=Authorizer(),
            voice_session_factory=lambda key, key_epoch: voice_service,
            status_interval=0,
        )

        with patch("runtime.CodexStatusAgent", StatusAgentSpy):
            await runtime._run_session()

        self.assertEqual(
            events,
            [
                "disable",
                "compact",
                "disable",
                "compact",
                ("rotate", credential.master_key, credential.key_epoch),
                "detail",
            ],
        )

    async def test_rollout_change_publishes_before_long_heartbeat(self):
        class ChangeCollector(FakeCollector):
            def __init__(self):
                super().__init__(
                    [
                        {"codex_alert_revision": 1, "codex_alert_pending": 0},
                        {"codex_alert_revision": 2, "codex_alert_pending": 1},
                    ]
                )
                self.changed = threading.Event()

            def wait_for_change(self, timeout):
                changed = self.changed.wait(min(timeout, 0.05))
                self.changed.clear()
                return changed

        collector = ChangeCollector()
        factory = FakeTransportFactory([{}])
        runtime = AgentRuntime(
            collector=collector,
            transport_factory=factory,
            status_interval=30.0,
            retry_delays=(0.01,),
            disconnect_timeout=0.1,
        )
        task = asyncio.create_task(runtime.run())
        try:
            await factory.wait_for_instances(1)
            transport = factory.instances[0]
            await wait_for_messages(transport, 2)
            started = asyncio.get_running_loop().time()
            collector.changed.set()
            published = False
            for _ in range(50):
                status_messages = [
                    json.loads(payload)
                    for message_type, payload in transport.messages
                    if message_type == STATUS_MESSAGE
                ]
                if len(status_messages) >= 2:
                    published = True
                    break
                await asyncio.sleep(0.01)

            self.assertTrue(published)
            self.assertLess(asyncio.get_running_loop().time() - started, 1.0)
            self.assertEqual(status_messages[-1]["codex_alert_revision"], 2)
        finally:
            collector.changed.set()
            runtime.submit(AgentCommand.STOP)
            await asyncio.wait_for(task, 1)

    async def test_unchanged_rollout_wakeup_does_not_publish_extra_status(self):
        class CodexSource(FakeCollector):
            def __init__(self):
                super().__init__([{"codex_alert_revision": 1}])
                self.changed = threading.Event()

            def wait_for_change(self, timeout):
                changed = self.changed.wait(min(timeout, 0.05))
                self.changed.clear()
                return changed

        codex = CodexSource()
        trae = FakeCollector([{"trae_online": False}])
        collector = CompositeStateCollector(
            codex,
            trae,
            now=time.monotonic,
        )
        factory = FakeTransportFactory([{}])
        runtime = AgentRuntime(
            collector=collector,
            transport_factory=factory,
            status_interval=30.0,
            retry_delays=(0.01,),
            disconnect_timeout=0.1,
        )
        task = asyncio.create_task(runtime.run())
        try:
            await factory.wait_for_instances(1)
            transport = factory.instances[0]
            await wait_for_messages(transport, 2)
            codex.changed.set()
            for _ in range(100):
                if codex.calls >= 2:
                    break
                await asyncio.sleep(0.01)
            else:
                self.fail("unchanged status was not recollected")
            await asyncio.sleep(0.05)

            status_messages = [
                payload
                for message_type, payload in transport.messages
                if message_type == STATUS_MESSAGE
                and json.loads(payload).get("source") != "trae"
            ]
            self.assertEqual(len(status_messages), 1)
        finally:
            codex.changed.set()
            runtime.submit(AgentCommand.STOP)
            await asyncio.wait_for(task, 1)

    async def test_watcher_error_falls_back_without_stopping_collection(self):
        class FailingWaitCollector(FakeCollector):
            def __init__(self):
                super().__init__([{"codex_alert_revision": 1}])
                self.wait_calls = 0

            def wait_for_change(self, _timeout):
                self.wait_calls += 1
                if self.wait_calls == 1:
                    raise OSError("kqueue failed")
                time.sleep(0.01)
                return False

        collector = FailingWaitCollector()
        runtime = AgentRuntime(
            collector=collector,
            status_interval=0.01,
        )
        task = asyncio.create_task(runtime._collect_status())
        try:
            for _ in range(100):
                if collector.calls >= 2:
                    break
                await asyncio.sleep(0.01)
            else:
                self.fail("collection did not recover from the watcher error")
            self.assertFalse(task.done())
        finally:
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task

    async def test_changes_during_status_write_publish_latest_revision_serially(self):
        class BurstCollector(FakeCollector):
            def __init__(self):
                super().__init__(
                    [
                        {"codex_alert_revision": 1},
                        {"codex_alert_revision": 2},
                        {"codex_alert_revision": 3},
                    ]
                )
                self.changed = threading.Event()

            def wait_for_change(self, timeout):
                changed = self.changed.wait(min(timeout, 0.05))
                self.changed.clear()
                return changed

        class BlockingTransport(FakeTransport):
            def __init__(self):
                super().__init__()
                self.revision_two_started = asyncio.Event()
                self.release_revision_two = asyncio.Event()
                self.active_status_writes = 0
                self.max_active_status_writes = 0

            async def send_json(self, payload, message_type):
                if message_type != STATUS_MESSAGE:
                    await super().send_json(payload, message_type)
                    return
                self.active_status_writes += 1
                self.max_active_status_writes = max(
                    self.max_active_status_writes,
                    self.active_status_writes,
                )
                try:
                    status = json.loads(payload)
                    if status["codex_alert_revision"] == 2:
                        self.revision_two_started.set()
                        await self.release_revision_two.wait()
                    await super().send_json(payload, message_type)
                finally:
                    self.active_status_writes -= 1

        collector = BurstCollector()
        transport = BlockingTransport()
        runtime = AgentRuntime(
            collector=collector,
            transport_factory=lambda: transport,
            status_interval=30.0,
            retry_delays=(0.01,),
            disconnect_timeout=0.1,
        )
        task = asyncio.create_task(runtime.run())
        try:
            await wait_for_messages(transport, 2)
            collector.changed.set()
            await asyncio.wait_for(transport.revision_two_started.wait(), 1)
            collector.changed.set()
            for _ in range(100):
                if runtime.snapshot().codex.alert_revision == 3:
                    break
                await asyncio.sleep(0.01)
            else:
                self.fail("latest status was not collected during the write")
            transport.release_revision_two.set()
            for _ in range(100):
                revisions = [
                    json.loads(payload)["codex_alert_revision"]
                    for message_type, payload in transport.messages
                    if message_type == STATUS_MESSAGE
                ]
                if revisions[-1:] == [3]:
                    break
                await asyncio.sleep(0.01)
            else:
                self.fail("latest status was not published after the write")

            self.assertEqual(revisions, [1, 2, 3])
            self.assertEqual(transport.max_active_status_writes, 1)
        finally:
            collector.changed.set()
            transport.release_revision_two.set()
            runtime.submit(AgentCommand.STOP)
            await asyncio.wait_for(task, 1)

    async def test_keychain_load_failure_publishes_forgettable_pairing_error(self):
        watch_id = bytes(range(16))
        watch_identity_public = public_key_bytes(
            ec.derive_private_key(23, ec.SECP256R1()).public_key()
        )

        class IdentityStore:
            def load_or_create(self):
                return type(
                    "Identity",
                    (),
                    {
                        "private_key": ec.derive_private_key(
                            17, ec.SECP256R1()
                        ),
                        "display_name": "Desk Agent",
                    },
                )()

        class CredentialStore:
            def load(self, requested_watch_id):
                self.requested_watch_id = requested_watch_id
                raise ValueError("corrupt Keychain credential")

        class Transport:
            async def discover_watch(self):
                return WatchDiscovery(watch_id, 3, watch_identity_public)

        states = []
        authorizer = SecureConnectionAuthorizer(
            identity_store=IdentityStore(),
            credential_store=CredentialStore(),
        )

        with self.assertRaisesRegex(ValueError, "corrupt Keychain"):
            await authorizer.authorize(
                Transport(), lambda *state: states.append(state)
            )

        self.assertEqual(
            states[-1],
            (PairingState.ERROR, "", "0E0F", "Desk Agent"),
        )

    async def test_watch_identity_mismatch_publishes_forgettable_pairing_error(self):
        watch_id = bytes(range(16))
        discovered_identity = public_key_bytes(
            ec.derive_private_key(23, ec.SECP256R1()).public_key()
        )
        stored_identity = public_key_bytes(
            ec.derive_private_key(29, ec.SECP256R1()).public_key()
        )

        class IdentityStore:
            def load_or_create(self):
                return type(
                    "Identity",
                    (),
                    {
                        "private_key": ec.derive_private_key(
                            17, ec.SECP256R1()
                        ),
                        "display_name": "Desk Agent",
                    },
                )()

        class CredentialStore:
            def load(self, requested_watch_id):
                return WatchCredential(
                    requested_watch_id,
                    b"K" * 16,
                    3,
                    b"F" * 16,
                    stored_identity,
                )

        class Transport:
            async def discover_watch(self):
                return WatchDiscovery(watch_id, 3, discovered_identity)

        states = []
        authorizer = SecureConnectionAuthorizer(
            identity_store=IdentityStore(),
            credential_store=CredentialStore(),
        )

        with self.assertRaisesRegex(PermissionError, "does not match"):
            await authorizer.authorize(
                Transport(), lambda *state: states.append(state)
            )

        self.assertEqual(
            states[-1],
            (PairingState.ERROR, "", "0E0F", "Desk Agent"),
        )

    async def test_unknown_watch_enrolls_and_publishes_verification_code(self):
        identity_key = ec.derive_private_key(17, ec.SECP256R1())
        identity_public = public_key_bytes(identity_key.public_key())
        fingerprint = __import__("hashlib").sha256(identity_public).digest()[:16]
        watch_id = bytes(range(16))
        watch_identity_public = public_key_bytes(
            ec.derive_private_key(23, ec.SECP256R1()).public_key()
        )
        credential = WatchCredential(
            watch_id=watch_id,
            master_key=b"K" * 16,
            key_epoch=3,
            fingerprint=fingerprint,
            watch_identity_public=watch_identity_public,
        )
        events = []

        class IdentityStore:
            def load_or_create(self):
                return type(
                    "Identity",
                    (),
                    {
                        "private_key": identity_key,
                        "display_name": "Desk Agent",
                    },
                )()

        class CredentialStore:
            def load(self, requested_watch_id):
                self.requested_watch_id = requested_watch_id
                return None

        class Client:
            async def request_pairing(self, transport, identity, display_name):
                events.append(("request", transport, identity, display_name))
                return PairingPresentation(19, watch_id, "482731")

            async def confirm_pairing(self, accepted):
                events.append(("confirm", accepted))
                return PairingResult(True, credential)

        class Transport:
            async def discover_watch(self):
                return WatchDiscovery(watch_id, 3, watch_identity_public)

        states = []
        authorizer = SecureConnectionAuthorizer(
            identity_store=IdentityStore(),
            credential_store=CredentialStore(),
            pairing_client=Client(),
        )

        result = await authorizer.authorize(
            Transport(), lambda *state: states.append(state)
        )

        self.assertEqual(result, credential)
        self.assertEqual(events[0][-1], "Desk Agent")
        self.assertEqual(events[-1], ("confirm", True))
        self.assertIn(
            (PairingState.AWAITING_CONFIRMATION, "482731", "0E0F", "Desk Agent"),
            states,
        )
        self.assertEqual(states[-1][0], PairingState.PAIRED)

    async def test_known_watch_authenticates_and_accepts_new_epoch(self):
        watch_id = bytes(range(16))
        watch_identity_public = public_key_bytes(
            ec.derive_private_key(29, ec.SECP256R1()).public_key()
        )
        old = WatchCredential(
            watch_id, b"A" * 16, 2, b"F" * 16, watch_identity_public
        )
        updated = WatchCredential(
            watch_id, b"B" * 16, 5, b"F" * 16, watch_identity_public
        )

        class IdentityStore:
            def load_or_create(self):
                return type(
                    "Identity",
                    (),
                    {"private_key": object(), "display_name": "Travel Agent"},
                )()

        class CredentialStore:
            def load(self, requested_watch_id):
                self.requested_watch_id = requested_watch_id
                return old

        class Client:
            async def authenticate(
                self, transport, identity, credential, display_name
            ):
                self.arguments = (
                    transport,
                    identity,
                    credential,
                    display_name,
                )
                return AuthenticationResult(True, True, updated)

        class Transport:
            async def discover_watch(self):
                return WatchDiscovery(watch_id, 5, watch_identity_public)

        client = Client()
        states = []
        authorizer = SecureConnectionAuthorizer(
            identity_store=IdentityStore(),
            credential_store=CredentialStore(),
            pairing_client=client,
        )

        result = await authorizer.authorize(
            Transport(), lambda *state: states.append(state)
        )

        self.assertEqual(result, updated)
        self.assertEqual(client.arguments[2], old)
        self.assertEqual(client.arguments[3], "Travel Agent")
        self.assertEqual(states[0][0], PairingState.REKEYING)
        self.assertEqual(states[-1][0], PairingState.PAIRED)

    async def test_active_watch_refreshes_rotated_credential_in_place(self):
        identity_key = ec.derive_private_key(17, ec.SECP256R1())
        watch_id = bytes(range(16))
        watch_identity_public = public_key_bytes(
            ec.derive_private_key(29, ec.SECP256R1()).public_key()
        )
        old = WatchCredential(
            watch_id, b"A" * 16, 2, b"F" * 16, watch_identity_public
        )
        updated = WatchCredential(
            watch_id, b"B" * 16, 5, b"F" * 16, watch_identity_public
        )

        class IdentityStore:
            def load_or_create(self):
                return type(
                    "Identity",
                    (),
                    {
                        "private_key": identity_key,
                        "display_name": "Desk Agent",
                    },
                )()

        class Client:
            async def authenticate(
                self, transport, identity, credential, display_name
            ):
                self.arguments = (
                    transport,
                    identity,
                    credential,
                    display_name,
                )
                return AuthenticationResult(True, True, updated)

        class Transport:
            async def discover_watch(self):
                return WatchDiscovery(watch_id, 5, watch_identity_public)

        client = Client()
        states = []
        authorizer = SecureConnectionAuthorizer(
            identity_store=IdentityStore(), pairing_client=client
        )

        result = await authorizer.refresh_if_needed(
            Transport(), old, lambda *state: states.append(state)
        )

        self.assertEqual(result.credential, updated)
        self.assertTrue(result.verified)
        self.assertEqual(client.arguments[2], old)
        self.assertEqual(states[0][0], PairingState.REKEYING)
        self.assertEqual(states[-1][0], PairingState.PAIRED)

    async def test_transient_active_epoch_check_keeps_existing_credential(self):
        watch_identity_public = public_key_bytes(
            ec.derive_private_key(29, ec.SECP256R1()).public_key()
        )
        credential = WatchCredential(
            bytes(range(16)),
            b"A" * 16,
            2,
            b"F" * 16,
            watch_identity_public,
        )

        class Transport:
            async def discover_watch(self):
                raise asyncio.TimeoutError("temporary GATT timeout")

        states = []
        authorizer = SecureConnectionAuthorizer()

        result = await authorizer.refresh_if_needed(
            Transport(), credential, lambda *state: states.append(state)
        )

        self.assertEqual(result.credential, credential)
        self.assertFalse(result.verified)
        self.assertEqual(states, [])

    async def test_runtime_does_not_open_production_surface_before_authorization(self):
        release = asyncio.Event()
        credential = WatchCredential(
            bytes(range(16)),
            b"K" * 16,
            9,
            b"F" * 16,
            b"\x02" + b"W" * 32,
        )

        class Authorizer:
            async def authorize(self, transport, publish):
                publish(PairingState.AUTHENTICATING, "", "0E0F", "Desk Agent")
                await release.wait()
                return credential

        factory = FakeTransportFactory([{"disconnect_after_messages": 2}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            secure_authorizer=Authorizer(),
            voice_session_factory=lambda key, key_epoch: type(
                "VoiceSession",
                (),
                {
                    "state": VoiceState.ERROR,
                    "prepare": lambda self: asyncio.sleep(0, result=VoiceState.ERROR),
                    "disconnect": lambda self: asyncio.sleep(0),
                },
            )(),
            status_interval=0.01,
        )
        session = asyncio.create_task(runtime._run_session())
        await factory.wait_for_instances(1)
        await asyncio.sleep(0)

        self.assertEqual(factory.instances[0].messages, [])
        self.assertEqual(factory.instances[0].voice_payloads, [])
        self.assertIs(runtime.snapshot().pairing_state, PairingState.AUTHENTICATING)

        release.set()
        result = await asyncio.wait_for(session, 1)
        self.assertEqual(result, "disconnected")
        self.assertEqual(
            [message_type for message_type, _ in factory.instances[0].messages],
            [EVENT_MESSAGE, STATUS_MESSAGE],
        )
        self.assertEqual(runtime.snapshot().agent_name, "Desk Agent")
        self.assertEqual(runtime.snapshot().watch_identity_suffix, "0E0F")

    async def test_authorization_failure_exposes_pairing_recovery_actions(self):
        class Authorizer:
            async def authorize(self, transport, publish):
                publish(
                    PairingState.AUTHENTICATING,
                    "",
                    "0E0F",
                    "Desk Agent",
                )
                raise PermissionError("Agent authentication was rejected")

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=FakeTransportFactory([{}]),
            secure_authorizer=Authorizer(),
            status_interval=0.01,
            retry_delays=(1.0,),
        )
        task = asyncio.create_task(runtime.run())
        for _ in range(100):
            if runtime.snapshot().pairing_state is PairingState.ERROR:
                break
            await asyncio.sleep(0.01)

        self.assertIs(runtime.snapshot().pairing_state, PairingState.ERROR)

        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_collects_trae_snapshot_independently(self):
        collector = FakeCollector(
            [
                {
                    "codex_model": "gpt-menu",
                    "trae_online": True,
                    "trae_model": "Model A",
                    "trae_effort": "high",
                    "trae_fresh": True,
                    "trae_tasks": [
                        {
                            "project": "StopWatch",
                            "title": "Status page",
                            "detail": "Collecting state",
                            "state": "running",
                        }
                    ],
                }
            ]
        )
        runtime = AgentRuntime(
            collector=collector,
            transport_factory=FakeTransportFactory([]),
            connection_enabled=False,
            status_interval=0.01,
            retry_delays=(0.01,),
        )

        task = asyncio.create_task(runtime.run())
        await wait_for_trae_model(runtime, "Model A")

        self.assertEqual(runtime.snapshot().codex.model, "gpt-menu")
        self.assertEqual(runtime.snapshot().trae.project, "StopWatch")
        self.assertEqual(runtime.snapshot().trae.task_state, "running")
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_setup_failure_publishes_sanitized_error_without_changing_ble(self):
        runtime = AgentRuntime(
            collector=FakeCollector(),
            connection_enabled=True,
        )
        runtime._transition(ConnectionState.CONNECTED, "watch connected")
        runtime._transition_voice(VoiceState.DRIVER_REQUIRED)
        token = runtime.begin_voice_setup()
        secret = "0123456789abcdef0123456789abcdef"
        worker = threading.Thread(
            target=lambda: runtime.report_voice_setup_failure(
                token,
                "  Authorization\nfailed {} {}".format(secret, "detail " * 40)
            )
        )

        worker.start()
        worker.join(1)

        self.assertFalse(worker.is_alive())
        snapshot = runtime.snapshot()
        self.assertIs(snapshot.state, ConnectionState.CONNECTED)
        self.assertTrue(snapshot.connection_enabled)
        self.assertEqual(snapshot.detail, "watch connected")
        self.assertIs(snapshot.voice_state, VoiceState.DRIVER_REQUIRED)
        self.assertNotIn(secret, snapshot.voice_detail)
        self.assertNotIn("\n", snapshot.voice_detail)
        self.assertIn("[redacted]", snapshot.voice_detail)
        self.assertLessEqual(len(snapshot.voice_detail), 120)
        self.assertIsNotNone(runtime.begin_voice_setup())

    async def test_setup_success_publishes_immediate_driver_state(self):
        restart_runtime = AgentRuntime(collector=FakeCollector())
        restart_runtime._transition_voice(VoiceState.DRIVER_REQUIRED)
        restart_token = restart_runtime.begin_voice_setup()
        ready_runtime = AgentRuntime(collector=FakeCollector())
        ready_runtime._transition_voice(VoiceState.DRIVER_REQUIRED)
        ready_token = ready_runtime.begin_voice_setup()

        restart_runtime.report_voice_setup_result(
            restart_token,
            DriverState.RESTART_REQUIRED,
        )
        restart_snapshot = restart_runtime.snapshot()
        ready_runtime.report_voice_setup_result(ready_token, DriverState.READY)
        ready_snapshot = ready_runtime.snapshot()

        self.assertIs(restart_snapshot.voice_state, VoiceState.RESTART_REQUIRED)
        self.assertEqual(restart_snapshot.voice_detail, "")
        self.assertIs(ready_snapshot.voice_state, VoiceState.READY)
        self.assertEqual(ready_snapshot.voice_detail, "")

    async def test_setup_result_cannot_overwrite_reconnect_voice_state(self):
        runtime = AgentRuntime(collector=FakeCollector())
        runtime._transition_voice(VoiceState.DRIVER_REQUIRED)
        token = runtime.begin_voice_setup()

        runtime._transition_voice(VoiceState.ACCESSIBILITY_REQUIRED)
        applied = runtime.report_voice_setup_result(token, DriverState.READY)

        self.assertFalse(applied)
        self.assertIs(
            runtime.snapshot().voice_state,
            VoiceState.ACCESSIBILITY_REQUIRED,
        )

    async def test_setup_failure_cannot_overwrite_reconnect_voice_state(self):
        runtime = AgentRuntime(collector=FakeCollector())
        runtime._transition_voice(VoiceState.DRIVER_REQUIRED)
        token = runtime.begin_voice_setup()

        runtime._transition_voice(VoiceState.RECORDING)
        applied = runtime.report_voice_setup_failure(
            token,
            "Driver installation was cancelled or failed",
        )

        self.assertFalse(applied)
        snapshot = runtime.snapshot()
        self.assertIs(snapshot.voice_state, VoiceState.RECORDING)
        self.assertEqual(snapshot.voice_detail, "")

    async def test_setup_result_cannot_overwrite_new_driver_generation(self):
        runtime = AgentRuntime(collector=FakeCollector())
        runtime._transition_voice(VoiceState.DRIVER_REQUIRED)
        token = runtime.begin_voice_setup()

        runtime._transition_voice(VoiceState.DRIVER_REQUIRED)
        applied = runtime.report_voice_setup_result(
            token,
            DriverState.RESTART_REQUIRED,
        )

        self.assertFalse(applied)
        self.assertIs(runtime.snapshot().voice_state, VoiceState.DRIVER_REQUIRED)

    async def test_readiness_refresh_enables_voice_without_restarting_ble(self):
        factory = FakeTransportFactory([{}])

        class VoiceSession:
            def __init__(self):
                self.prepare_count = 0
                self.state = VoiceState.DRIVER_REQUIRED

            async def prepare(self):
                self.prepare_count += 1
                self.state = (
                    VoiceState.DRIVER_REQUIRED
                    if self.prepare_count == 1
                    else VoiceState.READY
                )
                return self.state

            async def disconnect(self):
                self.state = VoiceState.READY

            async def handle(self, _payload):
                pass

        service = VoiceSession()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: service,
            voice_nonce_factory=lambda: b"12345678",
            status_interval=0.01,
        )
        task = asyncio.create_task(runtime.run())
        await wait_for_state(runtime, ConnectionState.CONNECTED)
        for _ in range(100):
            if runtime.snapshot().voice_state is VoiceState.DRIVER_REQUIRED:
                break
            await asyncio.sleep(0.01)
        else:
            self.fail("initial voice readiness was not published")

        setup_token = runtime.begin_voice_setup()
        runtime.report_voice_setup_result(setup_token, DriverState.READY)
        runtime.refresh_voice_readiness()

        for _ in range(100):
            if any(
                entry[0] == "ready"
                for entry in factory.instances[0].voice_payloads
            ):
                break
            await asyncio.sleep(0.01)
        else:
            self.fail("refreshed voice readiness did not enable the service")
        self.assertIs(runtime.snapshot().voice_state, VoiceState.READY)
        self.assertIs(runtime.snapshot().state, ConnectionState.CONNECTED)
        self.assertEqual(len(factory.instances), 1)
        self.assertEqual(factory.instances[0].disconnect_count, 0)
        self.assertEqual(
            [entry[0] for entry in factory.instances[0].voice_payloads[:2]],
            ["unavailable", "ready"],
        )

        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_production_assembly_classifies_absent_blackhole_as_restart_required(self):
        class Bridge:
            def find_device_uid(self, _name):
                raise VoiceAudioDeviceNotFound("BlackHole 2ch is not visible")

        with patch("runtime.VoiceAudioBridge", return_value=Bridge()), patch(
            "runtime._blackhole_receipt_present", return_value=True
        ):
            service = _create_voice_session(bytes(range(16)))
            state = await service.prepare()

        self.assertIs(state, VoiceState.RESTART_REQUIRED)

    async def test_production_assembly_maps_core_audio_query_error_to_voice_error(self):
        class Bridge:
            def find_device_uid(self, _name):
                raise VoiceAudioError("Core Audio device query failed")

        factory = FakeTransportFactory([{"disconnect_after_messages": 2}])
        with patch("runtime.VoiceAudioBridge", return_value=Bridge()), patch(
            "runtime._blackhole_receipt_present", return_value=True
        ):
            runtime = AgentRuntime(
                collector=FakeCollector(),
                transport_factory=factory,
                voice_key_loader=lambda: bytes(range(16)),
                status_interval=0.01,
            )
            result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertIs(runtime.snapshot().voice_state, VoiceState.ERROR)
        self.assertEqual(
            runtime.snapshot().voice_detail,
            "Core Audio device query failed",
        )

    async def test_runtime_disconnect_cleans_voice_before_ble_disconnect(self):
        events = []
        key = bytes(range(16))
        factory = FakeTransportFactory(
            [{"events": events, "disconnect_after_voice": True}]
        )

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                events.append("voice.prepare")
                return self.state

            async def disconnect(self):
                events.append("voice.disconnect")

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: bytes(range(1, 9)),
        )

        result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(
            events[-3:],
            ["voice.disable", "voice.disconnect", "ble.disconnect"],
        )
        self.assertEqual(factory.instances[0].input_disable_count, 1)
        ready = decode_control(key, factory.instances[0].voice_payloads[0][1])
        self.assertEqual(ready.kind, READY)
        self.assertEqual(ready.target_bps, 8192)

    async def test_missing_key_is_loaded_once_and_status_transport_continues(self):
        calls = 0

        def missing_key():
            nonlocal calls
            calls += 1
            raise VoiceKeyError("voice key stored in Keychain is malformed")

        factory = FakeTransportFactory(
            [{"disconnect_after_messages": 2}]
        )
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=missing_key,
            status_interval=0.01,
        )

        result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(calls, 1)
        self.assertEqual(
            [message_type for message_type, _payload in factory.instances[0].messages],
            [EVENT_MESSAGE, STATUS_MESSAGE],
        )
        self.assertIs(runtime.snapshot().state, ConnectionState.CONNECTED)
        self.assertIs(runtime.snapshot().voice_state, VoiceState.ERROR)
        self.assertEqual(
            runtime.snapshot().voice_detail,
            "voice key stored in Keychain is malformed",
        )
        self.assertEqual(factory.instances[0].input_payloads, [])
        self.assertFalse(
            any(
                payload[0] == "ready"
                for payload in factory.instances[0].voice_payloads
            )
        )

    async def test_input_actions_start_before_voice_and_survive_unavailable_voice(self):
        key = bytes(range(16))
        nonce = b"input001"
        action = encode_action(key, InputAction.SUBMIT, 1, 123, nonce)
        for unavailable_state in (
            VoiceState.TYPELESS_NOT_RUNNING,
            VoiceState.DRIVER_REQUIRED,
        ):
            with self.subTest(unavailable_state=unavailable_state):
                events = []
                factory = FakeTransportFactory([{"events": events}])

                class VoiceSession:
                    state = unavailable_state

                    async def prepare(self):
                        events.append("voice.prepare")
                        return self.state

                    async def disconnect(self):
                        events.append("voice.disconnect")

                class SubmitController:
                    def __init__(self):
                        self.actions = []

                    def handle(self, submitted_action):
                        self.actions.append(submitted_action)
                        return True

                controller = SubmitController()
                runtime = AgentRuntime(
                    collector=FakeCollector(),
                    transport_factory=factory,
                    voice_key_loader=lambda: key,
                    voice_session_factory=lambda _key: VoiceSession(),
                    voice_nonce_factory=lambda: b"voice001",
                    input_nonce_factory=lambda: nonce,
                    submit_controller_factory=lambda: controller,
                    status_interval=30,
                )

                session = asyncio.create_task(runtime._run_session())
                await factory.wait_for_instances(1)
                transport = factory.instances[0]
                await asyncio.wait_for(transport.input_enable_started.wait(), 1)
                for _ in range(100):
                    if runtime.snapshot().voice_state is unavailable_state:
                        break
                    await asyncio.sleep(0.01)
                else:
                    self.fail("voice unavailable state was not published")

                self.assertIsNotNone(transport.input_handler)
                self.assertEqual(transport.input_disable_count, 0)
                await transport.input_handler(action)
                await asyncio.sleep(0)

                self.assertEqual(controller.actions, [InputAction.SUBMIT])
                self.assertLess(
                    events.index("input.enable"), events.index("voice.prepare")
                )
                if unavailable_state is VoiceState.DRIVER_REQUIRED:
                    self.assertIn("voice.disable", events)
                else:
                    self.assertIn("voice.enable", events)
                    control = decode_control(key, transport.voice_payloads[0][1])
                    self.assertEqual(
                        control.capabilities,
                        EXTERNAL_MICROPHONE_CAPABILITY,
                    )
                self.assertEqual(transport.input_disable_count, 0)
                session.cancel()
                with self.assertRaises(asyncio.CancelledError):
                    await session
                if unavailable_state is VoiceState.DRIVER_REQUIRED:
                    self.assertGreater(
                        events.index("input.disable"),
                        events.index("voice.disable"),
                    )
                else:
                    self.assertLess(
                        events.index("input.disable"),
                        events.index("voice.disconnect"),
                    )
                self.assertEqual(transport.input_disable_count, 1)
                ready = transport.input_payloads[0][1]
                self.assertEqual(decode_input_ready(key, ready), nonce)

    async def test_missing_accessibility_overrides_driver_and_blocks_input_dispatch(
        self,
    ):
        key = bytes(range(16))
        nonce = b"inputax1"
        action = encode_action(key, InputAction.SUBMIT, 1, 124, nonce)
        factory = FakeTransportFactory([{}])

        class VoiceSession:
            state = VoiceState.DRIVER_REQUIRED

            async def prepare(self):
                return self.state

            async def disconnect(self):
                pass

        class SubmitController:
            def __init__(self):
                self.actions = []

            def handle(self, submitted_action):
                self.actions.append(submitted_action)
                return True

        controller = SubmitController()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: VoiceSession(),
            input_nonce_factory=lambda: nonce,
            submit_controller_factory=lambda: controller,
            accessibility_checker=lambda: False,
            status_interval=30,
        )

        session = asyncio.create_task(runtime._run_session())
        await factory.wait_for_instances(1)
        transport = factory.instances[0]
        await asyncio.wait_for(transport.input_enable_started.wait(), 1)
        for _ in range(100):
            if (
                runtime.snapshot().voice_state
                is VoiceState.ACCESSIBILITY_REQUIRED
            ):
                break
            await asyncio.sleep(0.01)
        else:
            self.fail("Accessibility-required state was not published")

        self.assertEqual(transport.input_disable_count, 0)
        self.assertIsNotNone(transport.input_handler)
        await transport.input_handler(action)
        self.assertEqual(controller.actions, [])
        control = decode_control(key, transport.voice_payloads[0][1])
        self.assertEqual(control.kind, UNAVAILABLE)
        self.assertEqual(control.reason, ACCESSIBILITY_REQUIRED)

        session.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await session

    async def test_accessibility_recovery_refresh_restores_ready_voice(self):
        key = bytes(range(16))
        permission = {"available": False}
        factory = FakeTransportFactory([{}])

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                pass

            async def handle(self, _payload):
                pass

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"voiceax1",
            input_nonce_factory=lambda: b"inputax2",
            accessibility_checker=lambda: permission["available"],
            typeless_checker=lambda: True,
            status_interval=0.01,
        )

        session = asyncio.create_task(runtime._run_session())
        await factory.wait_for_instances(1)
        transport = factory.instances[0]
        for _ in range(100):
            if (
                runtime.snapshot().voice_state
                is VoiceState.ACCESSIBILITY_REQUIRED
            ):
                break
            await asyncio.sleep(0.01)
        else:
            self.fail("Accessibility-required state was not published")

        permission["available"] = True
        transport.voice_enable_started.clear()
        runtime.refresh_voice_readiness()
        await asyncio.wait_for(transport.voice_enable_started.wait(), 1)

        self.assertIs(runtime.snapshot().voice_state, VoiceState.READY)
        self.assertTrue(
            any(item[0] == "ready" for item in transport.voice_payloads)
        )
        session.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await session

    async def test_input_decoder_rejects_duplicate_out_of_order_old_nonce_and_malformed(self):
        key = bytes(range(16))
        nonce = b"input002"
        old_nonce = b"oldnonce"
        valid = encode_action(key, InputAction.FORCE_STEER, 2, 200, nonce)
        duplicate = valid
        out_of_order = encode_action(key, InputAction.SUBMIT, 1, 201, nonce)
        stale = encode_action(key, InputAction.SUBMIT, 3, 202, old_nonce)
        factory = FakeTransportFactory(
            [
                {
                    "input_notifications": [
                        valid,
                        duplicate,
                        out_of_order,
                        stale,
                        b"malformed",
                    ],
                    "disconnect_after_messages": 2,
                }
            ]
        )

        class VoiceSession:
            state = VoiceState.TYPELESS_NOT_RUNNING

            async def prepare(self):
                return self.state

            async def disconnect(self):
                pass

        class SubmitController:
            def __init__(self):
                self.actions = []

            def handle(self, action):
                self.actions.append(action)
                return False

        controller = SubmitController()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: VoiceSession(),
            input_nonce_factory=lambda: nonce,
            submit_controller_factory=lambda: controller,
            status_interval=0.01,
        )

        result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(controller.actions, [InputAction.FORCE_STEER])
        self.assertEqual(
            [item[0] for item in factory.instances[0].messages],
            [EVENT_MESSAGE, STATUS_MESSAGE],
        )

    async def test_reconnect_uses_fresh_input_nonce_and_rejects_old_connection_packet(self):
        key = bytes(range(16))
        first_nonce = b"input003"
        second_nonce = b"input004"
        stale = encode_action(key, InputAction.SUBMIT, 1, 300, first_nonce)
        factory = FakeTransportFactory(
            [
                {"disconnect_after_messages": 2},
                {
                    "input_notifications": [stale],
                    "disconnect_after_messages": 2,
                },
            ]
        )
        nonces = iter((first_nonce, second_nonce))

        class VoiceSession:
            state = VoiceState.TYPELESS_NOT_RUNNING

            async def prepare(self):
                return self.state

            async def disconnect(self):
                pass

        class SubmitController:
            def __init__(self):
                self.actions = []

            def handle(self, action):
                self.actions.append(action)
                return True

        controllers = []

        def make_controller():
            controller = SubmitController()
            controllers.append(controller)
            return controller

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: VoiceSession(),
            input_nonce_factory=lambda: next(nonces),
            submit_controller_factory=make_controller,
            status_interval=0.01,
        )

        self.assertEqual(await runtime._run_session(), "disconnected")
        self.assertEqual(await runtime._run_session(), "disconnected")

        ready_nonces = [
            decode_input_ready(key, transport.input_payloads[0][1])
            for transport in factory.instances
        ]
        self.assertEqual(ready_nonces, [first_nonce, second_nonce])
        self.assertEqual([controller.actions for controller in controllers], [[], []])

    async def test_submit_handler_exception_isolated_and_action_is_not_retried(self):
        key = bytes(range(16))
        nonce = b"input005"
        action = encode_action(key, InputAction.SUBMIT, 1, 400, nonce)
        factory = FakeTransportFactory(
            [
                {
                    "input_notifications": [action],
                    "disconnect_after_messages": 2,
                }
            ]
        )

        class VoiceSession:
            state = VoiceState.TYPELESS_NOT_RUNNING

            async def prepare(self):
                return self.state

            async def disconnect(self):
                pass

        class SubmitController:
            def __init__(self):
                self.calls = 0

            def handle(self, _action):
                self.calls += 1
                raise RuntimeError("AX unavailable")

        controller = SubmitController()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: VoiceSession(),
            input_nonce_factory=lambda: nonce,
            submit_controller_factory=lambda: controller,
            status_interval=0.01,
        )

        with self.assertLogs("runtime", level="WARNING"):
            result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(controller.calls, 1)
        self.assertEqual(len(factory.instances[0].messages), 2)

    async def test_input_enable_failure_does_not_prevent_voice_readiness(self):
        factory = FakeTransportFactory(
            [
                {
                    "input_enable_error": ConnectionError(
                        "input READY write failed"
                    ),
                    "disconnect_after_voice": True,
                }
            ]
        )

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                pass

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"voice007",
            input_nonce_factory=lambda: b"input007",
        )

        with self.assertLogs("runtime", level="ERROR"):
            result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertTrue(
            any(item[0] == "ready" for item in factory.instances[0].voice_payloads)
        )
        self.assertIs(runtime.snapshot().voice_state, VoiceState.READY)

    async def test_voice_key_material_is_redacted_from_snapshot_and_logs(self):
        key_text = bytes(range(16)).hex()

        def malformed_key():
            raise VoiceKeyError("malformed voice key {}".format(key_text))

        factory = FakeTransportFactory([{"disconnect_after_messages": 2}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=malformed_key,
            status_interval=0.01,
        )

        with self.assertLogs("runtime", level="INFO") as logs:
            result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertNotIn(key_text, runtime.snapshot().voice_detail)
        self.assertIn("[redacted]", runtime.snapshot().voice_detail)
        self.assertNotIn(key_text, "\n".join(logs.output))

    async def test_driver_states_are_unavailable_but_desktop_states_keep_external_mic(self):
        cases = (
            (VoiceState.DRIVER_REQUIRED, DRIVER_REQUIRED),
            (VoiceState.RESTART_REQUIRED, RESTART_REQUIRED),
            (VoiceState.ACCESSIBILITY_REQUIRED, ACCESSIBILITY_REQUIRED),
            (VoiceState.TYPELESS_NOT_RUNNING, TYPELESS_NOT_RUNNING),
        )
        key = bytes(range(16))

        for state, reason in cases:
            with self.subTest(state=state):
                factory = FakeTransportFactory(
                    [{"disconnect_after_messages": 2}]
                )

                class VoiceSession:
                    async def prepare(self):
                        return state

                    async def disconnect(self):
                        pass

                runtime = AgentRuntime(
                    collector=FakeCollector(),
                    transport_factory=factory,
                    voice_key_loader=lambda: key,
                    voice_session_factory=lambda _key: VoiceSession(),
                    voice_nonce_factory=lambda: b"12345678",
                    accessibility_checker=lambda: True,
                    status_interval=0.01,
                )

                result = await runtime._run_session()

                self.assertEqual(result, "disconnected")
                controls = factory.instances[0].voice_payloads
                self.assertGreaterEqual(len(controls), 1)
                control = decode_control(key, controls[0][1])
                if state in (
                    VoiceState.DRIVER_REQUIRED,
                    VoiceState.RESTART_REQUIRED,
                ):
                    self.assertEqual(controls[0][0], "unavailable")
                    self.assertEqual(control.kind, UNAVAILABLE)
                    self.assertEqual(control.reason, reason)
                else:
                    self.assertEqual(controls[0][0], "ready")
                    self.assertEqual(control.kind, READY)
                    self.assertEqual(
                        control.capabilities,
                        EXTERNAL_MICROPHONE_CAPABILITY,
                    )
                self.assertEqual(
                    [item[0] for item in factory.instances[0].messages],
                    [EVENT_MESSAGE, STATUS_MESSAGE],
                )
                self.assertIs(runtime.snapshot().voice_state, state)

    async def test_prepare_failure_keeps_service_owned_for_cleanup(self):
        events = []
        factory = FakeTransportFactory(
            [{"events": events, "disconnect_after_voice": True}]
        )

        class VoiceSession:
            async def prepare(self):
                raise RuntimeError("readiness failed")

            async def disconnect(self):
                events.append("voice.disconnect")

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"12345678",
        )

        await runtime._run_session()

        self.assertEqual(
            events[-4:],
            [
                "voice.disable",
                "voice.disconnect",
                "input.disable",
                "ble.disconnect",
            ],
        )
        self.assertIs(runtime.snapshot().voice_state, VoiceState.ERROR)
        self.assertEqual(runtime.snapshot().voice_detail, "readiness failed")

    async def test_ready_write_failure_cleans_service_before_ble_disconnect(self):
        events = []
        factory = FakeTransportFactory(
            [
                {
                    "events": events,
                    "voice_enable_error": ConnectionError("READY write failed"),
                    "disconnect_after_messages": 2,
                }
            ]
        )

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                events.append("voice.disconnect")

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"12345678",
        )

        result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(
            [item[0] for item in factory.instances[0].messages],
            [EVENT_MESSAGE, STATUS_MESSAGE],
        )
        self.assertIs(runtime.snapshot().voice_state, VoiceState.ERROR)
        self.assertEqual(runtime.snapshot().voice_detail, "READY write failed")
        self.assertEqual(
            events[-4:],
            [
                "voice.disable",
                "voice.disconnect",
                "input.disable",
                "ble.disconnect",
            ],
        )

    async def test_cancellation_while_ready_write_is_blocked_stops_active_recording(self):
        key = bytes(range(16))
        begin = pack_notification(
            key,
            PTT_BEGIN,
            7,
            0,
            100,
            BEGIN_BODY.pack(IMA_ADPCM, PUSH_TO_TALK, 16000, 320, TEST_TASK_ROUTE),
            b"12345678",
        )
        factory = FakeTransportFactory(
            [{"block_voice_enable": True, "voice_notification": begin}]
        )

        class Resource:
            def __init__(self):
                self.calls = []

            def prepare(self):
                pass

            def start(self):
                self.calls.append("start")

            def stop(self):
                self.calls.append("stop")

            def close(self):
                pass

            def write(self, samples):
                return len(samples)

        route = Resource()
        shortcut = Resource()
        service = VoiceSessionService(
            route,
            shortcut,
            key,
            readiness_checker=lambda: VoiceState.READY,
            sleep=lambda _delay: asyncio.sleep(0),
        )
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: service,
            voice_nonce_factory=lambda: b"abcdefgh",
            disconnect_timeout=0.1,
        )

        session = asyncio.create_task(runtime._run_session())
        await factory.wait_for_instances(1)
        transport = factory.instances[0]
        await asyncio.wait_for(transport.voice_notification_handled.wait(), 1)
        self.assertEqual(route.calls, ["start"])
        self.assertEqual(shortcut.calls, ["start"])

        session.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await session

        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertIs(service.state, VoiceState.READY)
        self.assertEqual(transport.disconnect_count, 1)

    async def test_cancellation_during_key_load_still_disconnects_ble(self):
        loader_started = threading.Event()
        loader_release = threading.Event()

        def blocked_loader():
            loader_started.set()
            loader_release.wait()
            return bytes(range(16))

        factory = FakeTransportFactory([{}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=blocked_loader,
            disconnect_timeout=0.1,
        )
        session = asyncio.create_task(runtime._run_session())
        await factory.wait_for_instances(1)
        await asyncio.wait_for(asyncio.to_thread(loader_started.wait), 1)

        session.cancel()
        await asyncio.wait_for(factory.instances[0].disconnected_event.wait(), 1)
        loader_release.set()
        with self.assertRaises(asyncio.CancelledError):
            await session

        self.assertEqual(factory.instances[0].disconnect_count, 1)

    async def test_cancellation_during_prepare_cleans_owned_service(self):
        prepare_started = asyncio.Event()
        disconnect_calls = 0

        class VoiceSession:
            async def prepare(self):
                prepare_started.set()
                await asyncio.Event().wait()

            async def disconnect(self):
                nonlocal disconnect_calls
                disconnect_calls += 1

        factory = FakeTransportFactory([{}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            disconnect_timeout=0.1,
        )
        session = asyncio.create_task(runtime._run_session())
        await asyncio.wait_for(prepare_started.wait(), 1)

        session.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await session

        self.assertEqual(disconnect_calls, 1)
        self.assertEqual(factory.instances[0].disconnect_count, 1)

    async def test_cancellation_during_subscription_cleans_owned_service(self):
        disconnect_calls = 0

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                nonlocal disconnect_calls
                disconnect_calls += 1

        factory = FakeTransportFactory([{"block_voice_enable": True}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"12345678",
            disconnect_timeout=0.1,
        )
        session = asyncio.create_task(runtime._run_session())
        await factory.wait_for_instances(1)
        await asyncio.wait_for(
            factory.instances[0].voice_enable_started.wait(), 1
        )

        session.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await session

        self.assertEqual(disconnect_calls, 1)
        self.assertEqual(factory.instances[0].disconnect_count, 1)

    async def test_cancellation_during_active_recording_stops_both_resources(self):
        key = bytes(range(16))
        begin = pack_notification(
            key,
            PTT_BEGIN,
            9,
            0,
            100,
            BEGIN_BODY.pack(IMA_ADPCM, PUSH_TO_TALK, 16000, 320, TEST_TASK_ROUTE),
            b"abcdefgh",
        )

        class Resource:
            def __init__(self):
                self.calls = []

            def prepare(self):
                pass

            def start(self):
                self.calls.append("start")

            def stop(self):
                self.calls.append("stop")

            def close(self):
                pass

            def write(self, samples):
                return len(samples)

        route = Resource()
        shortcut = Resource()
        service = VoiceSessionService(
            route,
            shortcut,
            key,
            readiness_checker=lambda: VoiceState.READY,
            sleep=lambda _delay: asyncio.sleep(0),
        )
        factory = FakeTransportFactory([{"voice_notification": begin}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: service,
            voice_nonce_factory=lambda: b"12345678",
            status_interval=30,
            disconnect_timeout=0.1,
        )
        session = asyncio.create_task(runtime._run_session())
        await factory.wait_for_instances(1)
        await asyncio.wait_for(
            factory.instances[0].voice_notification_handled.wait(), 1
        )

        session.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await session

        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])

    async def test_task_submission_failure_does_not_poison_voice_readiness(self):
        key = bytes(range(16))
        session_nonce = b"12345678"
        begin = pack_notification(
            key,
            PTT_BEGIN,
            7,
            0,
            100,
            BEGIN_BODY.pack(
                IMA_ADPCM,
                PUSH_TO_TALK,
                16000,
                320,
                TEST_TASK_ROUTE,
            ),
            session_nonce,
        )
        end = pack_notification(
            key,
            PTT_END,
            7,
            1,
            110,
            EndSummary(0, 0, 0, 0, 0, 10, 256).pack(),
            session_nonce,
        )

        class Route:
            def prepare(self):
                pass

            def start(self):
                pass

            def write(self, samples):
                return len(samples)

            def stop(self):
                pass

            def close(self):
                pass

        class Shortcut:
            def start(self):
                pass

            def stop(self):
                pass

        class TranscriptSource:
            def mark(self):
                return "cursor"

            async def wait_after(self, cursor, timeout):
                return "transcript"

        class FailingSubmitter:
            def snapshot(self, route):
                return "target"

            async def submit(self, target, text):
                raise RuntimeError("desktop target disappeared")

        service = VoiceSessionService(
            Route(),
            Shortcut(),
            key,
            readiness_checker=lambda: VoiceState.READY,
            sleep=lambda _delay: asyncio.sleep(0),
            transcript_source=TranscriptSource(),
            task_submitter=FailingSubmitter(),
        )
        factory = FakeTransportFactory(
            [
                {
                    "voice_notifications": [begin, end],
                    "disconnect_after_messages": 2,
                }
            ]
        )
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: service,
            voice_nonce_factory=lambda: session_nonce,
            status_interval=0.01,
        )

        result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertIs(runtime.snapshot().voice_state, VoiceState.READY)
        self.assertEqual(runtime.snapshot().voice_detail, "")

    async def test_voice_consumer_failure_isolated_from_status_session(self):
        events = []
        factory = FakeTransportFactory(
            [
                {
                    "events": events,
                    "voice_failure_error": RuntimeError("voice notification queue overflow"),
                    "disconnect_after_messages": 2,
                }
            ]
        )

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                events.append("voice.disconnect")

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"12345678",
            status_interval=0.01,
        )

        result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(
            [item[0] for item in factory.instances[0].messages],
            [EVENT_MESSAGE, STATUS_MESSAGE],
        )
        self.assertIs(runtime.snapshot().voice_state, VoiceState.ERROR)
        self.assertEqual(
            runtime.snapshot().voice_detail,
            "voice notification queue overflow",
        )
        self.assertEqual(
            events[-4:],
            [
                "voice.disable",
                "voice.disconnect",
                "input.disable",
                "ble.disconnect",
            ],
        )

    async def test_voice_cleanup_timeouts_still_reach_ble_disconnect(self):
        events = []
        factory = FakeTransportFactory(
            [
                {
                    "events": events,
                    "disconnect_after_voice": True,
                    "block_voice_disable": True,
                }
            ]
        )

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                events.append("voice.disconnect")
                await asyncio.Event().wait()

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"12345678",
            disconnect_timeout=0.01,
        )

        result = await asyncio.wait_for(runtime._run_session(), 1)

        self.assertEqual(result, "disconnected")
        self.assertEqual(events[-1], "ble.disconnect")
        self.assertEqual(events.count("voice.disable"), 1)
        self.assertEqual(events.count("voice.disconnect"), 1)
        self.assertEqual(events.count("ble.disconnect"), 1)

    async def test_input_cleanup_timeout_still_reaches_voice_and_ble_cleanup(self):
        events = []
        factory = FakeTransportFactory(
            [
                {
                    "events": events,
                    "disconnect_after_voice": True,
                    "block_input_disable": True,
                }
            ]
        )

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                events.append("voice.disconnect")

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            input_nonce_factory=lambda: b"input006",
            disconnect_timeout=0.03,
        )

        result = await asyncio.wait_for(runtime._run_session(), 1)

        self.assertEqual(result, "disconnected")
        self.assertEqual(events.count("input.disable"), 1)
        self.assertEqual(events.count("voice.disable"), 1)
        self.assertEqual(events.count("voice.disconnect"), 1)
        self.assertEqual(events.count("ble.disconnect"), 1)

    async def test_repeated_cancellation_uses_one_shutdown_budget(self):
        events = []
        factory = FakeTransportFactory(
            [
                {
                    "events": events,
                    "block_voice_disable": True,
                    "block_disconnect": True,
                }
            ]
        )

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                events.append("voice.disconnect")
                await asyncio.Event().wait()

        shutdown_budget = 0.09
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"12345678",
            status_interval=30,
            disconnect_timeout=shutdown_budget,
        )
        session = asyncio.create_task(runtime._run_session())
        await factory.wait_for_instances(1)
        transport = factory.instances[0]
        for _ in range(100):
            if transport.voice_payloads:
                break
            await asyncio.sleep(0.01)
        else:
            self.fail("voice did not become ready")

        started = asyncio.get_running_loop().time()
        session.cancel()
        for _ in range(100):
            if "voice.disable" in events:
                break
            await asyncio.sleep(0.001)
        session.cancel()
        await asyncio.sleep(0)
        session.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await session
        elapsed = asyncio.get_running_loop().time() - started

        self.assertLess(elapsed, shutdown_budget + 0.09)
        self.assertEqual(events.count("voice.disable"), 1)
        self.assertEqual(events.count("voice.disconnect"), 1)
        self.assertEqual(events.count("ble.disconnect"), 1)

    async def test_real_voice_session_stalled_drain_obeys_aggregate_deadline(self):
        key = bytes(range(16))
        begin = pack_notification(
            key,
            PTT_BEGIN,
            11,
            0,
            100,
            BEGIN_BODY.pack(IMA_ADPCM, PUSH_TO_TALK, 16000, 320, TEST_TASK_ROUTE),
            b"abcdefgh",
        )
        drain_started = asyncio.Event()
        release_drain = asyncio.Event()

        async def stalled_drain(_seconds):
            drain_started.set()
            await release_drain.wait()

        class Resource:
            def __init__(self):
                self.calls = []

            def prepare(self):
                pass

            def start(self):
                self.calls.append("start")

            def stop(self):
                self.calls.append("stop")

            def close(self):
                pass

            def write(self, samples):
                return len(samples)

        route = Resource()
        shortcut = Resource()
        service = VoiceSessionService(
            route,
            shortcut,
            key,
            readiness_checker=lambda: VoiceState.READY,
            sleep=stalled_drain,
        )
        factory = FakeTransportFactory(
            [
                {
                    "disconnect_after_voice": True,
                    "voice_notification": begin,
                }
            ]
        )
        shutdown_budget = 0.03
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: service,
            voice_nonce_factory=lambda: b"12345678",
            disconnect_timeout=shutdown_budget,
        )
        session = asyncio.create_task(runtime._run_session())
        await asyncio.wait_for(drain_started.wait(), 1)
        started = asyncio.get_running_loop().time()

        completion_limit = shutdown_budget + 0.05
        done, _ = await asyncio.wait(
            {session}, timeout=completion_limit
        )
        completed_within_deadline = session in done
        if not completed_within_deadline:
            release_drain.set()
        await session
        elapsed = asyncio.get_running_loop().time() - started

        self.assertTrue(completed_within_deadline)
        self.assertLess(elapsed, completion_limit)
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(factory.instances[0].disconnect_count, 1)
        self.assertEqual(
            [
                task
                for task in asyncio.all_tasks()
                if task is not asyncio.current_task() and not task.done()
            ],
            [],
        )

    async def test_consumer_stalled_in_real_end_drain_cannot_block_shutdown(self):
        key = bytes(range(16))
        begin = pack_notification(
            key,
            PTT_BEGIN,
            13,
            0,
            100,
            BEGIN_BODY.pack(IMA_ADPCM, PUSH_TO_TALK, 16000, 320, TEST_TASK_ROUTE),
            b"abcdefgh",
        )
        end = pack_notification(
            key,
            PTT_END,
            13,
            1,
            101,
            EndSummary(0, 0, 0, 0, 0, 1, 256).pack(),
            b"abcdefgh",
        )
        drain_started = asyncio.Event()
        release_drain = asyncio.Event()

        async def stalled_drain(_seconds):
            drain_started.set()
            await release_drain.wait()

        class Resource:
            def __init__(self):
                self.calls = []

            def prepare(self):
                pass

            def start(self):
                self.calls.append("start")

            def stop(self):
                self.calls.append("stop")

            def close(self):
                pass

            def write(self, samples):
                return len(samples)

        route = Resource()
        shortcut = Resource()
        service = VoiceSessionService(
            route,
            shortcut,
            key,
            readiness_checker=lambda: VoiceState.READY,
            sleep=stalled_drain,
        )

        class Client:
            is_connected = True

            def __init__(self):
                self.callback = None
                self.sent_notifications = False

            async def start_notify(self, _uuid, callback):
                self.callback = callback

            async def write_gatt_char(self, uuid, _payload, response):
                self.response = response
                if (
                    uuid == VOICE_CONTROL_UUID
                    and self.callback is not None
                    and not self.sent_notifications
                ):
                    self.sent_notifications = True
                    self.callback(None, begin)
                    self.callback(None, end)

            async def stop_notify(self, _uuid):
                pass

            async def disconnect(self):
                self.is_connected = False

        class Transport(BleStatusTransport):
            def __init__(self):
                super().__init__()
                self._client = Client()
                self.disconnect_count = 0

            async def connect(self, on_device_found=None):
                if on_device_found:
                    on_device_found()
                return True

            async def send_json(self, _payload, _message_type):
                await asyncio.sleep(0)

            async def disconnect(self):
                self.disconnect_count += 1
                await super().disconnect()

        transport = Transport()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=lambda: transport,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: service,
            voice_nonce_factory=lambda: b"12345678",
            disconnect_timeout=0.03,
        )
        session = asyncio.create_task(runtime._run_session())
        await asyncio.wait_for(drain_started.wait(), 1)

        session.cancel()
        done, _ = await asyncio.wait({session}, timeout=0.12)
        completed_within_deadline = session in done
        if not completed_within_deadline:
            release_drain.set()
        with self.assertRaises(asyncio.CancelledError):
            await session

        self.assertTrue(completed_within_deadline)
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(transport.disconnect_count, 1)
        self.assertEqual(
            [
                task
                for task in asyncio.all_tasks()
                if task is not asyncio.current_task() and not task.done()
            ],
            [],
        )

    async def test_unavailable_encoding_failure_cannot_skip_cleanup(self):
        events = []
        factory = FakeTransportFactory(
            [{"events": events, "disconnect_after_voice": True}]
        )

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def disconnect(self):
                events.append("voice.disconnect")

        nonce_calls = 0

        def nonce_factory():
            nonlocal nonce_calls
            nonce_calls += 1
            if nonce_calls == 2:
                raise RuntimeError("nonce unavailable")
            return b"12345678"

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=nonce_factory,
        )

        result = await runtime._run_session()

        self.assertEqual(result, "disconnected")
        self.assertEqual(
            events[-3:],
            ["voice.disable", "voice.disconnect", "ble.disconnect"],
        )

    async def test_disconnect_clears_recording_snapshot_after_voice_cleanup(self):
        factory = FakeTransportFactory(
            [
                {
                    "voice_notification": b"begin",
                    "disconnect_after_voice": True,
                }
            ]
        )

        class VoiceSession:
            state = VoiceState.READY

            async def prepare(self):
                return self.state

            async def handle(self, payload):
                self.assert_payload = payload
                self.state = VoiceState.RECORDING

            async def disconnect(self):
                self.state = VoiceState.READY

        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: bytes(range(16)),
            voice_session_factory=lambda _key: VoiceSession(),
            voice_nonce_factory=lambda: b"12345678",
        )

        await runtime._run_session()

        self.assertIs(runtime.snapshot().voice_state, VoiceState.READY)
        self.assertEqual(runtime.snapshot().voice_detail, "")

    async def test_cancelling_status_wait_does_not_leak_sleep_task(self):
        runtime = AgentRuntime(
            collector=FakeCollector(),
            status_interval=30.0,
        )

        class Transport:
            voice_failure = asyncio.get_running_loop().create_future()

        real_create_task = asyncio.create_task
        created = []

        def capture_task(coroutine):
            task = real_create_task(coroutine)
            created.append(task)
            return task

        with patch("runtime.asyncio.create_task", side_effect=capture_task):
            wait_task = real_create_task(
                runtime._wait_for_status_interval(Transport())
            )
            for _ in range(10):
                if created:
                    break
                await asyncio.sleep(0)
            wait_task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await wait_task

        try:
            self.assertEqual(len(created), 1)
            self.assertTrue(created[0].done())
        finally:
            for task in created:
                if not task.done():
                    task.cancel()
                try:
                    await task
                except asyncio.CancelledError:
                    pass

    async def test_repeated_stop_during_status_wait_preserves_cancellation(self):
        for iteration in range(10):
            collector = FakeCollector([{"codex_model": "gpt-stop"}])
            factory = FakeTransportFactory([{}])
            runtime = AgentRuntime(
                collector=collector,
                transport_factory=factory,
                status_interval=0.01,
                retry_delays=(0.01,),
                disconnect_timeout=0.1,
            )
            task = asyncio.create_task(runtime.run())
            await wait_for_codex_model(runtime, "gpt-stop")
            await factory.wait_for_instances(1)
            transport = factory.instances[0]
            await transport.connected_event.wait()

            runtime.submit(AgentCommand.STOP)
            done, _ = await asyncio.wait((task,), timeout=0.2)
            if not done:
                transport._connected = False
                if runtime._status_changed is not None:
                    runtime._status_changed.set()
                await asyncio.wait_for(task, 1)
                self.fail(
                    "stop cancellation was lost on iteration {}".format(
                        iteration
                    )
                )

    async def test_collects_codex_status_while_connection_is_disabled(self):
        collector = FakeCollector(
            [
                {
                    "codex_model": "gpt-menu",
                    "codex_effort": "high",
                    "codex_alert_pending": 1,
                    "codex_alert_revision": 9,
                }
            ]
        )
        factory = FakeTransportFactory([])
        runtime = AgentRuntime(
            collector=collector,
            transport_factory=factory,
            connection_enabled=False,
            status_interval=0.01,
            retry_delays=(0.01,),
        )

        task = asyncio.create_task(runtime.run())
        await wait_for_codex_model(runtime, "gpt-menu")

        snapshot = runtime.snapshot()
        self.assertIs(snapshot.state, ConnectionState.DISABLED)
        self.assertEqual(snapshot.codex.effort, "high")
        self.assertEqual(snapshot.codex.alert_pending, 1)
        self.assertEqual(snapshot.codex.alert_revision, 9)
        self.assertEqual(factory.instances, [])
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_bluetooth_transition_preserves_codex_snapshot(self):
        collector = FakeCollector([{"codex_model": "gpt-preserved"}])
        factory = FakeTransportFactory([{}])
        runtime = AgentRuntime(
            collector=collector,
            transport_factory=factory,
            status_interval=0.01,
            retry_delays=(0.01,),
        )

        task = asyncio.create_task(runtime.run())
        await wait_for_codex_model(runtime, "gpt-preserved")
        await factory.wait_for_instances(1)
        await factory.instances[0].connected_event.wait()

        self.assertIs(runtime.snapshot().state, ConnectionState.CONNECTED)
        self.assertEqual(runtime.snapshot().codex.model, "gpt-preserved")
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_failed_refresh_keeps_last_successful_snapshot(self):
        collector = FakeCollector(
            [{"codex_model": "gpt-stable"}, RuntimeError("collector failed")]
        )
        runtime = AgentRuntime(
            collector=collector,
            transport_factory=FakeTransportFactory([]),
            connection_enabled=False,
            status_interval=0.01,
            retry_delays=(0.01,),
        )

        task = asyncio.create_task(runtime.run())
        await wait_for_codex_model(runtime, "gpt-stable")
        for _ in range(100):
            if collector.calls >= 2:
                break
            await asyncio.sleep(0.01)

        self.assertGreaterEqual(collector.calls, 2)
        self.assertEqual(runtime.snapshot().codex.model, "gpt-stable")
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_ble_sender_reuses_the_collected_dictionary(self):
        status = {
            "codex_model": "gpt-shared",
            "codex_effort": "medium",
            "codex_5h_left_pct": 51,
        }
        collector = FakeCollector([status])
        factory = FakeTransportFactory([{}])
        runtime = AgentRuntime(
            collector=collector,
            transport_factory=factory,
            status_interval=1.0,
            retry_delays=(0.01,),
        )

        task = asyncio.create_task(runtime.run())
        await factory.wait_for_instances(1)
        transport = factory.instances[0]
        await wait_for_messages(transport, 2)

        self.assertEqual(collector.calls, 1)
        self.assertEqual(transport.messages[0][0], EVENT_MESSAGE)
        self.assertEqual(json.loads(transport.messages[0][1])["type"], "time_sync")
        self.assertEqual(transport.messages[1][0], STATUS_MESSAGE)
        self.assertEqual(json.loads(transport.messages[1][1]), status)
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_initial_disabled_state_waits_without_scanning(self):
        factory = FakeTransportFactory([])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            connection_enabled=False,
            status_interval=0.01,
            retry_delays=(0.01,),
        )

        task = asyncio.create_task(runtime.run())
        await wait_for_state(runtime, ConnectionState.DISABLED)
        self.assertEqual(factory.instances, [])
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_disable_while_connected_disconnects_and_stops_scanning(self):
        factory = FakeTransportFactory([{}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            status_interval=0.01,
            retry_delays=(0.01,),
        )

        task = asyncio.create_task(runtime.run())
        await factory.wait_for_instances(1)
        transport = factory.instances[0]
        await transport.connected_event.wait()
        runtime.submit(AgentCommand.DISABLE)
        await transport.disconnected_event.wait()
        await wait_for_state(runtime, ConnectionState.DISABLED)
        await asyncio.sleep(0.03)
        self.assertEqual(len(factory.instances), 1)
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_reconnect_replaces_the_active_transport_once(self):
        factory = FakeTransportFactory([{}, {}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            status_interval=0.01,
            retry_delays=(0.01,),
        )

        task = asyncio.create_task(runtime.run())
        await factory.wait_for_instances(1)
        await factory.instances[0].connected_event.wait()
        await wait_for_messages(factory.instances[0], 1)
        runtime.submit(AgentCommand.RECONNECT)
        await factory.wait_for_instances(2)
        await factory.instances[1].connected_event.wait()
        await wait_for_messages(factory.instances[1], 1)
        self.assertEqual(factory.instances[0].disconnect_count, 1)
        self.assertEqual(factory.instances[0].messages[0][0], EVENT_MESSAGE)
        self.assertEqual(
            json.loads(factory.instances[0].messages[0][1])["type"], "time_sync"
        )
        self.assertEqual(factory.instances[1].messages[0][0], EVENT_MESSAGE)
        self.assertEqual(
            json.loads(factory.instances[1].messages[0][1])["type"], "time_sync"
        )
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_failed_time_sync_write_disconnects_and_retries(self):
        factory = FakeTransportFactory(
            [{"send_error": ConnectionError("time sync failed")}, {}]
        )
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            status_interval=0.01,
            retry_delays=(0.01,),
        )

        task = asyncio.create_task(runtime.run())
        await factory.wait_for_instances(2)
        await wait_for_messages(factory.instances[1], 1)

        self.assertEqual(factory.instances[0].disconnect_count, 1)
        self.assertEqual(factory.instances[1].messages[0][0], EVENT_MESSAGE)
        self.assertEqual(
            json.loads(factory.instances[1].messages[0][1])["type"], "time_sync"
        )
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_not_found_enters_waiting_before_retry(self):
        factory = FakeTransportFactory([{"connect_result": False}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            status_interval=0.01,
            retry_delays=(1.0,),
        )

        task = asyncio.create_task(runtime.run())
        await wait_for_state(runtime, ConnectionState.WAITING)
        self.assertIn("not found", runtime.snapshot().detail.lower())
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_stop_interrupts_an_active_scan(self):
        factory = FakeTransportFactory([{"block_connect": True}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            status_interval=0.01,
            retry_delays=(1.0,),
        )

        task = asyncio.create_task(runtime.run())
        await factory.wait_for_instances(1)
        await factory.instances[0].connect_started.wait()
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)
        self.assertEqual(factory.instances[0].disconnect_count, 1)

    async def test_stop_is_bounded_when_disconnect_hangs(self):
        factory = FakeTransportFactory([{"block_disconnect": True}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            status_interval=0.01,
            retry_delays=(1.0,),
            disconnect_timeout=0.01,
        )

        task = asyncio.create_task(runtime.run())
        await factory.wait_for_instances(1)
        await factory.instances[0].connected_event.wait()
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)
        self.assertEqual(factory.instances[0].disconnect_count, 1)

    async def test_unexpected_connect_error_is_visible(self):
        factory = FakeTransportFactory([{"connect_error": RuntimeError("boom")}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            status_interval=0.01,
            retry_delays=(1.0,),
        )

        task = asyncio.create_task(runtime.run())
        await wait_for_state(runtime, ConnectionState.ERROR)
        self.assertEqual(runtime.snapshot().detail, "boom")
        runtime.submit(AgentCommand.STOP)
        await asyncio.wait_for(task, 1)

    async def test_permission_and_power_errors_have_specific_states(self):
        cases = (
            ("Bluetooth permission denied", ConnectionState.UNAUTHORIZED),
            ("Bluetooth powered off and unavailable", ConnectionState.UNAVAILABLE),
            ("Bluetooth device is turned off", ConnectionState.UNAVAILABLE),
        )
        for message, expected in cases:
            with self.subTest(message=message):
                factory = FakeTransportFactory(
                    [{"connect_error": RuntimeError(message)}]
                )
                runtime = AgentRuntime(
                    collector=FakeCollector(),
                    transport_factory=factory,
                    status_interval=0.01,
                    retry_delays=(1.0,),
                )
                task = asyncio.create_task(runtime.run())
                await wait_for_state(runtime, expected)
                runtime.submit(AgentCommand.STOP)
                await asyncio.wait_for(task, 1)


class AgentRuntimePetTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        class Cache:
            def __init__(self):
                self.stored = []
                self.removed = []

            def store(self, watch_id, package):
                self.stored.append((watch_id, package))

            def remove_watch(self, watch_id):
                self.removed.append(watch_id)

        self.cache = Cache()
        pixels = WIDTH * HEIGHT
        self.package = encode_package(
            WIDTH,
            HEIGHT,
            (0xF800,),
            1,
            (PetFrame(120, bytes([0]) * pixels),),
        )
        self.credential = WatchCredential(
            bytes(range(16)),
            b"K" * 16,
            9,
            b"F" * 16,
            b"\x02" + b"W" * 32,
        )

    async def test_authenticated_query_publishes_authoritative_watch_state(self):
        created = []

        class Uploader:
            def __init__(self, watch_id):
                self.watch_id = watch_id

            async def query(self):
                return PetWatchState(
                    self.watch_id,
                    PetCapability.READY,
                    "custom",
                    1,
                    1,
                    4,
                    b"H" * 32,
                    True,
                )

            async def close(self):
                pass

        def factory(_transport, key, epoch, _nonce_factory, *, watch_id):
            self.assertEqual((key, epoch), (b"K" * 16, 9))
            uploader = Uploader(watch_id)
            created.append(uploader)
            return uploader

        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=factory,
            pet_cache=self.cache,
        )
        context = _BleSessionContext(FakeTransport())
        context.credential = self.credential
        context.voice_key = self.credential.master_key
        context.voice_key_epoch = self.credential.key_epoch

        await runtime._prepare_pet(context)

        self.assertEqual(len(created), 1)
        self.assertIs(context.pet_uploader, created[0])
        self.assertEqual(runtime.pet_snapshot().watch_id, self.credential.watch_id)
        self.assertEqual(runtime.pet_snapshot().active_hash, b"H" * 32)

    async def test_missing_optional_service_does_not_touch_input_or_voice(self):
        events = []

        class Uploader:
            async def query(self):
                raise PetUnsupportedError("Firmware update required")

            async def close(self):
                events.append("pet.close")

        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: Uploader(),
            pet_cache=self.cache,
        )
        transport = FakeTransport(events=events)
        context = _BleSessionContext(transport)
        context.credential = self.credential
        context.voice_key = self.credential.master_key
        context.voice_key_epoch = self.credential.key_epoch

        await runtime._prepare_pet(context)

        self.assertEqual(
            runtime.pet_snapshot().capability, PetCapability.UNSUPPORTED
        )
        self.assertEqual(events, [])
        self.assertEqual(transport.input_disable_count, 0)
        self.assertEqual(transport.voice_payloads, [])

    async def test_unauthenticated_context_never_constructs_uploader(self):
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: self.fail(
                "unauthenticated pet uploader was constructed"
            ),
            pet_cache=self.cache,
        )

        await runtime._prepare_pet(_BleSessionContext(FakeTransport()))

        self.assertEqual(
            runtime.pet_snapshot().capability, PetCapability.OFFLINE
        )

    async def test_one_request_slot_reports_busy_and_explicit_cancel_finishes_active(self):
        started = asyncio.Event()
        release = asyncio.Event()
        cancel_calls = []

        class Uploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def upload(self, package, progress, *, request_id=None):
                progress(
                    PetProgress(
                        request_id,
                        "sending",
                        0,
                        len(package.encoded),
                    )
                )
                started.set()
                await release.wait()
                progress(
                    PetProgress(
                        request_id,
                        "installed",
                        len(package.encoded),
                        len(package.encoded),
                    )
                )
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "custom",
                    1,
                    1,
                    1,
                    package.sha256,
                    True,
                )

            async def cancel(self, request_id):
                cancel_calls.append(request_id)
                release.set()

            async def close(self):
                release.set()

        self_watch_id = self.credential.watch_id
        uploader = Uploader()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: uploader,
            pet_cache=self.cache,
        )
        context = _BleSessionContext(FakeTransport())
        context.credential = self.credential
        context.voice_key = self.credential.master_key
        context.voice_key_epoch = self.credential.key_epoch
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        first_states = []
        installed_snapshot_hashes = []
        busy_states = []

        def receive_first(progress):
            first_states.append(progress)
            if progress.phase == "installed":
                installed_snapshot_hashes.append(
                    runtime.pet_snapshot().active_hash
                )

        first = PetRequest(
            "request-one", PetOperation.UPLOAD, self.package, receive_first
        )
        second = PetRequest(
            "request-two", PetOperation.UPLOAD, self.package, busy_states.append
        )

        self.assertTrue(runtime.submit_pet_request(first))
        await asyncio.wait_for(started.wait(), 1)
        self.assertFalse(runtime.submit_pet_request(second))
        self.assertEqual(busy_states[-1].error_code, "busy")
        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "request-one",
                    PetOperation.CANCEL,
                    None,
                    lambda _state: None,
                )
            )
        )
        for _ in range(100):
            if runtime._pet_request is None:
                break
            await asyncio.sleep(0.01)

        self.assertEqual(cancel_calls, ["request-one"])
        self.assertEqual(runtime.pet_snapshot().active_hash, self.package.sha256)
        self.assertEqual(first_states[-1].phase, "installed")
        self.assertEqual(installed_snapshot_hashes, [self.package.sha256])
        self.assertEqual(
            self.cache.stored,
            [(self.credential.watch_id, self.package)],
        )

    async def test_repeated_cancel_ingress_owns_one_bounded_cancel_task(self):
        started = asyncio.Event()
        finish = asyncio.Event()
        cancel_calls = []

        class Uploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def upload(self, package, progress, *, request_id=None):
                started.set()
                await finish.wait()
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "custom",
                    1,
                    1,
                    1,
                    package.sha256,
                    True,
                )

            async def cancel(self, request_id):
                cancel_calls.append(request_id)
                await finish.wait()

            async def close(self):
                finish.set()

        self_watch_id = self.credential.watch_id
        uploader = Uploader()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: uploader,
            pet_cache=self.cache,
        )
        context = _BleSessionContext(FakeTransport())
        context.credential = self.credential
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "cancel-flood",
                    PetOperation.UPLOAD,
                    self.package,
                    lambda _progress: None,
                )
            )
        )
        await asyncio.wait_for(started.wait(), 1)

        for _ in range(32):
            self.assertTrue(
                runtime.submit_pet_request(
                    PetRequest(
                        "cancel-flood",
                        PetOperation.CANCEL,
                        None,
                        lambda _progress: None,
                    )
                )
            )
        for _ in range(100):
            if cancel_calls:
                break
            await asyncio.sleep(0)

        self.assertEqual(cancel_calls, ["cancel-flood"])
        finish.set()

    async def test_hung_cancel_times_out_without_leaking_cancel_ownership(self):
        started = asyncio.Event()
        finish = asyncio.Event()

        class Uploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def upload(self, package, progress, *, request_id=None):
                started.set()
                await finish.wait()
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "custom",
                    1,
                    1,
                    1,
                    package.sha256,
                    True,
                )

            async def cancel(self, request_id):
                await asyncio.Event().wait()

            async def close(self):
                finish.set()

        self_watch_id = self.credential.watch_id
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: Uploader(),
            pet_cache=self.cache,
            disconnect_timeout=0.01,
        )
        context = _BleSessionContext(FakeTransport())
        context.credential = self.credential
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "hung-cancel",
                    PetOperation.UPLOAD,
                    self.package,
                    lambda _progress: None,
                )
            )
        )
        await asyncio.wait_for(started.wait(), 1)
        cancel_progress = []

        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "hung-cancel",
                    PetOperation.CANCEL,
                    None,
                    cancel_progress.append,
                )
            )
        )
        for _ in range(100):
            if runtime._pet_cancel_task is None:
                break
            await asyncio.sleep(0.01)

        self.assertIsNone(runtime._pet_cancel_task)
        self.assertEqual(cancel_progress[-1].error_code, "cancel_failed")
        busy_progress = []
        self.assertFalse(
            runtime.submit_pet_request(
                PetRequest(
                    "after-cancel",
                    PetOperation.RESTORE,
                    None,
                    busy_progress.append,
                )
            )
        )
        self.assertEqual(busy_progress[-1].error_code, "busy")

        finish.set()
        for _ in range(100):
            if runtime._pet_request is None:
                break
            await asyncio.sleep(0.01)
        self.assertIsNone(runtime._pet_request)

    async def test_restore_removes_cache_only_after_authoritative_state(self):
        class Uploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "custom",
                    1,
                    1,
                    4,
                    self.package_hash,
                    True,
                )

            async def restore(self, progress, *, request_id=None):
                progress(PetProgress(request_id, "applying", 0, 0))
                progress(PetProgress(request_id, "installed", 0, 0))
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    5,
                    bytes(32),
                    True,
                )

            async def close(self):
                pass

            package_hash = self.package.sha256

        self_watch_id = self.credential.watch_id
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: Uploader(),
            pet_cache=self.cache,
        )
        context = _BleSessionContext(FakeTransport())
        context.credential = self.credential
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        states = []

        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "restore",
                    PetOperation.RESTORE,
                    None,
                    states.append,
                )
            )
        )
        for _ in range(100):
            if runtime._pet_request is None:
                break
            await asyncio.sleep(0.01)

        self.assertEqual(self.cache.removed, [self.credential.watch_id])
        self.assertEqual(runtime.pet_snapshot().source, "built-in")
        self.assertEqual(states[-1].phase, "installed")

    async def test_schema_mismatch_disables_upload_but_preserves_restore(self):
        upload_calls = []
        restore_calls = []

        class Uploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "custom",
                    2,
                    2,
                    4,
                    package_hash,
                    True,
                )

            async def upload(self, *args, **kwargs):
                upload_calls.append((args, kwargs))

            async def restore(self, progress, *, request_id=None):
                restore_calls.append(request_id)
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    2,
                    2,
                    5,
                    bytes(32),
                    True,
                )

            async def close(self):
                pass

        self_watch_id = self.credential.watch_id
        package_hash = self.package.sha256
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: Uploader(),
            pet_cache=self.cache,
        )
        context = _BleSessionContext(FakeTransport())
        context.credential = self.credential
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        upload_states = []

        self.assertFalse(
            runtime.submit_pet_request(
                PetRequest(
                    "schema-upload",
                    PetOperation.UPLOAD,
                    self.package,
                    upload_states.append,
                )
            )
        )
        self.assertEqual(upload_states[-1].error_code, "unsupported")
        self.assertEqual(upload_calls, [])
        self.assertTrue(runtime.pet_snapshot().usable)

        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "schema-restore",
                    PetOperation.RESTORE,
                    None,
                    lambda _progress: None,
                )
            )
        )
        for _ in range(100):
            if runtime._pet_request is None:
                break
            await asyncio.sleep(0.01)
        self.assertEqual(restore_calls, ["schema-restore"])
        self.assertEqual(runtime.pet_snapshot().source, "built-in")

    async def test_restore_reconciliation_publishes_actual_custom_state(self):
        class Uploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "custom",
                    1,
                    1,
                    4,
                    package_hash,
                    True,
                )

            async def restore(self, progress, *, request_id=None):
                progress(PetProgress(request_id, "applying", 0, 0))
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "custom",
                    1,
                    1,
                    5,
                    package_hash,
                    True,
                )

            async def close(self):
                pass

        self_watch_id = self.credential.watch_id
        package_hash = self.package.sha256
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: Uploader(),
            pet_cache=self.cache,
        )
        context = _BleSessionContext(FakeTransport())
        context.credential = self.credential
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        states = []

        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "restore-ambiguous",
                    PetOperation.RESTORE,
                    None,
                    states.append,
                )
            )
        )
        for _ in range(100):
            if runtime._pet_request is None:
                break
            await asyncio.sleep(0.01)

        self.assertEqual(runtime.pet_snapshot().source, "custom")
        self.assertEqual(runtime.pet_snapshot().generation, 5)
        self.assertEqual(states[-1].phase, "error")
        self.assertEqual(self.cache.removed, [])

    async def test_failed_restore_reconciliation_marks_runtime_offline(self):
        class Uploader:
            def __init__(self):
                self.watch_state = ready_state

            async def query(self):
                return self.watch_state

            async def restore(self, progress, *, request_id=None):
                self.watch_state = dataclasses.replace(
                    ready_state,
                    capability=PetCapability.OFFLINE,
                    usable=False,
                )
                raise PetUploadError("restore result could not be reconciled")

            async def close(self):
                pass

        self_watch_id = self.credential.watch_id
        ready_state = PetWatchState(
            self_watch_id,
            PetCapability.READY,
            "custom",
            1,
            1,
            4,
            self.package.sha256,
            True,
        )
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: Uploader(),
            pet_cache=self.cache,
        )
        context = _BleSessionContext(FakeTransport())
        context.credential = self.credential
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()

        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "restore-offline",
                    PetOperation.RESTORE,
                    None,
                    lambda _progress: None,
                )
            )
        )
        for _ in range(100):
            if runtime._pet_request is None:
                break
            await asyncio.sleep(0.01)

        self.assertEqual(
            runtime.pet_snapshot().capability, PetCapability.OFFLINE
        )
        self.assertFalse(runtime.pet_snapshot().usable)

    async def test_reconnect_constructs_new_uploader_and_requeries(self):
        queries = []

        class Uploader:
            def __init__(self, generation):
                self.generation = generation

            async def query(self):
                queries.append(self.generation)
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    self.generation,
                    bytes(32),
                    True,
                )

            async def close(self):
                pass

        self_watch_id = self.credential.watch_id
        uploaders = iter((Uploader(1), Uploader(2)))
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: next(uploaders),
            pet_cache=self.cache,
        )
        first = _BleSessionContext(FakeTransport())
        first.credential = self.credential
        await runtime._prepare_pet(first)
        await runtime._retire_pet_uploader(first)
        second = _BleSessionContext(FakeTransport())
        second.credential = self.credential

        await runtime._prepare_pet(second)

        self.assertEqual(queries, [1, 2])
        self.assertEqual(runtime.pet_snapshot().generation, 2)

    async def test_credential_replacement_closes_old_pet_before_new_epoch(self):
        events = []
        updated = dataclasses.replace(
            self.credential, master_key=b"N" * 16, key_epoch=10
        )

        class OldUploader:
            async def close(self):
                events.append("old.close")

        class NewUploader:
            async def query(self):
                events.append("new.query")
                return PetWatchState(
                    updated.watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def close(self):
                pass

        def factory(_transport, key, epoch, _nonce_factory, *, watch_id):
            events.append(("factory", key, epoch, watch_id))
            return NewUploader()

        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=factory,
            pet_cache=self.cache,
        )
        transport = FakeTransport(events=events)
        transport._input_disabled = False
        context = _BleSessionContext(transport)
        context.credential = self.credential
        context.voice_key = self.credential.master_key
        context.voice_key_epoch = self.credential.key_epoch
        context.pet_uploader = OldUploader()
        runtime._pet_uploader = context.pet_uploader
        runtime._prepare_voice = lambda _context: asyncio.sleep(0)

        self.assertTrue(await runtime._replace_voice_credential(context, updated))

        self.assertLess(events.index("old.close"), events.index("input.disable"))
        self.assertIn(("factory", b"N" * 16, 10, updated.watch_id), events)
        self.assertEqual(runtime.pet_snapshot().capability, PetCapability.READY)

    async def test_late_progress_from_replaced_credential_is_suppressed(self):
        events = []
        started = asyncio.Event()
        release = asyncio.Event()
        callbacks = []
        updated = dataclasses.replace(
            self.credential, master_key=b"N" * 16, key_epoch=10
        )

        class OldUploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def upload(self, package, progress, *, request_id=None):
                progress(PetProgress(request_id, "sending", 0, len(package.encoded)))
                started.set()
                await release.wait()
                progress(
                    PetProgress(
                        request_id,
                        "installed",
                        len(package.encoded),
                        len(package.encoded),
                    )
                )
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "custom",
                    1,
                    1,
                    1,
                    package.sha256,
                    True,
                )

            async def close(self):
                events.append("old.close")
                release.set()
                await asyncio.sleep(0)

        class NewUploader:
            async def query(self):
                events.append("new.query")
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def close(self):
                pass

        self_watch_id = self.credential.watch_id
        old = OldUploader()

        def factory(_transport, key, _epoch, _nonce_factory, *, watch_id):
            return old if key == self.credential.master_key else NewUploader()

        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=factory,
            pet_cache=self.cache,
        )
        context = _BleSessionContext(FakeTransport())
        context.credential = self.credential
        context.voice_key = self.credential.master_key
        context.voice_key_epoch = self.credential.key_epoch
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        runtime._prepare_voice = lambda _context: asyncio.sleep(0)
        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "old-request",
                    PetOperation.UPLOAD,
                    self.package,
                    callbacks.append,
                )
            )
        )
        await asyncio.wait_for(started.wait(), 1)
        count_before_rekey = len(callbacks)

        self.assertTrue(await runtime._replace_voice_credential(context, updated))
        await asyncio.sleep(0)

        self.assertEqual(len(callbacks), count_before_rekey)
        self.assertEqual(self.cache.stored, [])
        self.assertEqual(events[:2], ["old.close", "new.query"])

    async def test_rekey_cancels_and_awaits_stalled_old_upload_before_new_query(self):
        events = []
        started = asyncio.Event()
        release_write = asyncio.Event()
        operation_done = asyncio.Event()
        updated = dataclasses.replace(
            self.credential, master_key=b"N" * 16, key_epoch=10
        )

        class OldUploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def upload(self, package, progress, *, request_id=None):
                started.set()
                try:
                    await release_write.wait()
                    events.append("old.late.write")
                    return PetWatchState(
                        self_watch_id,
                        PetCapability.READY,
                        "custom",
                        1,
                        1,
                        1,
                        package.sha256,
                        True,
                    )
                finally:
                    operation_done.set()

            async def close(self):
                events.append("old.close")
                await operation_done.wait()

        class NewUploader:
            async def query(self):
                events.append("new.query")
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def close(self):
                pass

        self_watch_id = self.credential.watch_id
        old = OldUploader()

        def factory(_transport, key, _epoch, _nonce_factory, *, watch_id):
            return old if key == self.credential.master_key else NewUploader()

        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=factory,
            pet_cache=self.cache,
            disconnect_timeout=0.01,
        )
        transport = FakeTransport(events=events)
        context = _BleSessionContext(transport)
        context.credential = self.credential
        context.voice_key = self.credential.master_key
        context.voice_key_epoch = self.credential.key_epoch
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        runtime._prepare_voice = lambda _context: asyncio.sleep(0)
        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "stalled",
                    PetOperation.UPLOAD,
                    self.package,
                    lambda _progress: None,
                )
            )
        )
        await asyncio.wait_for(started.wait(), 1)

        self.assertTrue(await runtime._replace_voice_credential(context, updated))
        release_write.set()
        await asyncio.sleep(0)

        self.assertNotIn("old.late.write", events)
        self.assertEqual(events[-1], "new.query")

    async def test_rekey_fails_closed_when_old_upload_cannot_quiesce(self):
        events = []
        started = asyncio.Event()
        ignored_cancel = asyncio.Event()
        release = asyncio.Event()
        operation_done = asyncio.Event()
        updated = dataclasses.replace(
            self.credential, master_key=b"N" * 16, key_epoch=10
        )

        class OldUploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def upload(self, package, progress, *, request_id=None):
                started.set()
                try:
                    await release.wait()
                except asyncio.CancelledError:
                    ignored_cancel.set()
                    await release.wait()
                events.append("old.late.write")
                operation_done.set()
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "custom",
                    1,
                    1,
                    1,
                    package.sha256,
                    True,
                )

            async def close(self):
                events.append("old.close")
                await operation_done.wait()

        class NewUploader:
            async def query(self):
                events.append("new.query")

        self_watch_id = self.credential.watch_id
        old = OldUploader()

        def factory(_transport, key, _epoch, _nonce_factory, *, watch_id):
            return old if key == self.credential.master_key else NewUploader()

        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=factory,
            pet_cache=self.cache,
            disconnect_timeout=0.02,
        )
        context = _BleSessionContext(FakeTransport(events=events))
        context.credential = self.credential
        context.voice_key = self.credential.master_key
        context.voice_key_epoch = self.credential.key_epoch
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        runtime._prepare_voice = lambda _context: asyncio.sleep(0)
        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "stubborn",
                    PetOperation.UPLOAD,
                    self.package,
                    lambda _progress: None,
                )
            )
        )
        await asyncio.wait_for(started.wait(), 1)

        self.assertFalse(
            await runtime._replace_voice_credential(context, updated)
        )
        await asyncio.wait_for(ignored_cancel.wait(), 1)
        self.assertEqual(context.credential, self.credential)
        self.assertNotIn("new.query", events)
        self.assertEqual(
            runtime.pet_snapshot().capability, PetCapability.OFFLINE
        )

        release.set()
        for _ in range(100):
            if operation_done.is_set():
                break
            await asyncio.sleep(0.01)
        self.assertTrue(await runtime._retire_pet_uploader(context))

    async def test_rekey_fails_closed_when_old_uploader_close_fails(self):
        events = []
        updated = dataclasses.replace(
            self.credential, master_key=b"N" * 16, key_epoch=10
        )

        class OldUploader:
            async def close(self):
                events.append("old.close")
                raise RuntimeError("close failed")

        class NewUploader:
            async def query(self):
                events.append("new.query")

        def factory(_transport, key, _epoch, _nonce_factory, *, watch_id):
            return NewUploader()

        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=factory,
            pet_cache=self.cache,
        )
        context = _BleSessionContext(FakeTransport(events=events))
        context.credential = self.credential
        context.voice_key = self.credential.master_key
        context.voice_key_epoch = self.credential.key_epoch
        context.pet_uploader = OldUploader()
        context.pet_generation = 1
        runtime._pet_uploader = context.pet_uploader
        runtime._pet_generation = 1
        runtime._prepare_voice = lambda _context: asyncio.sleep(0)

        self.assertFalse(
            await runtime._replace_voice_credential(context, updated)
        )

        self.assertEqual(context.credential, self.credential)
        self.assertEqual(events, ["old.close"])
        self.assertEqual(
            runtime.pet_snapshot().capability, PetCapability.OFFLINE
        )

    async def test_shutdown_cancels_stalled_pet_before_transport_disconnect(self):
        events = []
        started = asyncio.Event()
        release_write = asyncio.Event()
        operation_done = asyncio.Event()

        class Uploader:
            async def query(self):
                return PetWatchState(
                    self_watch_id,
                    PetCapability.READY,
                    "built-in",
                    1,
                    1,
                    0,
                    bytes(32),
                    True,
                )

            async def upload(self, package, progress, *, request_id=None):
                started.set()
                try:
                    await release_write.wait()
                    events.append("pet.late.write")
                    return PetWatchState(
                        self_watch_id,
                        PetCapability.READY,
                        "custom",
                        1,
                        1,
                        1,
                        package.sha256,
                        True,
                    )
                finally:
                    operation_done.set()

            async def close(self):
                events.append("pet.close")
                await operation_done.wait()

        self_watch_id = self.credential.watch_id
        uploader = Uploader()
        runtime = AgentRuntime(
            collector=FakeCollector(),
            pet_uploader_factory=lambda *_args, **_kwargs: uploader,
            disconnect_timeout=0.1,
            pet_cache=self.cache,
        )
        transport = FakeTransport(events=events)
        transport._connected = True
        context = _BleSessionContext(transport)
        context.credential = self.credential
        await runtime._prepare_pet(context)
        runtime._loop = asyncio.get_running_loop()
        self.assertTrue(
            runtime.submit_pet_request(
                PetRequest(
                    "shutdown-stalled",
                    PetOperation.UPLOAD,
                    self.package,
                    lambda _progress: None,
                )
            )
        )
        await asyncio.wait_for(started.wait(), 1)

        await runtime._shutdown_session(context, False)
        release_write.set()
        await asyncio.sleep(0)

        self.assertNotIn("pet.late.write", events)
        self.assertLess(events.index("pet.close"), events.index("ble.disconnect"))

    async def test_agent_quit_retires_pet_before_other_channels(self):
        events = []

        class Uploader:
            async def close(self):
                events.append("pet.close")

        runtime = AgentRuntime(
            collector=FakeCollector(),
            disconnect_timeout=0.2,
            pet_cache=self.cache,
        )
        transport = FakeTransport(events=events)
        transport._connected = True
        transport._input_disabled = False
        transport._voice_disabled = False
        context = _BleSessionContext(transport)
        context.pet_uploader = Uploader()
        context.pet_generation = 1
        runtime._pet_generation = 1
        runtime._pet_uploader = context.pet_uploader

        await runtime._shutdown_session(context, False)

        self.assertLess(events.index("pet.close"), events.index("input.disable"))
        self.assertLess(events.index("input.disable"), events.index("voice.disable"))
        self.assertLess(events.index("voice.disable"), events.index("ble.disconnect"))
        self.assertEqual(
            runtime.pet_snapshot().capability, PetCapability.OFFLINE
        )


class PrivacyDocumentationContractTests(unittest.TestCase):
    def test_readmes_document_secure_task_detail_boundary(self):
        root = pathlib.Path(__file__).resolve().parents[2]
        for path in (root / "README.md", root / "mac-codex-agent/README.md"):
            with self.subTest(path=path):
                normalized = " ".join(
                    path.read_text(encoding="utf-8").split()
                ).lower()
                self.assertIn(
                    "task names and status use the existing status channel",
                    normalized,
                )
                self.assertIn(
                    "exact last replies are encrypted and authenticated",
                    normalized,
                )
                self.assertIn(
                    "paired master key and its key epoch", normalized
                )
                self.assertIn(
                    "reply plaintext is never sent or logged", normalized
                )
                self.assertIn("route metadata remains visible", normalized)
                self.assertNotIn(
                    "never oauth credentials, prompts, responses, or source code",
                    normalized,
                )
                self.assertNotIn(
                    "does not send response text to the watch", normalized
                )


class FakeRuntime:
    def __init__(self):
        self.commands = []
        self.voice_refresh_count = 0
        self.voice_setup_failures = []
        self.voice_setup_results = []
        self.voice_setup_token = object()
        self.agent_names = []
        self.pet_requests = []
        self.pet_state = PetWatchState(
            None,
            PetCapability.OFFLINE,
            "built-in",
            0,
            0,
            0,
            bytes(32),
            False,
        )

    def submit(self, command):
        self.commands.append(command)

    def set_agent_name(self, value):
        self.agent_names.append(value)

    def refresh_voice_readiness(self):
        self.voice_refresh_count += 1

    def begin_voice_setup(self):
        return self.voice_setup_token

    def report_voice_setup_failure(self, token, detail):
        self.voice_setup_failures.append((token, detail))

    def report_voice_setup_result(self, token, state):
        self.voice_setup_results.append((token, state))

    def submit_pet_request(self, request):
        self.pet_requests.append(request)
        return True

    def pet_snapshot(self):
        return self.pet_state


class AgentControllerTests(unittest.TestCase):
    def test_pet_controller_methods_submit_immutable_runtime_requests(self):
        runtime = FakeRuntime()
        controller = AgentController(
            preferences=MemoryPreferences(True), runtime=runtime
        )
        pixels = WIDTH * HEIGHT
        package = encode_package(
            WIDTH,
            HEIGHT,
            (0x001F,),
            1,
            (PetFrame(120, bytes([0]) * pixels),),
        )
        progress = []

        upload_id = controller.upload_pet(package, progress.append)
        controller.cancel_pet(upload_id)
        restore_id = controller.restore_default(progress.append)

        self.assertEqual(
            [request.operation for request in runtime.pet_requests],
            [PetOperation.UPLOAD, PetOperation.CANCEL, PetOperation.RESTORE],
        )
        self.assertIs(runtime.pet_requests[0].package, package)
        self.assertIsNone(runtime.pet_requests[1].package)
        self.assertNotEqual(upload_id, restore_id)
        self.assertEqual(controller.pet_snapshot(), runtime.pet_state)

    def test_forget_current_watch_removes_credential_and_reconnects(self):
        class Authorizer:
            def __init__(self):
                self.forgot = 0

            def forget_current_watch(self):
                self.forgot += 1
                return True

        runtime = FakeRuntime()
        authorizer = Authorizer()
        controller = AgentController(
            preferences=MemoryPreferences(True),
            runtime=runtime,
            secure_authorizer=authorizer,
        )

        self.assertTrue(controller.forget_current_watch())

        self.assertEqual(authorizer.forgot, 1)
        self.assertEqual(runtime.commands, [AgentCommand.RECONNECT])

    def test_forget_current_watch_does_not_reconnect_without_known_watch(self):
        class Authorizer:
            def forget_current_watch(self):
                return False

        runtime = FakeRuntime()
        controller = AgentController(
            preferences=MemoryPreferences(True),
            runtime=runtime,
            secure_authorizer=Authorizer(),
        )

        self.assertFalse(controller.forget_current_watch())

        self.assertEqual(runtime.commands, [])

    def test_rename_persists_name_updates_menu_and_reconnects_for_signed_sync(self):
        class IdentityStore:
            def load_or_create(self):
                return type("Identity", (), {"display_name": "Studio Mac"})()

            def rename(self, value):
                self.requested = value
                return "Travel Agent"

        runtime = FakeRuntime()
        identity_store = IdentityStore()
        controller = AgentController(
            preferences=MemoryPreferences(True),
            runtime=runtime,
            identity_store=identity_store,
        )

        result = controller.rename_agent("  Travel Agent  ")

        self.assertEqual(result, "Travel Agent")
        self.assertEqual(identity_store.requested, "  Travel Agent  ")
        self.assertEqual(runtime.agent_names, ["Studio Mac", "Travel Agent"])
        self.assertEqual(runtime.commands, [AgentCommand.RECONNECT])

    def test_voice_setup_result_is_reported_through_the_runtime(self):
        runtime = FakeRuntime()
        controller = AgentController(
            preferences=MemoryPreferences(True), runtime=runtime
        )

        token = controller.begin_voice_setup()
        controller.report_voice_setup_result(
            token,
            DriverState.RESTART_REQUIRED,
        )

        self.assertEqual(
            runtime.voice_setup_results,
            [(runtime.voice_setup_token, DriverState.RESTART_REQUIRED)],
        )
        self.assertEqual(runtime.commands, [])

    def test_voice_setup_failure_is_reported_through_the_runtime(self):
        runtime = FakeRuntime()
        controller = AgentController(
            preferences=MemoryPreferences(True), runtime=runtime
        )

        token = controller.begin_voice_setup()
        controller.report_voice_setup_failure(
            token,
            "Driver package verification failed",
        )

        self.assertEqual(
            runtime.voice_setup_failures,
            [
                (
                    runtime.voice_setup_token,
                    "Driver package verification failed",
                )
            ],
        )
        self.assertEqual(runtime.commands, [])

    def test_voice_readiness_refresh_is_scoped_to_the_runtime(self):
        runtime = FakeRuntime()
        controller = AgentController(
            preferences=MemoryPreferences(True), runtime=runtime
        )

        controller.refresh_voice_readiness()

        self.assertEqual(runtime.voice_refresh_count, 1)
        self.assertEqual(runtime.commands, [])

    def test_stop_does_not_wait_for_blocked_voice_key_loader(self):
        loader_started = threading.Event()
        emergency_release = threading.Event()
        events = []

        def blocked_loader():
            loader_started.set()
            emergency_release.wait()
            return bytes(range(16))

        factory = FakeTransportFactory([{"events": events}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=blocked_loader,
            status_interval=0.01,
            disconnect_timeout=0.03,
        )
        controller = AgentController(
            preferences=MemoryPreferences(True), runtime=runtime
        )
        controller.start()
        self.assertTrue(loader_started.wait(1))

        started = time.monotonic()
        stopped = controller.stop(timeout=0.5)
        elapsed = time.monotonic() - started
        if not stopped:
            emergency_release.set()
            controller.stop(timeout=1)

        self.assertTrue(stopped)
        self.assertLess(elapsed, 0.25)
        self.assertEqual(events.count("ble.disconnect"), 1)

    def test_stop_honors_voice_shutdown_deadline_with_stalled_real_drain(self):
        key = bytes(range(16))
        begin = pack_notification(
            key,
            PTT_BEGIN,
            12,
            0,
            100,
            BEGIN_BODY.pack(IMA_ADPCM, PUSH_TO_TALK, 16000, 320, TEST_TASK_ROUTE),
            b"abcdefgh",
        )
        recording_started = threading.Event()
        drain_holder = {}

        async def stalled_drain(_seconds):
            drain_holder["loop"] = asyncio.get_running_loop()
            drain_holder["release"] = asyncio.Event()
            await drain_holder["release"].wait()

        class Resource:
            def __init__(self, marks_recording=False):
                self.calls = []
                self.marks_recording = marks_recording

            def prepare(self):
                pass

            def start(self):
                self.calls.append("start")
                if self.marks_recording:
                    recording_started.set()

            def stop(self):
                self.calls.append("stop")

            def close(self):
                pass

            def write(self, samples):
                return len(samples)

        route = Resource()
        shortcut = Resource(marks_recording=True)
        service = VoiceSessionService(
            route,
            shortcut,
            key,
            readiness_checker=lambda: VoiceState.READY,
            sleep=stalled_drain,
        )
        factory = FakeTransportFactory([{"voice_notification": begin}])
        runtime = AgentRuntime(
            collector=FakeCollector(),
            transport_factory=factory,
            voice_key_loader=lambda: key,
            voice_session_factory=lambda _key: service,
            voice_nonce_factory=lambda: b"12345678",
            status_interval=0.01,
            disconnect_timeout=0.03,
        )
        controller = AgentController(
            preferences=MemoryPreferences(True), runtime=runtime
        )
        controller.start()
        self.assertTrue(recording_started.wait(1))

        started = time.monotonic()
        stopped = controller.stop(timeout=0.5)
        elapsed = time.monotonic() - started
        if not stopped and "release" in drain_holder:
            drain_holder["loop"].call_soon_threadsafe(
                drain_holder["release"].set
            )
            controller.stop(timeout=1)

        self.assertTrue(stopped)
        self.assertLess(elapsed, 0.25)
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(factory.instances[0].disconnect_count, 1)

    def test_connection_setting_is_persisted_before_command(self):
        preferences = MemoryPreferences()
        runtime = FakeRuntime()
        controller = AgentController(preferences=preferences, runtime=runtime)

        controller.set_connection_enabled(False)

        self.assertFalse(preferences.connection_enabled())
        self.assertEqual(runtime.commands, [AgentCommand.DISABLE])


if __name__ == "__main__":
    unittest.main()
