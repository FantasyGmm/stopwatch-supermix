/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_codex_status.h"
#include "assets/codex_pet_assets.h"
#include "codex_ble_transport.h"
#include "codex_status_model.h"
#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <esp_websocket_client.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

using namespace mooncake;

extern "C" const lv_image_dsc_t logo_codex;

namespace {

constexpr uint32_t kCodexBlue = 0x25C7FF;
constexpr uint32_t kCodexRed  = 0xFF4A2D;
constexpr uint32_t kTextDim   = 0x8A8F98;
constexpr const char* kDefaultServerHost = "Ethan-MacBook-Air.local";
constexpr int kDefaultServerPort         = 8765;
constexpr const char* kDefaultBleName    = "StopWatch-Codex";
constexpr uint32_t kWifiFallbackDelayMs  = 15000;
constexpr uint32_t kStatusStaleMs        = 10000;

lv_obj_t* make_label(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font, uint32_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

lv_obj_t* make_center_label(lv_obj_t* parent, const char* text, int y, const lv_font_t* font, uint32_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

lv_obj_t* make_arc(lv_obj_t* parent, int x, int y, uint32_t color, int value, int rotation)
{
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 92, 92);
    lv_obj_set_pos(arc, x, y);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, value);
    lv_arc_set_rotation(arc, rotation);
    lv_arc_set_bg_angles(arc, 0, 115);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x303042), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

lv_obj_t* make_arc_value(lv_obj_t* parent, int x, int y, const char* text, uint32_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, 92);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

struct QuotaWidgets {
    lv_obj_t* pct = nullptr;
    lv_obj_t* bar = nullptr;
};

QuotaWidgets make_quota_row(lv_obj_t* parent, int y, const char* label_text)
{
    constexpr int label_x = 54;
    constexpr int bar_x   = 102;
    constexpr int bar_w   = 244;
    constexpr int pct_x   = 346;
    constexpr int pct_w   = 66;

    make_label(parent, label_text, label_x, y - 2, &lv_font_montserrat_18, 0xFFFFFF);

    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, bar_w, 10);
    lv_obj_set_pos(bar, bar_x, y + 8);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x303042), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kCodexBlue), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_t* pct = lv_label_create(parent);
    lv_label_set_text(pct, "--%");
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pct, lv_color_hex(kCodexBlue), 0);
    lv_obj_set_width(pct, pct_w);
    lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(pct, pct_x, y - 6);

    return {pct, bar};
}

const char* battery_symbol(int percent)
{
    if (percent >= 88) {
        return LV_SYMBOL_BATTERY_FULL;
    }
    if (percent >= 63) {
        return LV_SYMBOL_BATTERY_3;
    }
    if (percent >= 38) {
        return LV_SYMBOL_BATTERY_2;
    }
    if (percent >= 15) {
        return LV_SYMBOL_BATTERY_1;
    }
    return LV_SYMBOL_BATTERY_EMPTY;
}

void copy_short(char* dest, size_t dest_size, const char* source)
{
    if (dest_size == 0) {
        return;
    }
    if (source == nullptr || source[0] == '\0') {
        std::snprintf(dest, dest_size, "--");
        return;
    }
    std::snprintf(dest, dest_size, "%s", source);
    dest[dest_size - 1] = '\0';
}

void uppercase_short(char* dest, size_t dest_size, const char* source)
{
    copy_short(dest, dest_size, source);
    for (size_t i = 0; dest[i] != '\0'; ++i) {
        dest[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(dest[i])));
    }
}

}  // namespace

AppCodexStatus::AppCodexStatus()
{
    setAppInfo().name = "Codex";
    setAppInfo().icon = (void*)&icon_codex;
}

AppCodexStatus::~AppCodexStatus() = default;

void AppCodexStatus::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppCodexStatus::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager     = std::make_unique<input::KeyManager>();
    _last_battery_ms = 0;
    _last_network_ms = 0;
    _last_status_payload_ms = 0;
    _ws_connected    = false;
    _wifi_started    = false;
    _wifi_fallback_started = false;
    _ble_was_connected = false;
    _server_host     = Settings("codex_status").GetString("host", kDefaultServerHost);
    _transport_mode  = Settings("codex_status").GetString("transport", "auto");
    _ble_name        = Settings("codex_status").GetString("ble_name", kDefaultBleName);
    if (_transport_mode != "auto" && _transport_mode != "ble" && _transport_mode != "wifi") {
        _transport_mode = "auto";
    }
    _ble_wait_started_ms = GetHAL().millis();

    {
        LvglLockGuard lock;
        create_view();
    }

    if (_transport_mode != "wifi") {
        _ble_transport = std::make_unique<CodexBleTransport>();
        if (_ble_transport->start(_ble_name)) {
            set_status("BLE PAIR", kTextDim);
            set_host_text(_ble_name.c_str());
        } else {
            _ble_transport.reset();
            set_status("BLE ERR", kCodexRed);
            if (_transport_mode == "auto") {
                _wifi_fallback_started = true;
                ensure_network_started();
            }
        }
    } else {
        _wifi_fallback_started = true;
        ensure_network_started();
    }
}

