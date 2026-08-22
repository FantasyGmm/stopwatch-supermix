/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <memory>
#include <array>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <string>
#include <esp_event_base.h>
#include "codex_alert_carousel.h"
#include "codex_pending_soft_glow.h"
#include "codex_task_detail_store.h"
#include "codex_task_focus_deck.h"
#include "codex_task_focus_gesture.h"
#include "codex_task_focus_view.h"
#include "codex_runtime_session.h"
#include "agent_panel_model.h"
#include "agent_panel_ui_layout.h"
#include "agent_registry_store.h"
#include "secure_pairing_session.h"
#include "secure_pairing_wire.h"
#include "status_message_router.h"
#include "status_page_swipe_logic.h"
#include "trae_card_tap_logic.h"
#include "trae_status_model.h"
#include "trae_task_carousel.h"
#include "trae_status_page.h"
#include "input_action_controller.h"
#include "pinned_microphone_controller.h"
#include "voice_input_controller.h"
#include "runtime/services/shared_wifi_service.h"

class CodexBleTransport;
class BleVoiceProbeController;
struct esp_websocket_event_data;
typedef struct esp_websocket_client* esp_websocket_client_handle_t;

enum class CodexTaskFocusMode : uint8_t {
    Status,
    TaskDeck,
    TaskDetail,
};

