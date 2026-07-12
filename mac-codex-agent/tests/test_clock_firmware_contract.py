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


if __name__ == "__main__":
    unittest.main()
