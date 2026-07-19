import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AssistantTraceFirmwareContractTests(unittest.TestCase):
    def test_runtime_records_lifecycle_boundaries(self):
        runtime = (
            ROOT / "main/runtime/assistant/assistant_runtime.cpp"
        ).read_text(encoding="utf-8")
        for token in (
            "AssistantTraceEvent::ResourceDecision",
            "AssistantTraceEvent::ActionSubmit",
            "AssistantTraceEvent::Callback",
            "AssistantTraceEvent::StopTimeout",
            "AssistantTraceEvent::Release",
        ):
            self.assertIn(token, runtime)

    def test_main_runtime_thread_flushes_trace(self):
        supervisor = (ROOT / "main/runtime/runtime_supervisor.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("GetAssistantTrace().flush()", supervisor)

    def test_suppressed_button_feedback_is_correlated(self):
        button = (ROOT / "main/hal/hal_button.cpp").read_text(encoding="utf-8")
        self.assertIn("AssistantTraceComponent::Button", button)
        self.assertIn("AssistantTraceResult::Suppressed", button)


if __name__ == "__main__":
    unittest.main()
