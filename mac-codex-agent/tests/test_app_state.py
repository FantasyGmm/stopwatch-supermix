import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from app_state import (
    AgentSnapshot,
    CodexSnapshot,
    ConnectionState,
    MemoryPreferences,
    present,
)


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
                "codex_today_min": 38,
                "codex_week_min": 214,
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
        self.assertEqual(view.today_text, "Today: 38 min")
        self.assertEqual(view.this_week_text, "This Week: 214 min")

    def test_missing_codex_values_render_as_dashes(self):
        view = present(AgentSnapshot(ConnectionState.SCANNING, True))

        self.assertEqual(view.model_text, "Model: --")
        self.assertEqual(view.effort_text, "Effort: --")
        self.assertEqual(view.five_hour_text, "5H Remaining: --")
        self.assertEqual(view.seven_day_text, "7D Remaining: --")
        self.assertEqual(view.today_text, "Today: --")
        self.assertEqual(view.this_week_text, "This Week: --")

    def test_numeric_values_are_clamped_without_treating_zero_as_missing(self):
        codex = CodexSnapshot.from_status(
            {
                "codex_5h_left_pct": -3,
                "codex_week_left_pct": 140,
                "codex_today_min": 0,
                "codex_week_min": -20,
            }
        )
        view = present(AgentSnapshot(ConnectionState.WAITING, True, codex=codex))

        self.assertEqual(view.five_hour_text, "5H Remaining: 0%")
        self.assertEqual(view.seven_day_text, "7D Remaining: 100%")
        self.assertEqual(view.today_text, "Today: 0 min")
        self.assertEqual(view.this_week_text, "This Week: 0 min")

    def test_boolean_numeric_values_are_treated_as_missing(self):
        codex = CodexSnapshot.from_status(
            {
                "codex_5h_left_pct": True,
                "codex_today_min": False,
            }
        )
        view = present(AgentSnapshot(ConnectionState.ERROR, True, codex=codex))

        self.assertEqual(view.five_hour_text, "5H Remaining: --")
        self.assertEqual(view.today_text, "Today: --")


if __name__ == "__main__":
    unittest.main()
