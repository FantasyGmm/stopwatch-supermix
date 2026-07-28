import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKER_PATH = (
    ROOT
    / "main"
    / "apps"
    / "app_setup"
    / "workers"
    / "display_protection.cpp"
)


class DisplayProtectionSettingsFirmwareContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.setup_app = (
            ROOT / "main" / "apps" / "app_setup" / "app_setup.cpp"
        ).read_text(encoding="utf-8")
        cls.workers_header = (
            ROOT / "main" / "apps" / "app_setup" / "workers" / "workers.h"
        ).read_text(encoding="utf-8")
        cls.worker = (
            WORKER_PATH.read_text(encoding="utf-8") if WORKER_PATH.exists() else ""
        )

    def test_worker_source_exists(self):
        self.assertTrue(WORKER_PATH.exists())

    def test_device_menu_places_display_protection_immediately_after_brightness(self):
        device_section = self.setup_app.split('"Device"', 1)[1].split(
            '"Time & Date"', 1
        )[0]
        entries = re.findall(r'\{"([^"]+)",\s*\n\s*\[&\]\(\)', device_section)
        brightness = entries.index("Brightness")
        self.assertEqual("Display Protection", entries[brightness + 1])
        self.assertIn(
            "std::make_unique<DisplayProtectionWorker>()", self.setup_app
        )

    def test_worker_declares_owned_display_protection_view(self):
        self.assertIn(
            "class DisplayProtectionWorker : public WorkerBase", self.workers_header
        )
        self.assertIn("class DisplayProtectionConfigView;", self.workers_header)
        self.assertIn(
            "std::unique_ptr<DisplayProtectionConfigView> _view;",
            self.workers_header,
        )

    def test_page_uses_approved_single_page_geometry(self):
        self.assertIn("setSize(466, 466)", self.worker)
        self.assertIn("removeFlag(LV_OBJ_FLAG_SCROLLABLE)", self.worker)
        self.assertIn('createChargingSection("Charging Protection")', self.worker)
        self.assertIn("setSize(320, 88)", self.worker)
        self.assertIn("align(LV_ALIGN_TOP_MID, 0, 46)", self.worker)
        self.assertIn("&lv_font_montserrat_22", self.worker)
        self.assertIn("setSize(80, 44)", self.worker)
        self.assertIn(
            "_charging_label->align(LV_ALIGN_TOP_MID, 0, 0)", self.worker
        )
        self.assertIn(
            "_charging_switch->align(LV_ALIGN_TOP_MID, 0, 35)", self.worker
        )

        self.assertIn("align(LV_ALIGN_TOP_MID, 0, 144)", self.worker)
        self.assertIn("align(LV_ALIGN_TOP_MID, 0, 180)", self.worker)
        self.assertIn("align(LV_ALIGN_TOP_MID, 0, 222)", self.worker)
        self.assertIn("align(LV_ALIGN_TOP_MID, 0, 258)", self.worker)
        self.assertGreaterEqual(
            self.worker.count("setTextFont(&lv_font_montserrat_20)"), 2
        )

        self.assertIn("_ok_button->align(LV_ALIGN_CENTER, 0, 175)", self.worker)
        self.assertIn("_ok_button->setSize(374, 130)", self.worker)
        self.assertIn("_ok_button->setRadius(77)", self.worker)
        self.assertIn('_ok_button->label().setText("OK")', self.worker)

    def test_discrete_linked_sliders_use_labels_and_approved_style(self):
        self.assertEqual(2, self.worker.count("std::make_unique<Slider>"))
        self.assertIn(
            "_dim_slider->setRange("
            "0, DisplayProtectionSettingsModel::DimSliderMax, false)",
            self.worker,
        )
        self.assertIn(
            "_screen_off_slider->setRange("
            "0, DisplayProtectionSettingsModel::ScreenOffSliderMax, false)",
            self.worker,
        )
        self.assertIn("_model.setDimSliderPosition", self.worker)
        self.assertIn("_model.setScreenOffSliderPosition", self.worker)
        self.assertIn("_model.dimSliderPosition()", self.worker)
        self.assertIn("_model.screenOffSliderPosition()", self.worker)
        self.assertIn("_model.dimLabel()", self.worker)
        self.assertIn("_model.screenOffLabel()", self.worker)
        self.assertEqual(2, self.worker.count("applySliderStyle(*"))
        self.assertGreaterEqual(self.worker.count("lv_color_hex(0x343434)"), 2)
        self.assertGreaterEqual(self.worker.count("lv_color_hex(0x4AD78C)"), 3)
        self.assertIn("lv_color_hex(0xFFFFFF), LV_PART_KNOB", self.worker)
        self.assertIn("if (_syncing_controls)", self.worker)
        self.assertIn("_syncing_controls = true", self.worker)
        self.assertIn("_syncing_controls = false", self.worker)

    def test_sliders_expand_touch_target_for_watch_interaction(self):
        style = self.worker.split("void applySliderStyle", 1)[1].split(
            "void syncControls", 1
        )[0]
        self.assertIn("lv_obj_set_ext_click_area(slider.get(), 18)", style)

    def test_edits_only_mutate_the_draft_model(self):
        view = self.worker.split("DisplayProtectionWorker::DisplayProtectionWorker", 1)[0]
        self.assertIn("_model.setProtectWhileCharging", view)
        self.assertIn("_model.setDimSliderPosition", view)
        self.assertIn("_model.setScreenOffSliderPosition", view)
        self.assertNotIn("GetGlobalDisplayProtectionService", view)
        self.assertNotIn("applyConfig", view)

    def test_ok_is_the_only_apply_and_persists_once(self):
        self.assertEqual(1, self.worker.count("applyConfig"))
        update = self.worker.split("void DisplayProtectionWorker::update()", 1)[1]
        save_guard = update.index("if (_view && _view->consumeSaveRequested())")
        apply = update.index(
            "GetGlobalDisplayProtectionService().applyConfig(_view->config(), true)"
        )
        done = update.index("_is_done = true")
        self.assertLess(save_guard, apply)
        self.assertLess(apply, done)


if __name__ == "__main__":
    unittest.main()
