import asyncio
import json
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from app_state import ConnectionState, MemoryPreferences
from protocol import EVENT_MESSAGE, STATUS_MESSAGE
from runtime import AgentCommand, AgentController, AgentRuntime


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
    ):
        self.connect_result = connect_result
        self.connect_error = connect_error
        self.block_connect = block_connect
        self.block_disconnect = block_disconnect
        self.send_error = send_error
        self.connect_started = asyncio.Event()
        self.connected_event = asyncio.Event()
        self.disconnected_event = asyncio.Event()
        self._connect_release = asyncio.Event()
        self._connected = False
        self.disconnect_count = 0
        self.messages = []

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
        if self.block_disconnect:
            await asyncio.Event().wait()

    async def send_json(self, payload, message_type):
        if self.send_error is not None:
            raise self.send_error
        self.messages.append((message_type, payload))


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
