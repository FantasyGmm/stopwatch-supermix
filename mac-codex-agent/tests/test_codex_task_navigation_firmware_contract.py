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

    def test_manual_screen_off_release_is_suppressed_before_task_navigation(self):
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

        sync_start = app.index(
            "void AppCodexStatus::sync_display_protection_presentation()"
        )
        sync_end = app.index(
            "void AppCodexStatus::update_screen_off_control", sync_start
        )
        sync = app[sync_start:sync_end]
        self.assertIn(
            "const bool woke_screen = _screen_off && !screen_off;", sync
        )
        wake = sync.index("if (woke_screen)")
        wake_suppression = sync.index("_suppress_task_navigation = true;", wake)
        self.assertLess(wake, wake_suppression)

    def test_manual_screen_off_is_latched_until_both_buttons_are_released(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        control_start = app.index("AppCodexStatus::update_screen_off_control")
        control_end = app.index("void AppCodexStatus::set_percent", control_start)
        control = app[control_start:control_end]

        self.assertEqual(1, control.count("screenOffNow()"))
        self.assertIn(
            "if (!left_pressed && !right_pressed)", control
        )
        self.assertIn("if (_suppress_task_navigation)", control)
        request = control.index("screenOffNow()")
        suppress = control.index("_suppress_task_navigation = true;", request)
        self.assertLess(request, suppress)

        on_running = app.index("void AppCodexStatus::onRunning()")
        navigation = app.index("key_event == input::KeyEvent::GoPrevious", on_running)
        release = app.index(
            "GetHAL().btnA.isReleased() && GetHAL().btnB.isReleased()",
            on_running,
        )
        self.assertLess(release, navigation)


if __name__ == "__main__":
    unittest.main()
