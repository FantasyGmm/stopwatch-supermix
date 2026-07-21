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


if __name__ == "__main__":
    unittest.main()
