/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_codex_status.h"
#include "codex_status_priority.h"
#include "assets/codex_pet_assets.h"
#include "codex_ble_transport.h"
#include "codex_pending_glow_logic.h"
#include "ble_voice_probe_controller.h"
#include "voice_input_controller.h"
#include "codex_status_ui_layout.h"
#include "codex_status_model.h"
#include "status_message_router.h"
#include "trae_status_model.h"
#include <apps/common/edge_control/global_edge_control_service.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <runtime/display/global_display_protection_service.h>
#include <wifi_manager.h>
#include <esp_websocket_client.h>
#include <mooncake_log.h>
#include <algorithm>
#include <array>
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
constexpr uint32_t kTraeStatusStaleMs    = 15000;
constexpr uint32_t kScreenOffHoldMs      = 2000;
constexpr int kPageWidth                 = 466;
constexpr int kPageCount                 = 2;

bool is_ascii_text(const char* text)
{
    if (text == nullptr) {
        return true;
    }
    const auto* cursor = reinterpret_cast<const unsigned char*>(text);
    while (*cursor != '\0') {
        if (*cursor >= 0x80) {
            return false;
        }
        ++cursor;
    }
    return true;
}

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
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_letter_space(pct, 0, 0);
    lv_obj_set_style_text_color(pct, lv_color_hex(kSignalWhite), 0);
    lv_obj_set_width(pct, kQuotaPercentWidth);
    lv_label_set_long_mode(pct, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(pct, kQuotaPercentX, y - 5);

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

lv_obj_t* make_page_dot(lv_obj_t* parent, int x)
{
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_x(dot, x);
    lv_obj_set_y(dot, kPageIndicatorY);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(kEtchedGray), 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(kSignalWhite), 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}

lv_obj_t* make_fixed_label(
    lv_obj_t* parent,
    const char* text,
    int x,
    int y,
    int width,
    const lv_font_t* font,
    uint32_t color,
    lv_text_align_t align
)
{
    lv_obj_t* label = make_label(parent, text, x, y, font, color);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    return label;
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

void AppCodexStatus::handle_action(
    CodexButtonAction action,
    uint32_t now_ms)
{
    (void)_input_action_controller.enqueue(action, now_ms);
}

void AppCodexStatus::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppCodexStatus::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    const uint32_t now = GetHAL().millis();
    _key_manager     = std::make_unique<input::KeyManager>();
    _last_battery_ms = 0;
    _last_network_ms = 0;
    _last_status_payload_ms = 0;
    _last_trae_status_payload_ms = 0;
    _screen_off_left_press_started_ms = 0;
    _screen_off =
        GetGlobalDisplayProtectionService().snapshot().state ==
        DisplayProtectionState::ScreenOff;
    _trae_stale = false;
    _suppress_task_navigation = false;
    GetHAL().updateButtonStates();
    _input_action_controller.disconnect();
    _voice_button_dispatcher.reset(GetHAL().btnB.isPressed());
    _production_voice_ready = false;
    _production_voice_link_usable = false;
    _production_voice_mtu = 23;
    _production_voice_connection_epoch = 0;
    _voice_status.store(VoiceInputUiState::Off);
    _ws_connected    = false;
    _wifi_lease.reset();
    _wifi_started    = false;
    _wifi_fallback_started = false;
    _ble_was_connected = false;
    _alert_carousel.reset();
    _trae_snapshot = {};
    _trae_carousel.reset();
    _page_swipe.cancel();
    _trae_card_tap.cancel();
    _current_page = 0;
    _last_page_touch_state = LV_INDEV_STATE_REL;
    _server_host     = Settings("codex_status").GetString("host", kDefaultServerHost);
    _transport_mode  = Settings("codex_status").GetString("transport", "auto");
    _ble_name        = Settings("codex_status").GetString("ble_name", kDefaultBleName);
    if (_transport_mode != "auto" && _transport_mode != "ble" && _transport_mode != "wifi") {
        _transport_mode = "auto";
    }
    _ble_wait_started_ms = now;

    {
        LvglLockGuard lock;
        create_view();
    }

    if (_transport_mode != "wifi") {
        _ble_transport = std::make_unique<CodexBleTransport>();
        if (_ble_transport->start(_ble_name)) {
            _voice_probe = std::make_unique<BleVoiceProbeController>();
            if (!_voice_probe->initialize()) {
                _voice_probe.reset();
            }
            _voice_input = std::make_unique<VoiceInputController>();
            (void)_voice_input->initialize();
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
    GetHAL().updateButtonStates();
    update_screen_off_control(now);
    const bool woke_screen =
        _screen_off &&
        GetGlobalDisplayProtectionService().snapshot().state !=
            DisplayProtectionState::ScreenOff;
    const bool left_pressed = GetHAL().btnA.isPressed();
    const bool right_pressed = GetHAL().btnB.isPressed();
    input::KeyEvent key_event = input::KeyEvent::None;
    if (_key_manager) {
        key_event = _key_manager->update(false);
    }
    if (key_event == input::KeyEvent::GoNext) {
        key_event = input::KeyEvent::None;
    }

    if (woke_screen) {
        _suppress_task_navigation = true;
        _input_action_controller.clear_pending();
    }
    if (_suppress_task_navigation) {
        key_event = input::KeyEvent::None;
        if (GetHAL().btnA.isReleased() && GetHAL().btnB.isReleased()) {
            _suppress_task_navigation = false;
        }
    }
    const VoiceButtonDispatchDecision voice_decision = _voice_button_dispatcher.dispatch(
        left_pressed,
        right_pressed,
        woke_screen,
        key_event == input::KeyEvent::GoHome,
        now,
        _voice_input.get(),
        this,
        GetHAL().buttonInputSuppressed());
    if (voice_decision == VoiceButtonDispatchDecision::Close) {
        _input_action_controller.disconnect();
        _voice_button_dispatcher.reset(right_pressed);
        close();
        return;
    }

    {
        LvglLockGuard lock;
        sync_display_protection_presentation();
        update_page_swipe(now);
        bool alert_changed = false;
        if (_current_page == 0 && key_event == input::KeyEvent::GoPrevious) {
            alert_changed = _alert_carousel.step(-1, now);
        } else {
            alert_changed = _alert_carousel.update(now);
        }
        if (alert_changed) {
            render_alert();
        }
        if (_trae_carousel.update(now)) {
            render_trae_status();
        }
        if (_last_trae_status_payload_ms != 0 &&
            now - _last_trae_status_payload_ms > kTraeStatusStaleMs) {
            _last_trae_status_payload_ms = 0;
            _trae_stale = true;
            render_trae_status();
        }
    }
    bool ble_connected = _ble_transport && _ble_transport->connected();
    if (_ble_transport) {
        _ble_transport->poll(now);
        ble_connected = _ble_transport && _ble_transport->connected();
        const uint32_t connection_epoch =
            _ble_transport->connection_epoch();
        if (connection_epoch != _production_voice_connection_epoch) {
            _production_voice_connection_epoch = connection_epoch;
            _production_voice_ready = false;
            _production_voice_link_usable = false;
            _production_voice_mtu = 23;
            _input_action_controller.disconnect();
            _voice_button_dispatcher.reset(right_pressed);
            if (_voice_input) {
                _voice_input->handle_connection_epoch(connection_epoch);
            }
        }

        CodexInputActionControl input_control = {};
        while (_ble_transport->take_input_action_control(input_control)) {
            _input_action_controller.set_ready(input_control.control);
        }
        input_action::WireAction input_action = {};
        while (_input_action_controller.take(input_action)) {
            const CodexBleNotifyResult result =
                _ble_transport->notify_input_action(
                    input_action.bytes.data(), input_action.bytes.size());
            _input_action_controller.record_result(
                result == CodexBleNotifyResult::Accepted);
        }

        if (_voice_input) {
            CodexProductionVoiceControl production_control;
            while (_ble_transport->take_production_voice_control(production_control)) {
                _production_voice_ready =
                    production_control.control.kind == voice_input::ControlKind::Ready;
            }
            const uint16_t voice_mtu = _ble_transport->negotiated_mtu();
            const bool production_link_usable =
                ble_connected && _production_voice_ready &&
                _ble_transport->voice_subscribed() &&
                _ble_transport->voice_notification_limit() >=
                    voice_input::kAudioCharacteristicValueSize;
            if (production_link_usable != _production_voice_link_usable ||
                voice_mtu != _production_voice_mtu) {
                _production_voice_link_usable = production_link_usable;
                _production_voice_mtu = voice_mtu;
                _voice_input->set_ready(production_link_usable, voice_mtu);
            }
            _voice_input->poll(now);
            voice_input::Notification notification;
            while (_voice_input->take_notification(notification)) {
                const CodexBleNotifyResult result = _ble_transport->notify_voice(
                    notification.bytes.data(), notification.size);
                VoiceNotificationSendResult send_result =
                    VoiceNotificationSendResult::PermanentFailure;
                if (result == CodexBleNotifyResult::Accepted) {
                    send_result = VoiceNotificationSendResult::Accepted;
                } else if (result == CodexBleNotifyResult::Congested) {
                    send_result = VoiceNotificationSendResult::Congested;
                }
                _voice_input->record_notification_result(
                    notification,
                    send_result,
                    now);
                if (result != CodexBleNotifyResult::Accepted) {
                    break;
                }
            }
        }
        if (_voice_probe) {
            CodexBleVoiceControl control;
            while (_ble_transport->take_voice_control(control)) {
                _voice_probe->handle_control(
                    control,
                    _ble_transport->voice_subscribed(),
                    _ble_transport->negotiated_mtu(),
                    now);
            }
            _voice_probe->poll(now);
            ble_voice_probe::Notification notification;
            while (_voice_probe->take_notification(notification)) {
                std::array<uint8_t, ble_voice_probe::kMaxNotificationSize> bytes = {};
                const size_t length = ble_voice_probe::encode_notification(
                    notification, bytes.data(), bytes.size());
                _voice_probe->record_notify_result(
                    notification.kind,
                    _ble_transport->notify_voice(bytes.data(), length),
                    now);
            }
        }
        CodexBleMessage message;
        while (_ble_transport->take_message(message)) {
            handle_status_json(
                message.payload.data(),
                static_cast<int>(message.payload.size()),
                "BLE LIVE"
            );
        }

        if (ble_connected && !_ble_was_connected) {
            stop_network_fallback();
            _ble_was_connected = true;
            set_status("BLE WAIT", kTextDim);
            set_host_text(_ble_name.c_str());
            LvglLockGuard lock;
            render_trae_status();
        } else if (!ble_connected && _ble_was_connected) {
            _production_voice_ready = false;
            _input_action_controller.disconnect();
            _voice_button_dispatcher.reset(right_pressed);
            if (_voice_input) {
                _voice_input->handle_disconnect();
            }
            if (_voice_probe) {
                _voice_probe->handle_disconnect();
            }
            _ble_was_connected = false;
            _ble_wait_started_ms = now;
            set_status("BLE PAIR", kTextDim);
            set_host_text(_ble_name.c_str());
            LvglLockGuard lock;
            render_trae_status();
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

    _voice_status.store(
        _voice_input ? _voice_input->ui_state() : VoiceInputUiState::Off);
    render_status();
}

void AppCodexStatus::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();
    _voice_button_dispatcher.reset(false);
    _input_action_controller.disconnect();
    stop_network_fallback();
    if (_voice_input) {
        _voice_status.store(VoiceInputUiState::Off);
        _voice_input->shutdown();
        _voice_input.reset();
    }
    if (_voice_probe) {
        _voice_probe->shutdown();
        _voice_probe.reset();
    }
    if (_ble_transport) {
        _ble_transport->stop();
        _ble_transport.reset();
    }

    LvglLockGuard lock;
    destroy_view();
    _alert_carousel.reset();
    _trae_carousel.reset();
    _trae_snapshot = {};
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
    lv_obj_remove_flag(_panel, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    _page_strip = lv_obj_create(_panel);
    lv_obj_set_size(_page_strip, 932, 466);
    lv_obj_set_pos(_page_strip, 0, 0);
    lv_obj_remove_flag(_page_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_page_strip, 0, 0);
    lv_obj_set_style_border_width(_page_strip, 0, 0);
    lv_obj_set_style_pad_all(_page_strip, 0, 0);
    lv_obj_set_style_bg_color(_page_strip, lv_color_hex(kInstrumentBlack), 0);

    _codex_page = lv_obj_create(_page_strip);
    lv_obj_set_size(_codex_page, 466, 466);
    lv_obj_set_pos(_codex_page, 0, 0);
    lv_obj_remove_flag(_codex_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_codex_page, 0, 0);
    lv_obj_set_style_border_width(_codex_page, 0, 0);
    lv_obj_set_style_pad_all(_codex_page, 0, 0);
    lv_obj_set_style_bg_color(_codex_page, lv_color_hex(kInstrumentBlack), 0);

    _trae_page_root = lv_obj_create(_page_strip);
    lv_obj_set_size(_trae_page_root, 466, 466);
    lv_obj_set_pos(_trae_page_root, 466, 0);
    lv_obj_remove_flag(_trae_page_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_trae_page_root, 0, 0);
    lv_obj_set_style_border_width(_trae_page_root, 0, 0);
    lv_obj_set_style_pad_all(_trae_page_root, 0, 0);
    lv_obj_set_style_bg_color(_trae_page_root, lv_color_hex(kInstrumentBlack), 0);

    _pending_glow = std::make_unique<CodexPendingSoftGlow>();
    _pending_glow->init(_codex_page);

    lv_obj_t* logo = lv_image_create(_codex_page);
    lv_image_set_src(logo, &logo_codex);
    lv_image_set_scale(logo, kLogoScale);
    lv_obj_set_pos(logo, kLogoX, kLogoY);

    make_label(_codex_page, "Codex", kBrandX, kBrandY, &MontserratSemiBold26, kSignalWhite);

    _status_label = make_center_label(_codex_page, "START", kStatusY, &lv_font_montserrat_14, kEtchedGray);
    lv_obj_set_width(_status_label, 180);
    lv_obj_set_style_text_align(_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_status_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(_status_label, LV_ALIGN_TOP_MID, 0, kStatusY);

    make_frame_segment(
        _codex_page,
        kBeaconSpine.x,
        kBeaconSpine.y,
        kBeaconSpine.w,
        kBeaconSpine.h,
        kBeaconViolet
    );

    lv_obj_t* model_caption = make_telemetry_label(
        _codex_page,
        kTelemetryX,
        kModelCaptionY,
        kTelemetryWidth,
        "MODEL",
        &lv_font_montserrat_14,
        kEtchedGray
    );
    lv_obj_set_style_text_letter_space(model_caption, 3, 0);
    _model_value = make_telemetry_label(
        _codex_page,
        kTelemetryX,
        kModelValueY,
        kTelemetryWidth,
        "--",
        &MontserratSemiBold26,
        kSignalWhite
    );

    lv_obj_t* effort_caption = make_telemetry_label(
        _codex_page,
        kTelemetryX,
        kEffortCaptionY,
        kTelemetryWidth,
        "EFFORT",
        &lv_font_montserrat_14,
        kEtchedGray
    );
    lv_obj_set_style_text_letter_space(effort_caption, 3, 0);
    _effort_value = make_telemetry_label(
        _codex_page,
        kTelemetryX,
        kEffortValueY,
        kTelemetryWidth,
        "--",
        &MontserratSemiBold26,
        kSignalWhite
    );

    _pet_image = lv_image_create(_codex_page);
    lv_image_set_src(_pet_image, codex_pet_idle_frames[0]);
    lv_image_set_antialias(_pet_image, false);
    lv_image_set_scale(_pet_image, kPetScale);
    lv_obj_clear_flag(_pet_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(_pet_image, kPetX, kPetY);

    if (_pet_timer == nullptr) {
        _pet_timer = lv_timer_create(AppCodexStatus::pet_timer_cb, codex_pet_idle_durations_ms[0], this);
    }

    QuotaWidgets week = make_quota_row(_codex_page, kWeekY, "7D");
    _week_pct = week.pct;
    _week_bar = week.bar;

    make_line(_codex_page, kAlertDividerX, kAlertDividerY, kAlertDividerWidth);

    _pending_label = make_fixed_label(
        _codex_page,
        "PENDING",
        kPendingLabelX,
        kStateLabelY,
        kStateLabelWidth,
        &MontserratBold16,
        kAlertVermilion,
        LV_TEXT_ALIGN_CENTER
    );
    _pending_count = make_fixed_label(
        _codex_page,
        "0",
        kPendingCountX,
        kStateCountY,
        kStateCountWidth,
        &MontserratSemiBold26,
        kEtchedGray,
        LV_TEXT_ALIGN_CENTER
    );
    _running_label = make_fixed_label(
        _codex_page,
        "RUNNING",
        kRunningLabelX,
        kStateLabelY,
        kStateLabelWidth,
        &MontserratBold16,
        kRunningCyan,
        LV_TEXT_ALIGN_CENTER
    );
    _running_count = make_fixed_label(
        _codex_page,
        "0",
        kRunningCountX,
        kStateCountY,
        kStateCountWidth,
        &MontserratSemiBold26,
        kEtchedGray,
        LV_TEXT_ALIGN_CENTER
    );

    _task_index = make_fixed_label(
        _codex_page,
        "NO ALERTS",
        kTaskIndexX,
        kTaskIndexY,
        kTaskIndexWidth,
        &lv_font_montserrat_14,
        kEtchedGray,
        LV_TEXT_ALIGN_CENTER
    );
    _alert_project = make_fixed_label(
        _codex_page,
        "--",
        kAlertProjectX,
        kAlertProjectY,
        kAlertProjectWidth,
        &lv_font_montserrat_22,
        kBeaconViolet,
        LV_TEXT_ALIGN_CENTER
    );
    lv_label_set_long_mode(_alert_project, LV_LABEL_LONG_MODE_DOTS);
    _alert_detail = make_fixed_label(
        _codex_page,
        "",
        kAlertDetailX,
        kAlertDetailY,
        kAlertDetailWidth,
        &font_puhui_14_1,
        kSignalWhite,
        LV_TEXT_ALIGN_CENTER
    );
    lv_label_set_long_mode(_alert_detail, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(_alert_detail, kAlertDetailHeight);
    _pending_glow->set_status_targets(
        _pending_label,
        _pending_count,
        _running_label,
        _running_count
    );

    _trae_status_page = std::make_unique<TraeStatusPage>();
    _trae_status_page->init(_trae_page_root);
    render_trae_status();
    if (_trae_status_page != nullptr) {
        _trae_status_page->set_active(_current_page == 1 && !_screen_off);
    }

    _page_dot_codex = make_page_dot(_panel, 223);
    _page_dot_trae = make_page_dot(_panel, 237);
    update_page_indicator();

    _battery_label = lv_label_create(_panel);
    lv_obj_set_style_text_font(_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_battery_label, 150);
    lv_obj_set_style_text_align(_battery_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_battery_label, LV_ALIGN_TOP_MID, 0, kBatteryY);
    update_battery();
}

void AppCodexStatus::destroy_view()
{
    lv_anim_delete(this, page_animation_cb);
    _page_swipe.cancel();
    _trae_card_tap.cancel();
    _last_page_touch_state = LV_INDEV_STATE_REL;

    if (_pending_glow) {
        _pending_glow->shutdown();
        _pending_glow.reset();
    }
    if (_trae_status_page) {
        _trae_status_page->shutdown();
        _trae_status_page.reset();
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
    _page_strip     = nullptr;
    _codex_page     = nullptr;
    _trae_page_root = nullptr;
    _page_dot_codex = nullptr;
    _page_dot_trae  = nullptr;
    _effort_value   = nullptr;
    _week_pct       = nullptr;
    _week_bar       = nullptr;
    _pending_label  = nullptr;
    _pending_count  = nullptr;
    _running_label  = nullptr;
    _running_count  = nullptr;
    _task_index     = nullptr;
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
    {
        std::lock_guard<std::mutex> lock(_status_mutex);
        _base_status = status == nullptr ? "" : status;
        _base_status_color = color;
        _base_status_is_error = color == kCodexRed;
    }
    render_status();
}

void AppCodexStatus::render_status()
{
    std::string base_status;
    uint32_t base_color = kTextDim;
    bool base_is_error = false;
    {
        std::lock_guard<std::mutex> lock(_status_mutex);
        base_status = _base_status;
        base_color = _base_status_color;
        base_is_error = _base_status_is_error;
    }
    const VoiceInputUiState voice_state = _voice_status.load();
    const CodexStatusPresentation presentation = resolve_codex_status(
        base_status.c_str(),
        base_color,
        base_is_error,
        voice_state == VoiceInputUiState::Active,
        voice_state == VoiceInputUiState::Error,
        kCodexBlue,
        kCodexRed,
        kTextDim);
    LvglLockGuard lock;
    if (_status_label != nullptr) {
        lv_label_set_text(_status_label, presentation.text);
        lv_obj_set_style_text_color(
            _status_label, lv_color_hex(presentation.color), 0);
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
    const StatusMessageSource source = route_status_message(
        data,
        static_cast<size_t>(length)
    );
    if (source == StatusMessageSource::Unknown) {
        return false;
    }
    if (source == StatusMessageSource::Trae) {
        TraeStatusSnapshot snapshot;
        const uint32_t now = GetHAL().millis();
        if (!parse_trae_status_message(
                data,
                static_cast<size_t>(length),
                now,
                snapshot
            )) {
            return false;
        }

        LvglLockGuard lock;
        _trae_snapshot = snapshot;
        _last_trae_status_payload_ms = now;
        _trae_stale = false;
        _trae_carousel.apply(snapshot, now);
        render_trae_status();
        return true;
    }

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

    {
        LvglLockGuard lock;
        if (_model_value != nullptr && model[0] != '\0') {
            lv_label_set_text(_model_value, model);
        }
        if (_effort_value != nullptr && effort[0] != '\0') {
            lv_label_set_text(_effort_value, effort);
        }
        set_percent(_week_pct, _week_bar, snapshot.week_left_pct);

        if (_alert_carousel.apply(snapshot, GetHAL().millis())) {
            render_alert();
        }
    }
    _last_status_payload_ms = GetHAL().millis();
    set_status(live_status, kCodexBlue);
    return true;
}

void AppCodexStatus::render_alert()
{
    if (_pending_count == nullptr || _running_count == nullptr ||
        _task_index == nullptr || _alert_project == nullptr ||
        _alert_detail == nullptr) {
        return;
    }

    char pending_text[12];
    char running_text[12];
    std::snprintf(
        pending_text,
        sizeof(pending_text),
        "%d",
        std::max(_alert_carousel.pending_count(), 0)
    );
    std::snprintf(
        running_text,
        sizeof(running_text),
        "%d",
        std::max(_alert_carousel.running_count(), 0)
    );
    lv_label_set_text(_pending_count, pending_text);
    lv_label_set_text(_running_count, running_text);
    lv_obj_set_style_text_color(
        _pending_count,
        lv_color_hex(
            _alert_carousel.pending_count() > 0 ? kAlertVermilion : kEtchedGray
        ),
        0
    );
    lv_obj_set_style_text_color(
        _running_count,
        lv_color_hex(
            _alert_carousel.running_count() > 0 ? kRunningCyan : kEtchedGray
        ),
        0
    );

    const std::string heading = _alert_carousel.heading();
    const CodexTaskAlert* alert = _alert_carousel.current();
    lv_label_set_text(_task_index, heading.c_str());
    if (alert == nullptr) {
        lv_label_set_text(_alert_project, "--");
        lv_obj_set_style_text_font(_alert_project, &lv_font_montserrat_22, 0);
        lv_label_set_text(_alert_detail, "");
        lv_obj_set_style_text_color(_task_index, lv_color_hex(kTextDim), 0);
        sync_pending_glow();
        return;
    }

    uint32_t state_color = kCompletedGreen;
    if (alert->state == CodexTaskState::Pending) {
        state_color = kCodexRed;
    } else if (alert->state == CodexTaskState::Running) {
        state_color = kRunningCyan;
    }
    lv_label_set_text(_alert_project, alert->project.c_str());
    lv_obj_set_style_text_font(
        _alert_project,
        is_ascii_text(alert->project.c_str())
            ? &lv_font_montserrat_22
            : &font_puhui_14_1,
        0
    );
    lv_label_set_text(_alert_detail, alert->title.c_str());
    lv_obj_set_style_text_color(
        _task_index,
        lv_color_hex(state_color),
        0
    );
    sync_pending_glow();
}

void AppCodexStatus::render_trae_status()
{
    if (_trae_status_page == nullptr) {
        return;
    }
    const bool bluetooth_connected =
        _ble_transport && _ble_transport->connected();
    _trae_status_page->render(
        _trae_snapshot,
        _trae_carousel.current(),
        _trae_carousel.next(),
        _trae_stale,
        bluetooth_connected
    );
}

void AppCodexStatus::update_page_swipe(uint32_t now_ms)
{
    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr) {
        _page_swipe.cancel();
        _trae_card_tap.cancel();
        _last_page_touch_state = LV_INDEV_STATE_REL;
        return;
    }

    if (GetGlobalDisplayProtectionService().touchInputSuppressed()) {
        _page_swipe.cancel();
        _trae_card_tap.cancel();
        _last_page_touch_state = LV_INDEV_STATE_REL;
        return;
    }
    const lv_indev_state_t state = lv_indev_get_state(indev);

    auto& controls = GetGlobalEdgeControlService();
    if (_screen_off ||
        controls.ownsTouch() || controls.lastReleaseConsumed()) {
        _page_swipe.cancel();
        _trae_card_tap.cancel();
        _last_page_touch_state = LV_INDEV_STATE_REL;
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (state == LV_INDEV_STATE_PR) {
        if (_last_page_touch_state == LV_INDEV_STATE_REL) {
            _page_swipe.press(point.x, point.y, now_ms);
            const bool task_candidate =
                _current_page == 1 &&
                _trae_status_page != nullptr &&
                _trae_status_page->contains_task_point(point.x, point.y);
            _trae_card_tap.press(
                point.x,
                point.y,
                now_ms,
                task_candidate
            );
        } else {
            _page_swipe.move(point.x, point.y);
            _trae_card_tap.move(point.x, point.y);
        }
    } else if (_last_page_touch_state == LV_INDEV_STATE_PR) {
        const StatusPageSwipeResult result =
            _page_swipe.release(now_ms, _current_page);
        if (result == StatusPageSwipeResult::Codex) {
            set_page(0, true);
        } else if (result == StatusPageSwipeResult::Trae) {
            set_page(1, true);
        } else if (_trae_card_tap.release(now_ms) &&
                   _trae_carousel.manual_next(now_ms)) {
            render_trae_status();
        }
    }
    _last_page_touch_state = state;
}

void AppCodexStatus::set_page(int page, bool animate)
{
    if (_page_strip == nullptr) {
        return;
    }
    page = std::clamp(page, 0, kPageCount - 1);
    const int target_x = -page * kPageWidth;
    const int current_x = lv_obj_get_x(_page_strip);
    _page_swipe.cancel();
    _trae_card_tap.cancel();
    _last_page_touch_state = LV_INDEV_STATE_REL;
    _current_page = page;
    update_page_indicator();
    if (_trae_status_page != nullptr) {
        _trae_status_page->set_active(_current_page == 1 && !_screen_off);
    }

    lv_anim_delete(this, page_animation_cb);
    if (!animate || current_x == target_x) {
        lv_obj_set_x(_page_strip, target_x);
        return;
    }

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, this);
    lv_anim_set_exec_cb(&animation, page_animation_cb);
    lv_anim_set_values(&animation, current_x, target_x);
    lv_anim_set_duration(&animation, 140);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

void AppCodexStatus::update_page_indicator()
{
    if (_page_dot_codex == nullptr || _page_dot_trae == nullptr) {
        return;
    }
    const bool codex_active = _current_page == 0;
    lv_obj_set_style_bg_opa(
        _page_dot_codex,
        codex_active ? LV_OPA_COVER : LV_OPA_TRANSP,
        0
    );
    lv_obj_set_style_bg_opa(
        _page_dot_trae,
        codex_active ? LV_OPA_TRANSP : LV_OPA_COVER,
        0
    );
}

void AppCodexStatus::page_animation_cb(void* target, int32_t x)
{
    auto* self = static_cast<AppCodexStatus*>(target);
    if (self != nullptr && self->_page_strip != nullptr) {
        lv_obj_set_x(self->_page_strip, x);
    }
}

void AppCodexStatus::sync_pending_glow()
{
    if (_pending_glow == nullptr) {
        return;
    }
    const bool screen_off =
        GetGlobalDisplayProtectionService().snapshot().state ==
        DisplayProtectionState::ScreenOff;
    _pending_glow->set_state(_alert_carousel.display_state(), screen_off);
}

void AppCodexStatus::sync_display_protection_presentation()
{
    const DisplayProtectionSnapshot protection =
        GetGlobalDisplayProtectionService().snapshot();
    const bool screen_off =
        protection.state == DisplayProtectionState::ScreenOff;
    const bool woke_screen = _screen_off && !screen_off;
    if (_screen_off == screen_off) {
        return;
    }

    _screen_off = screen_off;
    if (woke_screen) {
        _suppress_task_navigation = true;
        _screen_off_left_press_started_ms = 0;
    }
    _page_swipe.cancel();
    _trae_card_tap.cancel();
    _last_page_touch_state = LV_INDEV_STATE_REL;
    sync_pending_glow();
    if (_trae_status_page != nullptr) {
        _trae_status_page->set_active(_current_page == 1 && !_screen_off);
    }
}

void AppCodexStatus::update_screen_off_control(uint32_t now_ms)
{
    const bool left_pressed = GetHAL().btnA.isPressed();
    const bool right_pressed = GetHAL().btnB.isPressed();

    if (!left_pressed && !right_pressed) {
        _screen_off_left_press_started_ms = 0;
        return;
    }

    if (_suppress_task_navigation) {
        return;
    }

    if (!left_pressed) {
        _screen_off_left_press_started_ms = 0;
        return;
    }

    if (right_pressed) {
        _screen_off_left_press_started_ms = 0;
        return;
    }

    if (_screen_off_left_press_started_ms == 0) {
        _screen_off_left_press_started_ms = now_ms;
        return;
    }

    if (now_ms - _screen_off_left_press_started_ms >= kScreenOffHoldMs) {
        GetGlobalDisplayProtectionService().screenOffNow();
        _suppress_task_navigation = true;
        _screen_off_left_press_started_ms = 0;
    }
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
