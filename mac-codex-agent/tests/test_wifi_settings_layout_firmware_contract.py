import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class WifiSettingsLayoutFirmwareContractTests(unittest.TestCase):
    def test_saved_network_rows_reset_button_padding_before_child_alignment(self):
        worker = (
            ROOT / "main" / "apps" / "app_setup" / "workers" / "wifi.cpp"
        ).read_text(encoding="utf-8")

        row_body = worker.split(
            "auto row = std::make_unique<Button>(_saved_list->get());", 1
        )[1].split("_network_rows.push_back(std::move(row));", 1)[0]

        self.assertIn("constexpr int kSsidMarkerGap = 12;", worker)
        style_index = row_body.index("applyCompactButtonStyle(*row")
        padding_index = row_body.index("row->setPaddingAll(0);")
        ssid_index = row_body.index("row->label().align(")
        marker_index = row_body.index("auto marker = std::make_unique<Label>(row->get());")

        self.assertLess(style_index, padding_index)
        self.assertLess(padding_index, ssid_index)
        self.assertLess(padding_index, marker_index)

    def test_done_action_uses_standard_settings_ok_style(self):
        worker = (
            ROOT / "main" / "apps" / "app_setup" / "workers" / "wifi.cpp"
        ).read_text(encoding="utf-8")

        style_signature = "void applyConfirmationButtonStyle(Button& button)"
        self.assertIn(style_signature, worker)
        style_body = worker.split(style_signature, 1)[1].split("}", 1)[0]

        self.assertIn("constexpr int kDoneActionWidth = 374;", worker)
        self.assertIn("constexpr int kDoneActionHeight = 130;", worker)
        self.assertIn("constexpr int kDoneActionCenterY = 175;", worker)
        self.assertIn("button.setRadius(77);", style_body)
        self.assertIn("button.setBgColor(lv_color_hex(0x4AD78C));", style_body)
        self.assertIn(
            "button.label().setTextFont(&lv_font_montserrat_28);", style_body
        )
        self.assertIn(
            "button.label().setTextColor(lv_color_hex(0x0F5831));", style_body
        )
        self.assertIn('_done_action->label().setText("OK");', worker)
        self.assertIn("applyConfirmationButtonStyle(*_done_action);", worker)


if __name__ == "__main__":
    unittest.main()