void AppCodexStatus::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    const uint32_t now = GetHAL().millis();
    const bool ble_connected = _ble_transport && _ble_transport->connected();
    if (_ble_transport) {
        _ble_transport->poll(now);
        CodexBleMessage message;
        while (_ble_transport->take_message(message)) {
            const bool handled = handle_status_json(
                    message.payload.data(),
                    static_cast<int>(message.payload.size()),
                    "BLE LIVE"
                );
            if (message.type == kCodexBleStatusMessage && handled) {
                _last_status_payload_ms = now;
            }
        }

        if (ble_connected && !_ble_was_connected) {
            stop_websocket();
            _ble_was_connected = true;
            set_status("BLE WAIT", kTextDim);
            set_host_text(_ble_name.c_str());
        } else if (!ble_connected && _ble_was_connected) {
            _ble_was_connected = false;
            _ble_wait_started_ms = now;
            set_status("BLE PAIR", kTextDim);
            set_host_text(_ble_name.c_str());
        }

        if (ble_connected && _last_status_payload_ms != 0 &&
            now - _last_status_payload_ms > kStatusStaleMs) {
            set_status("BLE WAIT", kTextDim);
            _last_status_payload_ms = 0;
        }
    }

    if (_transport_mode == "auto" && !ble_connected && !_wifi_fallback_started &&
        now - _ble_wait_started_ms >= kWifiFallbackDelayMs) {
        _wifi_fallback_started = true;
        set_status("WIFI", kTextDim);
        ensure_network_started();
    }

    if (_wifi_fallback_started && !ble_connected && now - _last_network_ms >= 2000) {
        _last_network_ms = now;
        ensure_network_started();
        if (_wifi_started && WifiManager::GetInstance().IsConnected() && _ws_client == nullptr) {
            start_websocket();
        }
    }

    if (now - _last_battery_ms >= 1000) {
        _last_battery_ms = now;
        LvglLockGuard lock;
        update_battery();
    }
}

void AppCodexStatus::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();
    WifiManager::GetInstance().SetEventCallback(nullptr);
    stop_websocket();
    if (_ble_transport) {
        _ble_transport->stop();
        _ble_transport.reset();
    }

    LvglLockGuard lock;
    destroy_view();
}

