/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_codex_status.h"
#include "assets/codex_pet_assets.h"
#include "codex_ble_transport.h"
#include "codex_pending_glow_logic.h"
#include "codex_status_ui_layout.h"
#include "codex_status_model.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <wifi_manager.h>
#include <esp_websocket_client.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

using namespace mooncake;

extern "C" const lv_image_dsc_t logo_codex;
LV_FONT_DECLARE(font_puhui_14_1)

namespace {

using namespace codex_status_ui;

constexpr uint32_t kCodexBlue = kBeaconViolet;
constexpr uint32_t kCodexRed = kAlertVermilion;
constexpr uint32_t kHandledGreen = 0x3CB371;
constexpr uint32_t kCompletedGreen = kHandledGreen;
constexpr uint32_t kTextDim = kEtchedGray;
constexpr const char* kDefaultServerHost = "Ethan-MacBook-Air.local";
constexpr int kDefaultServerPort         = 8765;
constexpr const char* kDefaultBleName    = "StopWatch-Codex";
constexpr uint32_t kWifiFallbackDelayMs  = 15000;
constexpr uint32_t kStatusStaleMs        = 10000;
constexpr uint32_t kScreenOffHoldMs      = 2000;

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

lv_obj_t* make_frame_segment(
    lv_obj_t* parent,
    int x,
    int y,
    int width,
    int height,
    uint32_t color
)
{
    lv_obj_t* segment = lv_obj_create(parent);
    lv_obj_set_size(segment, width, height);
    lv_obj_set_pos(segment, x, y);
    lv_obj_set_style_border_width(segment, 0, 0);
    lv_obj_set_style_pad_all(segment, 0, 0);
    lv_obj_set_style_radius(segment, 0, 0);
    lv_obj_set_style_bg_color(segment, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
    lv_obj_clear_flag(segment, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(segment, LV_OBJ_FLAG_CLICKABLE);
    return segment;
}

lv_obj_t* make_telemetry_label(
    lv_obj_t* parent,
    int x,
    int y,
    int width,
    const char* text,
    const lv_font_t* font,
    uint32_t color
)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, x, y);
    return label;
}

struct QuotaWidgets {
    lv_obj_t* pct = nullptr;
    lv_obj_t* bar = nullptr;
};

QuotaWidgets make_quota_row(lv_obj_t* parent, int y, const char* label_text)
{
    make_label(parent, label_text, kQuotaX, y - 2, &lv_font_montserrat_18, kSignalWhite);

    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, kQuotaBarWidth, 8);
    lv_obj_set_pos(bar, kQuotaBarX, y + 8);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kPanelGraphite), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kQuotaMint), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_t* pct = lv_label_create(parent);
    lv_label_set_text(pct, "--%");
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pct, lv_color_hex(kSignalWhite), 0);
    lv_obj_set_width(pct, kQuotaPercentWidth);
    lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(pct, kQuotaPercentX, y - 6);

    return {pct, bar};
}

