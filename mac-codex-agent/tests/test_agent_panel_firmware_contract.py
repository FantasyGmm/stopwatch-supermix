import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AgentPanelFirmwareContractTests(unittest.TestCase):
    def test_focus_rail_uses_approved_structure_and_copy(self):
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        self.assertIn('#include "agent_panel_ui_layout.h"', header)
        self.assertIn("_agent_rail", header)
        self.assertIn("lv_obj_set_scroll_snap_y", source)
        self.assertIn('"Switch request"', source)
        self.assertIn('"%u PAIRED"', source)
        self.assertIn('"Hold to reset"', source)
        self.assertIn('"PAIRING REQUEST"', source)
        self.assertIn('"Allow + Switch"', source)
        self.assertIn('"No paired Agents"', source)
        self.assertIn("LV_OBJ_FLAG_HIDDEN", source)
        self.assertIn("_agent_row_wrappers", header)
        self.assertIn("agent_panel_scroll_event_cb", header)
        self.assertIn("agent_panel_row_gesture_cb", header)
        self.assertIn("lv_obj_scroll_to_view", source)
        self.assertIn("lv_obj_set_flex_flow", source)
        self.assertIn("lv_obj_set_style_opa", source)
        self.assertNotIn('make_button("×"', source)

    def test_focus_rail_secondary_states_match_approved_contract(self):
        header = (
            ROOT / "main/apps/app_codex_status/app_codex_status.h"
        ).read_text()
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        for label in (
            "PAIRING REQUEST",
            "Allow + Switch",
            "Allow",
            "Reject",
            "Dismiss",
            "Delete Agent?",
            "Reset all Agents?",
            "Hold to reset",
            "No paired Agents",
            "Switching...",
            "Couldn't switch",
            "Couldn't delete",
            "Couldn't reset",
        ):
            self.assertIn(f'"{label}"', source)
        self.assertIn("deadline_ms()", source)
        self.assertIn("kResetHoldMs", source)
        self.assertIn("_agent_reset_hold_started_ms", header)
        self.assertIn("_agent_panel_notice_until_ms", header)
        self.assertIn("_agent_pairing_expired", header)
        self.assertIn("LV_EVENT_PRESS_LOST", source)

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

    def test_panel_exclusively_owns_vertical_gestures_while_visible(self):
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        render = source.split(
            "void AppCodexStatus::render_agent_panel()", 1
        )[1].split("void AppCodexStatus::update_agent_panel_touch", 1)[0]
        close = source.split("void AppCodexStatus::onClose()", 1)[1].split(
            "void AppCodexStatus::create_view()", 1
        )[0]

        self.assertNotIn(
            "GetGlobalEdgeControlService().setEnabled(!snapshot.visible)",
            render,
        )
        self.assertIn(
            "GetGlobalEdgeControlService().setEnabled(false)", render
        )
        self.assertIn("GetGlobalEdgeControlService().setEnabled(true)", close)

    def test_panel_follows_drag_and_settles_without_rebuild(self):
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        destroy = source.split(
            "void AppCodexStatus::destroy_view()", 1
        )[1].split("void AppCodexStatus::update_battery()", 1)[0]
        settle = source.partition(
            "void AppCodexStatus::settle_agent_panel(bool animate)"
        )[2].partition("void AppCodexStatus::update_agent_panel_touch")[0]
        touch = source.split(
            "void AppCodexStatus::update_agent_panel_touch", 1
        )[1].split("void AppCodexStatus::agent_panel_event_cb", 1)[0]

        self.assertIn("lv_anim_set_duration(&animation, 180)", settle)
        self.assertIn("lv_anim_path_ease_out", settle)
        self.assertIn("agent_panel_animation_cb", settle)
        self.assertIn(
            "lv_anim_delete(this, agent_panel_animation_cb)", destroy
        )
        self.assertIn("_agent_panel_model.vertical_drag_offset()", touch)
        self.assertIn("std::clamp", touch)
        self.assertIn("settle_agent_panel(true)", touch)
        self.assertNotIn("render_agent_panel();", touch)
        self.assertIn("y <= kAgentPanelHiddenY", settle)
        self.assertIn("_agent_panel_model.set_visible(true)", touch)
        self.assertEqual(
            touch.count(
                "if (_last_agent_touch_state == LV_INDEV_STATE_PR)"
            ),
            3,
        )

    def test_panel_has_request_switch_delete_and_reset_controls(self):
        source = (
            ROOT / "main/apps/app_codex_status/app_codex_status.cpp"
        ).read_text()
        for label in (
            "Allow",
            "Allow + Switch",
            "Reject",
            "Delete Agent?",
            "Reset all Agents?",
            "Connected",
            "Switch request",
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
