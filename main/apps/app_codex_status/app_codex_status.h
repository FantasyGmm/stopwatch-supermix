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
#include <cstdint>
#include <string>
#include <esp_event_base.h>
#include "codex_alert_carousel.h"
#include "codex_pending_soft_glow.h"
#include "status_message_router.h"
#include "status_page_swipe_logic.h"
#include "trae_card_tap_logic.h"
#include "trae_status_model.h"
#include "trae_task_carousel.h"
#include "trae_status_page.h"
#include "runtime/services/shared_wifi_service.h"

class CodexBleTransport;
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
    lv_obj_t* _five_hour_pct     = nullptr;
    lv_obj_t* _five_hour_bar     = nullptr;
    lv_obj_t* _week_pct          = nullptr;
    lv_obj_t* _week_bar          = nullptr;
    lv_obj_t* _activity_title    = nullptr;
    lv_obj_t* _alert_project     = nullptr;
    lv_obj_t* _alert_detail      = nullptr;
    lv_obj_t* _battery_label     = nullptr;
    lv_obj_t* _status_label      = nullptr;
    lv_obj_t* _host_label        = nullptr;
    lv_obj_t* _pet_image         = nullptr;
    lv_timer_t* _pet_timer       = nullptr;
    esp_websocket_client_handle_t _ws_client = nullptr;
    std::unique_ptr<CodexBleTransport> _ble_transport;
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
    int _screen_off_restore_brightness = 80;
    bool _wifi_started           = false;
    bool _wifi_fallback_started  = false;
    bool _ble_was_connected      = false;
    bool _ws_connected           = false;
    bool _screen_off             = false;
    bool _screen_off_wake_armed  = true;
    bool _trae_stale             = false;
    int _current_page            = 0;
    lv_indev_state_t _last_page_touch_state = LV_INDEV_STATE_REL;
    CodexAlertCarousel _alert_carousel;
    TraeStatusSnapshot _trae_snapshot;
    TraeTaskCarousel _trae_carousel;
    StatusPageSwipeLogic _page_swipe;
    TraeCardTapLogic _trae_card_tap;

    void create_view();
    void destroy_view();
    void update_battery();
    void ensure_network_started();
    void stop_network_fallback();
    void start_websocket();
    void stop_websocket();
    void set_status(const char* status, uint32_t color);
    void set_host_text(const char* prefix);
    bool handle_status_json(const char* data, int length, const char* live_status = "LIVE");
    void set_percent(lv_obj_t* label, lv_obj_t* bar, int value);
    void render_alert();
    void render_trae_status();
    void sync_pending_glow();
    void update_page_swipe(uint32_t now_ms);
    void set_page(int page, bool animate);
    void update_page_indicator();
    bool update_screen_off_control(uint32_t now_ms);
    void enter_screen_off();
    void wake_screen();
    static void page_animation_cb(void* target, int32_t x);
    static void pet_timer_cb(lv_timer_t* timer);
    static void websocket_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
};
