import asyncio
import json
import pathlib
import sys
import threading
import time
import unittest
from unittest.mock import patch


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from app_state import ConnectionState, MemoryPreferences
from agent import VOICE_CONTROL_UUID, BleStatusTransport
from input_action_protocol import (
    InputAction,
    decode_ready as decode_input_ready,
    encode_action,
)
from protocol import EVENT_MESSAGE, STATUS_MESSAGE
from runtime import AgentCommand, AgentController, AgentRuntime, _create_voice_session
from voice_audio import VoiceAudioDeviceNotFound, VoiceAudioError
from voice_driver import DriverState
from voice_input_crypto import VoiceKeyError
from voice_input_protocol import (
    ACCESSIBILITY_REQUIRED,
    BEGIN_BODY,
    DRIVER_REQUIRED,
    IMA_ADPCM,
    PTT_BEGIN,
    PTT_END,
    READY,
    RESTART_REQUIRED,
    TYPELESS_NOT_RUNNING,
    UNAVAILABLE,
    decode_control,
    pack_notification,
    EndSummary,
)
from voice_session import VoiceSessionService, VoiceState


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

    def setUp(self):
        self.keychain_patch = patch(
            "runtime.load_key_from_keychain",
            side_effect=VoiceKeyError("test Keychain has no voice key"),
        )
        self.keychain_patch.start()
        self.addCleanup(self.keychain_patch.stop)

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
                self.assertIn("voice.disable", events)
                self.assertEqual(transport.input_disable_count, 0)
                session.cancel()
                with self.assertRaises(asyncio.CancelledError):
                    await session
                self.assertGreater(
                    events.index("input.disable"), events.index("voice.disable")
                )
                self.assertEqual(transport.input_disable_count, 1)
                ready = transport.input_payloads[0][1]
                self.assertEqual(decode_input_ready(key, ready), nonce)

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

    async def test_non_ready_states_send_exact_authenticated_unavailable(self):
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
                    status_interval=0.01,
                )

                result = await runtime._run_session()

                self.assertEqual(result, "disconnected")
                controls = factory.instances[0].voice_payloads
                self.assertEqual(len(controls), 1)
                self.assertEqual(controls[0][0], "unavailable")
                control = decode_control(key, controls[0][1])
                self.assertEqual(control.kind, UNAVAILABLE)
                self.assertEqual(control.reason, reason)
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
            BEGIN_BODY.pack(IMA_ADPCM, 16000, 320),
            b"12345678",
        )
        factory = FakeTransportFactory(
            [{"block_voice_enable": True, "voice_notification": begin}]
        )

        class Resource:
            def __init__(self):
                self.calls = []

            def start(self):
                self.calls.append("start")

            def stop(self):
                self.calls.append("stop")

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
            BEGIN_BODY.pack(IMA_ADPCM, 16000, 320),
            b"abcdefgh",
        )

        class Resource:
            def __init__(self):
                self.calls = []

            def start(self):
                self.calls.append("start")

            def stop(self):
                self.calls.append("stop")

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
            BEGIN_BODY.pack(IMA_ADPCM, 16000, 320),
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

            def start(self):
                self.calls.append("start")

            def stop(self):
                self.calls.append("stop")

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
            BEGIN_BODY.pack(IMA_ADPCM, 16000, 320),
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

            def start(self):
                self.calls.append("start")

            def stop(self):
                self.calls.append("stop")

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


class FakeRuntime:
    def __init__(self):
        self.commands = []
        self.voice_refresh_count = 0
        self.voice_setup_failures = []
        self.voice_setup_results = []
        self.voice_setup_token = object()

    def submit(self, command):
        self.commands.append(command)

    def refresh_voice_readiness(self):
        self.voice_refresh_count += 1

    def begin_voice_setup(self):
        return self.voice_setup_token

    def report_voice_setup_failure(self, token, detail):
        self.voice_setup_failures.append((token, detail))

    def report_voice_setup_result(self, token, state):
        self.voice_setup_results.append((token, state))


class AgentControllerTests(unittest.TestCase):
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
            BEGIN_BODY.pack(IMA_ADPCM, 16000, 320),
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

            def start(self):
                self.calls.append("start")
                if self.marks_recording:
                    recording_started.set()

            def stop(self):
                self.calls.append("stop")

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
