import asyncio
import json
import pathlib
import sys
import unittest
from unittest.mock import patch


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from app_state import ConnectionState, MemoryPreferences
from protocol import EVENT_MESSAGE, STATUS_MESSAGE
from runtime import AgentCommand, AgentController, AgentRuntime
from voice_input_crypto import VoiceKeyError
from voice_input_protocol import (
    ACCESSIBILITY_REQUIRED,
    DRIVER_REQUIRED,
    READY,
    RESTART_REQUIRED,
    TYPELESS_NOT_RUNNING,
    UNAVAILABLE,
    decode_control,
)
from voice_session import VoiceState


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
        voice_notification=None,
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
        self.voice_notification = voice_notification
        self.connect_started = asyncio.Event()
        self.connected_event = asyncio.Event()
        self.disconnected_event = asyncio.Event()
        self._connect_release = asyncio.Event()
        self._connected = False
        self.disconnect_count = 0
        self.messages = []
        self.voice_failure = None
        self.voice_payloads = []
        self._voice_disabled = False

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


async def wait_for_messages(transport, count):
    for _ in range(100):
        if len(transport.messages) >= count:
            return
        await asyncio.sleep(0.01)
    raise AssertionError("message count did not reach {}".format(count))


class AgentRuntimeTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.keychain_patch = patch(
            "runtime.load_key_from_keychain",
            side_effect=VoiceKeyError("test Keychain has no voice key"),
        )
        self.keychain_patch.start()
        self.addCleanup(self.keychain_patch.stop)

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
            events[-3:],
            ["voice.disable", "voice.disconnect", "ble.disconnect"],
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

        with self.assertRaisesRegex(ConnectionError, "READY write failed"):
            await runtime._run_session()

        self.assertEqual(
            events[-3:],
            ["voice.disable", "voice.disconnect", "ble.disconnect"],
        )

    async def test_voice_consumer_failure_ends_session_and_cleans_up(self):
        events = []
        factory = FakeTransportFactory(
            [
                {
                    "events": events,
                    "voice_failure_error": RuntimeError("voice notification queue overflow"),
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

        with self.assertRaisesRegex(RuntimeError, "queue overflow"):
            await runtime._run_session()

        self.assertEqual(
            events[-3:],
            ["voice.disable", "voice.disconnect", "ble.disconnect"],
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

    def submit(self, command):
        self.commands.append(command)


class AgentControllerTests(unittest.TestCase):
    def test_connection_setting_is_persisted_before_command(self):
        preferences = MemoryPreferences()
        runtime = FakeRuntime()
        controller = AgentController(preferences=preferences, runtime=runtime)

        controller.set_connection_enabled(False)

        self.assertFalse(preferences.connection_enabled())
        self.assertEqual(runtime.commands, [AgentCommand.DISABLE])


if __name__ == "__main__":
    unittest.main()
