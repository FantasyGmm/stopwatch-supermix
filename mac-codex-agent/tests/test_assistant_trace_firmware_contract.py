import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AssistantTraceFirmwareContractTests(unittest.TestCase):
    def test_release_guards_assistant_memory_diagnostics(self):
        runtime = (
            ROOT / "main/runtime/assistant/assistant_runtime.cpp"
        ).read_text(encoding="utf-8")
        memory_helper = runtime.split(
            "void log_memory_stage(const char* boundary)", 1
        )[1].split("\n}", 1)[0]

        self.assertIn("#if CONFIG_STOPWATCH_DEBUG_COMMANDS", memory_helper)
        self.assertIn("#else", memory_helper)
        self.assertIn("(void)boundary;", memory_helper)

    def test_release_guards_runtime_snapshot_collection_and_success_logs(self):
        supervisor = (
            ROOT / "main/runtime/runtime_supervisor.cpp"
        ).read_text(encoding="utf-8")
        start = supervisor.split(
            "AssistantStartResult RuntimeSupervisor::startAssistant()", 1
        )[1].split("bool RuntimeSupervisor::requestStopAssistant()", 1)[0]
        update = supervisor.split(
            "void RuntimeSupervisor::update(uint32_t now_ms)", 1
        )[1].split("AssistantRuntimeState RuntimeSupervisor::assistantState", 1)[0]
        stopped = supervisor.split(
            "void RuntimeSupervisor::finishStoppedBoundary()", 1
        )[1].split("\n}", 1)[0]

        for body in (start, update, stopped):
            self.assertIn("#if CONFIG_STOPWATCH_DEBUG_COMMANDS", body)
        self.assertIn("CaptureRuntimeDiagnosticSnapshot()", start)
        self.assertIn("CaptureRuntimeDiagnosticSnapshot()", update)
        self.assertIn("CaptureRuntimeDiagnosticSnapshot()", stopped)

    def test_release_keeps_field_evidence_without_transient_trace_spam(self):
        defaults = (ROOT / "sdkconfig.unified.defaults").read_text(
            encoding="utf-8"
        )
        codex = (
            ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp"
        ).read_text(encoding="utf-8")
        status = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING=y", defaults)
        self.assertNotIn("CONFIG_BT_NIMBLE_LOG_LEVEL_INFO=y", defaults)
        self.assertIn('ESP_LOGI(kTag, "connected pending Agent")', codex)
        self.assertIn('ESP_LOGE(kTag, "NimBLE reset: %d", reason)', codex)
        self.assertNotIn('"PINMIC"', status)

    def test_trace_header_reads_firmware_debug_configuration(self):
        header = (ROOT / "main/runtime/assistant/assistant_trace.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("#include <sdkconfig.h>", header)
        self.assertIn("#if !defined(STOPWATCH_HOST_TEST)", header)

    def test_trace_host_test_declares_host_configuration(self):
        runner = (ROOT / "scripts/run_unified_offline_validation.sh").read_text(
            encoding="utf-8"
        )
        trace_stage = runner.split("Assistant trace host test", 1)[1].split(
            "Resource supervisor host test", 1
        )[0]
        self.assertIn("-DSTOPWATCH_HOST_TEST", trace_stage)

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

    def test_close_intent_is_traced_before_runtime_enters_stopping(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text(
            encoding="utf-8"
        )
        accepted_path = app.split(
            "applyPresentationSignal(AssistantPresentationSignal::Closing);", 1
        )[1].split("void AppAssistant::onClose", 1)[0]
        self.assertLess(
            accepted_path.index("AssistantTraceEvent::CloseRequest"),
            accepted_path.index("runtime.requestStopAssistant()"),
        )

    def test_runtime_traces_real_listening_and_speaking_states(self):
        runtime = (
            ROOT / "main/runtime/assistant/assistant_runtime.cpp"
        ).read_text(encoding="utf-8")
        for token in (
            "ManagerHelper::EventId::ListeningStatusChanged",
            "ManagerHelper::EventId::SpeakingStatusChanged",
            "AssistantTraceDetail::Listening",
            "AssistantTraceDetail::Speaking",
        ):
            self.assertIn(token, runtime)


if __name__ == "__main__":
    unittest.main()
