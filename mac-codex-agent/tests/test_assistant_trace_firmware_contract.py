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

    def test_status_remains_available_after_assistant_app_closes(self):
        service = (ROOT / "main/debug/debug_command_service.cpp").read_text(
            encoding="utf-8"
        )
        status_branch = service.split(
            "if (command.kind == DebugCommandKind::AssistantStatus)", 1
        )[1].split("if (command.kind", 1)[0]
        self.assertIn("log_assistant_status()", status_branch)
        self.assertIn("DEBUG ASSISTANT STATUS OK", status_branch)
        self.assertNotIn("RequestDebugAssistantStatus", status_branch)


if __name__ == "__main__":
    unittest.main()
