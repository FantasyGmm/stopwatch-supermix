import hashlib
import pathlib
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from AppKit import (
    NSAlertFirstButtonReturn,
    NSApplication,
    NSColor,
    NSControlStateValueOn,
)

from app_state import (
    AgentSnapshot,
    CodexSnapshot,
    ConnectionState,
    PairingState,
    TraeSnapshot,
)
from menu_app import MenuBarApplication, confirm_voice_setup, prompt_agent_name
from voice_driver import BlackHoleInstaller, DriverInstallError, DriverState
from voice_session import VoiceState


class FakeController:
    def __init__(
        self,
        voice_state=VoiceState.READY,
        pairing_state=PairingState.PAIRED,
    ):
        self.started = False
        self.stopped = False
        self.connection_values = []
        self.reconnect_count = 0
        self.voice_refresh_count = 0
        self.voice_state = voice_state
        self.voice_detail = ""
        self.voice_setup_failures = []
        self.voice_setup_results = []
        self.voice_revision = 0
        self.agent_name = "Studio Mac"
        self.pairing_state = pairing_state
        self.renamed_agents = []
        self.forgot_watch_count = 0
        self.pet_snapshot_value = object()

    def start(self):
        self.started = True

    def snapshot(self):
        return AgentSnapshot(
            ConnectionState.CONNECTED,
            True,
            voice_state=self.voice_state,
            voice_detail=self.voice_detail,
            agent_name=self.agent_name,
            pairing_state=self.pairing_state,
            verification_code=(
                "123456"
                if self.pairing_state is PairingState.AWAITING_CONFIRMATION
                else ""
            ),
            watch_identity_suffix="0E0F",
            codex=CodexSnapshot.from_status(
                {
                    "codex_model": "gpt-5.3",
                    "codex_effort": "high",
                    "codex_5h_left_pct": 72,
                    "codex_week_left_pct": 64,
                    "codex_alert_pending": 1,
                    "codex_alert_running": 1,
                    "codex_alert_completed": 1,
                    "codex_alert_revision": 4,
                    "codex_alerts": [
                        {
                            "project": "StopWatch",
                            "title": "Task alert support",
                            "time": "09:41",
                            "state": "pending",
                            "outcome": "waiting",
                        },
                        {
                            "project": "Docs",
                            "title": "Update guide",
                            "time": "09:02",
                            "state": "running",
                            "outcome": "running",
                        },
                        {
                            "project": "Website",
                            "title": "Header fix",
                            "time": "08:12",
                            "state": "completed",
                            "outcome": "completed",
                        },
                    ],
                }
            ),
            trae=TraeSnapshot.from_status(
                {
                    "trae_online": True,
                    "trae_model": "Model A",
                    "trae_effort": "high",
                    "trae_fresh": True,
                    "trae_tasks": [
                        {
                            "project": "StopWatch",
                            "title": "Trae status page",
                            "detail": "Building menu state",
                            "state": "running",
                        }
                    ],
                }
            ),
        )

    def set_connection_enabled(self, enabled):
        self.connection_values.append(enabled)

    def reconnect(self):
        self.reconnect_count += 1

    def rename_agent(self, value):
        self.renamed_agents.append(value)
        self.agent_name = value.strip()
        self.reconnect_count += 1
        return self.agent_name

    def forget_current_watch(self):
        self.forgot_watch_count += 1
        self.reconnect_count += 1
        return True

    def refresh_voice_readiness(self):
        self.voice_refresh_count += 1

    def pet_snapshot(self):
        return self.pet_snapshot_value

    def begin_voice_setup(self):
        return (
            self.voice_revision
            if self.voice_state is VoiceState.DRIVER_REQUIRED
            else None
        )

    def report_voice_setup_failure(self, token, detail):
        if (
            token != self.voice_revision
            or self.voice_state is not VoiceState.DRIVER_REQUIRED
        ):
            return False
        self.voice_setup_failures.append(detail)
        self.voice_revision += 1
        self.voice_state = VoiceState.DRIVER_REQUIRED
        self.voice_detail = detail
        return True

    def report_voice_setup_result(self, token, state):
        if (
            token != self.voice_revision
            or self.voice_state is not VoiceState.DRIVER_REQUIRED
        ):
            return False
        self.voice_setup_results.append(state)
        self.voice_revision += 1
        self.voice_state = {
            DriverState.READY: VoiceState.READY,
            DriverState.RESTART_REQUIRED: VoiceState.RESTART_REQUIRED,
        }[state]
        self.voice_detail = ""
        return True

    def stop(self, timeout=5.0):
        self.stopped = True
        return True


