import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AgentPanelFirmwareContractTests(unittest.TestCase):
    def test_codex_page_owns_pull_down_agent_panel(self):
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        self.assertIn('#include "agent_panel_model.h"', header)
        self.assertIn("AgentPanelModel _agent_panel_model", header)
        self.assertIn("lv_obj_t* _agent_panel_root", header)
        self.assertIn("update_agent_panel_touch", header)
        self.assertIn("render_agent_panel", header)
        self.assertIn("dispatch_agent_panel_decision", header)
        self.assertIn("lv_obj_move_foreground(_agent_panel_root)", source)
        self.assertIn("kAgentPanelHiddenY", source)
        self.assertIn('"AGENTS', source)

        running = source.split("void AppCodexStatus::onRunning()", 1)[1].split(
            "void AppCodexStatus::onClose()", 1
        )[0]
        self.assertLess(
            running.index("update_agent_panel_touch(now)"),
            running.index("update_page_swipe(now)"),
        )
        self.assertIn("_agent_panel_model.owns_touch()", source)

    def test_panel_has_request_switch_delete_and_reset_controls(self):
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        for label in (
            "Allow",
            "Allow + Switch",
            "Reject",
            "Delete Agent?",
            "Reset All Agents?",
            "Connected",
            "Requesting",
            "Offline",
        ):
            self.assertIn(f'"{label}"', source)
        self.assertIn("AgentPanelAction::ConfirmDelete", source)
        self.assertIn("AgentPanelAction::ConfirmReset", source)
        self.assertIn("remove_and_rotate", source)
        self.assertIn("reset_all", source)

    def test_switch_cleanup_runs_outside_lvgl_lock(self):
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        dispatch = source.split(
            "void AppCodexStatus::dispatch_agent_panel_decision", 1
        )[1].split("void AppCodexStatus::", 1)[0]
        self.assertIn("cancel_for_home", dispatch)
        self.assertIn("voice_cleanup_complete", dispatch)
        self.assertIn("request_active_disconnect", dispatch)
        self.assertNotIn("LvglLockGuard", dispatch)


if __name__ == "__main__":
    unittest.main()
