import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AssistantWakeGateFirmwareContractTests(unittest.TestCase):
    def test_connected_agent_sleeps_until_wakenet_opens_audio_channel(self):
        runtime = (
            ROOT / "main" / "runtime" / "assistant" / "assistant_runtime.cpp"
        ).read_text(encoding="utf-8")
        overlay = (
            ROOT / "main" / "cmake" / "brookesia_xiaozhi_mcp_ownership_overlay.cmake"
        ).read_text(encoding="utf-8")

        wait_block = re.search(
            r"set\(wait_for_afe_wake \[=\[(.*?)\]=\]\)", overlay, re.DOTALL
        )
        self.assertIsNotNone(wait_block)
        self.assertIn("WakeStart", wait_block.group(1))
        self.assertNotIn("esp_xiaozhi_chat_open_audio_channel", wait_block.group(1))
        self.assertNotIn("post_delayed", wait_block.group(1))
        self.assertIn(
            'string(REPLACE "${connected_auto_open}" "${wait_for_afe_wake}"',
            overlay,
        )

        started_block = re.search(
            r"if \(\(signals & Started\).*?\n    \}", runtime, re.DOTALL
        )
        self.assertIsNotNone(started_block)
        self.assertIn("submitSleep(session)", started_block.group(0))
        self.assertIn("ManagerHelper::GeneralAction::Sleep", runtime)


if __name__ == "__main__":
    unittest.main()
