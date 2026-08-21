import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CodexTaskNavigationFirmwareContractTests(unittest.TestCase):
    def test_task_deck_opens_and_remains_visible_on_the_codex_page(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        visibility = app.split(
            "void AppCodexStatus::sync_task_focus_visibility()", 1
        )[1].split("void AppCodexStatus::render_task_focus()", 1)[0]
        opener = app.split("void AppCodexStatus::open_task_deck()", 1)[1].split(
            "void AppCodexStatus::cancel_task_focus_touch()", 1
        )[0]

        # Page order is pinned microphone (0), Codex (1), TRAE (2).
        self.assertIn("_current_page == 1", visibility)
        self.assertIn("if (_current_page != 1)", opener)

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

    def test_codex_page_reserves_right_click_and_keeps_left_task_navigation(self):
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        key_update = app.index("_key_manager->update(false)")
        self.assertIn(
            "if (key_event == input::KeyEvent::GoNext)", app[key_update:]
        )
        consume_next = app.index(
            "if (key_event == input::KeyEvent::GoNext)", key_update
        )
        clear_next = app.index("key_event = input::KeyEvent::None", consume_next)
        dispatch = app.index("_voice_button_dispatcher.dispatch", clear_next)
        previous = app.index("key_event == input::KeyEvent::GoPrevious")
        previous_step = app.index("_alert_carousel.step(-1, now)", previous)
        running = app.split("void AppCodexStatus::onRunning()", 1)[1].split(
            "void AppCodexStatus::onClose()", 1
        )[0]

        self.assertLess(key_update, consume_next)
        self.assertLess(consume_next, clear_next)
        self.assertLess(clear_next, dispatch)
        self.assertLess(previous, previous_step)
        self.assertEqual(running.count("key_event == input::KeyEvent::GoNext"), 1)
        self.assertNotIn("_alert_carousel.step(1, now)", running)
        self.assertIn("key_event == input::KeyEvent::GoHome", running)

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

    def test_touch_wake_waits_for_physical_release_before_page_gestures(self):
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()
        app = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()

        self.assertNotIn("StatusPageTouchWakeGate", header + app)

        sync_start = app.index(
            "void AppCodexStatus::sync_display_protection_presentation()"
        )
        sync_end = app.index(
            "void AppCodexStatus::update_screen_off_control", sync_start
        )
        sync = app[sync_start:sync_end]
        self.assertNotIn("DisplayWakeReason::Touch", sync)

        update_start = app.index("void AppCodexStatus::update_page_swipe")
        update_end = app.index("void AppCodexStatus::set_page", update_start)
        update = app[update_start:update_end]
        gate = update.index(
            "GetGlobalDisplayProtectionService().touchInputSuppressed()"
        )
        swipe = update.index("_page_swipe.press(")
        trae_tap = update.index("_trae_card_tap.press(")
        self.assertLess(gate, swipe)
        self.assertLess(gate, trae_tap)
        gated_path = update[gate:swipe]
        self.assertIn("_page_swipe.cancel()", gated_path)
        self.assertIn("_trae_card_tap.cancel()", gated_path)
        self.assertIn("_last_page_touch_state = LV_INDEV_STATE_REL", gated_path)


if __name__ == "__main__":
    unittest.main()