lv_obj_t* make_line(lv_obj_t* parent, int x, int y, int width)
{
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, width, 1);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(kEtchedGray), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_40, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
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
    _screen_off_left_press_started_ms = 0;
    _screen_off_restore_brightness = std::max(10, GetHAL().getBackLightBrightness());
    _screen_off = false;
    _screen_off_wake_armed = true;
    _ws_connected    = false;
    _wifi_lease.reset();
    _wifi_started    = false;
    _wifi_fallback_started = false;
    _ble_was_connected = false;
    _alert_carousel.reset();
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
    const uint32_t now = GetHAL().millis();
    input::KeyEvent key_event = input::KeyEvent::None;
    if (_key_manager) {
        key_event = _key_manager->update();
    }

    const bool woke_screen = update_screen_off_control(now);
    if (!woke_screen && key_event == input::KeyEvent::GoHome) {
        close();
        return;
    }

    {
        LvglLockGuard lock;
        if (_alert_carousel.update(now)) {
            render_alert();
        }
    }
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
            stop_network_fallback();
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
    if (_screen_off) {
        wake_screen();
    }
    stop_network_fallback();
    if (_ble_transport) {
        _ble_transport->stop();
        _ble_transport.reset();
    }

    LvglLockGuard lock;
    destroy_view();
    _alert_carousel.reset();
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
    lv_obj_set_style_bg_color(_panel, lv_color_hex(kInstrumentBlack), 0);

    _pending_glow = std::make_unique<CodexPendingSoftGlow>();
    _pending_glow->init(_panel);

    lv_obj_t* logo = lv_image_create(_panel);
    lv_image_set_src(logo, &logo_codex);
    lv_image_set_scale(logo, kLogoScale);
    lv_obj_set_pos(logo, kLogoX, kLogoY);

    make_label(_panel, "Codex", kBrandX, kBrandY, &lv_font_montserrat_24, kSignalWhite);

    _status_label = make_center_label(_panel, "START", kStatusY, &lv_font_montserrat_14, kEtchedGray);
    lv_obj_set_width(_status_label, 180);
    lv_obj_set_style_text_align(_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(_status_label, LV_ALIGN_TOP_MID, 0, kStatusY);

    make_frame_segment(
        _panel,
        kBeaconSpine.x,
        kBeaconSpine.y,
        kBeaconSpine.w,
        kBeaconSpine.h,
        kBeaconViolet
    );

    lv_obj_t* model_caption = make_telemetry_label(
        _panel,
        kTelemetryX,
        kModelCaptionY,
        kTelemetryWidth,
        "MODEL",
        &lv_font_montserrat_14,
        kEtchedGray
    );
    lv_obj_set_style_text_letter_space(model_caption, 3, 0);
    _model_value = make_telemetry_label(
        _panel,
        kTelemetryX,
        kModelValueY,
        kTelemetryWidth,
        "--",
        &lv_font_montserrat_24,
        kSignalWhite
    );

    lv_obj_t* effort_caption = make_telemetry_label(
        _panel,
        kTelemetryX,
        kEffortCaptionY,
        kTelemetryWidth,
        "EFFORT",
        &lv_font_montserrat_14,
        kEtchedGray
    );
    lv_obj_set_style_text_letter_space(effort_caption, 3, 0);
    _effort_value = make_telemetry_label(
        _panel,
        kTelemetryX,
        kEffortValueY,
        kTelemetryWidth,
        "--",
        &lv_font_montserrat_24,
        kSignalWhite
    );

    _pet_image = lv_image_create(_panel);
    lv_image_set_src(_pet_image, codex_pet_idle_frames[0]);
    lv_image_set_antialias(_pet_image, false);
    lv_image_set_scale(_pet_image, kPetScale);
    lv_obj_clear_flag(_pet_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(_pet_image, kPetX, kPetY);

    if (_pet_timer == nullptr) {
        _pet_timer = lv_timer_create(AppCodexStatus::pet_timer_cb, codex_pet_idle_durations_ms[0], this);
    }

    QuotaWidgets five_hour = make_quota_row(_panel, kFiveHourY, "5H");
    _five_hour_pct = five_hour.pct;
    _five_hour_bar = five_hour.bar;
    QuotaWidgets week = make_quota_row(_panel, kWeekY, "7D");
    _week_pct = week.pct;
    _week_bar = week.bar;

    make_line(_panel, kAlertDividerX, kAlertDividerY, kAlertDividerWidth);

    _activity_title = make_center_label(
        _panel, "NO ALERTS", kAlertHeadingY, &lv_font_montserrat_18, kEtchedGray
    );
    _alert_project = make_center_label(
        _panel, "--", kAlertProjectY, &lv_font_montserrat_18, kBeaconViolet
    );
    _alert_detail = make_center_label(
        _panel, "", kAlertDetailY, &lv_font_montserrat_14, kEtchedGray
    );
    lv_obj_set_style_text_font(_alert_project, &font_puhui_14_1, 0);
    lv_obj_set_style_text_font(_alert_detail, &font_puhui_14_1, 0);
    const auto configure_alert_label = [](lv_obj_t* label, int y, int width) {
        lv_obj_set_width(label, width);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    };
    configure_alert_label(_activity_title, kAlertHeadingY, kAlertHeadingWidth);
    configure_alert_label(_alert_project, kAlertProjectY, kAlertProjectWidth);
    configure_alert_label(_alert_detail, kAlertDetailY, kAlertDetailWidth);
    _pending_glow->set_heading(_activity_title);

    _battery_label = lv_label_create(_panel);
    lv_obj_set_style_text_font(_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_battery_label, 150);
    lv_obj_set_style_text_align(_battery_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_battery_label, LV_ALIGN_TOP_MID, 0, kBatteryY);
    update_battery();

}

void AppCodexStatus::destroy_view()
{
    if (_pending_glow) {
        _pending_glow->shutdown();
        _pending_glow.reset();
    }

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
    _activity_title = nullptr;
    _alert_project  = nullptr;
    _alert_detail   = nullptr;
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

    uint32_t color = kEtchedGray;
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
    if (!_wifi_lease.valid()) {
        _wifi_lease = GetSharedWifiService().acquireSta(RuntimeOwner::Codex);
        if (!_wifi_lease.valid()) {
            _wifi_started = false;
            set_status("WIFI BUSY", kCodexRed);
            return;
        }
    }

    auto& wifi = WifiManager::GetInstance();
    _wifi_started = true;

    if (wifi.IsConnected()) {
        _wifi_started = true;
        set_status(_ws_connected ? "LIVE" : "CONNECT", _ws_connected ? kCodexBlue : kTextDim);
        set_host_text(_server_host.c_str());
        return;
    }

    if (_ws_client != nullptr || _ws_connected) {
        stop_websocket();
    }

    if (wifi.IsConfigMode()) {
        set_status("SETUP", kCodexRed);
        set_host_text(wifi.GetApSsid().c_str());
        return;
    }

    set_status("SCAN", kTextDim);
    set_host_text(_server_host.c_str());
}

void AppCodexStatus::stop_network_fallback()
{
    stop_websocket();
    _wifi_lease.reset();
    _wifi_started = false;
}

void AppCodexStatus::start_websocket()
{
    if (_ws_client != nullptr || !_wifi_lease.valid() || !WifiManager::GetInstance().IsConnected()) {
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
        GetHAL().playBootSfx();
        return true;
    }
    if (kind == CodexMessageKind::TimeSync) {
        const bool applied = GetHAL().syncUnixTime(
            snapshot.unix_time_ms,
            snapshot.utc_offset_minutes
        );
        if (!applied) {
            mclog::tagError(getAppInfo().name, "time sync event rejected by HAL");
        }
        return applied;
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

    if (_alert_carousel.apply(snapshot, GetHAL().millis())) {
        render_alert();
    }
    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, live_status);
        lv_obj_set_style_text_color(_status_label, lv_color_hex(kCodexBlue), 0);
    }
    return true;
}

