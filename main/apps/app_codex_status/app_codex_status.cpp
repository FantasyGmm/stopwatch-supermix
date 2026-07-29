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
#include <esp_random.h>
#include <mbedtls/sha256.h>
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
constexpr int kAgentPanelHiddenY         = -430;
constexpr int kAgentPanelVisibleY        = 0;
constexpr int kAgentPanelRowHeight       = 40;

bool fill_agent_entropy(void*, uint8_t* output, size_t size)
{
    if (output == nullptr || size == 0) {
        return false;
    }
    esp_fill_random(output, size);
    return std::any_of(output, output + size, [](uint8_t byte) {
        return byte != 0;
    });
}

bool agent_fingerprint(
    const secure_pairing::IdentityPublicKey& public_key,
    secure_pairing::AgentFingerprint& output)
{
    std::array<uint8_t, 32> digest = {};
    if (mbedtls_sha256(
            public_key.data(),
            public_key.size(),
            digest.data(),
            0) != 0) {
        return false;
    }
    std::copy_n(digest.begin(), output.size(), output.begin());
    return true;
}

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
    _agent_panel_model = {};
    _agent_panel_dirty = false;
    _agent_switch_pending = false;
    _agent_switch_target = {};
    clear_pairing_runtime();
    {
        std::lock_guard<std::mutex> decision_lock(_agent_decision_mutex);
        _pending_agent_decision = {};
    }
    _current_page = 0;
    _last_page_touch_state = LV_INDEV_STATE_REL;
    _last_agent_touch_state = LV_INDEV_STATE_REL;
    _server_host     = Settings("codex_status").GetString("host", kDefaultServerHost);
    _transport_mode  = Settings("codex_status").GetString("transport", "auto");
    _ble_name        = Settings("codex_status").GetString("ble_name", kDefaultBleName);
    if (_transport_mode != "auto" && _transport_mode != "ble" && _transport_mode != "wifi") {
        _transport_mode = "auto";
    }
    _ble_wait_started_ms = now;

    _agent_registry_ready = false;
    _agent_registry_backend =
        std::make_unique<secure_pairing::NvsRegistryStorageBackend>();
    if (_agent_registry_backend->ready()) {
        _agent_registry_store =
            std::make_unique<secure_pairing::AgentRegistryStore>(
                *_agent_registry_backend);
        const secure_pairing::StoreResult loaded =
            _agent_registry_store->load(_agent_registry);
        if (loaded == secure_pairing::StoreResult::Ok) {
            _agent_registry_ready = true;
        } else if (loaded == secure_pairing::StoreResult::NotFound &&
                   secure_pairing::AgentRegistry::initialize(
                       fill_agent_entropy,
                       nullptr,
                       _agent_registry) ==
                       secure_pairing::RegistryResult::Ok &&
                   _agent_registry_store->save(_agent_registry) ==
                       secure_pairing::StoreResult::Ok) {
            _agent_registry_ready = true;
        }
    }
    sync_agent_panel_registry();

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
            if (_agent_registry_ready) {
                (void)_ble_transport->set_voice_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
                (void)_voice_input->set_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
            }
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

    if (woke_screen) {
        _suppress_task_navigation = true;
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
        _voice_input.get());
    if (voice_decision == VoiceButtonDispatchDecision::Close) {
        close();
        return;
    }

    {
        LvglLockGuard lock;
        sync_display_protection_presentation();
        if (_agent_panel_dirty) {
            render_agent_panel();
            _agent_panel_dirty = false;
        }
        update_agent_panel_touch(now);
        if (_agent_panel_model.owns_touch()) {
            _page_swipe.cancel();
            _trae_card_tap.cancel();
            _last_page_touch_state = LV_INDEV_STATE_REL;
        } else {
            update_page_swipe(now);
        }
        bool alert_changed = false;
        if (_current_page == 0 && key_event == input::KeyEvent::GoPrevious) {
            alert_changed = _alert_carousel.step(-1, now);
        } else if (_current_page == 0 && key_event == input::KeyEvent::GoNext) {
            alert_changed = _alert_carousel.step(1, now);
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
    AgentPanelDecision agent_decision = {};
    while (take_agent_panel_decision(agent_decision)) {
        dispatch_agent_panel_decision(agent_decision, now);
    }
    complete_agent_switch_if_ready();

    bool ble_connected = _ble_transport && _ble_transport->connected();
    if (_ble_transport) {
        process_secure_pairing(now);
        _ble_transport->poll(now);
        ble_connected = _ble_transport && _ble_transport->connected();
        if (_voice_input) {
            const uint32_t connection_epoch =
                _ble_transport->connection_epoch();
            if (connection_epoch != _production_voice_connection_epoch) {
                _production_voice_connection_epoch = connection_epoch;
                _production_voice_ready = false;
                _production_voice_link_usable = false;
                _production_voice_mtu = 23;
                _voice_input->handle_connection_epoch(connection_epoch);
                sync_agent_panel_registry();
                _agent_panel_dirty = true;
            }
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
    _agent_registry_store.reset();
    _agent_registry_backend.reset();
    _agent_registry_ready = false;
    _agent_switch_pending = false;
    _agent_switch_target = {};
    clear_pairing_runtime();

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

    _agent_panel_root = lv_obj_create(_panel);
    lv_obj_set_size(_agent_panel_root, 466, 466);
    lv_obj_set_pos(
        _agent_panel_root,
        0,
        _agent_panel_model.snapshot().visible
            ? kAgentPanelVisibleY
            : kAgentPanelHiddenY);
    lv_obj_set_style_radius(_agent_panel_root, 0, 0);
    lv_obj_set_style_border_width(_agent_panel_root, 0, 0);
    lv_obj_set_style_pad_all(_agent_panel_root, 0, 0);
    lv_obj_set_style_bg_color(
        _agent_panel_root, lv_color_hex(kPanelGraphite), 0);
    lv_obj_set_style_bg_opa(_agent_panel_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_agent_panel_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(_agent_panel_root);
    render_agent_panel();
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
    _agent_panel_root = nullptr;
    _agent_row_buttons = {};
    _agent_delete_buttons = {};
    _agent_allow_button = nullptr;
    _agent_allow_switch_button = nullptr;
    _agent_reject_button = nullptr;
    _agent_reset_button = nullptr;
    _agent_confirm_button = nullptr;
    _agent_cancel_button = nullptr;
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

void AppCodexStatus::render_agent_panel()
{
    if (_agent_panel_root == nullptr) {
        return;
    }
    const AgentPanelSnapshot snapshot = _agent_panel_model.snapshot();
    lv_obj_set_y(
        _agent_panel_root,
        snapshot.visible ? kAgentPanelVisibleY : kAgentPanelHiddenY);
    lv_obj_clean(_agent_panel_root);
    _agent_row_buttons = {};
    _agent_delete_buttons = {};
    _agent_allow_button = nullptr;
    _agent_allow_switch_button = nullptr;
    _agent_reject_button = nullptr;
    _agent_reset_button = nullptr;
    _agent_confirm_button = nullptr;
    _agent_cancel_button = nullptr;

    make_center_label(
        _agent_panel_root,
        "AGENTS",
        10,
        &lv_font_montserrat_18,
        kSignalWhite);

    auto make_button = [this](
                           const char* text,
                           int x,
                           int y,
                           int width,
                           int height,
                           uint32_t color) {
        lv_obj_t* button = lv_button_create(_agent_panel_root);
        lv_obj_set_size(button, width, height);
        lv_obj_set_pos(button, x, y);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_t* label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(
            label, lv_color_hex(kSignalWhite), 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(
            button,
            AppCodexStatus::agent_panel_event_cb,
            LV_EVENT_CLICKED,
            this);
        return button;
    };

    if (snapshot.confirmation != AgentPanelConfirmation::None) {
        const char* title =
            snapshot.confirmation == AgentPanelConfirmation::DeleteAgent
                ? "Delete Agent?"
                : "Reset All Agents?";
        make_center_label(
            _agent_panel_root,
            title,
            150,
            &lv_font_montserrat_22,
            kSignalWhite);
        _agent_confirm_button = make_button(
            "Confirm", 62, 230, 160, 54, kAlertVermilion);
        _agent_cancel_button = make_button(
            "Cancel", 244, 230, 160, 54, kEtchedGray);
    } else if (snapshot.request.present) {
        make_center_label(
            _agent_panel_root,
            snapshot.request.display_name.data(),
            75,
            &lv_font_montserrat_18,
            kSignalWhite);
        make_center_label(
            _agent_panel_root,
            snapshot.request.verification_code.data(),
            120,
            &lv_font_montserrat_28,
            kBeaconViolet);
        _agent_allow_button = make_button(
            "Allow", 30, 200, 126, 52, kHandledGreen);
        _agent_allow_switch_button = make_button(
            "Allow + Switch", 170, 200, 190, 52, kBeaconViolet);
        _agent_reject_button = make_button(
            "Reject", 374, 200, 76, 52, kAlertVermilion);
    } else {
        for (size_t index = 0; index < snapshot.count; ++index) {
            const AgentPanelRow& row = snapshot.rows[index];
            const int y =
                46 + static_cast<int>(index) * kAgentPanelRowHeight;
            uint32_t row_color = 0x303238;
            const char* state = "Offline";
            uint32_t state_color = kEtchedGray;
            if (row.state == AgentPanelRowState::Connected) {
                state = "Connected";
                state_color = kHandledGreen;
                row_color = 0x26382F;
            } else if (row.state == AgentPanelRowState::Requesting) {
                state = "Requesting";
                state_color = kBeaconViolet;
                row_color = 0x302B43;
            }
            lv_obj_t* row_button = lv_button_create(_agent_panel_root);
            _agent_row_buttons[index] = row_button;
            lv_obj_set_size(row_button, 382, 34);
            lv_obj_set_pos(row_button, 12, y);
            lv_obj_set_style_radius(row_button, 6, 0);
            lv_obj_set_style_border_width(row_button, 0, 0);
            lv_obj_set_style_bg_color(
                row_button, lv_color_hex(row_color), 0);
            lv_obj_add_event_cb(
                row_button,
                AppCodexStatus::agent_panel_event_cb,
                LV_EVENT_CLICKED,
                this);
            lv_obj_t* name = make_label(
                row_button,
                row.display_name.data(),
                8,
                4,
                &lv_font_montserrat_14,
                kSignalWhite);
            lv_obj_set_width(name, 225);
            lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_t* state_label = make_label(
                row_button,
                state,
                242,
                4,
                &lv_font_montserrat_14,
                state_color);
            lv_obj_set_width(state_label, 120);
            lv_obj_set_style_text_align(
                state_label, LV_TEXT_ALIGN_RIGHT, 0);
            _agent_delete_buttons[index] = make_button(
                "×", 402, y, 50, 34, kAlertVermilion);
        }
        if (snapshot.count == 0) {
            make_center_label(
                _agent_panel_root,
                "No paired Agents",
                175,
                &lv_font_montserrat_18,
                kEtchedGray);
        }
        _agent_reset_button = make_button(
            "Reset", 176, 386, 114, 40, kAlertVermilion);
    }

    make_center_label(
        _agent_panel_root,
        "AGENTS  \xE2\x86\x91",
        438,
        &lv_font_montserrat_14,
        kEtchedGray);
    lv_obj_move_foreground(_agent_panel_root);
}

void AppCodexStatus::update_agent_panel_touch(uint32_t now_ms)
{
    (void)now_ms;
    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr || _screen_off ||
        GetGlobalDisplayProtectionService().touchInputSuppressed()) {
        _agent_panel_model.cancel_touch();
        _last_agent_touch_state = LV_INDEV_STATE_REL;
        return;
    }
    auto& controls = GetGlobalEdgeControlService();
    if (controls.ownsTouch() || controls.lastReleaseConsumed()) {
        _agent_panel_model.cancel_touch();
        _last_agent_touch_state = LV_INDEV_STATE_REL;
        return;
    }
    const lv_indev_state_t state = lv_indev_get_state(indev);
    lv_point_t point = {};
    lv_indev_get_point(indev, &point);
    if (state == LV_INDEV_STATE_PR) {
        if (_last_agent_touch_state == LV_INDEV_STATE_REL) {
            _agent_panel_model.touch_press(point.x, point.y);
        } else {
            _agent_panel_model.touch_move(point.x, point.y);
        }
    } else if (_last_agent_touch_state == LV_INDEV_STATE_PR) {
        _agent_panel_model.touch_move(point.x, point.y);
        const AgentPanelAction action =
            _agent_panel_model.touch_release();
        if (action == AgentPanelAction::Open ||
            action == AgentPanelAction::Close) {
            render_agent_panel();
        }
    }
    _last_agent_touch_state = state;
}

void AppCodexStatus::agent_panel_event_cb(lv_event_t* event)
{
    auto* self =
        static_cast<AppCodexStatus*>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }
    lv_obj_t* target =
        static_cast<lv_obj_t*>(lv_event_get_target(event));
    const AgentPanelSnapshot snapshot =
        self->_agent_panel_model.snapshot();
    for (size_t index = 0; index < snapshot.count; ++index) {
        if (target == self->_agent_row_buttons[index]) {
            self->queue_agent_panel_decision(
                AgentPanelDecision{
                    AgentPanelAction::SwitchAgent,
                    snapshot.rows[index].fingerprint});
            return;
        }
        if (target == self->_agent_delete_buttons[index]) {
            self->queue_agent_panel_decision(
                AgentPanelDecision{
                    AgentPanelAction::Delete,
                    snapshot.rows[index].fingerprint});
            return;
        }
    }
    if (target == self->_agent_allow_button) {
        self->queue_agent_panel_decision(
            self->_agent_panel_model.allow_pairing(false));
    } else if (target == self->_agent_allow_switch_button) {
        self->queue_agent_panel_decision(
            self->_agent_panel_model.allow_pairing(true));
    } else if (target == self->_agent_reject_button) {
        self->queue_agent_panel_decision(
            self->_agent_panel_model.reject_pairing());
    } else if (target == self->_agent_reset_button) {
        self->queue_agent_panel_decision(
            self->_agent_panel_model.request_reset());
    } else if (target == self->_agent_confirm_button) {
        self->queue_agent_panel_decision(
            self->_agent_panel_model.confirm_destructive(true));
    } else if (target == self->_agent_cancel_button) {
        self->queue_agent_panel_decision(
            self->_agent_panel_model.confirm_destructive(false));
    }
}

void AppCodexStatus::queue_agent_panel_decision(
    const AgentPanelDecision& decision)
{
    if (decision.action == AgentPanelAction::None) {
        return;
    }
    std::lock_guard<std::mutex> lock(_agent_decision_mutex);
    if (_pending_agent_decision.action == AgentPanelAction::None) {
        _pending_agent_decision = decision;
    }
}

bool AppCodexStatus::take_agent_panel_decision(
    AgentPanelDecision& decision)
{
    std::lock_guard<std::mutex> lock(_agent_decision_mutex);
    if (_pending_agent_decision.action == AgentPanelAction::None) {
        return false;
    }
    decision = _pending_agent_decision;
    _pending_agent_decision = {};
    return true;
}

void AppCodexStatus::dispatch_agent_panel_decision(
    const AgentPanelDecision& decision,
    uint32_t now_ms)
{
    if (decision.action == AgentPanelAction::Delete) {
        (void)_agent_panel_model.request_delete(decision.fingerprint);
        _agent_panel_dirty = true;
        return;
    }
    if (decision.action == AgentPanelAction::ResetAll) {
        (void)_agent_panel_model.request_reset();
        _agent_panel_dirty = true;
        return;
    }
    if (decision.action == AgentPanelAction::Reject) {
        if (_ble_transport) {
            const uint16_t handle = _ble_transport->pending_handle();
            if (handle != secure_pairing::kNoAgentConnection) {
                const uint32_t transaction =
                    _secure_pairing_session.presentation().transaction_id;
                if (_secure_pairing_session.state() ==
                        secure_pairing::PairingState::AwaitingConfirmation &&
                    _agent_registry_store != nullptr) {
                    (void)_secure_pairing_session.confirm_pairing(
                        false,
                        _agent_registry,
                        *_agent_registry_store,
                        fill_agent_entropy,
                        nullptr,
                        now_ms);
                    if (transaction != 0) {
                        (void)send_pairing_empty(
                            handle,
                            secure_pairing::WireMessageType::Rejected,
                            transaction);
                    }
                }
                (void)_ble_transport->reject_pending(handle);
            }
        }
        clear_pairing_runtime();
        _agent_panel_dirty = true;
        return;
    }
    if (decision.action == AgentPanelAction::AllowPairing ||
        decision.action == AgentPanelAction::AllowAndSwitch) {
        const uint16_t handle = _ble_transport
                                    ? _ble_transport->pending_handle()
                                    : secure_pairing::kNoAgentConnection;
        if (_agent_registry_store == nullptr ||
            handle == secure_pairing::kNoAgentConnection ||
            _secure_pairing_session.state() !=
                secure_pairing::PairingState::AwaitingConfirmation ||
            _secure_pairing_session.confirm_pairing(
                true,
                _agent_registry,
                *_agent_registry_store,
                fill_agent_entropy,
                nullptr,
                now_ms) != secure_pairing::SessionResult::Ok ||
            !send_pairing_credential(
                handle,
                _secure_pairing_session.presentation().credential)) {
            set_status("PAIR ERR", kCodexRed);
            return;
        }
        _pairing_allow_switch =
            decision.action == AgentPanelAction::AllowAndSwitch;
        _agent_panel_model.clear_pairing_request();
        sync_agent_panel_registry();
        _agent_panel_dirty = true;
        set_status("PAIRING", kBeaconViolet);
        return;
    }
    if (decision.action == AgentPanelAction::ConfirmDelete &&
        _agent_registry_ready) {
        const auto before = _agent_registry.snapshot();
        if (_agent_registry.remove_and_rotate(
                decision.fingerprint,
                fill_agent_entropy,
                nullptr) == secure_pairing::RegistryResult::Ok &&
            save_agent_registry()) {
            if (_ble_transport) {
                (void)_ble_transport->set_voice_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
                if (_ble_transport->connected()) {
                    const bool removing_active =
                        _ble_transport->active_fingerprint() ==
                        decision.fingerprint;
                    if (removing_active) {
                        (void)_ble_transport->request_active_disconnect(true);
                    }
                }
                const uint16_t pending = _ble_transport->pending_handle();
                if (pending != secure_pairing::kNoAgentConnection) {
                    (void)_ble_transport->reject_pending(pending);
                }
            }
            clear_pairing_runtime();
            if (_voice_input) {
                (void)_voice_input->set_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
            }
        } else {
            (void)secure_pairing::AgentRegistry::restore(
                before, _agent_registry);
            set_status("AGENT ERR", kCodexRed);
        }
        sync_agent_panel_registry();
        _agent_panel_dirty = true;
        return;
    }
    if (decision.action == AgentPanelAction::ConfirmReset) {
        const bool had_registry = _agent_registry_ready;
        const auto before = _agent_registry.snapshot();
        secure_pairing::RegistryResult reset_result =
            secure_pairing::RegistryResult::InvalidRecord;
        if (had_registry) {
            reset_result = _agent_registry.reset_all(
                fill_agent_entropy, nullptr);
        } else {
            reset_result = secure_pairing::AgentRegistry::initialize(
                fill_agent_entropy, nullptr, _agent_registry);
        }
        if (reset_result == secure_pairing::RegistryResult::Ok &&
            save_agent_registry()) {
            _agent_registry_ready = true;
            if (_voice_input) {
                (void)_voice_input->set_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
            }
            if (_ble_transport) {
                (void)_ble_transport->set_voice_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
                (void)_ble_transport->request_active_disconnect(true);
                const uint16_t pending = _ble_transport->pending_handle();
                if (pending != secure_pairing::kNoAgentConnection) {
                    (void)_ble_transport->reject_pending(pending);
                }
            }
            clear_pairing_runtime();
        } else {
            if (had_registry) {
                (void)secure_pairing::AgentRegistry::restore(
                    before, _agent_registry);
            }
            set_status("AGENT ERR", kCodexRed);
        }
        sync_agent_panel_registry();
        _agent_panel_dirty = true;
        return;
    }
    if (decision.action != AgentPanelAction::SwitchAgent ||
        !_agent_registry_ready) {
        _agent_panel_dirty = true;
        return;
    }

    AgentPanelDecision switch_decision =
        _agent_panel_model.request_switch(
            decision.fingerprint,
            _voice_input != nullptr &&
                _voice_input->ui_state() == VoiceInputUiState::Active);
    if (_agent_panel_model.snapshot().switch_waiting_for_voice &&
        _voice_input != nullptr) {
        _voice_input->cancel_for_home(now_ms);
        switch_decision =
            _agent_panel_model.voice_cleanup_complete();
    }
    if (switch_decision.action != AgentPanelAction::SwitchAgent) {
        return;
    }
    const auto before_selection = _agent_registry.snapshot();
    if (_agent_registry.select(
            switch_decision.fingerprint,
            static_cast<int64_t>(now_ms)) !=
            secure_pairing::RegistryResult::Ok ||
        !save_agent_registry()) {
        (void)secure_pairing::AgentRegistry::restore(
            before_selection, _agent_registry);
        set_status("AGENT ERR", kCodexRed);
        return;
    }
    _agent_switch_target = switch_decision.fingerprint;
    _agent_switch_pending = true;
    if (_ble_transport) {
        if (_ble_transport->active_fingerprint() ==
            _agent_switch_target) {
            _agent_switch_pending = false;
        } else if (_ble_transport->connected()) {
            (void)_ble_transport->request_active_disconnect();
        }
    }
    complete_agent_switch_if_ready();
    sync_agent_panel_registry();
    _agent_panel_dirty = true;
}

bool AppCodexStatus::send_pairing_empty(
    uint16_t connection_handle,
    secure_pairing::WireMessageType type,
    uint32_t transaction_id)
{
    if (!_ble_transport ||
        connection_handle == secure_pairing::kNoAgentConnection) {
        return false;
    }
    std::array<uint8_t, secure_pairing::kMaxPairingWireFrameSize> encoded = {};
    size_t encoded_size = 0;
    return secure_pairing::encode_empty_wire_frame(
               type,
               transaction_id,
               encoded.data(),
               encoded.size(),
               encoded_size) &&
           _ble_transport->notify_pairing(
               connection_handle, encoded.data(), encoded_size) ==
               CodexBleNotifyResult::Accepted;
}

bool AppCodexStatus::send_pairing_credential(
    uint16_t connection_handle,
    const secure_pairing::CredentialEnvelope& envelope)
{
    if (!_ble_transport ||
        connection_handle == secure_pairing::kNoAgentConnection) {
        return false;
    }
    std::array<uint8_t, secure_pairing::kMaxPairingWireFrameSize> encoded = {};
    size_t encoded_size = 0;
    return secure_pairing::encode_credential_envelope(
               envelope,
               encoded.data(),
               encoded.size(),
               encoded_size) &&
           _ble_transport->notify_pairing(
               connection_handle, encoded.data(), encoded_size) ==
               CodexBleNotifyResult::Accepted;
}

void AppCodexStatus::clear_pairing_runtime()
{
    _secure_pairing_session.reset();
    _pairing_completion = PairingCompletion::None;
    _pairing_completion_transaction = 0;
    _pairing_allow_switch = false;
    _agent_panel_model.clear_pairing_request();
}

void AppCodexStatus::complete_authenticated_pending(
    uint16_t connection_handle,
    PairingCompletion completion,
    uint32_t transaction_id,
    bool allow_switch)
{
    if (!_ble_transport) {
        return;
    }
    const auto reject_and_clear = [this, connection_handle]() {
        (void)_ble_transport->reject_pending(connection_handle);
        clear_pairing_runtime();
        sync_agent_panel_registry();
        _agent_panel_dirty = true;
    };
    const secure_pairing::WireMessageType response =
        completion == PairingCompletion::Authentication
            ? secure_pairing::WireMessageType::AuthenticationOk
            : secure_pairing::WireMessageType::CredentialAckOk;
    if (_ble_transport->is_active_handle(connection_handle)) {
        if (_ble_transport->active_fingerprint() !=
                _secure_pairing_session.authenticated_fingerprint() ||
            !send_pairing_empty(
                connection_handle, response, transaction_id)) {
            (void)send_pairing_empty(
                connection_handle,
                secure_pairing::WireMessageType::Error,
                transaction_id);
        }
        clear_pairing_runtime();
        sync_agent_panel_registry();
        _agent_panel_dirty = true;
        return;
    }
    if (!_ble_transport->authorize_pending(
            connection_handle,
            _secure_pairing_session.authenticated_fingerprint())) {
        (void)send_pairing_empty(
            connection_handle,
            secure_pairing::WireMessageType::Error,
            transaction_id);
        reject_and_clear();
        return;
    }
    sync_agent_panel_registry();
    _agent_panel_dirty = true;
    const auto select_and_save = [this](
                                     const secure_pairing::AgentFingerprint&
                                         fingerprint) {
        const auto before = _agent_registry.snapshot();
        if (_agent_registry.select(
                fingerprint,
                static_cast<int64_t>(GetHAL().millis())) ==
                secure_pairing::RegistryResult::Ok &&
            save_agent_registry()) {
            return true;
        }
        (void)secure_pairing::AgentRegistry::restore(before, _agent_registry);
        return false;
    };

    const secure_pairing::AgentFingerprint fingerprint =
        _secure_pairing_session.authenticated_fingerprint();

    if (!_ble_transport->connected()) {
        const bool is_selected =
            allow_switch || !_agent_registry.has_last_selected() ||
            _agent_registry.last_selected() == fingerprint;
        if (!is_selected) {
            (void)send_pairing_empty(
                connection_handle, response, transaction_id);
            reject_and_clear();
            return;
        }
        if (!select_and_save(fingerprint)) {
            (void)send_pairing_empty(
                connection_handle,
                secure_pairing::WireMessageType::Error,
                transaction_id);
            reject_and_clear();
            return;
        }
        if (send_pairing_empty(connection_handle, response, transaction_id) &&
            _ble_transport->promote_pending(connection_handle)) {
            clear_pairing_runtime();
            sync_agent_panel_registry();
            _agent_panel_dirty = true;
        } else {
            reject_and_clear();
        }
        return;
    }

    if (allow_switch) {
        if (!select_and_save(fingerprint)) {
            (void)send_pairing_empty(
                connection_handle,
                secure_pairing::WireMessageType::Error,
                transaction_id);
            reject_and_clear();
            return;
        }
        _pairing_completion = completion;
        _pairing_completion_transaction = transaction_id;
        _agent_switch_target =
            _secure_pairing_session.authenticated_fingerprint();
        _agent_switch_pending = true;
        (void)_ble_transport->request_active_disconnect();
        return;
    }

    (void)send_pairing_empty(connection_handle, response, transaction_id);
    reject_and_clear();
}

void AppCodexStatus::process_secure_pairing(uint32_t now_ms)
{
    if (!_ble_transport || !_agent_registry_ready) {
        return;
    }
    const uint16_t session_connection =
        _secure_pairing_session.connection_handle();
    const bool session_is_active =
        session_connection != secure_pairing::kNoAgentConnection &&
        _ble_transport->is_active_handle(session_connection);
    if (_secure_pairing_session.expire(now_ms)) {
        if (session_connection != secure_pairing::kNoAgentConnection &&
            !session_is_active) {
            (void)_ble_transport->reject_pending(session_connection);
        }
        clear_pairing_runtime();
        sync_agent_panel_registry();
        _agent_panel_dirty = true;
    } else if (
        session_connection != secure_pairing::kNoAgentConnection &&
        _ble_transport->pending_handle() != session_connection &&
        !session_is_active) {
        clear_pairing_runtime();
        sync_agent_panel_registry();
        _agent_panel_dirty = true;
    }
    CodexPairingFrame queued = {};
    while (_ble_transport->take_pairing_frame(queued)) {
        secure_pairing::WireFrameView frame = {};
        if (!secure_pairing::decode_wire_frame(
                queued.data.data(), queued.length, frame)) {
            continue;
        }
        if (frame.type == secure_pairing::WireMessageType::DiscoveryRequest) {
            std::array<uint8_t, secure_pairing::kMaxPairingWireFrameSize>
                response = {};
            size_t response_size = 0;
            secure_pairing::crypto::PublicKey watch_identity_public = {};
            if (frame.payload_size == 0 &&
                _agent_registry.watch_identity_public(watch_identity_public) &&
                secure_pairing::encode_discovery_response(
                    frame.transaction_id,
                    _agent_registry.watch_id(),
                    _agent_registry.key_epoch(),
                    watch_identity_public,
                    response.data(),
                    response.size(),
                    response_size)) {
                (void)_ble_transport->notify_pairing(
                    queued.connection_handle,
                    response.data(),
                    response_size);
            }
            continue;
        }

        if (frame.type == secure_pairing::WireMessageType::PairingRequest) {
            if (_ble_transport->is_active_handle(
                    queued.connection_handle)) {
                (void)send_pairing_empty(
                    queued.connection_handle,
                    secure_pairing::WireMessageType::Error,
                    frame.transaction_id);
                continue;
            }
            secure_pairing::PairingRequest request = {};
            secure_pairing::AgentFingerprint fingerprint = {};
            if (!secure_pairing::decode_pairing_request(
                    frame, queued.connection_handle, request) ||
                !agent_fingerprint(
                    request.agent_identity_public, fingerprint)) {
                (void)send_pairing_empty(
                    queued.connection_handle,
                    secure_pairing::WireMessageType::Error,
                    frame.transaction_id);
                continue;
            }
            const secure_pairing::SessionResult request_result =
                _secure_pairing_session.request_pairing(
                    request,
                    _agent_registry,
                    fill_agent_entropy,
                    nullptr,
                    now_ms);
            if (request_result != secure_pairing::SessionResult::Ok) {
                (void)send_pairing_empty(
                    queued.connection_handle,
                    secure_pairing::WireMessageType::Error,
                    frame.transaction_id);
                if (request_result == secure_pairing::SessionResult::Cooldown) {
                    (void)_ble_transport->reject_pending(
                        queued.connection_handle);
                }
                continue;
            }
            (void)_ble_transport->note_pairing_activity(
                queued.connection_handle, now_ms);
            const auto& presentation =
                _secure_pairing_session.presentation();
            secure_pairing::AgentName display_name = {};
            std::copy_n(
                request.agent_name.begin(),
                request.agent_name_size,
                display_name.begin());
            std::array<uint8_t, secure_pairing::kMaxPairingWireFrameSize>
                response = {};
            size_t response_size = 0;
            if (!secure_pairing::encode_pairing_offer(
                    presentation,
                    response.data(),
                    response.size(),
                    response_size) ||
                _ble_transport->notify_pairing(
                    queued.connection_handle,
                    response.data(),
                    response_size) != CodexBleNotifyResult::Accepted ||
                !_agent_panel_model.show_pairing_request(
                    display_name,
                    request.agent_name_size,
                    fingerprint,
                    presentation.verification_code.data())) {
                (void)_ble_transport->reject_pending(
                    queued.connection_handle);
                clear_pairing_runtime();
                continue;
            }
            _agent_panel_dirty = true;
            continue;
        }

        if (frame.type == secure_pairing::WireMessageType::PairingConfirm) {
            bool accepted = false;
            if (queued.connection_handle !=
                    _secure_pairing_session.connection_handle() ||
                !secure_pairing::decode_pairing_confirmation(frame, accepted) ||
                frame.transaction_id !=
                    _secure_pairing_session.presentation().transaction_id) {
                (void)send_pairing_empty(
                    queued.connection_handle,
                    secure_pairing::WireMessageType::Error,
                    frame.transaction_id);
            } else if (!accepted) {
                if (_agent_registry_store) {
                    (void)_secure_pairing_session.confirm_pairing(
                        false,
                        _agent_registry,
                        *_agent_registry_store,
                        fill_agent_entropy,
                        nullptr,
                        now_ms);
                }
                (void)send_pairing_empty(
                    queued.connection_handle,
                    secure_pairing::WireMessageType::Rejected,
                    frame.transaction_id);
                (void)_ble_transport->reject_pending(
                    queued.connection_handle);
                clear_pairing_runtime();
            }
            continue;
        }

        if (frame.type ==
            secure_pairing::WireMessageType::AuthenticationRequest) {
            secure_pairing::AuthenticationRequest request = {};
            if (!secure_pairing::decode_authentication_request(
                    frame, queued.connection_handle, request)) {
                (void)send_pairing_empty(
                    queued.connection_handle,
                    secure_pairing::WireMessageType::Error,
                    frame.transaction_id);
                continue;
            }
            const secure_pairing::SessionResult request_result =
                _secure_pairing_session.request_authentication(
                    request,
                    _agent_registry,
                    fill_agent_entropy,
                    nullptr,
                    now_ms);
            if (request_result != secure_pairing::SessionResult::Ok) {
                (void)send_pairing_empty(
                    queued.connection_handle,
                    secure_pairing::WireMessageType::Error,
                    frame.transaction_id);
                if (request_result == secure_pairing::SessionResult::Cooldown) {
                    (void)_ble_transport->reject_pending(
                        queued.connection_handle);
                }
                continue;
            }
            (void)_ble_transport->note_pairing_activity(
                queued.connection_handle, now_ms);
            std::array<uint8_t, secure_pairing::kMaxPairingWireFrameSize>
                response = {};
            size_t response_size = 0;
            if (secure_pairing::encode_authentication_challenge_wire(
                    _secure_pairing_session.authentication_challenge(),
                    _secure_pairing_session.watch_challenge_signature(),
                    _secure_pairing_session.watch_challenge_signature_size(),
                    response.data(),
                    response.size(),
                    response_size)) {
                (void)_ble_transport->notify_pairing(
                    queued.connection_handle,
                    response.data(),
                    response_size);
            }
            continue;
        }

        if (frame.type ==
            secure_pairing::WireMessageType::AuthenticationProof) {
            const uint8_t* signature = nullptr;
            size_t signature_size = 0;
            if (queued.connection_handle !=
                    _secure_pairing_session.connection_handle() ||
                frame.transaction_id !=
                    _secure_pairing_session.presentation().transaction_id ||
                _agent_registry_store == nullptr ||
                !secure_pairing::decode_authentication_proof(
                    frame, signature, signature_size) ||
                _secure_pairing_session.authenticate(
                    signature,
                    signature_size,
                    _agent_registry,
                    *_agent_registry_store,
                    fill_agent_entropy,
                    nullptr,
                    now_ms) != secure_pairing::SessionResult::Ok) {
                (void)send_pairing_empty(
                    queued.connection_handle,
                    secure_pairing::WireMessageType::Error,
                    frame.transaction_id);
                continue;
            }
            if (_secure_pairing_session.state() ==
                secure_pairing::PairingState::AwaitingCredentialAck) {
                (void)send_pairing_credential(
                    queued.connection_handle,
                    _secure_pairing_session.presentation().credential);
            } else {
                complete_authenticated_pending(
                    queued.connection_handle,
                    PairingCompletion::Authentication,
                    frame.transaction_id,
                    false);
            }
            continue;
        }

        if (frame.type == secure_pairing::WireMessageType::CredentialAck) {
            const uint8_t* proof = nullptr;
            size_t proof_size = 0;
            const auto purpose =
                _secure_pairing_session.presentation().credential.purpose;
            if (queued.connection_handle !=
                    _secure_pairing_session.connection_handle() ||
                !secure_pairing::decode_credential_ack_wire(
                    frame, proof, proof_size) ||
                _secure_pairing_session.acknowledge_credential(
                    frame.transaction_id,
                    proof,
                    proof_size,
                    now_ms) != secure_pairing::SessionResult::Ok) {
                (void)send_pairing_empty(
                    queued.connection_handle,
                    secure_pairing::WireMessageType::Error,
                    frame.transaction_id);
                continue;
            }
            const bool newly_paired =
                purpose == secure_pairing::CredentialPurpose::Pairing;
            complete_authenticated_pending(
                queued.connection_handle,
                PairingCompletion::CredentialAck,
                frame.transaction_id,
                newly_paired && _pairing_allow_switch);
        }
    }
}

void AppCodexStatus::complete_agent_switch_if_ready()
{
    if (!_agent_switch_pending || !_ble_transport ||
        _ble_transport->connected()) {
        return;
    }
    const auto clear_failed_switch = [this](uint16_t rejected_handle) {
        if (rejected_handle != secure_pairing::kNoAgentConnection) {
            (void)_ble_transport->reject_pending(rejected_handle);
        }
        _agent_switch_pending = false;
        _agent_switch_target = {};
        clear_pairing_runtime();
        sync_agent_panel_registry();
        _agent_panel_dirty = true;
    };
    const uint16_t pending = _ble_transport->pending_handle();
    if (pending == secure_pairing::kNoAgentConnection ||
        _ble_transport->pending_fingerprint() != _agent_switch_target) {
        clear_failed_switch(secure_pairing::kNoAgentConnection);
        return;
    }
    if (_pairing_completion != PairingCompletion::None &&
        _ble_transport->pending_fingerprint() == _agent_switch_target) {
        const secure_pairing::WireMessageType response =
            _pairing_completion == PairingCompletion::Authentication
                ? secure_pairing::WireMessageType::AuthenticationOk
                : secure_pairing::WireMessageType::CredentialAckOk;
        if (!send_pairing_empty(
                pending, response, _pairing_completion_transaction)) {
            clear_failed_switch(pending);
            return;
        }
    }
    if (_ble_transport->promote_pending(pending)) {
        _agent_switch_pending = false;
        _agent_switch_target = {};
        _pairing_completion = PairingCompletion::None;
        _pairing_completion_transaction = 0;
        _pairing_allow_switch = false;
        _secure_pairing_session.reset();
        sync_agent_panel_registry();
        _agent_panel_dirty = true;
    } else {
        clear_failed_switch(pending);
    }
}

void AppCodexStatus::sync_agent_panel_registry()
{
    const secure_pairing::AgentFingerprint active =
        _ble_transport
            ? _ble_transport->active_fingerprint()
            : secure_pairing::AgentFingerprint{};
    const secure_pairing::AgentFingerprint pending =
        _ble_transport
            ? _ble_transport->pending_fingerprint()
            : secure_pairing::AgentFingerprint{};
    const bool has_active = std::any_of(
        active.begin(), active.end(), [](uint8_t byte) {
            return byte != 0;
        });
    const bool has_pending = std::any_of(
        pending.begin(), pending.end(), [](uint8_t byte) {
            return byte != 0;
        });
    if (_agent_registry_ready) {
        _agent_panel_model.sync_registry(
            _agent_registry,
            has_active ? &active : nullptr,
            has_pending ? &pending : nullptr);
    }
}

bool AppCodexStatus::save_agent_registry()
{
    return _agent_registry_store != nullptr &&
           _agent_registry_store->save(_agent_registry) ==
               secure_pairing::StoreResult::Ok;
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
