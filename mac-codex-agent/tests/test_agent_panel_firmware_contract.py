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

    def test_key_rotation_terminates_every_inflight_pairing_session(self):
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        delete_flow = source.split(
            "decision.action == AgentPanelAction::ConfirmDelete", 1
        )[1].split(
            "decision.action == AgentPanelAction::ConfirmReset", 1
        )[0]
        reset_flow = source.split(
            "decision.action == AgentPanelAction::ConfirmReset", 1
        )[1].split(
            "decision.action != AgentPanelAction::SwitchAgent", 1
        )[0]
        self.assertIn("reject_pending(pending)", delete_flow)
        self.assertIn("clear_pairing_runtime()", delete_flow)
        self.assertNotIn("pending_fingerprint", delete_flow)
        self.assertIn("removing_active", delete_flow)
        self.assertIn("request_active_disconnect(", delete_flow)
        self.assertIn("if (removing_active)", delete_flow)
        self.assertIn("reject_pending(pending)", reset_flow)
        self.assertIn("clear_pairing_runtime()", reset_flow)
        self.assertIn("request_active_disconnect(true)", reset_flow)

    def test_pairing_gatt_frames_drive_session_and_physical_approval(self):
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        self.assertIn("SecurePairingSession _secure_pairing_session", header)
        self.assertIn("process_secure_pairing", header)
        pairing = source.split(
            "void AppCodexStatus::process_secure_pairing", 1
        )[1].split("void AppCodexStatus::complete_agent_switch_if_ready", 1)[0]
        for operation in (
            "take_pairing_frame",
            "decode_pairing_request",
            "request_pairing",
            "request_authentication",
            "authenticate",
            "acknowledge_credential",
        ):
            self.assertIn(operation, pairing)
        self.assertIn("authorize_pending", source)

        dispatch = source.split(
            "void AppCodexStatus::dispatch_agent_panel_decision", 1
        )[1].split("bool AppCodexStatus::send_pairing_empty", 1)[0]
        self.assertIn("AgentPanelAction::AllowPairing", dispatch)
        self.assertIn("AgentPanelAction::AllowAndSwitch", dispatch)
        self.assertIn("_secure_pairing_session.confirm_pairing", dispatch)

    def test_completed_nonselected_pending_is_released_even_if_notify_fails(self):
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        completion = source.split(
            "void AppCodexStatus::complete_authenticated_pending", 1
        )[1].split("void AppCodexStatus::process_secure_pairing", 1)[0]
        self.assertNotIn("disconnect_if_not_selected", completion)
        self.assertIn("reject_and_clear", completion)
        self.assertIn("(void)send_pairing_empty", completion)

        switch_completion = source.split(
            "void AppCodexStatus::complete_agent_switch_if_ready", 1
        )[1].split("void AppCodexStatus::sync_agent_panel_registry", 1)[0]
        self.assertIn("clear_failed_switch", switch_completion)
        self.assertIn("clear_failed_switch(pending)", switch_completion)

    def test_secure_pairing_enables_hkdf_in_all_firmware_builds(self):
        defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
        checked_in = (ROOT / "sdkconfig").read_text(encoding="utf-8")

        self.assertIn("CONFIG_MBEDTLS_HKDF_C=y", defaults.splitlines())
        self.assertIn("CONFIG_MBEDTLS_HKDF_C=y", checked_in.splitlines())


if __name__ == "__main__":
    unittest.main()