class MenuBarApplicationTests(unittest.TestCase):
    def test_agent_name_prompt_returns_text_only_after_save(self):
        class Field:
            def __init__(self):
                self.value = ""

            def setStringValue_(self, value):
                self.value = value

            def stringValue(self):
                return "Travel Agent"

        class Alert:
            def __init__(self, result):
                self.result = result
                self.buttons = []
                self.accessory = None

            def setMessageText_(self, _value):
                pass

            def setInformativeText_(self, _value):
                pass

            def setAccessoryView_(self, value):
                self.accessory = value

            def addButtonWithTitle_(self, value):
                self.buttons.append(value)

            def runModal(self):
                return self.result

        field = Field()
        alert = Alert(NSAlertFirstButtonReturn)
        self.assertEqual(
            prompt_agent_name(
                "Studio Mac",
                alert_factory=lambda: alert,
                field_factory=lambda: field,
            ),
            "Travel Agent",
        )
        self.assertEqual(field.value, "Studio Mac")
        self.assertEqual(alert.buttons, ["Save", "Cancel"])
        self.assertIs(alert.accessory, field)
        self.assertIsNone(
            prompt_agent_name(
                "Studio Mac",
                alert_factory=lambda: Alert(0),
                field_factory=Field,
            )
        )

    def test_pairing_rows_and_agent_rename_action_are_visible(self):
        NSApplication.sharedApplication()
        controller = FakeController(
            pairing_state=PairingState.AWAITING_CONFIRMATION
        )
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)

        self.assertEqual(delegate.agent_label.stringValue(), "Agent: Studio Mac")
        self.assertEqual(delegate.pairing_label.stringValue(), "Pairing: Confirm on Watch")
        self.assertEqual(delegate.verification_label.stringValue(), "Verification Code: 123456")
        self.assertEqual(delegate.watch_identity_label.stringValue(), "Watch: · 0E0F")
        delegate.agent_name_prompt = lambda current: "Travel Agent"

        delegate.renameAgent_(None)

        self.assertEqual(controller.renamed_agents, ["Travel Agent"])
        self.assertEqual(delegate.agent_label.stringValue(), "Agent: Travel Agent")

    def test_failed_pairing_exposes_retry_action(self):
        NSApplication.sharedApplication()
        controller = FakeController(pairing_state=PairingState.ERROR)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)

        self.assertFalse(delegate.retry_pairing_item.isHidden())
        delegate.retryPairing_(None)
        self.assertEqual(controller.reconnect_count, 1)

    def test_failed_pairing_can_forget_stale_watch_credential(self):
        NSApplication.sharedApplication()
        controller = FakeController(pairing_state=PairingState.ERROR)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)
        delegate.watch_forget_confirmer = lambda: True

        self.assertFalse(delegate.forget_watch_item.isHidden())
        delegate.forgetWatch_(None)

        self.assertEqual(controller.forgot_watch_count, 1)
        self.assertEqual(controller.reconnect_count, 1)

    def test_confirmation_explains_authorization_and_possible_restart(self):
        class Alert:
            def __init__(self):
                self.message = ""
                self.informative = ""
                self.buttons = []

            def setMessageText_(self, value):
                self.message = value

            def setInformativeText_(self, value):
                self.informative = value

            def addButtonWithTitle_(self, value):
                self.buttons.append(value)

            def runModal(self):
                return NSAlertFirstButtonReturn

        alert = Alert()

        confirmed = confirm_voice_setup(alert_factory=lambda: alert)

        self.assertTrue(confirmed)
        self.assertIn("administrator authorization", alert.informative)
        self.assertIn("restart", alert.informative.lower())
        self.assertEqual(alert.buttons, ["Install", "Cancel"])

    def test_setup_confirmation_cancel_does_not_start_worker(self):
        NSApplication.sharedApplication()
        controller = FakeController(VoiceState.DRIVER_REQUIRED)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)
        confirmations = []
        workers = []
        delegate.voice_setup_confirmer = lambda: confirmations.append(True) or False
        delegate.voice_worker_factory = lambda target: workers.append(target)

        delegate.setupVoice_(None)

        self.assertEqual(confirmations, [True])
        self.assertEqual(workers, [])
        self.assertIs(controller.voice_state, VoiceState.DRIVER_REQUIRED)
        self.assertFalse(delegate._installing_voice)
        self.assertTrue(delegate.setup_voice_item.isEnabled())

    def test_install_failure_is_published_as_visible_voice_error(self):
        class Installer:
            def install(self, _package):
                raise DriverInstallError("Driver package verification failed")

        class Worker:
            def __init__(self, target):
                self.target = target

            def start(self):
                pass

            def is_alive(self):
                return False

        NSApplication.sharedApplication()
        controller = FakeController(VoiceState.DRIVER_REQUIRED)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)
        workers = []
        delegate.voice_installer = Installer()
        delegate.voice_setup_confirmer = lambda: True
        delegate.voice_worker_factory = lambda target: workers.append(
            Worker(target)
        ) or workers[-1]
        delegate.call_after = lambda function, *args: function(*args)

        delegate.setupVoice_(None)
        with self.assertLogs("menu_app", level="ERROR"):
            workers[0].target()

        self.assertEqual(
            controller.voice_setup_failures,
            ["Driver package verification failed"],
        )
        self.assertIs(controller.voice_state, VoiceState.DRIVER_REQUIRED)
        self.assertEqual(
            delegate.voice_label.stringValue(),
            "Voice Input: Driver Required - Driver package verification failed",
        )
        self.assertFalse(delegate.setup_voice_item.isHidden())
        self.assertTrue(delegate.setup_voice_item.isEnabled())

        delegate.setupVoice_(None)
        self.assertEqual(len(workers), 2)

    def test_shutdown_cancels_and_reaps_blocked_installer_without_late_callback(self):
        temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(temporary_directory.cleanup)
        package = pathlib.Path(temporary_directory.name) / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        process_started = threading.Event()
        process_released = threading.Event()
        process_calls = []
        group_signals = []
        callbacks = []

        class Process:
            pid = 9876

            def wait(self, timeout=None):
                process_calls.append(("wait", timeout))
                if timeout is None:
                    process_released.wait(2)
                elif not process_released.is_set():
                    raise subprocess.TimeoutExpired("osascript", timeout)
                return -signal.SIGKILL

        def process_factory(_command, **kwargs):
            process_calls.append(("start", kwargs))
            process_started.set()
            return Process()

        def signal_process_group(_pid, signum):
            group_signals.append(signum)
            if signum == signal.SIGKILL:
                process_released.set()

        installer = BlackHoleInstaller(
            expected_sha256=hashlib.sha256(b"known package").hexdigest(),
            process_factory=process_factory,
            signal_process_group=signal_process_group,
            receipt_checker=lambda: True,
            device_checker=lambda: True,
        )
        NSApplication.sharedApplication()
        delegate = MenuBarApplication.alloc().initWithController_(
            FakeController(VoiceState.DRIVER_REQUIRED)
        )
        delegate.applicationDidFinishLaunching_(None)
        delegate.voice_installer = installer
        delegate.voice_package_path = package
        delegate.voice_setup_confirmer = lambda: True
        delegate.call_after = lambda *args: callbacks.append(args)
        self.addCleanup(process_released.set)

        delegate.setupVoice_(None)
        self.assertTrue(process_started.wait(1))
        worker = delegate._voice_install_thread
        started = time.monotonic()
        delegate._shutdown()
        elapsed = time.monotonic() - started

        if worker.is_alive():
            process_released.set()
            worker.join(1)
        self.assertLess(elapsed, 1.0)
        self.assertTrue(worker.daemon)
        self.assertFalse(worker.is_alive())
        self.assertEqual(group_signals, [signal.SIGTERM, signal.SIGKILL])
        self.assertGreaterEqual(
            len([call for call in process_calls if call[0] == "wait"]), 3
        )
        self.assertEqual(callbacks, [])

    def test_shutdown_rechecks_installer_after_worker_finishes_during_join(self):
        class Installer:
            def __init__(self):
                self.close_calls = []

            def close(self, timeout):
                self.close_calls.append(timeout)
                return len(self.close_calls) > 1

        class Worker:
            def __init__(self):
                self.alive = True
                self.join_calls = []

            def is_alive(self):
                return self.alive

            def join(self, timeout):
                self.join_calls.append(timeout)
                self.alive = False

        NSApplication.sharedApplication()
        delegate = MenuBarApplication.alloc().initWithController_(
            FakeController(VoiceState.DRIVER_REQUIRED)
        )
        delegate.applicationDidFinishLaunching_(None)
        installer = Installer()
        worker = Worker()
        delegate.voice_installer = installer
        delegate._voice_install_thread = worker

        with self.assertLogs("menu_app", level="INFO") as logs:
            delegate._shutdown()

        self.assertEqual(len(installer.close_calls), 2)
        self.assertEqual(installer.close_calls[1], 0.0)
        self.assertEqual(len(worker.join_calls), 1)
        self.assertIsNone(delegate._voice_install_thread)
        self.assertFalse(
            any(
                "installer did not stop" in message
                for message in logs.output
            )
        )

    def test_install_completion_after_shutdown_does_not_refresh_runtime_or_menu(self):
        NSApplication.sharedApplication()
        controller = FakeController(VoiceState.DRIVER_REQUIRED)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)

        delegate._shutdown()
        delegate._finish_voice_setup(DriverState.READY)

        self.assertEqual(controller.voice_refresh_count, 0)
        self.assertFalse(delegate.setup_voice_item.isEnabled())
        self.assertFalse(delegate.accessibility_item.isEnabled())

    def test_voice_completion_post_precedes_shutdown_ownership(self):
        NSApplication.sharedApplication()
        delegate = MenuBarApplication.alloc().initWithController_(
            FakeController(VoiceState.DRIVER_REQUIRED)
        )
        delegate.applicationDidFinishLaunching_(None)
        post_started = threading.Event()
        release_post = threading.Event()
        callbacks = []

        def call_after(*args):
            post_started.set()
            release_post.wait(1)
            callbacks.append(args)

        delegate.call_after = call_after
        post_worker = threading.Thread(
            target=lambda: delegate._schedule_voice_setup_completion(
                DriverState.READY,
                "",
                7,
            )
        )
        shutdown_worker = threading.Thread(target=delegate._shutdown)

        post_worker.start()
        self.assertTrue(post_started.wait(1))
        shutdown_worker.start()
        time.sleep(0.05)
        self.assertFalse(delegate._stopping)
        release_post.set()
        post_worker.join(1)
        shutdown_worker.join(1)

        self.assertFalse(post_worker.is_alive())
        self.assertFalse(shutdown_worker.is_alive())
        self.assertTrue(delegate._stopping)
        self.assertEqual(len(callbacks), 1)

    def test_setup_runs_off_appkit_and_disables_action_until_completion(self):
        class Installer:
            def __init__(self):
                self.packages = []

            def install(self, package):
                self.packages.append(package)
                return DriverState.READY

        class Worker:
            def __init__(self, target):
                self.target = target
                self.started = False

            def start(self):
                self.started = True

        NSApplication.sharedApplication()
        controller = FakeController(VoiceState.DRIVER_REQUIRED)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)
        installer = Installer()
        workers = []
        package = pathlib.Path("/private/test/BlackHole package.pkg")
        delegate.voice_installer = installer
        delegate.voice_package_path = package
        delegate.voice_setup_confirmer = lambda: True
        delegate.voice_worker_factory = lambda target: workers.append(
            Worker(target)
        ) or workers[-1]
        delegate.call_after = lambda function, *args: function(*args)

        delegate.setupVoice_(None)

        self.assertEqual(installer.packages, [])
        self.assertEqual(len(workers), 1)
        self.assertTrue(workers[0].started)
        self.assertFalse(delegate.setup_voice_item.isEnabled())
        workers[0].target()
        self.assertEqual(installer.packages, [package])
        self.assertEqual(controller.voice_setup_results, [DriverState.READY])
        self.assertEqual(controller.voice_refresh_count, 1)
        self.assertTrue(delegate.setup_voice_item.isHidden())
        self.assertFalse(delegate.setup_voice_item.isEnabled())

        delegate.setupVoice_(None)
        self.assertEqual(len(workers), 1)

    def test_menu_shows_setup_only_when_driver_is_missing(self):
        NSApplication.sharedApplication()
        controller = FakeController(VoiceState.DRIVER_REQUIRED)
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)

        self.assertEqual(
            delegate.voice_label.stringValue(), "Voice Input: Driver Required"
        )
        self.assertFalse(delegate.voice_item.isEnabled())
        self.assertIsNone(delegate.voice_item.action())
        self.assertFalse(delegate.setup_voice_item.isHidden())
        self.assertTrue(delegate.accessibility_item.isHidden())
        self.assertEqual(
            delegate.menu.indexOfItem_(delegate.voice_item),
            delegate.menu.indexOfItem_(delegate.device_item) + 1,
        )

    def test_menu_uses_exact_snapshot_voice_state_for_row_and_actions(self):
        NSApplication.sharedApplication()
        controller = FakeController()
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)
        cases = (
            (VoiceState.DRIVER_REQUIRED, "Driver Required", False, True),
            (VoiceState.RESTART_REQUIRED, "Restart Required", True, True),
            (
                VoiceState.ACCESSIBILITY_REQUIRED,
                "Accessibility Required",
                True,
                False,
            ),
            (VoiceState.TYPELESS_NOT_RUNNING, "Typeless Not Running", True, True),
            (VoiceState.READY, "Ready", True, True),
            (VoiceState.RECORDING, "Recording", True, True),
            (VoiceState.ERROR, "Error", True, True),
        )

        for state, label, setup_hidden, accessibility_hidden in cases:
            with self.subTest(state=state):
                controller.voice_state = state
                delegate.refresh_(None)
                self.assertEqual(
                    delegate.voice_label.stringValue(),
                    "Voice Input: {}".format(label),
                )
                self.assertEqual(delegate.setup_voice_item.isHidden(), setup_hidden)
                self.assertEqual(
                    delegate.accessibility_item.isHidden(), accessibility_hidden
                )

    def test_accessibility_action_opens_the_system_settings_url(self):
        class Workspace:
            def __init__(self):
                self.urls = []

            def openURL_(self, url):
                self.urls.append(url.absoluteString())
                return True

        NSApplication.sharedApplication()
        delegate = MenuBarApplication.alloc().initWithController_(
            FakeController(VoiceState.ACCESSIBILITY_REQUIRED)
        )
        delegate.applicationDidFinishLaunching_(None)
        self.addCleanup(delegate._shutdown)
        workspace = Workspace()
        delegate.workspace = workspace

        delegate.openAccessibilitySettings_(None)

        self.assertEqual(
            workspace.urls,
            [
                "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"
            ],
        )

    def test_codex_pet_action_precedes_connection_actions_and_reuses_window(self):
        # Creating a new panel for each menu click would lose the converted
        # preview and make late runtime progress disappear from the reopened UI.
        class PetWindow:
            constructed = []

            @classmethod
            def alloc(cls):
                return cls()

            def initWithAgentController_(self, controller):
                self.controller = controller
                self.snapshots = []
                self.show_count = 0
                self.front_count = 0
                self.stopped = False
                PetWindow.constructed.append(self)
                return self

            def applySnapshot_(self, snapshot):
                self.snapshots.append(snapshot)

            def showWindow_(self, _sender):
                self.show_count += 1

            def window(self):
                return self

            def makeKeyAndOrderFront_(self, _sender):
                self.front_count += 1

            def stopPreviewTimer(self):
                self.stopped = True

        NSApplication.sharedApplication()
        controller = FakeController()
        delegate = MenuBarApplication.alloc().initWithController_(controller)
        delegate.applicationDidFinishLaunching_(None)
        delegate.pet_window_controller_factory = PetWindow
        self.addCleanup(delegate._shutdown)

        self.assertEqual(delegate.codex_pet_item.title(), "Codex Pet…")
        separator_index = delegate.menu.indexOfItem_(delegate.codex_pet_item) + 1
        self.assertTrue(delegate.menu.itemAtIndex_(separator_index).isSeparatorItem())
        self.assertIs(
            delegate.menu.itemAtIndex_(separator_index + 1),
            delegate.connection_item,
        )

        delegate.openCodexPet_(None)
        delegate.openCodexPet_(None)

        self.assertEqual(len(PetWindow.constructed), 1)
        pet_window = PetWindow.constructed[0]
        self.assertEqual(pet_window.show_count, 2)
        self.assertEqual(pet_window.front_count, 2)
        self.assertEqual(len(pet_window.snapshots), 2)

    def test_menu_lifecycle_and_actions(self):
        NSApplication.sharedApplication()
        controller = FakeController()
        delegate = MenuBarApplication.alloc().initWithController_(controller)

        delegate.applicationDidFinishLaunching_(None)

        self.assertTrue(controller.started)
        self.assertFalse(delegate.menu.autoenablesItems())
        self.assertEqual(delegate.codex_header_label.stringValue(), "Codex Status")
        self.assertEqual(delegate.model_label.stringValue(), "Model: gpt-5.3")
        self.assertEqual(delegate.effort_label.stringValue(), "Effort: High")
        self.assertEqual(delegate.five_hour_label.stringValue(), "5H Remaining: 72%")
        self.assertEqual(delegate.seven_day_label.stringValue(), "7D Remaining: 64%")
        self.assertFalse(hasattr(delegate, "today_label"))
        self.assertFalse(hasattr(delegate, "this_week_label"))
        self.assertEqual(
            delegate.alert_item.title(),
            "Task Alerts · 1 Pending / 1 Running / 1 Completed",
        )
        self.assertTrue(delegate.alert_item.isEnabled())
        self.assertEqual(delegate.alert_submenu.numberOfItems(), 6)
        self.assertEqual(
            [label.stringValue() for label in delegate._alert_labels],
            [
                "Pending",
                "StopWatch · Waiting for Instruction · Task alert support · 09:41",
                "Running",
                "Docs · Running · Update guide · 09:02",
                "Completed",
                "Website · Completed · Header fix · 08:12",
            ],
        )
        self.assertTrue(
            delegate._alert_labels[1].textColor().isEqual_(NSColor.systemOrangeColor())
        )
        self.assertTrue(
            delegate._alert_labels[3].textColor().isEqual_(NSColor.systemBlueColor())
        )
        self.assertTrue(
            delegate._alert_labels[5].textColor().isEqual_(NSColor.secondaryLabelColor())
        )
        self.assertEqual(
            delegate.connection_header_label.stringValue(), "Connection Status"
        )
        self.assertEqual(delegate.trae_header_label.stringValue(), "Trae Status")
        self.assertEqual(delegate.trae_online_label.stringValue(), "Source: Online")
        self.assertEqual(delegate.trae_model_label.stringValue(), "Model: Model A")
        self.assertEqual(delegate.trae_effort_label.stringValue(), "Effort: High")
        self.assertEqual(delegate.trae_project_label.stringValue(), "Project: StopWatch")
        self.assertEqual(delegate.trae_state_label.stringValue(), "State: Running")
        self.assertEqual(delegate.trae_task_label.stringValue(), "Task: Trae status page")
        self.assertEqual(
            delegate.trae_detail_label.stringValue(),
            "Detail: Building menu state",
        )
        for item in (
            delegate.codex_header_item,
            delegate.model_item,
            delegate.effort_item,
            delegate.five_hour_item,
            delegate.seven_day_item,
            delegate.trae_header_item,
            delegate.trae_online_item,
            delegate.trae_model_item,
            delegate.trae_effort_item,
            delegate.trae_project_item,
            delegate.trae_state_item,
            delegate.trae_task_item,
            delegate.trae_detail_item,
            delegate.connection_header_item,
            delegate.agent_item,
            delegate.pairing_item,
            delegate.verification_item,
            delegate.watch_identity_item,
            delegate.bluetooth_item,
            delegate.device_item,
        ):
            self.assertFalse(item.isEnabled())
            self.assertIsNone(item.action())
            self.assertIsNotNone(item.view())
        for label in (
            delegate.codex_header_label,
            delegate.model_label,
            delegate.effort_label,
            delegate.five_hour_label,
            delegate.seven_day_label,
            delegate.connection_header_label,
            delegate.agent_label,
            delegate.bluetooth_label,
            delegate.device_label,
        ):
            self.assertTrue(label.textColor().isEqual_(NSColor.labelColor()))
        self.assertEqual(
            delegate.model_label.frame().origin.x
            - delegate.codex_header_label.frame().origin.x,
            16,
        )
        self.assertEqual(
            delegate.agent_label.frame().origin.x
            - delegate.connection_header_label.frame().origin.x,
            16,
        )
        self.assertNotEqual(
            delegate.codex_header_label.font().fontName(),
            delegate.model_label.font().fontName(),
        )
        self.assertEqual(delegate.bluetooth_label.stringValue(), "Bluetooth: Connected")
        self.assertEqual(delegate.connection_item.title(), "Enable Watch Connection")
        self.assertEqual(delegate.connection_item.state(), NSControlStateValueOn)
        self.assertEqual(delegate.status_item.button().title(), " Codex")
        self.assertIsNotNone(delegate.status_item.button().image())

        delegate.toggleConnection_(delegate.connection_item)
        delegate.reconnect_(None)

        self.assertEqual(controller.connection_values, [False])
        self.assertEqual(controller.reconnect_count, 1)

        delegate._shutdown()
        self.assertTrue(controller.stopped)
        self.assertIsNone(delegate.status_item)


if __name__ == "__main__":
    unittest.main()