class AppCodexStatus : public mooncake::AppAbility,
                       public CodexButtonActionSink {
public:
    AppCodexStatus();
    ~AppCodexStatus() override;

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<CodexPendingSoftGlow> _pending_glow;
    std::unique_ptr<TraeStatusPage> _trae_status_page;
    std::unique_ptr<CodexTaskFocusView> _task_focus_view;
    std::unique_ptr<voice_input::crypto::CcmContext> _task_detail_crypto;

    lv_obj_t* _panel             = nullptr;
    lv_obj_t* _page_strip        = nullptr;
    lv_obj_t* _codex_page        = nullptr;
    lv_obj_t* _pinned_microphone_page = nullptr;
    lv_obj_t* _trae_page_root    = nullptr;
    lv_obj_t* _page_dot_codex    = nullptr;
    lv_obj_t* _page_dot_pinned_microphone = nullptr;
    lv_obj_t* _page_dot_trae     = nullptr;
    lv_obj_t* _pinned_microphone_touch_target = nullptr;
    lv_obj_t* _pinned_microphone_connection_dot = nullptr;
    lv_obj_t* _pinned_microphone_connection_label = nullptr;
    lv_obj_t* _pinned_microphone_capsule = nullptr;
    lv_obj_t* _pinned_microphone_cup = nullptr;
    lv_obj_t* _pinned_microphone_stem = nullptr;
    lv_obj_t* _pinned_microphone_base = nullptr;
    lv_obj_t* _pinned_microphone_prompt_label = nullptr;
    lv_obj_t* _model_value       = nullptr;
    lv_obj_t* _effort_value      = nullptr;
    lv_obj_t* _week_pct          = nullptr;
    lv_obj_t* _week_bar          = nullptr;
    lv_obj_t* _pending_label     = nullptr;
    lv_obj_t* _pending_count     = nullptr;
    lv_obj_t* _running_label     = nullptr;
    lv_obj_t* _running_count     = nullptr;
    lv_obj_t* _task_index        = nullptr;
    lv_obj_t* _alert_project     = nullptr;
    lv_obj_t* _alert_detail      = nullptr;
    lv_obj_t* _battery_label     = nullptr;
    lv_obj_t* _status_label      = nullptr;
    lv_obj_t* _host_label        = nullptr;
    lv_obj_t* _pet_image         = nullptr;
    lv_obj_t* _agent_panel_root  = nullptr;
    lv_obj_t* _agent_panel_hint = nullptr;
    lv_obj_t* _agent_rail = nullptr;
    lv_obj_t* _agent_focus_band_left = nullptr;
    lv_obj_t* _agent_focus_band_right = nullptr;
    lv_obj_t* _agent_focus_marker = nullptr;
    lv_obj_t* _agent_count_label = nullptr;
    lv_obj_t* _agent_panel_notice_label = nullptr;
    lv_obj_t* _agent_pairing_countdown_label = nullptr;
    lv_obj_t* _agent_reset_progress = nullptr;
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_wrappers = {};
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_buttons = {};
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_delete_buttons = {};
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_names = {};
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_states = {};
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_ids = {};
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_times = {};
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_dots = {};
    lv_obj_t* _agent_manage_button = nullptr;
    lv_obj_t* _agent_manage_done_button = nullptr;
    lv_obj_t* _agent_allow_button = nullptr;
    lv_obj_t* _agent_allow_switch_button = nullptr;
    lv_obj_t* _agent_reject_button = nullptr;
    lv_obj_t* _agent_dismiss_button = nullptr;
    lv_obj_t* _agent_reset_button = nullptr;
    lv_obj_t* _agent_confirm_button = nullptr;
    lv_obj_t* _agent_cancel_button = nullptr;
    lv_timer_t* _pet_timer       = nullptr;
    esp_websocket_client_handle_t _ws_client = nullptr;
    CodexRuntimeSession _runtime_session;
    CodexBleTransport* _ble_transport = nullptr;
    BleVoiceProbeController* _voice_probe = nullptr;
    VoiceInputController* _voice_input = nullptr;
    WifiStaLease _wifi_lease;
    std::string _server_host;
    std::string _transport_mode;
    std::string _ble_name;
    uint8_t _pet_frame           = 0;
    uint32_t _last_battery_ms    = 0;
    uint32_t _last_network_ms    = 0;
    uint32_t _ble_wait_started_ms = 0;
    uint32_t _last_status_payload_ms = 0;
    uint32_t _last_trae_status_payload_ms = 0;
    uint32_t _screen_off_left_press_started_ms = 0;
    uint32_t _agent_reset_hold_started_ms = 0;
    uint32_t _agent_panel_notice_until_ms = 0;
    bool _wifi_started           = false;
    bool _wifi_fallback_started  = false;
    bool _ble_was_connected      = false;
    bool _ws_connected           = false;
    bool _screen_off             = false;
    bool _trae_stale             = false;
    int _current_page            = 1;
    lv_indev_state_t _last_page_touch_state = LV_INDEV_STATE_REL;
    int _last_pinned_microphone_render_state = -1;
    lv_indev_state_t _last_agent_touch_state = LV_INDEV_STATE_REL;
    lv_indev_state_t _last_task_touch_state = LV_INDEV_STATE_REL;
    int _agent_panel_drag_start_y = 0;
    int _task_deck_drag_start_y = 466;
    int _agent_revealed_delete_index = -1;
    bool _agent_reset_hold_active = false;
    bool _agent_reset_hold_queued = false;
    bool _agent_pairing_expired = false;
    int _task_touch_start_x = 0;
    int _task_touch_start_y = 0;
    int _task_touch_last_y = 0;
    bool _task_touch_moved = false;
    bool _task_deck_drag_active = false;
    bool _task_detail_started_at_top = false;
    codex_task_focus_gesture::TaskFocusGestureAxis _task_touch_axis =
        codex_task_focus_gesture::TaskFocusGestureAxis::Undecided;
    bool _agent_panel_manage_mode = false;
    bool _suppress_task_navigation = false;
    CodexAlertCarousel _alert_carousel;
    CodexTaskFocusDeck _task_focus_deck;
    CodexTaskDetailStore _task_detail_store;
    CodexTaskFocusMode _task_focus_mode = CodexTaskFocusMode::Status;
    uint32_t _task_detail_key_epoch = 0;
    std::mutex _task_state_mutex;
    std::mutex _task_controller_mutex;
    TraeStatusSnapshot _trae_snapshot;
    TraeTaskCarousel _trae_carousel;
    StatusPageSwipeLogic _page_swipe;
    TraeCardTapLogic _trae_card_tap;
    VoiceButtonDispatcher _voice_button_dispatcher;
    PinnedMicrophoneController _pinned_microphone_controller;
    AgentPanelModel _agent_panel_model;
    secure_pairing::AgentRegistry _agent_registry;
    secure_pairing::SecurePairingSession _secure_pairing_session;
    std::unique_ptr<secure_pairing::NvsRegistryStorageBackend>
        _agent_registry_backend;
    std::unique_ptr<secure_pairing::AgentRegistryStore>
        _agent_registry_store;
    bool _agent_registry_ready = false;
    bool _agent_panel_dirty = false;
    bool _agent_switch_pending = false;
    secure_pairing::AgentFingerprint _agent_switch_target = {};
    std::array<char, 32> _agent_panel_notice = {};
    enum class PairingCompletion : uint8_t {
        None,
        Authentication,
        CredentialAck,
    };
    PairingCompletion _pairing_completion = PairingCompletion::None;
    uint32_t _pairing_completion_transaction = 0;
    bool _pairing_allow_switch = false;
    std::mutex _agent_decision_mutex;
    AgentPanelDecision _pending_agent_decision = {};
    InputActionController* _input_action_controller = nullptr;
    bool _production_voice_ready = false;
    bool _production_voice_link_usable = false;
    uint8_t _production_voice_capabilities = 0;
    bool _task_voice_ready_applied = false;
    uint16_t _production_voice_mtu = 23;
    uint32_t _production_voice_connection_epoch = 0;
    std::mutex _status_mutex;
    std::string _base_status = "START";
    uint32_t _base_status_color = 0;
    bool _base_status_is_error = false;
    std::atomic<VoiceInputUiState> _voice_status{VoiceInputUiState::Off};

    struct TaskFocusStateSnapshot {
        CodexTaskFocusDeck deck;
        CodexTaskFocusMode mode = CodexTaskFocusMode::Status;
        CodexTaskAlert selected_task;
        std::string detail_text;
        int32_t detail_revision = -1;
        uint64_t selected_route = 0;
        bool has_selected_task = false;
        bool has_detail = false;
    };

    void create_view();
    void destroy_view();
    void update_battery();
    void ensure_network_started();
    void stop_network_fallback();
    void start_websocket();
    void stop_websocket();
    void set_status(const char* status, uint32_t color);
    void render_status();
    void set_host_text(const char* prefix);
    bool handle_status_json(const char* data, int length, const char* live_status = "LIVE");
    void set_percent(lv_obj_t* label, lv_obj_t* bar, int value);
    void render_alert();
    void render_trae_status();
    void render_pinned_microphone();
    void sync_pending_glow();
    void update_page_swipe(uint32_t now_ms);
    void update_task_focus_touch(uint32_t now_ms);
    void cancel_task_focus_touch();
    void open_task_deck();
    void set_task_focus_y(int y);
    void settle_task_deck_entry(bool open, bool animate);
    void render_task_focus();
    void render_task_focus_detail();
    void sync_task_focus_visibility();
    void sync_task_voice_route();
    void sync_task_voice_readiness();
    bool sync_task_detail_credential();
    void sync_task_edge_exclusion(CodexTaskFocusMode mode);
    TaskFocusStateSnapshot snapshot_task_focus_state();
    VoiceButtonSink* task_focus_voice_sink();
    bool sync_agent_panel_for_task_mode(CodexTaskFocusMode task_mode);
    void update_agent_panel_touch(uint32_t now_ms);
    void render_agent_panel();
    void update_agent_panel_runtime(uint32_t now_ms);
    void show_agent_panel_notice(const char* text, uint32_t now_ms);
    void sync_agent_rail_focus();
    void close_agent_delete_reveal();
    void set_agent_panel_y(int y);
    void settle_agent_panel(bool animate);
    void queue_agent_panel_decision(const AgentPanelDecision& decision);
    bool take_agent_panel_decision(AgentPanelDecision& decision);
    void dispatch_agent_panel_decision(
        const AgentPanelDecision& decision,
        uint32_t now_ms);
    void sync_agent_panel_registry();
    void process_secure_pairing(uint32_t now_ms);
    bool send_pairing_empty(
        uint16_t connection_handle,
        secure_pairing::WireMessageType type,
        uint32_t transaction_id);
    bool send_pairing_credential(
        uint16_t connection_handle,
        const secure_pairing::CredentialEnvelope& envelope);
    void complete_authenticated_pending(
        uint16_t connection_handle,
        PairingCompletion completion,
        uint32_t transaction_id,
        bool allow_switch);
    void clear_pairing_runtime();
    void complete_agent_switch_if_ready();
    bool save_agent_registry();
    static void agent_panel_event_cb(lv_event_t* event);
    static void agent_panel_scroll_event_cb(lv_event_t* event);
    static void agent_panel_row_gesture_cb(lv_event_t* event);
    static void agent_reset_hold_event_cb(lv_event_t* event);
    static void agent_panel_animation_cb(void* target, int32_t y);
    static void task_deck_animation_cb(void* target, int32_t y);
    static void task_deck_animation_completed_cb(lv_anim_t* animation);
    static void pinned_microphone_touch_event_cb(lv_event_t* event);
    void set_page(int page, bool animate);
    void update_page_indicator();
    void sync_display_protection_presentation();
    void update_screen_off_control(uint32_t now_ms);
    static void page_animation_cb(void* target, int32_t x);
    static void pet_timer_cb(lv_timer_t* timer);
    void handle_action(CodexButtonAction action, uint32_t now_ms) override;
    static void websocket_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
};