void AppCodexStatus::create_view()
{
    _panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_panel, 466, 466);
    lv_obj_align(_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_panel, 0, 0);
    lv_obj_set_style_border_width(_panel, 0, 0);
    lv_obj_set_style_pad_all(_panel, 0, 0);
    lv_obj_set_style_bg_color(_panel, lv_color_black(), 0);

    lv_obj_t* logo = lv_image_create(_panel);
    lv_image_set_src(logo, &logo_codex);
    lv_image_set_scale(logo, 170);
    lv_obj_set_pos(logo, 168, 22);

    make_label(_panel, "Codex", 218, 32, &lv_font_montserrat_24, kCodexBlue);

    _status_label = make_center_label(_panel, "START", 72, &lv_font_montserrat_14, kTextDim);
    _host_label = make_center_label(_panel, "", 92, &lv_font_montserrat_14, 0x5D6470);

    make_arc(_panel, 70, 105, kCodexRed, 0, 195);
    make_arc_value(_panel, 70, 138, "MODEL", 0xF2EAF9);
    _model_value = make_arc_value(_panel, 70, 157, "--", 0xFFFFFF);
    make_arc_value(_panel, 70, 182, "auto", 0xD8D0F0);

    make_arc(_panel, 306, 105, kCodexBlue, 0, 230);
    make_arc_value(_panel, 306, 138, "EFFORT", 0xF2EAF9);
    _effort_value = make_arc_value(_panel, 306, 157, "--", kCodexBlue);
    make_arc_value(_panel, 306, 182, "reason", 0xD8D0F0);

    _pet_image = lv_image_create(_panel);
    lv_image_set_src(_pet_image, codex_pet_idle_frames[0]);
    lv_image_set_antialias(_pet_image, false);
    lv_obj_clear_flag(_pet_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(_pet_image, 234 - CODEX_PET_FRAME_W / 2, 147 - 58);

    if (_pet_timer == nullptr) {
        _pet_timer = lv_timer_create(AppCodexStatus::pet_timer_cb, codex_pet_idle_durations_ms[0], this);
    }

    QuotaWidgets five_hour = make_quota_row(_panel, 220, "5H");
    _five_hour_pct = five_hour.pct;
    _five_hour_bar = five_hour.bar;
    QuotaWidgets week = make_quota_row(_panel, 268, "7D");
    _week_pct = week.pct;
    _week_bar = week.bar;

    make_center_label(_panel, "Activity", 320, &lv_font_montserrat_18, 0xF3EEF8);
    _day_value  = make_center_label(_panel, "DAY  -- m", 358, &lv_font_montserrat_14, 0xCCCCCC);
    _week_value = make_center_label(_panel, "WK   -- m", 388, &lv_font_montserrat_14, 0xCCCCCC);

    _battery_label = lv_label_create(_panel);
    lv_obj_set_style_text_font(_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_align(_battery_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    update_battery();
}

void AppCodexStatus::destroy_view()
{
    if (_pet_timer != nullptr) {
        lv_timer_delete(_pet_timer);
        _pet_timer = nullptr;
    }

    if (_panel != nullptr) {
        lv_obj_delete(_panel);
        _panel = nullptr;
    }

    _model_value    = nullptr;
    _effort_value   = nullptr;
    _five_hour_pct  = nullptr;
    _five_hour_bar  = nullptr;
    _week_pct       = nullptr;
    _week_bar       = nullptr;
    _day_value      = nullptr;
    _week_value     = nullptr;
    _battery_label  = nullptr;
    _status_label   = nullptr;
    _host_label     = nullptr;
    _pet_image      = nullptr;
    _pet_frame      = 0;
}

void AppCodexStatus::update_battery()
{
    if (_battery_label == nullptr) {
        return;
    }

    int percent = std::clamp(static_cast<int>(GetHAL().getBatteryLevel()), 0, 100);
    bool charging = GetHAL().isBatteryCharging();

    char text[32];
    if (charging) {
        std::snprintf(text, sizeof(text), LV_SYMBOL_CHARGE " %d%%", percent);
    } else {
        std::snprintf(text, sizeof(text), "%s %d%%", battery_symbol(percent), percent);
    }

    uint32_t color = 0xAAAAAA;
    if (charging) {
        color = 0x3CB371;
    } else if (percent < 15) {
        color = 0xE74C3C;
    }

    lv_label_set_text(_battery_label, text);
    lv_obj_set_style_text_color(_battery_label, lv_color_hex(color), 0);
}

void AppCodexStatus::ensure_network_started()
{
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsInitialized()) {
        WifiManagerConfig config;
        config.ssid_prefix = "StopWatch";
        config.language = "zh-CN";
        wifi.Initialize(config);
        wifi.SetPowerSaveLevel(WifiPowerSaveLevel::PERFORMANCE);
    }

    if (wifi.IsConnected()) {
        _wifi_started = true;
        set_status(_ws_connected ? "LIVE" : "CONNECT", _ws_connected ? kCodexBlue : kTextDim);
        set_host_text(_server_host.c_str());
        return;
    }

    if (_ws_client != nullptr || _ws_connected) {
        stop_websocket();
    }

    if (_wifi_started) {
        if (wifi.IsConfigMode()) {
            set_status("SETUP", kCodexRed);
            set_host_text(wifi.GetApSsid().c_str());
        } else {
            set_status("SCAN", kTextDim);
            set_host_text(_server_host.c_str());
        }
        return;
    }

    const auto& saved_ssids = SsidManager::GetInstance().GetSsidList();
    if (saved_ssids.empty()) {
        wifi.StartConfigAp();
        _wifi_started = true;
        set_status("SETUP", kCodexRed);
        set_host_text(wifi.GetApSsid().c_str());
        return;
    }

    wifi.StartStation();
    _wifi_started = true;
    set_status("SCAN", kTextDim);
    set_host_text(_server_host.c_str());
}

void AppCodexStatus::start_websocket()
{
    if (_ws_client != nullptr || !WifiManager::GetInstance().IsConnected()) {
        return;
    }

    char uri[160];
    std::snprintf(uri, sizeof(uri), "ws://%s:%d/", _server_host.c_str(), kDefaultServerPort);

    esp_websocket_client_config_t config = {};
    config.uri = uri;
    config.reconnect_timeout_ms = 5000;
    config.network_timeout_ms = 10000;
    config.buffer_size = 8192;
    config.ping_interval_sec = 10;
    config.pingpong_timeout_sec = 20;

    _ws_client = esp_websocket_client_init(&config);
    if (_ws_client == nullptr) {
        set_status("WS ERR", kCodexRed);
        return;
    }

    esp_websocket_register_events(_ws_client, WEBSOCKET_EVENT_ANY, AppCodexStatus::websocket_event_handler, this);
    esp_err_t err = esp_websocket_client_start(_ws_client);
    if (err != ESP_OK) {
        mclog::tagError(getAppInfo().name, "websocket start failed: {}", esp_err_to_name(err));
        esp_websocket_client_destroy(_ws_client);
        _ws_client = nullptr;
        set_status("WS ERR", kCodexRed);
        return;
    }

    set_status("WAITING", kTextDim);
    set_host_text(_server_host.c_str());
}

void AppCodexStatus::stop_websocket()
{
    if (_ws_client == nullptr) {
        return;
    }

    auto client = _ws_client;
    _ws_client = nullptr;
    _ws_connected = false;
    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
}

void AppCodexStatus::set_status(const char* status, uint32_t color)
{
    LvglLockGuard lock;
    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, status);
        lv_obj_set_style_text_color(_status_label, lv_color_hex(color), 0);
    }
}