void AppCodexStatus::render_alert()
{
    if (_activity_title == nullptr || _alert_project == nullptr || _alert_detail == nullptr) {
        return;
    }

    const std::string heading = _alert_carousel.heading();
    const CodexTaskAlert* alert = _alert_carousel.current();
    lv_label_set_text(_activity_title, heading.c_str());
    if (alert == nullptr) {
        lv_label_set_text(_alert_project, "--");
        lv_label_set_text(_alert_detail, "");
        lv_obj_set_style_text_color(_activity_title, lv_color_hex(kTextDim), 0);
        sync_pending_glow();
        return;
    }

    const char* state_label = "已完成";
    uint32_t state_color = kCompletedGreen;
    if (alert->state == CodexTaskState::Pending) {
        state_label = "等待指令";
        state_color = kCodexRed;
    } else if (alert->state == CodexTaskState::Running) {
        state_label = "正在处理";
        state_color = kCodexBlue;
    }
    const std::string detail =
        std::string(state_label) + " \xC2\xB7 " + alert->title;
    lv_label_set_text(_alert_project, alert->project.c_str());
    lv_label_set_text(_alert_detail, detail.c_str());
    lv_obj_set_style_text_color(
        _activity_title,
        lv_color_hex(state_color),
        0
    );
    sync_pending_glow();
}

void AppCodexStatus::sync_pending_glow()
{
    if (_pending_glow == nullptr) {
        return;
    }
    const bool active = codex_pending_glow::desired_active(
        _alert_carousel.display_state(),
        _screen_off
    );
    _pending_glow->set_active(active);
}

bool AppCodexStatus::update_screen_off_control(uint32_t now_ms)
{
    const bool left_pressed = GetHAL().btnA.isPressed();
    const bool right_pressed = GetHAL().btnB.isPressed();

    if (_screen_off) {
        if (!_screen_off_wake_armed) {
            if (!left_pressed && !right_pressed) {
                _screen_off_wake_armed = true;
            }
            return false;
        }

        if (left_pressed || right_pressed) {
            wake_screen();
            return true;
        }
        return false;
    }

    if (!left_pressed) {
        _screen_off_left_press_started_ms = 0;
        return false;
    }

    if (right_pressed) {
        _screen_off_left_press_started_ms = 0;
        return false;
    }

    if (_screen_off_left_press_started_ms == 0) {
        _screen_off_left_press_started_ms = now_ms;
        return false;
    }

    if (now_ms - _screen_off_left_press_started_ms >= kScreenOffHoldMs) {
        enter_screen_off();
    }
    return false;
}

void AppCodexStatus::enter_screen_off()
{
    _screen_off_restore_brightness = std::max(10, GetHAL().getBackLightBrightness());
    _screen_off = true;
    {
        LvglLockGuard lock;
        sync_pending_glow();
    }
    _screen_off_wake_armed = false;
    _screen_off_left_press_started_ms = 0;
    GetHAL().setBackLightBrightness(0, false);
}

void AppCodexStatus::wake_screen()
{
    GetHAL().setBackLightBrightness(_screen_off_restore_brightness, false);
    _screen_off = false;
    {
        LvglLockGuard lock;
        sync_pending_glow();
    }
    _screen_off_wake_armed = true;
    _screen_off_left_press_started_ms = 0;
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
