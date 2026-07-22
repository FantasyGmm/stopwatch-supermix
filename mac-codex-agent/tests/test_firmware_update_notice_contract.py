import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FirmwareUpdateNoticeContractTests(unittest.TestCase):
    def test_xiaozhi_overlay_reports_version_without_install_inputs(self):
        overlay = (ROOT / "main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake").read_text(encoding="utf-8")
        self.assertIn("stopwatch_duplicate_firmware_version", overlay)
        self.assertIn("stopwatch_report_remote_firmware_version", overlay)
        self.assertIn("info.current_version", overlay)
        self.assertIn("info.firmware_version", overlay)
        self.assertNotIn("info.firmware_url", overlay)
        self.assertNotIn("esp_https_ota", overlay)
        self.assertNotIn("esp_ota_", overlay)


if __name__ == "__main__":
    unittest.main()
