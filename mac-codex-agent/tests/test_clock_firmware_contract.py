import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ClockFirmwareContractTests(unittest.TestCase):
    def test_launcher_registers_only_the_merged_clock_application(self):
        main_cpp = (ROOT / "main" / "main.cpp").read_text(encoding="utf-8")
        apps_h = (ROOT / "main" / "apps" / "apps.h").read_text(encoding="utf-8")
        assets_h = (ROOT / "main" / "assets" / "assets.h").read_text(
            encoding="utf-8"
        )

        self.assertEqual(main_cpp.count("std::make_unique<AppClock>()"), 1)
        self.assertNotIn("std::make_unique<AppAlarmClock>()", main_cpp)
        self.assertNotIn("std::make_unique<AppWatchFace>()", main_cpp)
        self.assertNotIn("std::make_unique<AppStopWatch>()", main_cpp)
        self.assertIn('#include "app_clock/app_clock.h"', apps_h)
        self.assertNotIn("app_alarm_clock/app_alarm_clock.h", apps_h)
        self.assertNotIn("app_watch_face/app_watch_face.h", apps_h)
        self.assertNotIn("app_stopwatch/app_stopwatch.h", apps_h)
        self.assertIn("LV_IMG_DECLARE(icon_clock);", assets_h)
        self.assertNotIn("LV_IMG_DECLARE(icon_watch_face);", assets_h)
        self.assertNotIn("LV_IMG_DECLARE(icon_stopwatch);", assets_h)

    def test_clock_identity_and_selector_are_present(self):
        app_clock_cpp = (
            ROOT / "main" / "apps" / "app_clock" / "app_clock.cpp"
        ).read_text(encoding="utf-8")
        selection_cpp = (
            ROOT / "main" / "apps" / "app_clock" / "view" / "selection.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn('setAppInfo().name = "Clock"', app_clock_cpp)
        self.assertIn('setAppInfo().icon = (void*)&icon_clock', app_clock_cpp)
        for label in ("Alarm", "Watch Face", "Stopwatch"):
            self.assertIn(label, selection_cpp)

    def test_stopwatch_left_action_waits_for_short_release(self):
        stopwatch_view = (
            ROOT / "main" / "apps" / "app_stopwatch" / "view" / "view.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "if (GetHAL().btnA.wasClicked()) {\n        handle_btn_left_clicked();",
            stopwatch_view,
        )
        self.assertNotIn(
            "if (GetHAL().btnA.wasPressed()) {\n        handle_btn_left_clicked();",
            stopwatch_view,
        )

    def test_standalone_wrappers_and_unused_launcher_icons_are_removed(self):
        obsolete_paths = (
            "main/apps/app_alarm_clock/app_alarm_clock.h",
            "main/apps/app_alarm_clock/app_alarm_clock.cpp",
            "main/apps/app_watch_face/app_watch_face.h",
            "main/apps/app_watch_face/app_watch_face.cpp",
            "main/apps/app_stopwatch/app_stopwatch.h",
            "main/apps/app_stopwatch/app_stopwatch.cpp",
            "main/assets/images/icon_watch_face.c",
            "main/assets/images/icon_stopwatch.c",
        )
        for relative_path in obsolete_paths:
            self.assertFalse((ROOT / relative_path).exists(), relative_path)


if __name__ == "__main__":
    unittest.main()