void AppCodexStatus::set_host_text(const char* prefix)
{
    LvglLockGuard lock;
    if (_host_label == nullptr) {
        return;
    }

    if (prefix == nullptr || prefix[0] == '\0') {
        lv_label_set_text(_host_label, "");
        return;
    }
    lv_label_set_text(_host_label, prefix);
}

bool AppCodexStatus::handle_status_json(const char* data, int length, const char* live_status)
{
    CodexStatusSnapshot snapshot;
    const CodexMessageKind kind = parse_codex_message(data, static_cast<size_t>(length), snapshot);
    if (kind == CodexMessageKind::Invalid) {
        return false;
    }
    if (kind == CodexMessageKind::Chime) {
        audio::play_tone(660, 0.45f, 0.45f);
        return true;
    }

    char model[18];
    char effort[12];
    copy_short(model, sizeof(model), snapshot.model.c_str());
    uppercase_short(effort, sizeof(effort), snapshot.effort.c_str());

    LvglLockGuard lock;
    if (_model_value != nullptr && model[0] != '\0') {
        lv_label_set_text(_model_value, model);
    }
    if (_effort_value != nullptr && effort[0] != '\0') {
        lv_label_set_text(_effort_value, effort);
    }
    set_percent(_five_hour_pct, _five_hour_bar, snapshot.five_hour_left_pct);
    set_percent(_week_pct, _week_bar, snapshot.week_left_pct);

    char text[24];
    if (_day_value != nullptr && snapshot.today_minutes >= 0) {
        std::snprintf(text, sizeof(text), "DAY  %d m", snapshot.today_minutes);
        lv_label_set_text(_day_value, text);
    }
    if (_week_value != nullptr && snapshot.week_minutes >= 0) {
        std::snprintf(text, sizeof(text), "WK   %d m", snapshot.week_minutes);
        lv_label_set_text(_week_value, text);
    }
    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, live_status);
        lv_obj_set_style_text_color(_status_label, lv_color_hex(kCodexBlue), 0);
    }
    return true;
}

void AppCodexStatus::set_percent(lv_obj_t* label, lv_obj_t* bar, int value)
{
    char text[12];
    if (value < 0) {
        std::snprintf(text, sizeof(text), "--%%");
        value = 0;
    } else {
        value = std::clamp(value, 0, 100);
        std::snprintf(text, sizeof(text), "%d%%", value);
    }

    if (label != nullptr) {
        lv_label_set_text(label, text);
    }
    if (bar != nullptr) {
        lv_bar_set_value(bar, value, LV_ANIM_OFF);
    }
}

void AppCodexStatus::pet_timer_cb(lv_timer_t* timer)
{
    auto* self = static_cast<AppCodexStatus*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->_pet_image == nullptr) {
        return;
    }

    self->_pet_frame = (self->_pet_frame + 1) % CODEX_PET_IDLE_FRAME_COUNT;
    lv_image_set_src(self->_pet_image, codex_pet_idle_frames[self->_pet_frame]);
    lv_timer_set_period(timer, codex_pet_idle_durations_ms[self->_pet_frame]);
}

void AppCodexStatus::websocket_event_handler(void* handler_args, esp_event_base_t, int32_t event_id, void* event_data)
{
    auto* self = static_cast<AppCodexStatus*>(handler_args);
    if (self == nullptr) {
        return;
    }

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            self->_ws_connected = true;
            self->set_status("WAITING", kTextDim);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            self->_ws_connected = false;
            if (self->_ws_client != nullptr) {
                self->set_status("RETRY", kCodexRed);
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            self->_ws_connected = false;
            self->set_status("WS ERR", kCodexRed);
            break;
        case WEBSOCKET_EVENT_DATA: {
            auto* data = static_cast<esp_websocket_event_data_t*>(event_data);
            if (data != nullptr && data->op_code == 0x01 && data->payload_offset == 0 && data->data_len > 0) {
                self->handle_status_json(static_cast<const char*>(data->data_ptr), data->data_len);
            }
            break;
        }
        default:
            break;
    }
}
