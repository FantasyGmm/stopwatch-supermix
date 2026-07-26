import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CodexTaskNavigationFirmwareContractTests(unittest.TestCase):
    def test_carousel_exposes_manual_step_with_rotation_deadline_reset(self):
        header = (
            ROOT / "main/apps/app_codex_status/codex_alert_carousel.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/codex_alert_carousel.cpp"
        ).read_text()

        self.assertIn("bool step(int direction, uint32_t now_ms);", header)
        self.assertIn("bool CodexAlertCarousel::step(int direction, uint32_t now_ms)", source)
        self.assertIn("_last_rotation_ms = now_ms;", source)

    def test_codex_page_maps_top_buttons_to_previous_and_next_tasks(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        previous = app.index("key_event == input::KeyEvent::GoPrevious")
        previous_step = app.index("_alert_carousel.step(-1, now)", previous)
        next_event = app.index("key_event == input::KeyEvent::GoNext", previous_step)
        next_step = app.index("_alert_carousel.step(1, now)", next_event)
        self.assertLess(previous, previous_step)
        self.assertLess(previous_step, next_event)
        self.assertLess(next_event, next_step)

    def test_screen_wake_release_is_suppressed_before_task_navigation(self):
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        self.assertIn("bool _suppress_task_navigation", header)
        self.assertIn("_suppress_task_navigation = true;", app)
        suppression = app.index("if (_suppress_task_navigation)")
        navigation = app.index("key_event == input::KeyEvent::GoPrevious")
        self.assertLess(suppression, navigation)

    def test_screen_off_release_cannot_navigate_the_carousel(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        on_running = app.index("void AppCodexStatus::onRunning()")
        screen_off_guard = app.index("if (_screen_off) {", on_running)
        navigation = app.index("key_event == input::KeyEvent::GoPrevious", on_running)
        self.assertIn(
            "key_event = input::KeyEvent::None;",
            app[screen_off_guard:navigation],
        )


if __name__ == "__main__":
    unittest.main()
