import dataclasses
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from app_state import (
    AgentSnapshot,
    CodexSnapshot,
    ConnectionState,
    MemoryPreferences,
    PairingState,
    TraeSnapshot,
    present,
)
from voice_session import VoiceState


class AppStateTests(unittest.TestCase):
    def test_connected_presentation_is_checked_and_reconnectable(self):
        view = present(AgentSnapshot(ConnectionState.CONNECTED, True))

        self.assertEqual(view.agent_text, "Agent: Running")
        self.assertEqual(view.bluetooth_text, "Bluetooth: Connected")
        self.assertEqual(view.device_text, "Device: StopWatch-Codex")
        self.assertTrue(view.connection_checked)
        self.assertTrue(view.reconnect_enabled)
        self.assertEqual(view.symbol_name, "bolt.horizontal.circle.fill")

    def test_disabled_presentation_is_unchecked(self):
        view = present(AgentSnapshot(ConnectionState.DISABLED, False))

        self.assertEqual(view.bluetooth_text, "Bluetooth: Connection Off")
        self.assertFalse(view.connection_checked)
        self.assertFalse(view.reconnect_enabled)
        self.assertEqual(view.symbol_name, "bolt.horizontal.circle")

    def test_pairing_presentation_contains_only_display_safe_fields(self):
        snapshot = AgentSnapshot(
            ConnectionState.CONNECTING,
            True,
            agent_name="Desk Agent",
            pairing_state=PairingState.AWAITING_CONFIRMATION,
            verification_code="482731",
            watch_identity_suffix="7A2F",
        )

        view = present(snapshot)

        self.assertEqual(view.agent_text, "Agent: Desk Agent")
        self.assertEqual(view.pairing_text, "Pairing: Confirm on Watch")
        self.assertEqual(view.verification_text, "Verification Code: 482731")
        self.assertEqual(view.watch_identity_text, "Watch: · 7A2F")
        snapshot_fields = {field.name for field in dataclasses.fields(snapshot)}
        presentation_fields = {field.name for field in dataclasses.fields(view)}
        for forbidden in ("master_key", "private_key", "credential"):
            self.assertNotIn(forbidden, snapshot_fields)
            self.assertNotIn(forbidden, presentation_fields)

    def test_invalid_verification_code_is_never_presented(self):
        view = present(
            AgentSnapshot(
                ConnectionState.CONNECTING,
                True,
                pairing_state=PairingState.AWAITING_CONFIRMATION,
                verification_code="secret-key-material",
            )
        )

        self.assertEqual(view.verification_text, "Verification Code: --")

    def test_every_state_has_a_stable_label(self):
        labels = {
            ConnectionState.DISABLED: "Bluetooth: Connection Off",
            ConnectionState.SCANNING: "Bluetooth: Scanning",
            ConnectionState.WAITING: "Bluetooth: Waiting for Watch",
            ConnectionState.CONNECTING: "Bluetooth: Connecting",
            ConnectionState.CONNECTED: "Bluetooth: Connected",
            ConnectionState.DISCONNECTING: "Bluetooth: Disconnecting",
            ConnectionState.UNAUTHORIZED: "Bluetooth: Permission Required",
            ConnectionState.UNAVAILABLE: "Bluetooth: Unavailable",
            ConnectionState.ERROR: "Bluetooth: Connection Error",
        }

        for state, expected in labels.items():
            with self.subTest(state=state):
                view = present(AgentSnapshot(state, state is not ConnectionState.DISABLED))
                self.assertEqual(view.bluetooth_text, expected)

    def test_every_voice_state_has_an_exact_label_and_detail(self):
        labels = {
            VoiceState.DRIVER_REQUIRED: "Voice Input: Driver Required",
            VoiceState.RESTART_REQUIRED: "Voice Input: Restart Required",
            VoiceState.ACCESSIBILITY_REQUIRED: "Voice Input: Accessibility Required",
            VoiceState.TYPELESS_NOT_RUNNING: "Voice Input: Typeless Not Running",
            VoiceState.READY: "Voice Input: Ready",
            VoiceState.RECORDING: "Voice Input: Recording",
            VoiceState.ERROR: "Voice Input: Error",
        }

        for state, expected in labels.items():
            with self.subTest(state=state):
                view = present(
                    AgentSnapshot(
                        ConnectionState.CONNECTED,
                        True,
                        voice_state=state,
                        voice_detail="exact detail",
                    )
                )
                self.assertIs(view.voice_state, state)
                self.assertEqual(
                    view.voice_text,
                    "{} - exact detail".format(expected),
                )
                self.assertEqual(view.voice_detail, "exact detail")

    def test_connection_preference_round_trips(self):
        preferences = MemoryPreferences()

        self.assertTrue(preferences.connection_enabled())
        preferences.set_connection_enabled(False)
        self.assertFalse(preferences.connection_enabled())
        preferences.set_connection_enabled(True)
        self.assertTrue(preferences.connection_enabled())

    def test_complete_codex_snapshot_formats_watch_values(self):
        codex = CodexSnapshot.from_status(
            {
                "codex_model": "gpt-5.3",
                "codex_effort": "high",
                "codex_5h_left_pct": 72,
                "codex_week_left_pct": 64,
                "codex_alert_pending": 1,
                "codex_alert_running": 1,
                "codex_alert_completed": 1,
                "codex_alert_revision": 7,
                "codex_alerts": [
                    {
                        "project": "StopWatch",
                        "title": "Add task alerts",
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
                        "title": "Fix header",
                        "time": "08:12",
                        "state": "completed",
                        "outcome": "completed",
                    },
                ],
            },
            now=lambda: 123.0,
        )

        view = present(
            AgentSnapshot(ConnectionState.CONNECTED, True, codex=codex)
        )

        self.assertEqual(codex.updated_at, 123.0)
        self.assertEqual(view.model_text, "Model: gpt-5.3")
        self.assertEqual(view.effort_text, "Effort: High")
        self.assertEqual(view.five_hour_text, "5H Remaining: 72%")
        self.assertEqual(view.seven_day_text, "7D Remaining: 64%")
        self.assertEqual(
            view.alert_summary_text,
            "Task Alerts · 1 Pending / 1 Running / 1 Completed",
        )
        self.assertEqual(view.alert_revision, 7)
        self.assertEqual(
            [alert.project for alert in view.alerts],
            ["StopWatch", "Docs", "Website"],
        )
        self.assertEqual(
            [alert.outcome for alert in view.alerts],
            ["waiting", "running", "completed"],
        )

    def test_missing_codex_values_render_as_dashes(self):
        view = present(AgentSnapshot(ConnectionState.SCANNING, True))

        self.assertEqual(view.model_text, "Model: --")
        self.assertEqual(view.effort_text, "Effort: --")
        self.assertEqual(view.five_hour_text, "5H Remaining: --")
        self.assertEqual(view.seven_day_text, "7D Remaining: --")
        self.assertEqual(
            view.alert_summary_text,
            "Task Alerts · 0 Pending / 0 Running / 0 Completed",
        )
        self.assertEqual(view.alerts, ())

    def test_numeric_values_are_clamped_without_treating_zero_as_missing(self):
        codex = CodexSnapshot.from_status(
            {
                "codex_5h_left_pct": -3,
                "codex_week_left_pct": 140,
                "codex_alert_pending": -3,
                "codex_alert_running": 140,
                "codex_alert_completed": 150,
            }
        )
        view = present(AgentSnapshot(ConnectionState.WAITING, True, codex=codex))

        self.assertEqual(view.five_hour_text, "5H Remaining: 0%")
        self.assertEqual(view.seven_day_text, "7D Remaining: 100%")
        self.assertEqual(
            view.alert_summary_text,
            "Task Alerts · 0 Pending / 140 Running / 150 Completed",
        )

    def test_boolean_numeric_values_are_treated_as_missing(self):
        codex = CodexSnapshot.from_status(
            {
                "codex_5h_left_pct": True,
                "codex_alert_pending": False,
            }
        )
        view = present(AgentSnapshot(ConnectionState.ERROR, True, codex=codex))

        self.assertEqual(view.five_hour_text, "5H Remaining: --")
        self.assertEqual(
            view.alert_summary_text,
            "Task Alerts · 0 Pending / 0 Running / 0 Completed",
        )

    def test_invalid_alert_rows_are_ignored(self):
        codex = CodexSnapshot.from_status(
            {
                "codex_alerts": [
                    {"project": "ok", "title": "title", "time": "10:00", "state": "PENDING", "outcome": "waiting"},
                    {"project": "running", "title": "title", "time": "10:00", "state": "running", "outcome": "running"},
                    {"project": "bad outcome", "title": "title", "time": "10:01", "state": "pending", "outcome": "unknown"},
                    {"project": "missing state", "title": "title", "time": "10:02", "outcome": "waiting"},
                    "not a row",
                ]
            }
        )

        self.assertEqual(len(codex.alerts), 2)
        self.assertEqual(
            [alert.state for alert in codex.alerts],
            ["pending", "running"],
        )

    def test_agent_snapshot_contains_independent_trae_status(self):
        trae = TraeSnapshot.from_status(
            {
                "trae_online": True,
                "trae_model": "Model A",
                "trae_effort": "high",
                "trae_fresh": True,
                "trae_tasks": [
                    {
                        "project": "StopWatch",
                        "title": "Add Trae page",
                        "detail": "Building the collector",
                        "state": "running",
                    }
                ],
            }
        )

        snapshot = AgentSnapshot(
            ConnectionState.CONNECTED,
            True,
            trae=trae,
        )
        view = present(snapshot)

        self.assertTrue(snapshot.trae.online)
        self.assertEqual(snapshot.trae.project, "StopWatch")
        self.assertEqual(view.trae_online_text, "Source: Online")
        self.assertEqual(view.trae_model_text, "Model: Model A")
        self.assertEqual(view.trae_effort_text, "Effort: High")
        self.assertEqual(view.trae_project_text, "Project: StopWatch")
        self.assertEqual(view.trae_state_text, "State: Running")
        self.assertEqual(view.trae_task_text, "Task: Add Trae page")
        self.assertEqual(view.trae_detail_text, "Detail: Building the collector")


if __name__ == "__main__":
    unittest.main()
