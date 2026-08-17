import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "main/apps/app_codex_status/app_codex_status.h"
SOURCE = ROOT / "main/apps/app_codex_status/app_codex_status.cpp"


class CodexTaskFocusFirmwareContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = HEADER.read_text()
        cls.source = SOURCE.read_text()

    def test_app_owns_focus_detail_and_crypto_state(self):
        for token in (
            "CodexTaskFocusMode",
            "CodexTaskFocusDeck _task_focus_deck",
            "CodexTaskDetailStore _task_detail_store",
            "std::unique_ptr<CodexTaskFocusView> _task_focus_view",
            "std::unique_ptr<voice_input::crypto::CcmContext> _task_detail_crypto",
        ):
            self.assertIn(token, self.header)

    def test_status_applies_deck_before_retention_and_render(self):
        status = self.source.split("if (kind == CodexMessageKind::TaskDetail)", 1)[1]
        status = status.split("_last_status_payload_ms", 1)[0]
        self.assertLess(status.index("_task_focus_deck.apply(snapshot)"),
                        status.index("_task_detail_store.retain_routes"))
        retain = status.index("_task_detail_store.retain_routes")
        self.assertIn("render_task_focus", status[retain:])
        self.assertIn("sync_task_voice_route", status)

    def test_detail_is_a_separate_authenticated_transaction(self):
        detail = self.source.split("if (kind == CodexMessageKind::TaskDetail)", 1)[1]
        detail = detail.split("char model[18]", 1)[0]
        self.assertIn("decrypt_codex_task_detail", detail)
        self.assertIn("_task_detail_store.accept", detail)
        self.assertIn("render_task_focus_detail", detail)
        self.assertNotIn("_last_status_payload_ms", detail)
        self.assertNotIn("snapshot.model", detail)
        self.assertNotIn("chunk.text", detail)

    def test_touch_entry_browse_selection_and_panel_ownership(self):
        self.assertIn("StatusPageSwipeResult::OpenTaskDeck", self.source)
        self.assertIn("open_task_deck()", self.source)
        self.assertIn("_task_focus_deck.browse(direction)", self.source)
        self.assertIn("CodexTaskFocusEvent::StateBoundaryCrossed", self.source)
        self.assertIn("GetHAL().vibrate", self.source)
        self.assertIn("_task_focus_deck.focused_index() == 0", self.source)
        selection = self.source.split("_task_focus_deck.select_focused()", 1)[1]
        self.assertLess(selection.index("sync_task_voice_route"),
                        selection.index("render_task_focus_detail"))
        running = self.source.split("update_agent_panel_touch(now);", 1)[1]
        self.assertLess(running.index("_agent_panel_model.owns_touch()"),
                        running.index("update_task_focus_touch(now)"))
        self.assertIn("task_mode == CodexTaskFocusMode::Status",
                      running)

    def test_task_modes_hide_dormant_agent_handle_and_skip_its_touch(self):
        self.assertIn("sync_agent_panel_for_task_mode", self.header)
        helper = self.source.split(
            "bool AppCodexStatus::sync_agent_panel_for_task_mode", 1
        )[1].split("\n}", 1)[0]
        self.assertIn("task_mode != CodexTaskFocusMode::Status", helper)
        self.assertIn("!snapshot.visible", helper)
        self.assertIn("LV_OBJ_FLAG_HIDDEN", helper)
        self.assertIn("_agent_panel_model.cancel_touch()", helper)
        running = self.source.split(
            "const bool agent_panel_available =", 1
        )[1].split("bool alert_changed", 1)[0]
        self.assertIn("if (agent_panel_available)", running)
        self.assertIn("update_agent_panel_touch(now)", running)

    def test_voice_and_lifecycle_fail_closed(self):
        self.assertIn("task_focus_voice_sink()", self.source)
        sink = self.source.split("VoiceButtonSink* AppCodexStatus::task_focus_voice_sink", 1)[1]
        sink = sink.split("\n}", 1)[0]
        self.assertIn("CodexTaskFocusMode::TaskDetail", sink)
        self.assertIn("_production_voice_link_usable", sink)
        self.assertIn("task.selected_route != 0", sink)
        self.assertIn("_task_detail_store.reset_connection()", self.source)
        self.assertIn("_task_detail_store.reset_for_credential", self.source)
        self.assertIn("_task_detail_store.reset()", self.source)
        self.assertIn("_voice_input->set_task_route(0)", self.source)
        self.assertNotIn("task detail plaintext", self.source.lower())

    def test_task_modes_update_edge_exclusion_and_close_clears_it(self):
        self.assertIn("sync_task_edge_exclusion", self.header)
        helper = self.source.split(
            "void AppCodexStatus::sync_task_edge_exclusion", 1
        )[1].split("\n}", 1)[0]
        self.assertIn("kTaskDeckTouchExclusion", helper)
        self.assertIn("kTaskModeTouchExclusion", helper)
        self.assertIn("setTouchExclusion", helper)
        self.assertIn("sync_task_edge_exclusion(CodexTaskFocusMode::TaskDeck)",
                      self.source)
        self.assertIn("sync_task_edge_exclusion(CodexTaskFocusMode::TaskDetail)",
                      self.source)
        self.assertIn("setTouchExclusion(std::nullopt)", self.source)

    def test_credential_change_clears_detail_ui_and_rearms_voice_ready(self):
        credential = self.source.split(
            "bool AppCodexStatus::sync_task_detail_credential", 1
        )[1].split("\n}\n\nvoid AppCodexStatus::sync_task_voice_route", 1)[0]
        self.assertIn("_task_detail_store.reset_for_credential", credential)
        self.assertIn("return content_cleared", credential)
        self.assertIn("credential_cleared_detail", self.source)
        self.assertGreaterEqual(
            self.source.count("_task_voice_ready_applied = false"), 5)
        self.assertIn("render_task_focus_detail", self.source)

    def test_task_state_is_mutex_protected_without_external_calls_under_lock(self):
        self.assertIn("std::mutex _task_state_mutex", self.header)
        self.assertIn("std::mutex _task_controller_mutex", self.header)
        self.assertIn("TaskFocusStateSnapshot", self.header)
        self.assertIn("snapshot_task_focus_state", self.header)
        self.assertIn("std::lock_guard<std::mutex> task_lock(_task_state_mutex)",
                      self.source)
        snapshot = self.source.split(
            "AppCodexStatus::snapshot_task_focus_state()", 1
        )[1].split("\n}\n\nvoid AppCodexStatus::sync_task_edge_exclusion", 1)[0]
        self.assertNotIn("_voice_input", snapshot)
        self.assertNotIn("_ble_transport", snapshot)
        self.assertNotIn("_task_focus_view", snapshot)
        self.assertNotIn("GetGlobalEdgeControlService", snapshot)


if __name__ == "__main__":
    unittest.main()
