import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class AssistantSignalCoreUiContractTests(unittest.TestCase):
    def test_runtime_forwards_real_listening_and_speaking_status(self):
        mailbox = (ROOT / "main/runtime/assistant/assistant_event_mailbox.h").read_text()
        runtime = (ROOT / "main/runtime/assistant/assistant_runtime.cpp").read_text()

        self.assertIn("ListeningStatus", mailbox)
        self.assertIn("SpeakingStatus", mailbox)
        self.assertRegex(
            runtime,
            r"(?s)ListeningStatusChanged.*?mailbox\.publish\(\{session_id,\s*"
            r"AssistantEventKind::ListeningStatus,\s*\{\},\s*listening\}\)",
        )
        self.assertRegex(
            runtime,
            r"(?s)SpeakingStatusChanged.*?mailbox\.publish\(\{session_id,\s*"
            r"AssistantEventKind::SpeakingStatus,\s*\{\},\s*speaking\}\)",
        )


if __name__ == "__main__":
    unittest.main()
