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
#include "agent_panel_model.h"
#include "agent_registry_store.h"
#include "secure_pairing_session.h"
#include "secure_pairing_wire.h"
#include "status_message_router.h"
#include "status_page_swipe_logic.h"
#include "trae_card_tap_logic.h"
#include "trae_status_model.h"
#include "trae_task_carousel.h"
#include "trae_status_page.h"
#include "voice_input_controller.h"
#include "runtime/services/shared_wifi_service.h"

class CodexBleTransport;
class BleVoiceProbeController;
struct esp_websocket_event_data;
typedef struct esp_websocket_client* esp_websocket_client_handle_t;

class AppCodexStatus : public mooncake::AppAbility {
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

    lv_obj_t* _panel             = nullptr;
    lv_obj_t* _page_strip        = nullptr;
    lv_obj_t* _codex_page        = nullptr;
    lv_obj_t* _trae_page_root    = nullptr;
    lv_obj_t* _page_dot_codex    = nullptr;
    lv_obj_t* _page_dot_trae     = nullptr;
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
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_buttons = {};
    std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_delete_buttons = {};
    lv_obj_t* _agent_allow_button = nullptr;
    lv_obj_t* _agent_allow_switch_button = nullptr;
    lv_obj_t* _agent_reject_button = nullptr;
    lv_obj_t* _agent_reset_button = nullptr;
    lv_obj_t* _agent_confirm_button = nullptr;
    lv_obj_t* _agent_cancel_button = nullptr;
    lv_timer_t* _pet_timer       = nullptr;
    esp_websocket_client_handle_t _ws_client = nullptr;
    std::unique_ptr<CodexBleTransport> _ble_transport;
    std::unique_ptr<BleVoiceProbeController> _voice_probe;
    std::unique_ptr<VoiceInputController> _voice_input;
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
    bool _wifi_started           = false;
    bool _wifi_fallback_started  = false;
    bool _ble_was_connected      = false;
    bool _ws_connected           = false;
    bool _screen_off             = false;
    bool _trae_stale             = false;
    int _current_page            = 0;
    lv_indev_state_t _last_page_touch_state = LV_INDEV_STATE_REL;
    lv_indev_state_t _last_agent_touch_state = LV_INDEV_STATE_REL;
    bool _suppress_task_navigation = false;
    CodexAlertCarousel _alert_carousel;
    TraeStatusSnapshot _trae_snapshot;
    TraeTaskCarousel _trae_carousel;
    StatusPageSwipeLogic _page_swipe;
    TraeCardTapLogic _trae_card_tap;
    VoiceButtonDispatcher _voice_button_dispatcher;
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
    bool _production_voice_ready = false;
    bool _production_voice_link_usable = false;
    uint16_t _production_voice_mtu = 23;
    uint32_t _production_voice_connection_epoch = 0;
    std::mutex _status_mutex;
    std::string _base_status = "START";
    uint32_t _base_status_color = 0;
    bool _base_status_is_error = false;
    std::atomic<VoiceInputUiState> _voice_status{VoiceInputUiState::Off};

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
    void sync_pending_glow();
    void update_page_swipe(uint32_t now_ms);
    void update_agent_panel_touch(uint32_t now_ms);
    void render_agent_panel();
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
    void set_page(int page, bool animate);
    void update_page_indicator();
    void sync_display_protection_presentation();
    void update_screen_off_control(uint32_t now_ms);
    static void page_animation_cb(void* target, int32_t x);
    static void pet_timer_cb(lv_timer_t* timer);
    static void websocket_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
};
