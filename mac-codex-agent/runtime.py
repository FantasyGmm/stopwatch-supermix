"""Serialized BLE lifecycle and the menu application's thread controller."""

from __future__ import annotations

import asyncio
import enum
import logging
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


LOGGER = logging.getLogger(__name__)
DEFAULT_RETRY_DELAYS = (1.0, 2.0, 4.0, 8.0, 16.0, 30.0)


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
    ):
        self._collector = collector
        self._transport_factory = transport_factory
        self._connection_enabled = connection_enabled
        self._status_interval = status_interval
        self._retry_delays = tuple(retry_delays) or (1.0,)
        self._disconnect_timeout = disconnect_timeout
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
                codex=previous.codex,
            )
            self._snapshot = snapshot
        if previous.state is not state or previous.detail != detail:
            LOGGER.info("Bluetooth state: %s%s", state.value, ": " + detail if detail else "")

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
        transport = self._transport_factory()
        self._transition(ConnectionState.SCANNING)
        try:
            connected = await transport.connect(
                lambda: self._transition(ConnectionState.CONNECTING)
            )
            if not connected:
                return "not_found"
            self._transition(ConnectionState.CONNECTED)
            status_agent = CodexStatusAgent(self._collector, transport)
            if self._status_ready is not None:
                await self._status_ready.wait()
            while transport.connected:
                await status_agent.tick(status=dict(self._latest_status))
                await asyncio.sleep(self._status_interval)
            return "disconnected"
        finally:
            try:
                await asyncio.wait_for(
                    transport.disconnect(), timeout=self._disconnect_timeout
                )
            except asyncio.TimeoutError:
                LOGGER.warning("Bluetooth disconnect timed out")
            except Exception:
                LOGGER.exception("Bluetooth disconnect failed")


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
