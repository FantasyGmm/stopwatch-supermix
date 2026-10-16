/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_codex_status.h"
#include "codex_status_priority.h"
#include "assets/codex_pet_assets.h"
#include "assets/pinned_microphone_d.h"
#include "codex_ble_transport.h"
#include "codex_pending_glow_logic.h"
#include "ble_voice_probe_controller.h"
#include "voice_input_controller.h"
#include "pinned_microphone_meter.h"
#include "agent_panel_presenter.h"
#include "codex_status_ui_layout.h"
#include "codex_status_model.h"
#include "codex_status_codec.h"
#include "codex_task_detail_crypto.h"
#include "status_message_router.h"
#include "trae_status_model.h"
#include <apps/common/edge_control/global_edge_control_service.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <hal/audio/stopwatch_audio_backend.h>
#include <hal/utils/settings/settings.h>
#include <runtime/display/global_display_protection_service.h>
#include <runtime/power/low_battery_alert_logic.h>
#include <wifi_manager.h>
#include <esp_websocket_client.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <mooncake_log.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace mooncake;

extern "C" const lv_image_dsc_t logo_codex;
LV_FONT_DECLARE(font_puhui_14_1)
LV_FONT_DECLARE(CodexTaskTitle21)
LV_FONT_DECLARE(AgentPanelTitle16)
LV_FONT_DECLARE(PinnedMicTitle24)
LV_FONT_DECLARE(PinnedMicState22)

namespace {

constexpr EdgeControlRect kTaskDeckTouchExclusion = {183, 423, 283, 465};
constexpr EdgeControlRect kTaskModeTouchExclusion = {0, 0, 465, 465};

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
constexpr int kPageWidth                 = 466;
constexpr int kMaxPageCount              = 5;
// Retain the Host implementation and logical ID for a later profile.
constexpr bool kHostPageEnabled          = false;
constexpr int kAgentPanelHiddenY         = agent_panel_ui::kPanelHiddenY;
constexpr int kAgentPanelVisibleY        = 0;
constexpr int kTaskSwipeThreshold        = 36;
constexpr int kTaskTapTolerance          = 12;
constexpr int kTaskDeckHiddenY           = 466;
constexpr int kTaskDeckAutoDismissY      = 326;

int64_t current_unix_milliseconds()
{
    const std::time_t now = std::time(nullptr);
    if (now <= 0) {
        return 0;
    }
    const int64_t milliseconds = static_cast<int64_t>(now) * 1000LL;
    return milliseconds >=
            agent_panel_presentation::kMinimumUnixMilliseconds
        ? milliseconds
        : 0;
}

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

lv_obj_t* make_status_page_root(lv_obj_t* parent, int x)
{
    lv_obj_t* page = lv_obj_create(parent);
    lv_obj_set_size(page, kPageWidth, kPageWidth);
    lv_obj_set_pos(page, x, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(kInstrumentBlack), 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_CLICKABLE);
    return page;
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

void set_label_text_if_changed(lv_obj_t* label, const char* text)
{
    if (label == nullptr) {
        return;
    }
    const char* current = lv_label_get_text(label);
    const char* desired = text == nullptr ? "" : text;
    if (current == nullptr || std::strcmp(current, desired) != 0) {
        lv_label_set_text(label, desired);
    }
}

lv_obj_t* make_channel_dot(lv_obj_t* parent, int x, int y, int size)
{
    lv_obj_t* dot = make_frame_segment(
        parent, x, y, size, size, kEtchedGray);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    return dot;
}

void apply_channel_presentation(
    lv_obj_t* dot,
    lv_obj_t* label,
    const CodexChannelPresentation& presentation,
    uint32_t& cached_text_color,
    uint32_t& cached_dot_color,
    bool& cached_filled,
    bool& cache_valid)
{
    if (dot == nullptr || label == nullptr) {
        return;
    }
    set_label_text_if_changed(label, presentation.text);
    if (!cache_valid || cached_text_color != presentation.text_color) {
        lv_obj_set_style_text_color(
            label, lv_color_hex(presentation.text_color), 0);
        cached_text_color = presentation.text_color;
    }
    if (!cache_valid || cached_dot_color != presentation.dot_color) {
        lv_obj_set_style_bg_color(
            dot, lv_color_hex(presentation.dot_color), 0);
        lv_obj_set_style_border_color(
            dot, lv_color_hex(presentation.dot_color), 0);
        cached_dot_color = presentation.dot_color;
    }
    if (!cache_valid || cached_filled != presentation.filled) {
        lv_obj_set_style_bg_opa(
            dot, presentation.filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(dot, presentation.filled ? 0 : 1, 0);
        cached_filled = presentation.filled;
    }
    cache_valid = true;
}

void apply_agent_presentation(
    lv_obj_t* label,
    const char* text,
    uint32_t color,
    uint32_t& cached_color,
    bool& cache_valid)
{
    if (label == nullptr) {
        return;
    }
    const char* safe_text = text == nullptr ? "" : text;
    set_label_text_if_changed(label, safe_text);
    if (!cache_valid || cached_color != color) {
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        cached_color = color;
        cache_valid = true;
    }
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

bool trae_task_equal(const TraeTask& lhs, const TraeTask& rhs)
{
    return lhs.id == rhs.id && lhs.project == rhs.project &&
        lhs.title == rhs.title && lhs.detail == rhs.detail &&
        lhs.time == rhs.time && lhs.model == rhs.model &&
        lhs.effort == rhs.effort && lhs.state == rhs.state;
}

bool trae_snapshot_equal(
    const TraeStatusSnapshot& lhs,
    const TraeStatusSnapshot& rhs)
{
    if (lhs.online != rhs.online || lhs.process_state != rhs.process_state ||
        lhs.model != rhs.model || lhs.effort != rhs.effort ||
        lhs.revision != rhs.revision ||
        lhs.pending_count != rhs.pending_count ||
        lhs.running_count != rhs.running_count ||
        lhs.tasks_truncated != rhs.tasks_truncated ||
        lhs.task_count != rhs.task_count) {
        return false;
    }
    for (size_t index = 0; index < lhs.tasks.size(); ++index) {
        if (!trae_task_equal(lhs.tasks[index], rhs.tasks[index])) {
            return false;
        }
    }
    return true;
}

bool trae_channel_equal(
    const TraeChannelState& lhs,
    const TraeChannelState& rhs)
{
    const char* lhs_text = lhs.text == nullptr ? "" : lhs.text;
    const char* rhs_text = rhs.text == nullptr ? "" : rhs.text;
    return std::strcmp(lhs_text, rhs_text) == 0 &&
        lhs.dot_color == rhs.dot_color && lhs.text_color == rhs.text_color;
}

bool trae_channels_equal(
    const TraeChannelPresentation& lhs,
    const TraeChannelPresentation& rhs)
{
    return trae_channel_equal(lhs.agent, rhs.agent) &&
        trae_channel_equal(lhs.voice, rhs.voice);
}

void copy_task_id(std::array<char, 33>& destination, const TraeTask* task)
{
    destination.fill('\0');
    if (task != nullptr) {
        destination = task->id;
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
    if (action == CodexButtonAction::PreviousPage ||
        action == CodexButtonAction::NextPage) {
        if (!_screen_off && !_suppress_task_navigation) {
            LvglLockGuard lock;
            if (!_agent_panel_model.snapshot().visible &&
                !_agent_panel_model.owns_touch() && !_task_deck_drag_active) {
                step_page(action == CodexButtonAction::NextPage ? 1 : -1);
            }
        }
        return;
    }
    if (_input_action_controller) {
        (void)_input_action_controller->enqueue(action, now_ms);
    }
}

void AppCodexStatus::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppCodexStatus::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    sync_task_edge_exclusion(CodexTaskFocusMode::Status);

    const uint32_t now = GetHAL().millis();
    _key_manager     = std::make_unique<input::KeyManager>();
    _last_battery_ms = 0;
    _last_network_ms = 0;
    _last_status_payload_ms = 0;
    _screen_off =
        GetGlobalDisplayProtectionService().snapshot().state ==
        DisplayProtectionState::ScreenOff;
    _screen_off_runtime_cadence = {};
    _screen_off_voice_policy.reset();
    GetGlobalDisplayProtectionService().setRightButtonWakeDeferred(false);
    _suppress_task_navigation = false;
    {
        std::lock_guard<std::mutex> task_lock(_task_state_mutex);
        _task_focus_mode = CodexTaskFocusMode::Status;
        _task_focus_deck.reset();
        _task_detail_store.reset();
        _task_detail_crypto =
            std::make_unique<voice_input::crypto::CcmContext>();
        _task_detail_key_epoch = 0;
    }
    {
        std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
        _task_voice_ready_applied = false;
    }
    GetHAL().updateButtonStates();
    if (_input_action_controller) {
        _input_action_controller->disconnect();
    }
    _voice_button_dispatcher.reset(
        GetHAL().btnB.isPressed(), GetHAL().btnA.isPressed());
    _production_voice_ready = false;
    {
        std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
        _production_voice_link_usable = false;
        _production_voice_capabilities = 0;
        _production_voice_mtu = 23;
    }
    _production_voice_connection_epoch = 0;
    _pet_connection_epoch = 0;
    _pet_subscription_generation = 0;
    _voice_status.store(VoiceInputUiState::Off);
    _ws_connected    = false;
    _wifi_lease.reset();
    _wifi_started    = false;
    _wifi_fallback_started = false;
    _ble_was_connected = false;
    _alert_carousel.reset();
    _telemetry_snapshot = {};
    _latest_global_model.clear();
    _latest_global_effort.clear();
    _page_swipe.cancel();
    _agent_panel_model = {};
    _agent_panel_dirty = false;
    _agent_switch_pending = false;
    _agent_switch_target = {};
    _agent_reset_hold_started_ms = 0;
    _agent_panel_notice_until_ms = 0;
    _agent_reset_hold_active = false;
    _agent_reset_hold_queued = false;
    _agent_pairing_expired = false;
    _agent_panel_manage_mode = false;
    _agent_panel_notice = {};
    clear_pairing_runtime();
    {
        std::lock_guard<std::mutex> decision_lock(_agent_decision_mutex);
        _pending_agent_decision = {};
    }
    _current_page = 1;
    _active_page_order = {{0, 1, 2, -1, -1}};
    _active_page_count = 3;
    _agent_inventory_received = false;
    _inventory_codex_available = true;
    _inventory_trae_available = false;
    _page_indicator_count = 0;
    _page_indicator_slot = -1;
    _trae_status_received = false;
    _trae_status_snapshot = {};
    _trae_rendered_snapshot = {};
    _trae_rendered_channels = {};
    _trae_rendered_current_task = {};
    _trae_rendered_next_task = {};
    _trae_page_rendered = false;
    _agent_status_style_valid = false;
    _voice_status_style_valid = false;
    _last_page_touch_state = LV_INDEV_STATE_REL;
    _last_agent_touch_state = LV_INDEV_STATE_REL;
    _last_task_touch_state = LV_INDEV_STATE_REL;
    _alert_style_valid = false;
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
    (void)sync_task_detail_credential();

    _pet_files = std::make_unique<codex_pet::FatPetFileSystem>();
    _pet_allocator =
        std::make_unique<codex_pet::PsramPetBufferAllocator>();
    _pet_selection_store =
        std::make_unique<codex_pet::NvsPetSelectionStore>();
    _pet_store = std::make_unique<codex_pet::CodexPetStore>(
        *_pet_files, *_pet_allocator, *_pet_selection_store);
    _pet_selection = _pet_store->load_selection();
    _pet_runtime = std::make_unique<codex_pet::CodexPetRuntime>(
        *_pet_files, *_pet_allocator);
    (void)_pet_runtime->open(_pet_selection);
    _pet_rendered_selection = _pet_selection;
    _pet_rendered_selection_valid = true;

    {
        LvglLockGuard lock;
        create_view();
        if (_screen_off && _pet_timer != nullptr) {
            lv_timer_pause(_pet_timer);
        }
    }

    if (_transport_mode != "wifi") {
        const bool runtime_started = _runtime_session.start(_ble_name);
        _ble_transport = _runtime_session.transport();
        _input_action_controller = _runtime_session.input_actions();
        _voice_probe = _runtime_session.voice_probe();
        _voice_input = _runtime_session.voice_input();
        if (runtime_started) {
            _pet_transfer = std::make_unique<codex_pet::CodexPetTransfer>(
                *_pet_store, _ble_transport->pet_crypto());
            if (_agent_registry_ready) {
                (void)_ble_transport->set_voice_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
                (void)_ble_transport->set_pet_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
                if (_pet_transfer) {
                    _pet_transfer->on_credential_replaced(
                        _agent_registry.key_epoch());
                }
                std::lock_guard<std::mutex> controller_lock(
                    _task_controller_mutex);
                if (_voice_input) {
                    (void)_voice_input->set_credential(
                        _agent_registry.master_key(),
                        _agent_registry.key_epoch());
                }
            }
            sync_task_voice_route();
            set_status("BLE PAIR", kTextDim);
            set_host_text(_ble_name.c_str());
        } else {
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
    const DisplayProtectionState display_state =
        GetGlobalDisplayProtectionService().snapshot().state;
    const bool woke_screen =
        _screen_off && display_state != DisplayProtectionState::ScreenOff;
    const bool display_is_off =
        display_state == DisplayProtectionState::ScreenOff;
    if (_screen_off != display_is_off) {
        LvglLockGuard lock;
        sync_display_protection_presentation();
    }
    const bool left_pressed = GetHAL().btnA.isPressed();
    const bool right_pressed = GetHAL().btnB.isPressed();
    input::KeyEvent key_event = input::KeyEvent::None;
    if (_key_manager) {
        key_event = _key_manager->update(false);
    }
    // Codex-local edge gates own both buttons; the shared manager only owns Home.
    if (key_event == input::KeyEvent::GoNext ||
        key_event == input::KeyEvent::GoPrevious) {
        key_event = input::KeyEvent::None;
    }

    if (woke_screen) {
        _suppress_task_navigation = true;
        if (_input_action_controller) {
            _input_action_controller->clear_pending();
        }
    }
    if (_suppress_task_navigation) {
        key_event = input::KeyEvent::None;
        if (GetHAL().btnA.isReleased() && GetHAL().btnB.isReleased()) {
            _suppress_task_navigation = false;
        }
    }
    const bool pinned_blocks_button_voice =
        _pinned_microphone_controller.blocks_button_voice();
    VoiceButtonSink* const voice_sink =
        pinned_blocks_button_voice ? nullptr : task_focus_voice_sink();
    GetGlobalDisplayProtectionService().setRightButtonWakeDeferred(
        voice_sink != nullptr);
    const CodexScreenOffVoiceAction screen_off_voice_action =
        _screen_off_voice_policy.update(
            _screen_off,
            right_pressed,
            voice_sink != nullptr,
            now,
            VoiceButtonEdgeGate::kVoiceHoldMs);
    if (screen_off_voice_action ==
        CodexScreenOffVoiceAction::WakeDisplay) {
        GetGlobalDisplayProtectionService().wakeFromDeferredButton();
        _voice_button_dispatcher.reset(right_pressed, left_pressed);
        _suppress_task_navigation = true;
        if (_input_action_controller) {
            _input_action_controller->clear_pending();
        }
        return;
    }
    const bool global_input_suppressed = GetHAL().buttonInputSuppressed();
    const VoiceButtonDispatchDecision voice_decision = _voice_button_dispatcher.dispatch(
        left_pressed,
        right_pressed,
        woke_screen,
        key_event == input::KeyEvent::GoHome,
        now,
        voice_sink,
        _screen_off ? nullptr : this,
        global_input_suppressed);
    if (voice_decision == VoiceButtonDispatchDecision::Close) {
        if (_voice_input) {
            _pinned_microphone_controller.stop_for_safety(*_voice_input, now);
        }
        if (_input_action_controller) {
            _input_action_controller->disconnect();
        }
        _voice_button_dispatcher.reset(right_pressed, left_pressed);
        close();
        return;
    }

    if (!_screen_off_runtime_cadence.background_work_due(
            _screen_off, now)) {
        return;
    }

    if (!_screen_off) {
        LvglLockGuard lock;
        const uint32_t sampled_touch_ms =
            GetHAL().lastTouchSampleTimestampMs();
        const uint32_t touch_now =
            sampled_touch_ms == 0 ? now : sampled_touch_ms;
        sync_pet_runtime(now);
        if (_agent_panel_dirty) {
            render_agent_panel();
            _agent_panel_dirty = false;
        }
        update_agent_panel_runtime(now);
        const CodexTaskFocusMode task_mode =
            snapshot_task_focus_state().mode;
        bool consume_auto_dismiss_touch = false;
        if (_task_deck_auto_dismiss_latched) {
            lv_indev_t* indev = GetHAL().lvTouchpad;
            if (indev == nullptr ||
                lv_indev_get_state(indev) == LV_INDEV_STATE_REL) {
                _task_deck_auto_dismiss_latched = false;
            } else {
                consume_auto_dismiss_touch = true;
            }
        }
        const bool agent_panel_available =
            sync_agent_panel_for_task_mode(task_mode);
        if (agent_panel_available && !consume_auto_dismiss_touch) {
            update_agent_panel_touch(touch_now);
        }
        if (consume_auto_dismiss_touch) {
            _agent_panel_model.cancel_touch();
            cancel_task_focus_touch();
            _page_swipe.cancel();
            _last_page_touch_state = LV_INDEV_STATE_REL;
        } else if (agent_panel_available && _agent_panel_model.owns_touch()) {
            cancel_task_focus_touch();
            _page_swipe.cancel();
            _last_page_touch_state = LV_INDEV_STATE_REL;
        } else {
            if (task_mode != CodexTaskFocusMode::Status &&
                _agent_panel_model.snapshot().visible) {
                cancel_task_focus_touch();
                _page_swipe.cancel();
                _last_page_touch_state = LV_INDEV_STATE_REL;
                sync_task_focus_visibility();
            } else {
                sync_task_focus_visibility();
                if (task_mode == CodexTaskFocusMode::Status) {
                    update_page_swipe(touch_now);
                } else {
                    _page_swipe.cancel();
                    _last_page_touch_state = LV_INDEV_STATE_REL;
                    update_task_focus_touch(touch_now);
                }
            }
        }
        const bool alert_changed = _alert_carousel.update(now);
        if (alert_changed && _current_page == 1) {
            render_alert();
        }
        render_telemetry_pages();
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
        const uint32_t connection_epoch =
            _ble_transport->connection_epoch();
        if (connection_epoch != _production_voice_connection_epoch) {
            {
                LvglLockGuard lock;
                reset_agent_inventory();
            }
            codex_pet::PetCleanupSequence::run(
                [this]() {
                    if (_pet_transfer) {
                        _pet_transfer->on_disconnect(_pet_connection_epoch);
                    }
                },
                [this, connection_epoch, right_pressed, left_pressed, now]() {
                    _pet_connection_epoch = connection_epoch;
                    _pet_subscription_generation = 0;
                    _production_voice_connection_epoch = connection_epoch;
                    _production_voice_ready = false;
                    if (_input_action_controller) {
                        _input_action_controller->disconnect();
                    }
                    _voice_button_dispatcher.reset(right_pressed, left_pressed);
                    if (_voice_input) {
                        _pinned_microphone_controller.stop_for_safety(
                            *_voice_input, now);
                    }
                    {
                        std::lock_guard<std::mutex> controller_lock(
                            _task_controller_mutex);
                        _production_voice_link_usable = false;
                        _production_voice_capabilities = 0;
                        _production_voice_mtu = 23;
                        _task_voice_ready_applied = false;
                        if (_voice_input) {
                            _voice_input->handle_connection_epoch(
                                connection_epoch);
                        }
                    }
                    {
                        std::lock_guard<std::mutex> task_lock(
                            _task_state_mutex);
                        _task_detail_store.reset_connection();
                    }
                });
            sync_agent_panel_registry();
            _agent_panel_dirty = true;
        }

        process_pet_transfer(now, connection_epoch);

        CodexInputActionControl input_control = {};
        while (_ble_transport->take_input_action_control(input_control)) {
            if (_input_action_controller) {
                _input_action_controller->set_ready(input_control.control);
            }
        }
        input_action::WireAction input_action = {};
        while (_input_action_controller &&
               _input_action_controller->take(input_action)) {
            const CodexBleNotifyResult result =
                _ble_transport->notify_input_action(
                    input_action.bytes.data(), input_action.bytes.size());
            _input_action_controller->record_result(
                result == CodexBleNotifyResult::Accepted);
        }

        const uint8_t previous_voice_capabilities =
            _production_voice_capabilities;
        CodexProductionVoiceControl production_control;
        while (_ble_transport->take_production_voice_control(
                   production_control)) {
            _production_voice_ready =
                production_control.control.kind ==
                voice_input::ControlKind::Ready;
            _production_voice_capabilities = _production_voice_ready
                ? production_control.control.capabilities
                : 0;
        }
        const uint16_t voice_mtu = _ble_transport->negotiated_mtu();
        const bool production_link_usable =
            ble_connected && _production_voice_ready &&
            _ble_transport->voice_subscribed() &&
            _ble_transport->voice_notification_limit() >=
                voice_input::kAudioCharacteristicValueSize;
        {
            std::lock_guard<std::mutex> controller_lock(
                _task_controller_mutex);
            const bool readiness_changed =
                production_link_usable != _production_voice_link_usable ||
                voice_mtu != _production_voice_mtu ||
                previous_voice_capabilities != _production_voice_capabilities;
            const bool external_microphone_lost =
                (previous_voice_capabilities &
                 voice_input::kExternalMicrophoneCapability) != 0 &&
                (_production_voice_capabilities &
                 voice_input::kExternalMicrophoneCapability) == 0;
            if ((!production_link_usable || external_microphone_lost) &&
                _voice_input) {
                _pinned_microphone_controller.stop_for_safety(
                    *_voice_input, now);
            }
            _production_voice_link_usable = production_link_usable;
            _production_voice_mtu = voice_mtu;
            if (readiness_changed) {
                _task_voice_ready_applied = false;
            }
        }
        sync_task_voice_readiness();
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
        if (ble_connected && !_ble_was_connected) {
            _runtime_session.begin_connection();
            stop_network_fallback();
            _ble_was_connected = true;
            _last_status_payload_ms = 0;
            {
                LvglLockGuard lock;
                reset_agent_inventory();
            }
            clear_telemetry();
            set_status("BLE WAIT", kTextDim);
            set_host_text(_ble_name.c_str());
        } else if (!ble_connected && _ble_was_connected) {
            _production_voice_ready = false;
            _production_voice_capabilities = 0;
            _voice_button_dispatcher.reset(right_pressed, left_pressed);
            _runtime_session.handle_disconnect(
                _pinned_microphone_controller,
                _task_controller_mutex,
                now);
            {
                std::lock_guard<std::mutex> task_lock(_task_state_mutex);
                _task_detail_store.reset_connection();
            }
            {
                std::lock_guard<std::mutex> controller_lock(
                    _task_controller_mutex);
                _task_voice_ready_applied = false;
            }
            _ble_was_connected = false;
            _ble_wait_started_ms = now;
            _last_status_payload_ms = 0;
            if (!_ws_connected) {
                clear_telemetry();
            } else {
                LvglLockGuard lock;
                set_percent(_week_pct, _week_bar, -1);
            }
            set_status("BLE PAIR", kTextDim);
            set_host_text(_ble_name.c_str());
        }

        if ((ble_connected || _ws_connected) && _last_status_payload_ms != 0 &&
            now - _last_status_payload_ms > kStatusStaleMs) {
            set_status(ble_connected ? "BLE WAIT" : "WAITING", kTextDim);
            {
                LvglLockGuard lock;
                set_percent(_week_pct, _week_bar, -1);
            }
            _last_status_payload_ms = 0;
        }

        CodexBleMessage message;
        while (_ble_transport->take_message(message)) {
            handle_status_payload(
                message.payload.data(),
                message.payload.size(),
                message.encoding,
                "BLE LIVE"
            );
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

    if (!_screen_off && now - _last_battery_ms >= 1000) {
        _last_battery_ms = now;
        LvglLockGuard lock;
        update_battery();
    }

    {
        std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
        const VoiceInputUiState voice_status =
            _voice_input ? _voice_input->ui_state() : VoiceInputUiState::Off;
        _voice_status.store(voice_status);
        if (_voice_input) {
            _pinned_microphone_controller.sync(
                _production_voice_link_usable &&
                    (_production_voice_capabilities &
                     voice_input::kExternalMicrophoneCapability) != 0,
                _voice_input->ui_state() == VoiceInputUiState::Error,
                *_voice_input);
        }
    }
    if (!_screen_off) {
        LvglLockGuard lock;
        render_pinned_microphone();
        if (_task_focus_view != nullptr) {
            const CodexTaskFocusMode task_mode =
                snapshot_task_focus_state().mode;
            _task_focus_view->set_recording(
                task_mode == CodexTaskFocusMode::TaskDetail &&
                _voice_status.load() == VoiceInputUiState::Active);
        }
    }
    sync_task_voice_readiness();
    if (!_screen_off) {
        render_status();
    }
}

void AppCodexStatus::process_pet_transfer(
    uint32_t now_ms,
    uint32_t connection_epoch)
{
    if (!_ble_transport || !_pet_transfer) {
        return;
    }
    const codex_pet::PetSubscriptionSnapshot subscription =
        _ble_transport->pet_subscription();
    if (_pet_subscription_generation != subscription.generation) {
        _pet_transfer->on_subscription_changed(
            subscription.generation, subscription.subscribed);
        _pet_subscription_generation = subscription.generation;
    }
    if (!subscription.subscribed) {
        return;
    }

    const auto deliver = [this](
                             const codex_pet::PetTransferResult& result) {
        if (result.selection_changed) {
            _pet_selection = result.selection;
        }
        if (!result.has_status) {
            return;
        }
        const CodexBleNotifyResult notify_result =
            _ble_transport->notify_pet_status(
                result.status.data(),
                result.status_size,
                result.subscription_generation);
        _pet_transfer->record_status_delivery(
            result, notify_result == CodexBleNotifyResult::Accepted);
    };

    deliver(_pet_transfer->poll(
        now_ms,
        connection_epoch,
        _ble_transport->pet_key_epoch(),
        subscription.generation));

    const int64_t started_us = esp_timer_get_time();
    size_t handled = 0;
    CodexPetControlFrame frame{};
    while (handled < codex_pet::kPetControlsPerLoop &&
           esp_timer_get_time() - started_us <
               static_cast<int64_t>(codex_pet::kPetLoopBudgetUs) &&
           _ble_transport->take_pet_control(frame)) {
        ++handled;
        deliver(_pet_transfer->handle(
            frame,
            now_ms,
            connection_epoch,
            _ble_transport->pet_key_epoch(),
            subscription.generation));
    }
}

void AppCodexStatus::apply_pet_image()
{
    if (_pet_image == nullptr || _pet_runtime == nullptr) {
        return;
    }
    const lv_image_dsc_t* image = _pet_runtime->current_image();
    if (image == nullptr) {
        return;
    }
    lv_image_set_src(_pet_image, image);
    _pet_runtime->acknowledge_image_published();
    lv_image_set_scale(
        _pet_image,
        _pet_runtime->using_custom() ? LV_SCALE_NONE : kPetScale);
    lv_obj_set_pos(_pet_image, kPetX, kPetY);
    if (_pet_timer != nullptr) {
        lv_timer_set_period(
            _pet_timer, _pet_runtime->next_frame_delay_ms(GetHAL().millis()));
    }
}

void AppCodexStatus::sync_pet_runtime(uint32_t now_ms)
{
    if (_pet_runtime == nullptr) {
        return;
    }
    const bool selection_changed =
        !_pet_rendered_selection_valid ||
        _pet_rendered_selection.source != _pet_selection.source ||
        _pet_rendered_selection.generation != _pet_selection.generation ||
        _pet_rendered_selection.schema != _pet_selection.schema ||
        _pet_rendered_selection.slot != _pet_selection.slot ||
        _pet_rendered_selection.hash != _pet_selection.hash ||
        _pet_rendered_selection.usable != _pet_selection.usable;
    if (selection_changed) {
        (void)_pet_runtime->prepare_generation(_pet_selection);
        _pet_rendered_selection = _pet_selection;
        _pet_rendered_selection_valid = true;
    }
    const bool visible =
        _current_page == 1 && !_screen_off &&
        snapshot_task_focus_state().mode == CodexTaskFocusMode::Status;
    _pet_runtime->set_visible(visible, now_ms);
    if (_pet_runtime->take_frame_changed()) {
        apply_pet_image();
    }
}

void AppCodexStatus::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    GetGlobalEdgeControlService().setTouchExclusion(std::nullopt);
    GetGlobalDisplayProtectionService().setRightButtonWakeDeferred(false);
    _screen_off_voice_policy.reset();

    _key_manager.reset();
    _voice_button_dispatcher.reset(false);
    stop_network_fallback();
    _voice_status.store(VoiceInputUiState::Off);
    codex_pet::PetCleanupSequence::run(
        [this]() {
            if (_pet_transfer) {
                _pet_transfer->shutdown();
            }
            if (_ble_transport) {
                _ble_transport->clear_pet_credential();
            }
        },
        [this]() {
            _runtime_session.shutdown(
                _pinned_microphone_controller,
                _task_controller_mutex,
                GetHAL().millis());
        });
    _voice_input = nullptr;
    _voice_probe = nullptr;
    _input_action_controller = nullptr;
    _ble_transport = nullptr;
    _pet_transfer.reset();
    _pet_selection = {};
    _pet_rendered_selection = {};
    _pet_rendered_selection_valid = false;
    _pet_connection_epoch = 0;
    _pet_subscription_generation = 0;
    _agent_registry_store.reset();
    _agent_registry_backend.reset();
    _agent_registry_ready = false;
    _agent_switch_pending = false;
    _agent_switch_target = {};
    clear_pairing_runtime();
    {
        std::lock_guard<std::mutex> task_lock(_task_state_mutex);
        _task_focus_mode = CodexTaskFocusMode::Status;
        _task_focus_deck.reset();
        _task_detail_store.reset();
        if (_task_detail_crypto) {
            _task_detail_crypto->clear();
            _task_detail_crypto.reset();
        }
        _task_detail_key_epoch = 0;
    }
    {
        std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
        _task_voice_ready_applied = false;
    }

    LvglLockGuard lock;
    GetGlobalEdgeControlService().setEnabled(true);
    destroy_view();
    _pet_runtime.reset();
    _pet_store.reset();
    _pet_selection_store.reset();
    _pet_allocator.reset();
    _pet_files.reset();
    _alert_carousel.reset();
    _telemetry_snapshot = {};
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
    lv_obj_set_size(_page_strip, kMaxPageCount * kPageWidth, kPageWidth);
    lv_obj_set_pos(_page_strip, -466, 0);
    lv_obj_remove_flag(_page_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_page_strip, 0, 0);
    lv_obj_set_style_border_width(_page_strip, 0, 0);
    lv_obj_set_style_pad_all(_page_strip, 0, 0);
    lv_obj_set_style_bg_color(_page_strip, lv_color_hex(kInstrumentBlack), 0);

    _codex_page = lv_obj_create(_page_strip);
    lv_obj_set_size(_codex_page, 466, 466);
    lv_obj_set_pos(_codex_page, 466, 0);
    lv_obj_remove_flag(_codex_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_codex_page, 0, 0);
    lv_obj_set_style_border_width(_codex_page, 0, 0);
    lv_obj_set_style_pad_all(_codex_page, 0, 0);
    lv_obj_set_style_bg_color(_codex_page, lv_color_hex(kInstrumentBlack), 0);

    _pinned_microphone_page = lv_obj_create(_page_strip);
    lv_obj_set_size(_pinned_microphone_page, 466, 466);
    lv_obj_set_pos(_pinned_microphone_page, 0, 0);
    lv_obj_remove_flag(_pinned_microphone_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_pinned_microphone_page, 0, 0);
    lv_obj_set_style_border_width(_pinned_microphone_page, 0, 0);
    lv_obj_set_style_pad_all(_pinned_microphone_page, 0, 0);
    lv_obj_set_style_bg_color(
        _pinned_microphone_page, lv_color_hex(kInstrumentBlack), 0);

    _usage_page_root = make_status_page_root(_page_strip, 2 * kPageWidth);
    if (kHostPageEnabled) {
        _host_page_root = make_status_page_root(_page_strip, 3 * kPageWidth);
    }
    _trae_page_root = make_status_page_root(_page_strip, 4 * kPageWidth);
    _trae_status_page.init(_trae_page_root);

    _pending_glow = std::make_unique<CodexPendingSoftGlow>();
    _pending_glow->init(_codex_page);

    lv_obj_t* logo = lv_image_create(_codex_page);
    lv_image_set_src(logo, &logo_codex);
    lv_image_set_scale(logo, kLogoScale);
    lv_obj_set_pos(logo, kLogoX, kLogoY);

    make_label(_codex_page, "Codex", kBrandX, kBrandY, &MontserratSemiBold26, kSignalWhite);

    make_frame_segment(
        _codex_page,
        kBeaconSpine.x,
        kBeaconSpine.y,
        kBeaconSpine.w,
        kBeaconSpine.h,
        kBeaconViolet
    );

    lv_obj_t* agent_caption = make_telemetry_label(
        _codex_page,
        kTelemetryX,
        kStatusCaptionY,
        kTelemetryWidth,
        "AGENT",
        &lv_font_montserrat_12,
        kEtchedGray
    );
    lv_obj_set_style_text_letter_space(agent_caption, 2, 0);
    lv_obj_set_style_text_align(agent_caption, LV_TEXT_ALIGN_CENTER, 0);
    _agent_value = make_telemetry_label(
        _codex_page,
        kAgentStatusX,
        kStatusValueY,
        kAgentStatusWidth,
        "--",
        &lv_font_montserrat_14,
        kSignalWhite
    );
    lv_obj_set_style_text_align(_agent_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_height(_agent_value, kStatusValueHeight);
    lv_label_set_long_mode(_agent_value, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t* voice_caption = make_telemetry_label(
        _codex_page,
        kVoiceCaptionX,
        kStatusCaptionY,
        kVoiceColumnWidth,
        "VOICE",
        &lv_font_montserrat_12,
        kEtchedGray
    );
    lv_obj_set_style_text_letter_space(voice_caption, 2, 0);
    lv_obj_set_style_text_align(voice_caption, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* voice_status_row = lv_obj_create(_codex_page);
    lv_obj_remove_style_all(voice_status_row);
    lv_obj_set_pos(voice_status_row, kVoiceCaptionX, kStatusValueY);
    lv_obj_set_size(voice_status_row, kVoiceColumnWidth, kStatusValueHeight);
    lv_obj_remove_flag(voice_status_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(voice_status_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(voice_status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        voice_status_row, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(voice_status_row, kVoiceDotGap, 0);
    _voice_status_dot = make_channel_dot(
        voice_status_row, 0, 0, kVoiceDotSize);
    _voice_value = make_telemetry_label(
        voice_status_row,
        0,
        0,
        LV_SIZE_CONTENT,
        "--",
        &lv_font_montserrat_14,
        kSignalWhite
    );
    lv_obj_set_style_max_width(_voice_value, kVoiceStatusWidth, 0);
    lv_obj_set_style_text_align(_voice_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_height(_voice_value, kStatusValueHeight);

    lv_obj_t* model_caption = make_telemetry_label(
        _codex_page,
        kTelemetryX,
        kModelCaptionY,
        kTelemetryWidth,
        "MODEL",
        &lv_font_montserrat_14,
        kEtchedGray);
    lv_obj_set_style_text_letter_space(model_caption, 3, 0);
    _model_value = make_telemetry_label(
        _codex_page,
        kTelemetryX,
        kModelValueY,
        kTelemetryWidth,
        "--",
        &MontserratSemiBold26,
        kSignalWhite);
    lv_label_set_long_mode(_model_value, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(
        _model_value, kLongLabelScrollDurationMs, LV_PART_MAIN);
    lv_obj_set_size(_model_value, kTelemetryWidth, kLargeValueHeight);

    lv_obj_t* effort_caption = make_telemetry_label(
        _codex_page,
        kTelemetryX,
        kEffortCaptionY,
        kTelemetryWidth,
        "EFFORT",
        &lv_font_montserrat_14,
        kEtchedGray);
    lv_obj_set_style_text_letter_space(effort_caption, 3, 0);
    _effort_value = make_telemetry_label(
        _codex_page,
        kTelemetryX,
        kEffortValueY,
        kTelemetryWidth,
        "--",
        &MontserratSemiBold26,
        kSignalWhite);
    lv_label_set_long_mode(_effort_value, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(
        _effort_value, kLongLabelScrollDurationMs, LV_PART_MAIN);
    lv_obj_set_size(_effort_value, kTelemetryWidth, kLargeValueHeight);

    _pet_image = lv_image_create(_codex_page);
    lv_image_set_antialias(_pet_image, false);
    lv_image_set_pivot(_pet_image, 0, 0);
    lv_obj_clear_flag(_pet_image, LV_OBJ_FLAG_CLICKABLE);
    sync_pet_runtime(GetHAL().millis());

    if (_pet_timer == nullptr) {
        const uint32_t period = _pet_runtime
            ? _pet_runtime->next_frame_delay_ms(GetHAL().millis())
            : codex_pet_idle_durations_ms[0];
        _pet_timer = lv_timer_create(AppCodexStatus::pet_timer_cb, period, this);
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
        &MontserratBold22,
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
        &MontserratBold22,
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
        kProjectNameX,
        kProjectNameY,
        kProjectNameWidth,
        &MontserratSemiBold26,
        kBeaconViolet,
        LV_TEXT_ALIGN_CENTER
    );
    lv_obj_set_height(_alert_project, kProjectNameHeight);
    lv_label_set_long_mode(
        _alert_project, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(
        _alert_project, kLongLabelScrollDurationMs, LV_PART_MAIN);
    lv_obj_set_size(
        _alert_project, kProjectNameWidth, kProjectNameHeight);
    _alert_project_font_valid = true;
    _alert_project_non_ascii = false;
    _pending_glow->set_status_targets(
        _pending_label,
        _pending_count,
        _running_label,
        _running_count
    );

    _usage_status_page.init(_usage_page_root);
    if (kHostPageEnabled) {
        _host_status_page.init(_host_page_root);
    }
    render_telemetry_pages();
    layout_status_pages();

    make_center_label(
        _pinned_microphone_page,
        "固定麦克风",
        48,
        &PinnedMicTitle24,
        kSignalWhite);

    _pinned_microphone_connection_dot = lv_obj_create(_pinned_microphone_page);
    lv_obj_set_size(_pinned_microphone_connection_dot, 6, 6);
    lv_obj_set_pos(_pinned_microphone_connection_dot, 163, 90);
    lv_obj_set_style_radius(
        _pinned_microphone_connection_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_pinned_microphone_connection_dot, 0, 0);
    lv_obj_set_style_pad_all(_pinned_microphone_connection_dot, 0, 0);
    lv_obj_set_style_bg_color(
        _pinned_microphone_connection_dot, lv_color_hex(kQuotaMint), 0);
    lv_obj_clear_flag(
        _pinned_microphone_connection_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(
        _pinned_microphone_connection_dot, LV_OBJ_FLAG_SCROLLABLE);
    _pinned_microphone_connection_label = make_center_label(
        _pinned_microphone_page,
        "已连接到 Mac",
        84,
        &font_puhui_14_1,
        kEtchedGray);
    lv_obj_set_x(_pinned_microphone_connection_label, 8);

    _pinned_microphone_touch_target = lv_obj_create(_pinned_microphone_page);
    lv_obj_set_size(_pinned_microphone_touch_target, 232, 194);
    lv_obj_set_pos(_pinned_microphone_touch_target, 117, 122);
    lv_obj_set_style_radius(
        _pinned_microphone_touch_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(_pinned_microphone_touch_target, 0, 0);
    lv_obj_set_style_border_width(
        _pinned_microphone_touch_target, 0, 0);
    lv_obj_set_style_bg_opa(
        _pinned_microphone_touch_target, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(
        _pinned_microphone_touch_target,
        AppCodexStatus::pinned_microphone_touch_event_cb,
        LV_EVENT_CLICKED,
        this);

    _pinned_microphone_icon = lv_image_create(_pinned_microphone_page);
    lv_image_set_src(_pinned_microphone_icon, &pinned_microphone_d);
    lv_obj_set_pos(_pinned_microphone_icon, 185, 145);
    lv_obj_set_style_image_recolor(
        _pinned_microphone_icon, lv_color_hex(kEtchedGray), 0);
    lv_obj_set_style_image_recolor_opa(
        _pinned_microphone_icon, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_pinned_microphone_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_pinned_microphone_icon, LV_OBJ_FLAG_CLICKABLE);

    constexpr std::array<int, 4> kLevelLeftX = {120, 134, 148, 162};
    constexpr std::array<int, 4> kLevelRightX = {340, 326, 312, 298};
    constexpr std::array<int, 4> kLevelHeights = {12, 25, 43, 63};
    for (std::size_t index = 0; index < _pinned_microphone_level_left.size(); ++index) {
        const int height = kLevelHeights[index];
        const int y = 214 - height / 2;
        _pinned_microphone_level_left[index] = make_frame_segment(
            _pinned_microphone_page,
            kLevelLeftX[index],
            y,
            6,
            height,
            kEtchedGray);
        _pinned_microphone_level_right[index] = make_frame_segment(
            _pinned_microphone_page,
            kLevelRightX[index],
            y,
            6,
            height,
            kEtchedGray);
        lv_obj_set_style_radius(
            _pinned_microphone_level_left[index], 3, 0);
        lv_obj_set_style_radius(
            _pinned_microphone_level_right[index], 3, 0);
        lv_obj_add_flag(
            _pinned_microphone_level_left[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(
            _pinned_microphone_level_right[index], LV_OBJ_FLAG_HIDDEN);
    }
    _pinned_microphone_prompt_label = make_center_label(
        _pinned_microphone_page,
        "轻触开启",
        333,
        &PinnedMicState22,
        kSignalWhite);
    render_pinned_microphone();

    for (lv_obj_t*& dot : _page_dots) {
        dot = make_page_dot(_panel, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
    update_page_indicator();

    _battery_label = lv_label_create(_panel);
    lv_obj_set_style_text_font(_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_battery_label, 150);
    lv_obj_set_style_text_align(_battery_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_battery_label, LV_ALIGN_TOP_MID, 0, kBatteryY);
    update_battery();

    _task_focus_view = std::make_unique<CodexTaskFocusView>();
    _task_focus_view->init(_panel);
    _task_focus_view->set_visible(false);

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
        _agent_panel_root,
        lv_color_hex(agent_panel_ui::kBackground),
        0);
    lv_obj_set_style_bg_opa(_agent_panel_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_agent_panel_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(_agent_panel_root);
    render_agent_panel();
}

void AppCodexStatus::destroy_view()
{
    GetGlobalEdgeControlService().setTouchExclusion(std::nullopt);
    lv_anim_delete(this, page_animation_cb);
    lv_anim_delete(this, agent_panel_animation_cb);
    lv_anim_delete(this, task_deck_animation_cb);
    _page_swipe.cancel();
    cancel_task_focus_touch();
    _last_page_touch_state = LV_INDEV_STATE_REL;
    _last_pinned_microphone_render_state = -1;
    _last_pinned_microphone_meter_visible = false;
    _last_pinned_microphone_meter_heights = {};

    if (_pending_glow) {
        _pending_glow->shutdown();
        _pending_glow.reset();
    }
    _usage_status_page.shutdown();
    _host_status_page.shutdown();
    _trae_status_page.shutdown();
    if (_task_focus_view) {
        _task_focus_view->shutdown();
        _task_focus_view.reset();
    }

    if (_pet_timer != nullptr) {
        lv_timer_delete(_pet_timer);
        _pet_timer = nullptr;
    }
    if (_pet_runtime) {
        _pet_runtime->close();
    }

    if (_panel != nullptr) {
        lv_obj_delete(_panel);
        _panel = nullptr;
    }

    _agent_value    = nullptr;
    _model_value    = nullptr;
    _effort_value   = nullptr;
    _voice_status_dot = nullptr;
    _page_strip     = nullptr;
    _codex_page     = nullptr;
    _pinned_microphone_page = nullptr;
    _usage_page_root = nullptr;
    _host_page_root = nullptr;
    _trae_page_root = nullptr;
    _page_dots = {};
    _page_indicator_count = 0;
    _page_indicator_slot = -1;
    _pinned_microphone_touch_target = nullptr;
    _pinned_microphone_connection_dot = nullptr;
    _pinned_microphone_connection_label = nullptr;
    _pinned_microphone_icon = nullptr;
    _pinned_microphone_level_left = {};
    _pinned_microphone_level_right = {};
    _pinned_microphone_prompt_label = nullptr;
    _voice_value    = nullptr;
    _week_pct       = nullptr;
    _week_bar       = nullptr;
    _pending_label  = nullptr;
    _pending_count  = nullptr;
    _running_label  = nullptr;
    _running_count  = nullptr;
    _task_index     = nullptr;
    _alert_project  = nullptr;
    _alert_style_valid = false;
    _alert_project_font_valid = false;
    _alert_project_non_ascii = false;
    _battery_label  = nullptr;
    _host_label     = nullptr;
    _pet_image      = nullptr;
    _agent_panel_root = nullptr;
    _agent_panel_hint = nullptr;
    _agent_rail = nullptr;
    _agent_focus_band_left = nullptr;
    _agent_focus_band_right = nullptr;
    _agent_focus_marker = nullptr;
    _agent_count_label = nullptr;
    _agent_panel_notice_label = nullptr;
    _agent_pairing_countdown_label = nullptr;
    _agent_reset_progress = nullptr;
    _agent_pairing_countdown_label = nullptr;
    _agent_reset_progress = nullptr;
    _agent_row_wrappers = {};
    _agent_row_buttons = {};
    _agent_delete_buttons = {};
    _agent_row_names = {};
    _agent_row_states = {};
    _agent_row_ids = {};
    _agent_row_times = {};
    _agent_row_dots = {};
    _agent_manage_button = nullptr;
    _agent_manage_done_button = nullptr;
    _agent_allow_button = nullptr;
    _agent_allow_switch_button = nullptr;
    _agent_reject_button = nullptr;
    _agent_dismiss_button = nullptr;
    _agent_reset_button = nullptr;
    _agent_confirm_button = nullptr;
    _agent_cancel_button = nullptr;
    _agent_revealed_delete_index = -1;
    _agent_reset_hold_started_ms = 0;
    _agent_panel_notice_until_ms = 0;
    _agent_reset_hold_active = false;
    _agent_reset_hold_queued = false;
    _agent_pairing_expired = false;
    _agent_panel_manage_mode = false;
    _agent_panel_notice = {};
}

void AppCodexStatus::render_pinned_microphone()
{
    if (_pinned_microphone_touch_target == nullptr ||
        _pinned_microphone_connection_dot == nullptr ||
        _pinned_microphone_connection_label == nullptr ||
        _pinned_microphone_prompt_label == nullptr) {
        return;
    }

    PinnedMicrophoneState state = PinnedMicrophoneState::Unavailable;
    {
        std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
        state = _pinned_microphone_controller.state();
    }
    const bool starting = state == PinnedMicrophoneState::Starting;
    const bool active = state == PinnedMicrophoneState::Active;
    const bool stopping = state == PinnedMicrophoneState::Stopping;
    const bool unavailable = state == PinnedMicrophoneState::Unavailable;
    const bool error = state == PinnedMicrophoneState::Error;
    const bool bluetooth_connected =
        _ble_transport != nullptr && _ble_transport->connected();
    const PinnedMicrophoneStatusText status_text =
        pinned_microphone_status_text(bluetooth_connected, state);
    uint32_t prompt_color = kSignalWhite;
    uint32_t icon_color = kEtchedGray;
    if (active) {
        prompt_color = kQuotaMint;
        icon_color = kRunningCyan;
    } else if (starting) {
        prompt_color = kBeaconViolet;
        icon_color = kBeaconViolet;
    } else if (stopping) {
        prompt_color = kEtchedGray;
    } else if (unavailable) {
        prompt_color = kEtchedGray;
    } else if (error) {
        prompt_color = kAlertVermilion;
        icon_color = kAlertVermilion;
    }
    const int render_state =
        (static_cast<int>(state) << 1) | (bluetooth_connected ? 1 : 0);
    if (render_state != _last_pinned_microphone_render_state) {
        _last_pinned_microphone_render_state = render_state;

        lv_label_set_text(
            _pinned_microphone_connection_label, status_text.connection);
        lv_label_set_text(_pinned_microphone_prompt_label, status_text.prompt);
        lv_obj_set_style_text_color(
            _pinned_microphone_prompt_label, lv_color_hex(prompt_color), 0);
        lv_obj_set_style_bg_color(
            _pinned_microphone_connection_dot,
            lv_color_hex(
                error
                    ? kAlertVermilion
                    : (starting
                           ? kBeaconViolet
                           : (unavailable ? kEtchedGray : kQuotaMint))),
            0);
        if (_pinned_microphone_icon != nullptr) {
            lv_obj_set_style_image_recolor(
                _pinned_microphone_icon, lv_color_hex(icon_color), 0);
        }
        for (std::size_t index = 0;
             index < _pinned_microphone_level_left.size();
             ++index) {
            if (_pinned_microphone_level_left[index] != nullptr) {
                lv_obj_set_style_bg_color(
                    _pinned_microphone_level_left[index],
                    lv_color_hex(icon_color),
                    0);
            }
            if (_pinned_microphone_level_right[index] != nullptr) {
                lv_obj_set_style_bg_color(
                    _pinned_microphone_level_right[index],
                    lv_color_hex(icon_color),
                    0);
            }
        }
    }

    PinnedMicrophoneInputLevels levels{};
    if (active) {
        levels = GetStopWatchAudioBackend().inputLevels();
    }
    const PinnedMicrophoneMeterFrame meter =
        pinned_microphone_meter_frame(active, levels);
    if (meter.visible != _last_pinned_microphone_meter_visible) {
        _last_pinned_microphone_meter_visible = meter.visible;
        for (std::size_t index = 0;
             index < _pinned_microphone_level_left.size();
             ++index) {
            for (lv_obj_t* segment : {
                     _pinned_microphone_level_left[index],
                     _pinned_microphone_level_right[index],
                 }) {
                if (segment == nullptr) {
                    continue;
                }
                if (meter.visible) {
                    lv_obj_clear_flag(segment, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(segment, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
    if (!meter.visible) {
        _last_pinned_microphone_meter_heights = {};
        return;
    }
    if (meter.heights == _last_pinned_microphone_meter_heights) {
        return;
    }
    _last_pinned_microphone_meter_heights = meter.heights;
    for (std::size_t index = 0; index < meter.heights.size(); ++index) {
        const int height = meter.heights[index];
        const int y = 214 - height / 2;
        for (lv_obj_t* segment : {
                 _pinned_microphone_level_left[index],
                 _pinned_microphone_level_right[index],
             }) {
            if (segment != nullptr) {
                lv_obj_set_size(segment, 6, height);
                lv_obj_set_y(segment, y);
            }
        }
    }
}

void AppCodexStatus::pinned_microphone_touch_event_cb(lv_event_t* event)
{
    auto* self = static_cast<AppCodexStatus*>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    if (self == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> controller_lock(
            self->_task_controller_mutex);
        if (self->_voice_input == nullptr ||
            self->_pinned_microphone_touch_target == nullptr ||
            self->_pinned_microphone_controller.state() ==
                PinnedMicrophoneState::Starting ||
            self->_pinned_microphone_controller.state() ==
                PinnedMicrophoneState::Stopping) {
            return;
        }
        const bool enabled =
            !self->_pinned_microphone_controller.enabled();
        const bool voice_ready =
            self->_production_voice_link_usable &&
            (self->_production_voice_capabilities &
             voice_input::kExternalMicrophoneCapability) != 0;
        (void)self->_pinned_microphone_controller.set_enabled(
            enabled,
            voice_ready,
            *self->_voice_input,
            GetHAL().millis());
    }
    self->render_pinned_microphone();
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
    } else if (percent < LowBatteryAlertLogic::lowThreshold) {
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
    if (!_screen_off) {
        render_status();
    }
}

void AppCodexStatus::render_status()
{
    const CodexStatusChannels channels = current_status_channels();
    const char* agent_text = channels.connection.text;
    if (std::strcmp(channels.connection.text, "CONNECTED") == 0 &&
        _ble_transport != nullptr) {
        const secure_pairing::AgentRecord* active_agent =
            _agent_registry.find(_ble_transport->active_fingerprint());
        if (active_agent != nullptr && active_agent->display_name[0] != '\0') {
            agent_text = active_agent->display_name.data();
        }
    }
    LvglLockGuard lock;
    apply_agent_presentation(
        _agent_value,
        agent_text,
        channels.connection.text_color,
        _agent_status_text_color,
        _agent_status_style_valid);
    apply_channel_presentation(
        _voice_status_dot,
        _voice_value,
        channels.voice,
        _voice_status_text_color,
        _voice_status_dot_color,
        _voice_status_filled,
        _voice_status_style_valid);
    if (_current_page == 4) {
        render_trae_status_page(channels);
    }
}

CodexStatusChannels AppCodexStatus::current_status_channels()
{
    std::string base_status;
    bool base_is_error = false;
    {
        std::lock_guard<std::mutex> lock(_status_mutex);
        base_status = _base_status;
        base_is_error = _base_status_is_error;
    }
    const VoiceInputUiState voice_state = _voice_status.load();
    return resolve_codex_status_channels(
        base_status.c_str(),
        base_is_error,
        voice_state == VoiceInputUiState::Ready,
        voice_state == VoiceInputUiState::Starting,
        voice_state == VoiceInputUiState::Active,
        voice_state == VoiceInputUiState::Error,
        CodexStatusPalette{
            kQuotaMint,
            kRunningCyan,
            kBeaconViolet,
            kCodexRed,
            kTextDim,
            kSignalWhite,
        });
}

void AppCodexStatus::render_trae_status_page(
    const CodexStatusChannels& channels)
{
    if (_trae_page_root == nullptr || _current_page != 4 || _screen_off) {
        return;
    }

    TraeChannelPresentation trae_channels{
        {
            channels.connection.text,
            channels.connection.dot_color,
            channels.connection.text_color,
        },
        {
            channels.voice.text,
            channels.voice.dot_color,
            channels.voice.text_color,
        },
    };
    const uint32_t now = GetHAL().millis();
    const bool stale = !_trae_status_received ||
        now - _trae_status_snapshot.received_at_ms > kStatusStaleMs;
    const bool bluetooth_connected =
        _ble_transport != nullptr && _ble_transport->connected();
    const TraeTask* current = _trae_task_carousel.current();
    const TraeTask* next = _trae_task_carousel.next();
    std::array<char, 33> current_id{};
    std::array<char, 33> next_id{};
    copy_task_id(current_id, current);
    copy_task_id(next_id, next);

    if (_trae_page_rendered &&
        trae_snapshot_equal(_trae_status_snapshot, _trae_rendered_snapshot) &&
        trae_channels_equal(trae_channels, _trae_rendered_channels) &&
        stale == _trae_rendered_stale &&
        bluetooth_connected == _trae_rendered_ble_connected &&
        current_id == _trae_rendered_current_task &&
        next_id == _trae_rendered_next_task) {
        return;
    }

    _trae_status_page.render(
        _trae_status_snapshot,
        current,
        next,
        trae_channels,
        stale,
        bluetooth_connected);
    _trae_rendered_snapshot = _trae_status_snapshot;
    _trae_rendered_channels = trae_channels;
    _trae_rendered_stale = stale;
    _trae_rendered_ble_connected = bluetooth_connected;
    _trae_rendered_current_task = current_id;
    _trae_rendered_next_task = next_id;
    _trae_page_rendered = true;
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
    return length > 0 && handle_status_payload(
        data, static_cast<size_t>(length), CodexStatusEncoding::Json, live_status);
}

bool AppCodexStatus::handle_status_payload(
    const char* data,
    size_t length,
    CodexStatusEncoding encoding,
    const char* live_status)
{
    CodexDecodedStatusPayload decoded;
    if (!decode_codex_status_payload(data, length, encoding, decoded)) {
        return false;
    }
    cJSON* root = decoded.root.get();
    const StatusMessageSource source = route_status_object(root);
    if (source == StatusMessageSource::Unknown) {
        return false;
    }
    // Detail envelopes have exactly nine fields and no source discriminator.
    // Never route malformed native envelopes through inventory/TRAE fallbacks.
    if (decoded.has_native_detail && source != StatusMessageSource::LegacyCodex) {
        return false;
    }
    if (source == StatusMessageSource::AgentInventory) {
        AgentInventorySnapshot inventory;
        if (!parse_agent_inventory_object(root, inventory)) {
            return false;
        }
        LvglLockGuard lock;
        apply_agent_inventory(inventory);
        return true;
    }
    if (source == StatusMessageSource::Trae) {
        TraeStatusSnapshot snapshot;
        const uint32_t now = GetHAL().millis();
        if (!parse_trae_status_object(root, now, snapshot)) {
            return false;
        }
        {
            LvglLockGuard lock;
            if (!_agent_inventory_received || !_inventory_trae_available) {
                return false;
            }
            _trae_task_carousel.apply(snapshot, now);
            _trae_status_snapshot = snapshot;
            _trae_status_received = true;
            if (_current_page == 4) {
                render_trae_status_page(current_status_channels());
            }
        }
        return true;
    }

    CodexStatusSnapshot snapshot;
    const CodexMessageKind kind = parse_codex_message_object(
        root, snapshot, decoded.has_native_detail ? &decoded.native_detail : nullptr);
    if (kind == CodexMessageKind::Invalid) {
        if (decoded.has_native_detail) {
            return false;
        }
        return apply_telemetry_object(root, false);
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
    if (kind == CodexMessageKind::TaskDetail) {
        const bool credential_cleared_detail =
            sync_task_detail_credential();
        CodexTaskDetailChunk chunk;
        bool accepted = false;
        bool render_detail = false;
        {
            std::lock_guard<std::mutex> task_lock(_task_state_mutex);
            accepted = _agent_registry_ready &&
                _task_detail_crypto != nullptr &&
                _task_detail_crypto->ready() &&
                decrypt_codex_task_detail(
                    snapshot.task_detail,
                    _task_detail_key_epoch,
                    *_task_detail_crypto,
                    chunk) &&
                _task_detail_store.accept(chunk);
            render_detail =
                _task_focus_mode == CodexTaskFocusMode::TaskDetail &&
                ((credential_cleared_detail) ||
                 (accepted &&
                  chunk.route == _task_focus_deck.selected_route() &&
                  _task_detail_store.find(chunk.route) != nullptr));
        }
        if (credential_cleared_detail) {
            std::lock_guard<std::mutex> controller_lock(
                _task_controller_mutex);
            _task_voice_ready_applied = false;
        }
        if (render_detail) {
            LvglLockGuard lock;
            render_task_focus_detail();
        }
        if (!accepted) {
            mclog::tagWarn(getAppInfo().name, "task detail rejected");
            return false;
        }
        return true;
    }

    apply_telemetry_object(root, false);

    bool task_list_changed = false;
    {
        std::lock_guard<std::mutex> task_lock(_task_state_mutex);
        const uint64_t previous_selected_route =
            _task_focus_deck.selected_route();
        task_list_changed = _task_focus_deck.apply(snapshot);
        if (task_list_changed) {
            std::vector<uint64_t> routes;
            routes.reserve(_task_focus_deck.count());
            for (const CodexTaskAlert& task : _task_focus_deck.tasks()) {
                routes.push_back(task.route);
            }
            _task_detail_store.retain_routes(routes);
            if (_task_focus_mode == CodexTaskFocusMode::TaskDetail &&
                previous_selected_route != 0 &&
                previous_selected_route != _task_focus_deck.selected_route()) {
                _task_focus_mode = CodexTaskFocusMode::TaskDeck;
            }
        }
    }
    {
        LvglLockGuard lock;
        set_percent(_week_pct, _week_bar, snapshot.week_left_pct);
        if (_current_page == 2 || _current_page == 3) {
            render_telemetry_pages();
        }
        bool global_model_changed = false;
        bool global_effort_changed = false;
        if (!snapshot.model.empty() &&
            _latest_global_model != snapshot.model) {
            _latest_global_model = snapshot.model;
            global_model_changed = true;
        }
        if (!snapshot.effort.empty() &&
            _latest_global_effort != snapshot.effort) {
            _latest_global_effort = snapshot.effort;
            global_effort_changed = true;
        }

        const bool alert_changed =
            _alert_carousel.apply(snapshot, GetHAL().millis());
        if (_current_page == 1 &&
            (global_model_changed || global_effort_changed || alert_changed)) {
            render_alert();
        }
    }
    if (task_list_changed) {
        sync_task_voice_route();
        sync_task_edge_exclusion(snapshot_task_focus_state().mode);
        LvglLockGuard lock;
        render_task_focus();
    }
    _last_status_payload_ms = GetHAL().millis();
    set_status(live_status, kCodexBlue);
    return true;
}

void AppCodexStatus::render_alert()
{
    if (_pending_count == nullptr || _running_count == nullptr ||
        _task_index == nullptr || _alert_project == nullptr) {
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
    set_label_text_if_changed(_pending_count, pending_text);
    set_label_text_if_changed(_running_count, running_text);
    const uint32_t pending_color = _alert_carousel.pending_count() > 0
        ? kAlertVermilion
        : kEtchedGray;
    const uint32_t running_color = _alert_carousel.running_count() > 0
        ? kRunningCyan
        : kEtchedGray;
    if (!_alert_style_valid || _pending_count_color != pending_color) {
        lv_obj_set_style_text_color(
            _pending_count, lv_color_hex(pending_color), 0);
        _pending_count_color = pending_color;
    }
    if (!_alert_style_valid || _running_count_color != running_color) {
        lv_obj_set_style_text_color(
            _running_count, lv_color_hex(running_color), 0);
        _running_count_color = running_color;
    }

    const std::string heading = _alert_carousel.heading();
    const CodexTaskAlert* alert = _alert_carousel.current();
    set_label_text_if_changed(_task_index, heading.c_str());
    const char* model = alert != nullptr && !alert->model.empty()
        ? alert->model.c_str()
        : (_latest_global_model.empty() ? "--" : _latest_global_model.c_str());
    const char* effort = alert != nullptr && !alert->effort.empty()
        ? alert->effort.c_str()
        : (_latest_global_effort.empty()
               ? "--"
               : _latest_global_effort.c_str());
    set_label_text_if_changed(_model_value, model);
    set_label_text_if_changed(_effort_value, effort);
    const char* project = alert == nullptr ? "--" : alert->project.c_str();
    const bool project_non_ascii = !is_ascii_text(project);
    const char* current_project = lv_label_get_text(_alert_project);
    const bool project_changed = current_project == nullptr ||
        std::strcmp(current_project, project) != 0;
    if (project_changed) {
        lv_label_set_text(_alert_project, project);
        if (!_alert_project_font_valid ||
            project_non_ascii != _alert_project_non_ascii) {
            lv_obj_set_style_text_font(
                _alert_project,
                project_non_ascii ? &CodexTaskTitle21 : &MontserratSemiBold26,
                0);
        }
        _alert_project_non_ascii = project_non_ascii;
        _alert_project_font_valid = true;
    }
    uint32_t state_color = kCompletedGreen;
    if (alert == nullptr) {
        state_color = kTextDim;
    } else if (alert->state == CodexTaskState::Pending) {
        state_color = kCodexRed;
    } else if (alert->state == CodexTaskState::Running) {
        state_color = kRunningCyan;
    }
    if (!_alert_style_valid || _task_index_color != state_color) {
        lv_obj_set_style_text_color(
            _task_index, lv_color_hex(state_color), 0);
        _task_index_color = state_color;
    }
    _alert_style_valid = true;
    sync_pending_glow();
}

void AppCodexStatus::render_telemetry_pages()
{
    if (_screen_off || (_current_page != 2 &&
        !(kHostPageEnabled && _current_page == 3))) {
        return;
    }
    const int64_t now_ms = current_unix_milliseconds();
    const int64_t now_epoch_seconds = now_ms > 0 ? now_ms / 1000 : 0;
    expire_codex_telemetry(_telemetry_snapshot, now_epoch_seconds);
    if (_current_page == 2) {
        _usage_status_page.render(
            _telemetry_snapshot.usage,
            now_epoch_seconds);
    } else {
        _host_status_page.render(
            _telemetry_snapshot.host,
            now_epoch_seconds);
    }
}

int AppCodexStatus::page_slot_for_logical(int page) const
{
    for (size_t slot = 0; slot < _active_page_count; ++slot) {
        if (_active_page_order[slot] == page) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

bool AppCodexStatus::page_is_available(int page) const
{
    return page_slot_for_logical(page) >= 0;
}

void AppCodexStatus::layout_status_pages()
{
    _active_page_order.fill(-1);
    size_t count = 0;
    _active_page_order[count++] = 0;
    const bool codex_available = !_agent_inventory_received ||
        _inventory_codex_available;
    if (codex_available) {
        _active_page_order[count++] = 1;
    }
    if (_agent_inventory_received && _inventory_trae_available) {
        _active_page_order[count++] = 4;
    }
    _active_page_order[count++] = 2;
    if (kHostPageEnabled) {
        _active_page_order[count++] = 3;
    }
    _active_page_count = count;

    if (_page_strip != nullptr) {
        lv_obj_set_width(
            _page_strip,
            static_cast<int>(_active_page_count) * kPageWidth);
    }
    const auto place = [this](lv_obj_t* page, int logical_page) {
        if (page == nullptr) {
            return;
        }
        const int slot = page_slot_for_logical(logical_page);
        if (slot < 0) {
            if (!lv_obj_has_flag(page, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }
        if (lv_obj_has_flag(page, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
        }
        const int x = slot * kPageWidth;
        if (lv_obj_get_x(page) != x) {
            lv_obj_set_x(page, x);
        }
    };
    place(_pinned_microphone_page, 0);
    place(_codex_page, 1);
    place(_trae_page_root, 4);
    place(_usage_page_root, 2);
    place(_host_page_root, 3);

    _trae_status_page.set_active(_current_page == 4);
    _page_indicator_count = 0;
    _page_indicator_slot = -1;
    update_page_indicator();
}

void AppCodexStatus::apply_agent_inventory(
    const AgentInventorySnapshot& inventory)
{
    const bool availability_changed =
        inventory.codex != _inventory_codex_available ||
        inventory.trae != _inventory_trae_available;
    _agent_inventory_received = true;
    if (!availability_changed) {
        return;
    }

    const int previous_page = _current_page;
    _inventory_codex_available = inventory.codex;
    _inventory_trae_available = inventory.trae;
    if (!_inventory_trae_available) {
        _trae_status_received = false;
        _trae_status_snapshot = {};
        _trae_task_carousel.reset();
        _trae_page_rendered = false;
    }
    rebuild_status_page_layout(previous_page);
}

void AppCodexStatus::reset_agent_inventory()
{
    const int previous_page = _current_page;
    const bool availability_changed =
        !_inventory_codex_available || _inventory_trae_available;
    _agent_inventory_received = false;
    _inventory_codex_available = true;
    _inventory_trae_available = false;
    _trae_status_received = false;
    _trae_status_snapshot = {};
    _trae_task_carousel.reset();
    _trae_page_rendered = false;
    if (availability_changed) {
        rebuild_status_page_layout(previous_page);
    }
}

void AppCodexStatus::rebuild_status_page_layout(int previous_page)
{
    lv_anim_delete(this, page_animation_cb);
    _page_swipe.cancel();
    cancel_task_focus_touch();
    _last_page_touch_state = LV_INDEV_STATE_REL;
    if (_task_deck_drag_active) {
        settle_task_deck_entry(false, false);
    }

    layout_status_pages();
    const bool page_removed = !page_is_available(previous_page);
    if (page_removed && previous_page == 1) {
        lv_anim_delete(this, task_deck_animation_cb);
        {
            std::lock_guard<std::mutex> task_lock(_task_state_mutex);
            _task_focus_mode = CodexTaskFocusMode::Status;
        }
        _task_deck_drag_active = false;
        _task_deck_auto_dismiss_latched = false;
        if (_task_focus_view != nullptr) {
            _task_focus_view->set_vertical_offset(kTaskDeckHiddenY);
            _task_focus_view->set_visible(false);
        }
        sync_task_edge_exclusion(CodexTaskFocusMode::Status);
        sync_task_voice_route();
    }

    if (page_removed) {
        if (page_is_available(1)) {
            _current_page = 1;
        } else if (page_is_available(4)) {
            _current_page = 4;
        } else {
            _current_page = 2;
        }
        _pinned_microphone_controller.page_changed(_current_page);
    }

    const int slot = page_slot_for_logical(_current_page);
    if (_page_strip != nullptr && slot >= 0) {
        lv_obj_set_x(_page_strip, -slot * kPageWidth);
    }
    _trae_status_page.set_active(_current_page == 4);
    update_page_indicator();
    sync_pending_glow();
    sync_pet_runtime(GetHAL().millis());
    (void)sync_agent_panel_for_task_mode(snapshot_task_focus_state().mode);
    if (_current_page == 1) {
        render_alert();
    } else if (_current_page == 2 || _current_page == 3) {
        render_telemetry_pages();
    } else if (_current_page == 4) {
        render_trae_status_page(current_status_channels());
    }
}


void AppCodexStatus::clear_telemetry()
{
    LvglLockGuard lock;
    _telemetry_snapshot = {};
    _latest_global_model.clear();
    _latest_global_effort.clear();
    if (_current_page == 1) {
        render_alert();
    }
    set_percent(_week_pct, _week_bar, -1);
    render_telemetry_pages();
}


bool AppCodexStatus::apply_telemetry_object(
    cJSON* root,
    bool clear_when_absent)
{
    CodexTelemetrySnapshot parsed;
    const CodexTelemetryParseResult result = parse_codex_telemetry_object(root, parsed);
    if (result == CodexTelemetryParseResult::NoTelemetry) {
        if (clear_when_absent) {
            clear_telemetry();
            return true;
        }
        return false;
    }
    if (result != CodexTelemetryParseResult::Parsed) {
        return false;
    }

    LvglLockGuard lock;
    _telemetry_snapshot.version_supported = true;
    if (parsed.usage.present) {
        _telemetry_snapshot.usage = parsed.usage;
    }
    if (parsed.host.present) {
        _telemetry_snapshot.host = parsed.host;
    }
    render_telemetry_pages();
    return true;
}

void AppCodexStatus::render_agent_panel()
{
    if (_agent_panel_root == nullptr) {
        return;
    }
    const AgentPanelSnapshot snapshot = _agent_panel_model.snapshot();
    if (snapshot.confirmation != AgentPanelConfirmation::ResetAll) {
        _agent_reset_hold_started_ms = 0;
        _agent_reset_hold_active = false;
        _agent_reset_hold_queued = false;
    }
    lv_obj_clean(_agent_panel_root);
    _agent_panel_hint = nullptr;
    _agent_rail = nullptr;
    _agent_focus_band_left = nullptr;
    _agent_focus_band_right = nullptr;
    _agent_focus_marker = nullptr;
    _agent_count_label = nullptr;
    _agent_panel_notice_label = nullptr;
    _agent_row_wrappers = {};
    _agent_row_buttons = {};
    _agent_delete_buttons = {};
    _agent_row_names = {};
    _agent_row_states = {};
    _agent_row_ids = {};
    _agent_row_times = {};
    _agent_row_dots = {};
    _agent_manage_button = nullptr;
    _agent_manage_done_button = nullptr;
    _agent_allow_button = nullptr;
    _agent_allow_switch_button = nullptr;
    _agent_reject_button = nullptr;
    _agent_dismiss_button = nullptr;
    _agent_reset_button = nullptr;
    _agent_confirm_button = nullptr;
    _agent_cancel_button = nullptr;
    _agent_revealed_delete_index = -1;

    lv_obj_t* drag_handle = lv_obj_create(_agent_panel_root);
    lv_obj_set_pos(
        drag_handle,
        agent_panel_ui::kDragHandle.x,
        agent_panel_ui::kDragHandle.y);
    lv_obj_set_size(
        drag_handle,
        agent_panel_ui::kDragHandle.w,
        agent_panel_ui::kDragHandle.h);
    lv_obj_set_style_radius(drag_handle, 2, 0);
    lv_obj_set_style_border_width(drag_handle, 0, 0);
    lv_obj_set_style_bg_color(
        drag_handle, lv_color_hex(agent_panel_ui::kSecondaryText), 0);
    lv_obj_set_style_bg_opa(
        drag_handle,
        static_cast<lv_opa_t>(agent_panel_ui::kHandleOpacity),
        0);
    lv_obj_clear_flag(drag_handle, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* agent_title = make_center_label(
        _agent_panel_root,
        "AGENTS",
        28,
        &AgentPanelTitle16,
        agent_panel_ui::kPrimaryText);
    lv_obj_set_style_text_letter_space(agent_title, 2, 0);

    auto make_unstyled_button = [](lv_obj_t* parent) {
        lv_obj_t* button = lv_button_create(parent);
        lv_obj_remove_style_all(button);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        return button;
    };

    auto make_button = [this, &make_unstyled_button](
                           lv_obj_t* parent,
                           const char* text,
                           int x,
                           int y,
                           int width,
                           int height,
                           uint32_t color,
                           bool filled = true) {
        lv_obj_t* button = make_unstyled_button(parent);
        lv_obj_set_size(button, width, height);
        lv_obj_set_pos(button, x, y);
        lv_obj_set_style_radius(button, height / 2, 0);
        lv_obj_set_style_border_width(button, filled ? 0 : 1, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(color), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(
            button, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_t* label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(
            label,
            lv_color_hex(
                filled
                    ? agent_panel_ui::kPrimaryText
                    : color),
            0);
        lv_obj_center(label);
        lv_obj_add_event_cb(
            button,
            AppCodexStatus::agent_panel_event_cb,
            LV_EVENT_CLICKED,
            this);
        return button;
    };

    auto make_connected_wash_half = [](
                                        lv_obj_t* parent,
                                        int x,
                                        int width,
                                        int height,
                                        uint32_t start_color,
                                        uint32_t end_color,
                                        uint8_t opacity) {
        lv_obj_t* wash = lv_obj_create(parent);
        lv_obj_set_pos(wash, x, 0);
        lv_obj_set_size(wash, width, height);
        lv_obj_set_style_radius(wash, 0, 0);
        lv_obj_set_style_border_width(wash, 0, 0);
        lv_obj_set_style_pad_all(wash, 0, 0);
        lv_obj_set_style_bg_color(wash, lv_color_hex(start_color), 0);
        lv_obj_set_style_bg_grad_color(wash, lv_color_hex(end_color), 0);
        lv_obj_set_style_bg_grad_dir(wash, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(wash, static_cast<lv_opa_t>(opacity), 0);
        lv_obj_clear_flag(wash, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(wash, LV_OBJ_FLAG_CLICKABLE);
        return wash;
    };

    if (snapshot.confirmation == AgentPanelConfirmation::None &&
        !snapshot.request.present && snapshot.count > 0) {
        lv_obj_t*& header_action = _agent_panel_manage_mode
            ? _agent_manage_done_button
            : _agent_manage_button;
        header_action = make_button(
            _agent_panel_root,
            _agent_panel_manage_mode ? "DONE" : "MANAGE",
            agent_panel_ui::kManageRect.x,
            agent_panel_ui::kManageRect.y,
            agent_panel_ui::kManageRect.w,
            agent_panel_ui::kManageRect.h,
            agent_panel_ui::kSecondaryText,
            false);
        lv_obj_set_style_border_width(header_action, 0, 0);
        lv_obj_t* action_label = lv_obj_get_child(header_action, 0);
        if (action_label != nullptr) {
            lv_obj_align(action_label, LV_ALIGN_TOP_MID, 0, 31);
        }
    }

    if (snapshot.confirmation != AgentPanelConfirmation::None) {
        const bool deleting =
            snapshot.confirmation == AgentPanelConfirmation::DeleteAgent;
        const char* title = deleting
            ? "Delete Agent?"
            : "Reset all Agents?";
        make_center_label(
            _agent_panel_root,
            title,
            deleting ? 142 : 140,
            &lv_font_montserrat_22,
            agent_panel_ui::kPrimaryText);
        if (deleting) {
            for (size_t index = 0; index < snapshot.count; ++index) {
                if (snapshot.rows[index].fingerprint ==
                    snapshot.confirmation_fingerprint) {
                    make_center_label(
                        _agent_panel_root,
                        snapshot.rows[index].display_name.data(),
                        180,
                        is_ascii_text(
                            snapshot.rows[index].display_name.data())
                            ? &lv_font_montserrat_18
                            : &font_puhui_14_1,
                        agent_panel_ui::kSecondaryText);
                    break;
                }
            }
            _agent_confirm_button = make_button(
                _agent_panel_root,
                "Delete",
                82,
                240,
                302,
                54,
                agent_panel_ui::kDestructive);
            _agent_cancel_button = make_button(
                _agent_panel_root,
                "Cancel",
                133,
                310,
                200,
                48,
                agent_panel_ui::kSecondaryText,
                false);
        } else {
            lv_obj_t* explanation = make_center_label(
                _agent_panel_root,
                "This removes every paired Agent\nand rotates credentials.",
                178,
                &lv_font_montserrat_14,
                agent_panel_ui::kSecondaryText);
            lv_obj_set_width(explanation, 302);
            lv_obj_set_style_text_align(
                explanation, LV_TEXT_ALIGN_CENTER, 0);
            _agent_confirm_button = make_button(
                _agent_panel_root,
                "Hold to reset",
                92,
                250,
                282,
                58,
                agent_panel_ui::kDestructive);
            lv_obj_set_style_bg_opa(
                _agent_confirm_button, LV_OPA_20, 0);
            lv_obj_set_style_pad_all(_agent_confirm_button, 0, 0);
            _agent_reset_progress = lv_obj_create(_agent_confirm_button);
            lv_obj_set_pos(_agent_reset_progress, 0, 0);
            lv_obj_set_size(_agent_reset_progress, 0, 58);
            lv_obj_set_style_radius(_agent_reset_progress, 29, 0);
            lv_obj_set_style_border_width(_agent_reset_progress, 0, 0);
            lv_obj_set_style_bg_color(
                _agent_reset_progress,
                lv_color_hex(agent_panel_ui::kDestructive),
                0);
            lv_obj_set_style_bg_opa(
                _agent_reset_progress, LV_OPA_COVER, 0);
            lv_obj_clear_flag(
                _agent_reset_progress, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(
                _agent_reset_progress, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_move_to_index(_agent_reset_progress, 0);
            lv_obj_add_event_cb(
                _agent_confirm_button,
                AppCodexStatus::agent_reset_hold_event_cb,
                LV_EVENT_ALL,
                this);
            _agent_cancel_button = make_button(
                _agent_panel_root,
                "Cancel",
                153,
                326,
                160,
                48,
                agent_panel_ui::kSecondaryText,
                false);
        }
    } else if (snapshot.request.present) {
        make_center_label(
            _agent_panel_root,
            "PAIRING REQUEST",
            56,
            &lv_font_montserrat_14,
            agent_panel_ui::kRequesting);
        make_center_label(
            _agent_panel_root,
            snapshot.request.display_name.data(),
            88,
            is_ascii_text(snapshot.request.display_name.data())
                ? &lv_font_montserrat_22
                : &font_puhui_14_1,
            agent_panel_ui::kPrimaryText);
        make_center_label(
            _agent_panel_root,
            _agent_pairing_expired
                ? "Request expired"
                : "Match this code on your Mac",
            124,
            &lv_font_montserrat_14,
            agent_panel_ui::kSecondaryText);
        char grouped_code[8] = {};
        const char* raw_code = snapshot.request.verification_code.data();
        if (std::strlen(raw_code) >= 6) {
            std::snprintf(
                grouped_code,
                sizeof(grouped_code),
                "%.3s %.3s",
                raw_code,
                raw_code + 3);
        } else {
            std::snprintf(grouped_code, sizeof(grouped_code), "%s", raw_code);
        }
        make_center_label(
            _agent_panel_root,
            grouped_code,
            164,
            &lv_font_montserrat_36,
            _agent_pairing_expired
                ? agent_panel_ui::kSecondaryText
                : agent_panel_ui::kPrimaryText);
        _agent_pairing_countdown_label = make_center_label(
            _agent_panel_root,
            "--s remaining",
            210,
            &lv_font_montserrat_14,
            agent_panel_ui::kRequesting);
        if (_agent_pairing_expired) {
            _agent_dismiss_button = make_button(
                _agent_panel_root,
                "Dismiss",
                133,
                302,
                200,
                48,
                agent_panel_ui::kSecondaryText,
                false);
        } else {
            _agent_allow_button = make_button(
                _agent_panel_root,
                "Allow",
                82,
                302,
                302,
                50,
                agent_panel_ui::kPairing,
                false);
            _agent_allow_switch_button = make_button(
                _agent_panel_root,
                "Allow + Switch",
                82,
                236,
                302,
                54,
                agent_panel_ui::kPairing);
            _agent_reject_button = make_button(
                _agent_panel_root,
                "Reject",
                133,
                364,
                200,
                46,
                agent_panel_ui::kDestructive,
                false);
        }
    } else {
        if (snapshot.count == 0) {
            make_center_label(
                _agent_panel_root,
                "No paired Agents",
                192,
                &lv_font_montserrat_20,
                agent_panel_ui::kPrimaryText);
            lv_obj_t* helper = make_center_label(
                _agent_panel_root,
                "Open StopWatch Codex Agent on a Mac to pair",
                230,
                &lv_font_montserrat_14,
                agent_panel_ui::kSecondaryText);
            lv_obj_set_width(helper, 300);
            lv_obj_set_style_text_align(helper, LV_TEXT_ALIGN_CENTER, 0);
        } else if (!_agent_panel_manage_mode) {
            const int64_t now_unix_ms = current_unix_milliseconds();
            for (size_t index = 0; index < snapshot.count; ++index) {
                const AgentPanelRow& row = snapshot.rows[index];
                const agent_panel_ui::Rect row_rect =
                    agent_panel_ui::ledger_row_layout(
                        snapshot.count, index);
                lv_obj_t* row_button =
                    make_unstyled_button(_agent_panel_root);
                _agent_row_buttons[index] = row_button;
                lv_obj_set_pos(row_button, row_rect.x, row_rect.y);
                lv_obj_set_size(row_button, row_rect.w, row_rect.h);
                lv_obj_set_style_radius(row_button, 0, 0);
                lv_obj_set_style_border_width(row_button, 0, 0);
                lv_obj_set_style_pad_all(row_button, 0, 0);
                lv_obj_set_style_shadow_width(row_button, 0, 0);
                lv_obj_set_style_bg_color(
                    row_button,
                    lv_color_hex(agent_panel_ui::kSelection),
                    0);
                lv_obj_set_style_bg_opa(row_button, LV_OPA_TRANSP, 0);
                lv_obj_add_event_cb(
                    row_button,
                    AppCodexStatus::agent_panel_event_cb,
                    LV_EVENT_CLICKED,
                    this);

                uint32_t state_color = agent_panel_ui::kOffline;
                const char* state_text = "Offline";
                bool filled_dot = false;
                bool request_ring = false;
                int dot_border_width = 1;
                if (row.state == AgentPanelRowState::Connected) {
                    state_color = agent_panel_ui::kConnected;
                    state_text = "Connected";
                    filled_dot = true;
                    dot_border_width = 0;
                } else if (row.state == AgentPanelRowState::Requesting) {
                    state_color = agent_panel_ui::kRequesting;
                    state_text = "Switch request";
                    request_ring = true;
                    dot_border_width = 2;
                }
                if (_agent_switch_pending &&
                    row.fingerprint == _agent_switch_target) {
                    state_color = agent_panel_ui::kSelection;
                    state_text = "Switching...";
                    filled_dot = true;
                    request_ring = false;
                    dot_border_width = 0;
                }

                if (row.state == AgentPanelRowState::Connected) {
                    const int left_width = row_rect.w / 2;
                    make_connected_wash_half(
                        row_button,
                        0,
                        left_width,
                        row_rect.h,
                        agent_panel_ui::kBackground,
                        agent_panel_ui::kSelection,
                        agent_panel_ui::kConnectedWashOpacity);
                    make_connected_wash_half(
                        row_button,
                        left_width,
                        row_rect.w - left_width,
                        row_rect.h,
                        agent_panel_ui::kSelection,
                        agent_panel_ui::kBackground,
                        agent_panel_ui::kConnectedWashOpacity);
                    lv_obj_t* marker = lv_obj_create(row_button);
                    lv_obj_set_pos(marker, 3, row_rect.h / 4);
                    lv_obj_set_size(marker, 3, row_rect.h / 2);
                    lv_obj_set_style_radius(marker, 2, 0);
                    lv_obj_set_style_border_width(marker, 0, 0);
                    lv_obj_set_style_bg_color(
                        marker,
                        lv_color_hex(agent_panel_ui::kSelection),
                        0);
                    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
                    lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_clear_flag(marker, LV_OBJ_FLAG_CLICKABLE);
                }

                const int dot_y = (row_rect.h - 8) / 2;
                if (request_ring) {
                    lv_obj_t* request_halo = lv_obj_create(row_button);
                    lv_obj_set_pos(request_halo, 10, dot_y - 2);
                    lv_obj_set_size(request_halo, 12, 12);
                    lv_obj_set_style_radius(
                        request_halo, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_border_width(request_halo, 2, 0);
                    lv_obj_set_style_border_color(
                        request_halo, lv_color_hex(state_color), 0);
                    lv_obj_set_style_border_opa(
                        request_halo,
                        static_cast<lv_opa_t>(
                            agent_panel_ui::kRequestHaloOpacity),
                        0);
                    lv_obj_set_style_bg_opa(
                        request_halo, LV_OPA_TRANSP, 0);
                    lv_obj_clear_flag(
                        request_halo, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_clear_flag(
                        request_halo, LV_OBJ_FLAG_CLICKABLE);
                }

                lv_obj_t* dot = lv_obj_create(row_button);
                _agent_row_dots[index] = dot;
                lv_obj_set_pos(dot, 12, dot_y);
                lv_obj_set_size(dot, 8, 8);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_border_width(dot, dot_border_width, 0);
                lv_obj_set_style_border_color(
                    dot, lv_color_hex(state_color), 0);
                lv_obj_set_style_bg_color(
                    dot, lv_color_hex(state_color), 0);
                lv_obj_set_style_bg_opa(
                    dot, filled_dot ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
                lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);

                const int name_y = row_rect.h <= 44 ? 0 : 7;
                const int metadata_y = row_rect.h <= 44 ? 23 : 34;
                const lv_font_t* name_font =
                    is_ascii_text(row.display_name.data())
                        ? (row_rect.h <= 44
                               ? &lv_font_montserrat_16
                               : &lv_font_montserrat_18)
                        : &font_puhui_14_1;
                _agent_row_names[index] = make_label(
                    row_button,
                    row.display_name.data(),
                    30,
                    name_y,
                    name_font,
                    agent_panel_ui::kPrimaryText);
                lv_obj_set_width(
                    _agent_row_names[index], row_rect.w - 42);
                lv_label_set_long_mode(
                    _agent_row_names[index], LV_LABEL_LONG_MODE_DOTS);

                const agent_panel_ui::LedgerMetadataLayout metadata =
                    agent_panel_ui::ledger_metadata_layout(row_rect.w);
                _agent_row_states[index] = make_label(
                    row_button,
                    state_text,
                    metadata.state.x,
                    metadata_y,
                    &lv_font_montserrat_14,
                    state_color);
                lv_obj_set_width(
                    _agent_row_states[index], metadata.state.w);
                lv_obj_set_style_text_letter_space(
                    _agent_row_states[index], -1, 0);

                char short_id[12] = {};
                std::snprintf(
                    short_id,
                    sizeof(short_id),
                    "ID %s",
                    row.short_id.data());
                _agent_row_ids[index] = make_label(
                    row_button,
                    short_id,
                    metadata.id.x,
                    metadata_y,
                    &lv_font_montserrat_14,
                    agent_panel_ui::kSecondaryText);
                lv_obj_set_width(_agent_row_ids[index], metadata.id.w);
                lv_obj_set_style_text_letter_space(
                    _agent_row_ids[index], -1, 0);

                char last_connected[16] = {};
                const AgentPanelRowState presentation_state =
                    _agent_switch_pending &&
                        row.fingerprint == _agent_switch_target
                    ? AgentPanelRowState::Requesting
                    : row.state;
                agent_panel_presentation::format_last_connected(
                    presentation_state,
                    row.last_connected_at,
                    now_unix_ms,
                    last_connected,
                    sizeof(last_connected));
                _agent_row_times[index] = make_label(
                    row_button,
                    last_connected,
                    metadata.time.x,
                    metadata_y,
                    &lv_font_montserrat_14,
                    agent_panel_ui::kSecondaryText);
                lv_obj_set_width(
                    _agent_row_times[index], metadata.time.w);
                lv_obj_set_style_text_align(
                    _agent_row_times[index], LV_TEXT_ALIGN_RIGHT, 0);
                lv_obj_set_style_text_letter_space(
                    _agent_row_times[index], -1, 0);

                if (index + 1 < snapshot.count) {
                    lv_obj_t* divider = lv_obj_create(row_button);
                    lv_obj_set_pos(divider, 10, row_rect.h - 1);
                    lv_obj_set_size(divider, row_rect.w - 20, 1);
                    lv_obj_set_style_radius(divider, 0, 0);
                    lv_obj_set_style_border_width(divider, 0, 0);
                    lv_obj_set_style_bg_color(
                        divider,
                        lv_color_hex(agent_panel_ui::kDivider),
                        0);
                    lv_obj_set_style_bg_opa(
                        divider,
                        static_cast<lv_opa_t>(
                            agent_panel_ui::kDividerOpacity),
                        0);
                    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
                }
            }
        } else {
            const int half_width = agent_panel_ui::kFocusBand.w / 2;
            _agent_focus_band_left = lv_obj_create(_agent_panel_root);
            lv_obj_set_pos(
                _agent_focus_band_left,
                agent_panel_ui::kFocusBand.x,
                agent_panel_ui::kFocusBand.y);
            lv_obj_set_size(
                _agent_focus_band_left,
                half_width,
                agent_panel_ui::kFocusBand.h);
            lv_obj_set_style_radius(_agent_focus_band_left, 0, 0);
            lv_obj_set_style_border_width(_agent_focus_band_left, 0, 0);
            lv_obj_set_style_bg_color(
                _agent_focus_band_left,
                lv_color_hex(agent_panel_ui::kBackground),
                0);
            lv_obj_set_style_bg_grad_color(
                _agent_focus_band_left,
                lv_color_hex(agent_panel_ui::kSelection),
                0);
            lv_obj_set_style_bg_grad_dir(
                _agent_focus_band_left, LV_GRAD_DIR_HOR, 0);
            lv_obj_set_style_bg_opa(_agent_focus_band_left, LV_OPA_30, 0);
            lv_obj_clear_flag(
                _agent_focus_band_left, LV_OBJ_FLAG_SCROLLABLE);

            _agent_focus_band_right = lv_obj_create(_agent_panel_root);
            lv_obj_set_pos(
                _agent_focus_band_right,
                agent_panel_ui::kFocusBand.x + half_width,
                agent_panel_ui::kFocusBand.y);
            lv_obj_set_size(
                _agent_focus_band_right,
                agent_panel_ui::kFocusBand.w - half_width,
                agent_panel_ui::kFocusBand.h);
            lv_obj_set_style_radius(_agent_focus_band_right, 0, 0);
            lv_obj_set_style_border_width(_agent_focus_band_right, 0, 0);
            lv_obj_set_style_bg_color(
                _agent_focus_band_right,
                lv_color_hex(agent_panel_ui::kSelection),
                0);
            lv_obj_set_style_bg_grad_color(
                _agent_focus_band_right,
                lv_color_hex(agent_panel_ui::kBackground),
                0);
            lv_obj_set_style_bg_grad_dir(
                _agent_focus_band_right, LV_GRAD_DIR_HOR, 0);
            lv_obj_set_style_bg_opa(_agent_focus_band_right, LV_OPA_30, 0);
            lv_obj_clear_flag(
                _agent_focus_band_right, LV_OBJ_FLAG_SCROLLABLE);

            _agent_focus_marker = lv_obj_create(_agent_panel_root);
            lv_obj_set_pos(
                _agent_focus_marker,
                agent_panel_ui::kFocusMarker.x,
                agent_panel_ui::kFocusMarker.y);
            lv_obj_set_size(
                _agent_focus_marker,
                agent_panel_ui::kFocusMarker.w,
                agent_panel_ui::kFocusMarker.h);
            lv_obj_set_style_radius(_agent_focus_marker, 2, 0);
            lv_obj_set_style_border_width(_agent_focus_marker, 0, 0);
            lv_obj_set_style_bg_color(
                _agent_focus_marker,
                lv_color_hex(agent_panel_ui::kSelection),
                0);
            lv_obj_set_style_bg_opa(
                _agent_focus_marker, LV_OPA_COVER, 0);
            lv_obj_clear_flag(
                _agent_focus_marker, LV_OBJ_FLAG_SCROLLABLE);

            _agent_rail = lv_obj_create(_agent_panel_root);
            lv_obj_set_pos(
                _agent_rail,
                agent_panel_ui::kRailViewport.x,
                agent_panel_ui::kRailViewport.y);
            lv_obj_set_size(
                _agent_rail,
                agent_panel_ui::kRailViewport.w,
                agent_panel_ui::kRailViewport.h);
            lv_obj_set_style_radius(_agent_rail, 0, 0);
            lv_obj_set_style_border_width(_agent_rail, 0, 0);
            lv_obj_set_style_bg_opa(_agent_rail, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_left(_agent_rail, 0, 0);
            lv_obj_set_style_pad_right(_agent_rail, 0, 0);
            lv_obj_set_style_pad_top(
                _agent_rail,
                (agent_panel_ui::kRailViewport.h -
                    agent_panel_ui::kRailRowHeight) /
                    2,
                0);
            lv_obj_set_style_pad_bottom(
                _agent_rail,
                (agent_panel_ui::kRailViewport.h -
                    agent_panel_ui::kRailRowHeight) /
                    2,
                0);
            lv_obj_set_style_pad_row(_agent_rail, 0, 0);
            lv_obj_set_scroll_dir(_agent_rail, LV_DIR_VER);
            lv_obj_set_scroll_snap_y(
                _agent_rail, LV_SCROLL_SNAP_CENTER);
            lv_obj_set_style_anim_duration(_agent_rail, 160, 0);
            lv_obj_set_scrollbar_mode(
                _agent_rail, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_flex_flow(_agent_rail, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(
                _agent_rail,
                LV_FLEX_ALIGN_START,
                LV_FLEX_ALIGN_START,
                LV_FLEX_ALIGN_START);
            lv_obj_add_event_cb(
                _agent_rail,
                AppCodexStatus::agent_panel_scroll_event_cb,
                LV_EVENT_SCROLL,
                this);

            size_t initial_focus = 0;
            bool focus_found = false;
            for (size_t index = 0; index < snapshot.count; ++index) {
                const AgentPanelRow& row = snapshot.rows[index];
                if (!focus_found &&
                    row.state == AgentPanelRowState::Requesting) {
                    initial_focus = index;
                    focus_found = true;
                } else if (!focus_found &&
                    row.state == AgentPanelRowState::Connected) {
                    initial_focus = index;
                }

                lv_obj_t* wrapper = lv_obj_create(_agent_rail);
                _agent_row_wrappers[index] = wrapper;
                lv_obj_set_size(
                    wrapper,
                    agent_panel_ui::kRailViewport.w,
                    agent_panel_ui::kRailRowHeight);
                lv_obj_set_style_radius(wrapper, 0, 0);
                lv_obj_set_style_border_width(wrapper, 0, 0);
                lv_obj_set_style_bg_opa(wrapper, LV_OPA_TRANSP, 0);
                lv_obj_set_style_pad_all(wrapper, 0, 0);
                lv_obj_clear_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(wrapper, LV_OBJ_FLAG_SNAPPABLE);

                lv_obj_t* delete_button = make_button(
                    wrapper,
                    "Delete",
                    agent_panel_ui::kRailViewport.w -
                        agent_panel_ui::kDeleteRevealWidth,
                    7,
                    agent_panel_ui::kDeleteRevealWidth,
                    44,
                    agent_panel_ui::kDestructive,
                    false);
                _agent_delete_buttons[index] = delete_button;
                lv_obj_add_flag(delete_button, LV_OBJ_FLAG_HIDDEN);

                lv_obj_t* row_button = make_unstyled_button(wrapper);
                _agent_row_buttons[index] = row_button;
                lv_obj_set_pos(row_button, 0, 3);
                lv_obj_set_size(
                    row_button,
                    agent_panel_ui::kRailViewport.w,
                    52);
                lv_obj_set_style_radius(row_button, 0, 0);
                lv_obj_set_style_border_width(row_button, 0, 0);
                lv_obj_set_style_bg_opa(row_button, LV_OPA_TRANSP, 0);
                lv_obj_set_style_shadow_width(row_button, 0, 0);
                lv_obj_add_event_cb(
                    row_button,
                    AppCodexStatus::agent_panel_event_cb,
                    LV_EVENT_CLICKED,
                    this);
                lv_obj_add_event_cb(
                    row_button,
                    AppCodexStatus::agent_panel_row_gesture_cb,
                    LV_EVENT_GESTURE,
                    this);

                uint32_t state_color = agent_panel_ui::kOffline;
                const char* state = "Offline";
                bool filled_dot = false;
                if (row.state == AgentPanelRowState::Connected) {
                    state = "Connected";
                    state_color = agent_panel_ui::kConnected;
                    filled_dot = true;
                } else if (row.state == AgentPanelRowState::Requesting) {
                    state = "Switch request";
                    state_color = agent_panel_ui::kRequesting;
                    filled_dot = true;
                }
                if (_agent_switch_pending &&
                    row.fingerprint == _agent_switch_target) {
                    state = "Switching...";
                    state_color = agent_panel_ui::kSelection;
                    filled_dot = true;
                }

                lv_obj_t* dot = lv_obj_create(row_button);
                _agent_row_dots[index] = dot;
                lv_obj_set_pos(dot, 14, 22);
                lv_obj_set_size(dot, 8, 8);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_border_width(dot, filled_dot ? 0 : 1, 0);
                lv_obj_set_style_border_color(
                    dot, lv_color_hex(state_color), 0);
                lv_obj_set_style_bg_color(
                    dot, lv_color_hex(state_color), 0);
                lv_obj_set_style_bg_opa(
                    dot, filled_dot ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
                lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

                _agent_row_names[index] = make_label(
                    row_button,
                    row.display_name.data(),
                    32,
                    13,
                    is_ascii_text(row.display_name.data())
                        ? &lv_font_montserrat_18
                        : &font_puhui_14_1,
                    agent_panel_ui::kPrimaryText);
                lv_obj_set_width(_agent_row_names[index], 220);
                lv_label_set_long_mode(
                    _agent_row_names[index], LV_LABEL_LONG_MODE_DOTS);

                _agent_row_states[index] = make_label(
                    row_button,
                    state,
                    260,
                    17,
                    &lv_font_montserrat_14,
                    state_color);
                lv_obj_set_width(_agent_row_states[index], 120);
                lv_obj_set_style_text_align(
                    _agent_row_states[index], LV_TEXT_ALIGN_RIGHT, 0);
            }

            lv_obj_update_layout(_agent_rail);
            if (_agent_row_wrappers[initial_focus] != nullptr) {
                lv_obj_scroll_to_view(
                    _agent_row_wrappers[initial_focus], LV_ANIM_OFF);
            }
            sync_agent_rail_focus();

            _agent_reset_button = make_button(
                _agent_panel_root,
                "Reset all",
                agent_panel_ui::kResetRect.x,
                agent_panel_ui::kResetRect.y,
                agent_panel_ui::kResetRect.w,
                agent_panel_ui::kResetRect.h,
                agent_panel_ui::kDestructive,
                false);
        }
    }

    if (_agent_panel_notice[0] != '\0' &&
        _agent_panel_notice_until_ms != 0) {
        _agent_panel_notice_label = make_center_label(
            _agent_panel_root,
            _agent_panel_notice.data(),
            365,
            &lv_font_montserrat_14,
            agent_panel_ui::kDestructive);
    }

    _agent_panel_hint = lv_obj_create(_agent_panel_root);
    lv_obj_set_pos(
        _agent_panel_hint,
        agent_panel_ui::kCloseHint.x,
        agent_panel_ui::panel_hint_y(
            lv_obj_get_y(_agent_panel_root)));
    lv_obj_set_size(
        _agent_panel_hint,
        agent_panel_ui::kCloseHint.w,
        agent_panel_ui::kCloseHint.h);
    lv_obj_set_style_radius(_agent_panel_hint, 2, 0);
    lv_obj_set_style_border_width(_agent_panel_hint, 0, 0);
    lv_obj_set_style_bg_color(
        _agent_panel_hint,
        lv_color_hex(agent_panel_ui::kSecondaryText),
        0);
    lv_obj_set_style_bg_opa(
        _agent_panel_hint,
        static_cast<lv_opa_t>(agent_panel_ui::kHandleOpacity),
        0);
    lv_obj_clear_flag(_agent_panel_hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(_agent_panel_root);
    if (_last_agent_touch_state == LV_INDEV_STATE_REL) {
        settle_agent_panel(true);
    }
}

void AppCodexStatus::sync_agent_rail_focus()
{
    if (_agent_rail == nullptr) {
        return;
    }
    lv_area_t rail_coords = {};
    lv_obj_get_coords(_agent_rail, &rail_coords);
    const int rail_center =
        rail_coords.y1 + agent_panel_ui::kRailViewport.h / 2;
    const AgentPanelSnapshot snapshot = _agent_panel_model.snapshot();
    for (size_t index = 0; index < snapshot.count; ++index) {
        lv_obj_t* wrapper = _agent_row_wrappers[index];
        lv_obj_t* name = _agent_row_names[index];
        if (wrapper == nullptr || name == nullptr) {
            continue;
        }
        lv_area_t row_coords = {};
        lv_obj_get_coords(wrapper, &row_coords);
        const int row_center = (row_coords.y1 + row_coords.y2) / 2;
        const int delta = row_center > rail_center
            ? row_center - rail_center
            : rail_center - row_center;
        const bool ascii_name =
            is_ascii_text(snapshot.rows[index].display_name.data());
        const lv_font_t* font = ascii_name
            ? &lv_font_montserrat_16
            : &font_puhui_14_1;
        lv_opa_t opacity = LV_OPA_40;
        if (delta <= 20) {
            if (ascii_name) {
                font = &lv_font_montserrat_22;
            }
            opacity = LV_OPA_COVER;
        } else if (delta <= agent_panel_ui::kRailRowHeight + 20) {
            if (ascii_name) {
                font = &lv_font_montserrat_18;
            }
            opacity = LV_OPA_70;
        }
        lv_obj_set_style_text_font(name, font, 0);
        lv_obj_set_style_opa(wrapper, opacity, 0);
    }
}

void AppCodexStatus::close_agent_delete_reveal()
{
    const AgentPanelSnapshot snapshot = _agent_panel_model.snapshot();
    for (size_t index = 0; index < snapshot.count; ++index) {
        if (_agent_row_buttons[index] != nullptr) {
            lv_obj_set_x(_agent_row_buttons[index], 0);
        }
        if (_agent_delete_buttons[index] != nullptr) {
            lv_obj_add_flag(
                _agent_delete_buttons[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
    _agent_revealed_delete_index = -1;
}

void AppCodexStatus::agent_panel_scroll_event_cb(lv_event_t* event)
{
    auto* self =
        static_cast<AppCodexStatus*>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }
    self->close_agent_delete_reveal();
    self->sync_agent_rail_focus();
}

void AppCodexStatus::agent_panel_row_gesture_cb(lv_event_t* event)
{
    auto* self =
        static_cast<AppCodexStatus*>(lv_event_get_user_data(event));
    lv_indev_t* indev = lv_indev_active();
    if (self == nullptr || indev == nullptr ||
        lv_indev_get_gesture_dir(indev) != LV_DIR_LEFT) {
        return;
    }
    lv_obj_t* target =
        static_cast<lv_obj_t*>(lv_event_get_target(event));
    const AgentPanelSnapshot snapshot = self->_agent_panel_model.snapshot();
    for (size_t index = 0; index < snapshot.count; ++index) {
        if (target != self->_agent_row_buttons[index]) {
            continue;
        }
        self->close_agent_delete_reveal();
        self->_agent_revealed_delete_index = static_cast<int>(index);
        lv_obj_clear_flag(
            self->_agent_delete_buttons[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(
            self->_agent_row_buttons[index],
            -agent_panel_ui::kDeleteRevealWidth);
        return;
    }
}

void AppCodexStatus::agent_reset_hold_event_cb(lv_event_t* event)
{
    auto* self =
        static_cast<AppCodexStatus*>(lv_event_get_user_data(event));
    if (self == nullptr ||
        self->_agent_panel_model.snapshot().confirmation !=
            AgentPanelConfirmation::ResetAll) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        self->_agent_reset_hold_started_ms = GetHAL().millis();
        self->_agent_reset_hold_active = true;
        self->_agent_reset_hold_queued = false;
    } else if (code == LV_EVENT_RELEASED ||
               code == LV_EVENT_PRESS_LOST) {
        self->_agent_reset_hold_started_ms = 0;
        self->_agent_reset_hold_active = false;
        if (!self->_agent_reset_hold_queued &&
            self->_agent_reset_progress != nullptr) {
            lv_obj_set_width(self->_agent_reset_progress, 0);
        }
    }
}

void AppCodexStatus::update_agent_panel_runtime(uint32_t now_ms)
{
    if (_agent_pairing_countdown_label != nullptr) {
        const uint64_t deadline = _secure_pairing_session.deadline_ms();
        uint32_t remaining_seconds = 0;
        if (deadline > now_ms) {
            remaining_seconds = static_cast<uint32_t>(
                (deadline - now_ms + 999U) / 1000U);
        }
        char countdown[24] = {};
        std::snprintf(
            countdown,
            sizeof(countdown),
            "%us remaining",
            static_cast<unsigned>(remaining_seconds));
        lv_label_set_text(_agent_pairing_countdown_label, countdown);
    }

    if (_agent_reset_hold_active && !_agent_reset_hold_queued) {
        const uint32_t elapsed = now_ms - _agent_reset_hold_started_ms;
        const uint32_t clamped = std::min(
            elapsed, agent_panel_ui::kResetHoldMs);
        if (_agent_reset_progress != nullptr) {
            lv_obj_set_width(
                _agent_reset_progress,
                static_cast<int>(
                    clamped * 282U / agent_panel_ui::kResetHoldMs));
        }
        if (elapsed >= agent_panel_ui::kResetHoldMs) {
            _agent_reset_hold_active = false;
            _agent_reset_hold_queued = true;
            queue_agent_panel_decision(
                _agent_panel_model.confirm_destructive(true));
            _agent_panel_dirty = true;
        }
    }

    if (_agent_panel_notice_until_ms != 0 &&
        static_cast<int32_t>(
            now_ms - _agent_panel_notice_until_ms) >= 0) {
        _agent_panel_notice_until_ms = 0;
        _agent_panel_notice = {};
        if (_agent_panel_notice_label != nullptr) {
            lv_obj_add_flag(
                _agent_panel_notice_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AppCodexStatus::show_agent_panel_notice(
    const char* text,
    uint32_t now_ms)
{
    std::snprintf(
        _agent_panel_notice.data(),
        _agent_panel_notice.size(),
        "%s",
        text == nullptr ? "" : text);
    _agent_panel_notice_until_ms =
        now_ms + agent_panel_ui::kNoticeDurationMs;
    _agent_panel_dirty = true;
}

void AppCodexStatus::set_agent_panel_y(int y)
{
    if (_agent_panel_root == nullptr) {
        return;
    }
    lv_obj_set_y(_agent_panel_root, y);
    if (_agent_panel_hint != nullptr) {
        lv_obj_set_y(
            _agent_panel_hint,
            agent_panel_ui::panel_hint_y(y));
    }
}

void AppCodexStatus::settle_agent_panel(bool animate)
{
    if (_agent_panel_root == nullptr) {
        return;
    }
    const AgentPanelSnapshot snapshot = _agent_panel_model.snapshot();
    auto& controls = GetGlobalEdgeControlService();
    if (snapshot.visible) {
        GetGlobalEdgeControlService().setEnabled(false);
    } else {
        close_agent_delete_reveal();
        if (_agent_panel_manage_mode) {
            _agent_panel_manage_mode = false;
            _agent_panel_dirty = true;
        }
    }
    const int target_y =
        snapshot.visible ? kAgentPanelVisibleY : kAgentPanelHiddenY;
    const int current_y = lv_obj_get_y(_agent_panel_root);

    lv_anim_delete(this, agent_panel_animation_cb);
    if (!animate || current_y == target_y) {
        set_agent_panel_y(target_y);
        controls.setEnabled(!snapshot.visible);
        return;
    }
    GetGlobalEdgeControlService().setEnabled(false);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, this);
    lv_anim_set_exec_cb(&animation, agent_panel_animation_cb);
    lv_anim_set_values(&animation, current_y, target_y);
    lv_anim_set_duration(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

void AppCodexStatus::agent_panel_animation_cb(void* target, int32_t y)
{
    auto* self = static_cast<AppCodexStatus*>(target);
    if (self != nullptr && self->_agent_panel_root != nullptr) {
        self->set_agent_panel_y(y);
        if (y <= kAgentPanelHiddenY) {
            GetGlobalEdgeControlService().setEnabled(true);
        }
    }
}

void AppCodexStatus::update_agent_panel_touch(uint32_t now_ms)
{
    (void)now_ms;
    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr || _screen_off ||
        GetGlobalDisplayProtectionService().touchInputSuppressed()) {
        _agent_panel_model.cancel_touch();
        if (_last_agent_touch_state == LV_INDEV_STATE_PR) {
            settle_agent_panel(true);
        }
        _last_agent_touch_state = LV_INDEV_STATE_REL;
        return;
    }
    auto& controls = GetGlobalEdgeControlService();
    if (controls.ownsTouch() || controls.lastReleaseConsumed()) {
        _agent_panel_model.cancel_touch();
        if (_last_agent_touch_state == LV_INDEV_STATE_PR) {
            settle_agent_panel(true);
        }
        _last_agent_touch_state = LV_INDEV_STATE_REL;
        return;
    }
    const lv_indev_state_t state = lv_indev_get_state(indev);
    lv_point_t point = {};
    lv_indev_get_point(indev, &point);
    auto apply_drag_position = [this]() {
        if (_agent_panel_root == nullptr ||
            !_agent_panel_model.owns_touch()) {
            return;
        }
        set_agent_panel_y(std::clamp(
            _agent_panel_drag_start_y +
                _agent_panel_model.vertical_drag_offset(),
            kAgentPanelHiddenY,
            kAgentPanelVisibleY));
    };
    if (state == LV_INDEV_STATE_PR) {
        if (_last_agent_touch_state == LV_INDEV_STATE_REL) {
            if (_agent_panel_root != nullptr &&
                !_agent_panel_model.snapshot().visible &&
                lv_obj_get_y(_agent_panel_root) > kAgentPanelHiddenY) {
                _agent_panel_model.set_visible(true);
            }
            _agent_panel_model.touch_press(point.x, point.y);
            if (_agent_panel_model.owns_touch() &&
                _agent_panel_root != nullptr) {
                lv_anim_delete(this, agent_panel_animation_cb);
                _agent_panel_drag_start_y =
                    lv_obj_get_y(_agent_panel_root);
            }
        } else {
            _agent_panel_model.touch_move(point.x, point.y);
            apply_drag_position();
        }
    } else if (_last_agent_touch_state == LV_INDEV_STATE_PR) {
        _agent_panel_model.touch_move(point.x, point.y);
        apply_drag_position();
        (void)_agent_panel_model.touch_release();
        settle_agent_panel(true);
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
    if (target == self->_agent_manage_button) {
        self->_agent_panel_manage_mode = true;
        self->_agent_panel_dirty = true;
        return;
    }
    if (target == self->_agent_manage_done_button) {
        self->close_agent_delete_reveal();
        self->_agent_panel_manage_mode = false;
        self->_agent_panel_dirty = true;
        return;
    }
    for (size_t index = 0; index < snapshot.count; ++index) {
        if (target == self->_agent_row_buttons[index]) {
            if (self->_agent_panel_manage_mode) {
                return;
            }
            if (self->_agent_switch_pending) {
                return;
            }
            self->close_agent_delete_reveal();
            if (self->_agent_row_wrappers[index] != nullptr) {
                lv_obj_scroll_to_view(
                    self->_agent_row_wrappers[index], LV_ANIM_ON);
            }
            self->queue_agent_panel_decision(
                AgentPanelDecision{
                    AgentPanelAction::SwitchAgent,
                    snapshot.rows[index].fingerprint});
            return;
        }
        if (target == self->_agent_delete_buttons[index]) {
            self->close_agent_delete_reveal();
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
    } else if (target == self->_agent_dismiss_button) {
        self->clear_pairing_runtime();
        self->_agent_panel_dirty = true;
    } else if (target == self->_agent_reset_button) {
        self->close_agent_delete_reveal();
        self->queue_agent_panel_decision(
            self->_agent_panel_model.request_reset());
    } else if (target == self->_agent_confirm_button) {
        if (snapshot.confirmation ==
            AgentPanelConfirmation::DeleteAgent) {
            self->queue_agent_panel_decision(
                self->_agent_panel_model.confirm_destructive(true));
        }
    } else if (target == self->_agent_cancel_button) {
        self->_agent_reset_hold_started_ms = 0;
        self->_agent_reset_hold_active = false;
        self->_agent_reset_hold_queued = false;
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
                (void)_ble_transport->set_pet_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
                if (_pet_transfer) {
                    _pet_transfer->on_credential_replaced(
                        _agent_registry.key_epoch());
                }
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
            {
                std::lock_guard<std::mutex> controller_lock(
                    _task_controller_mutex);
                if (_voice_input) {
                    (void)_voice_input->set_credential(
                        _agent_registry.master_key(),
                        _agent_registry.key_epoch());
                }
                _task_voice_ready_applied = false;
            }
            const bool credential_cleared_detail =
                sync_task_detail_credential();
            sync_task_voice_route();
            if (credential_cleared_detail &&
                snapshot_task_focus_state().mode ==
                    CodexTaskFocusMode::TaskDetail) {
                LvglLockGuard lock;
                render_task_focus_detail();
            }
        } else {
            (void)secure_pairing::AgentRegistry::restore(
                before, _agent_registry);
            set_status("AGENT ERR", kCodexRed);
            show_agent_panel_notice("Couldn't delete", now_ms);
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
            {
                std::lock_guard<std::mutex> controller_lock(
                    _task_controller_mutex);
                if (_voice_input) {
                    (void)_voice_input->set_credential(
                        _agent_registry.master_key(),
                        _agent_registry.key_epoch());
                }
                _task_voice_ready_applied = false;
            }
            if (_ble_transport) {
                (void)_ble_transport->set_voice_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
                (void)_ble_transport->set_pet_credential(
                    _agent_registry.master_key(),
                    _agent_registry.key_epoch());
                if (_pet_transfer) {
                    _pet_transfer->on_credential_replaced(
                        _agent_registry.key_epoch());
                }
                (void)_ble_transport->request_active_disconnect(true);
                const uint16_t pending = _ble_transport->pending_handle();
                if (pending != secure_pairing::kNoAgentConnection) {
                    (void)_ble_transport->reject_pending(pending);
                }
            }
            clear_pairing_runtime();
            const bool credential_cleared_detail =
                sync_task_detail_credential();
            sync_task_voice_route();
            if (credential_cleared_detail &&
                snapshot_task_focus_state().mode ==
                    CodexTaskFocusMode::TaskDetail) {
                LvglLockGuard lock;
                render_task_focus_detail();
            }
        } else {
            if (had_registry) {
                (void)secure_pairing::AgentRegistry::restore(
                    before, _agent_registry);
            }
            set_status("AGENT ERR", kCodexRed);
            show_agent_panel_notice("Couldn't reset", now_ms);
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

    bool voice_active = false;
    {
        std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
        voice_active = _voice_input != nullptr &&
            _voice_input->ui_state() == VoiceInputUiState::Active;
    }
    AgentPanelDecision switch_decision =
        _agent_panel_model.request_switch(
            decision.fingerprint, voice_active);
    if (_agent_panel_model.snapshot().switch_waiting_for_voice) {
        std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
        if (_voice_input != nullptr) {
            _voice_input->cancel_for_home(now_ms);
        }
        switch_decision =
            _agent_panel_model.voice_cleanup_complete();
    }
    if (switch_decision.action != AgentPanelAction::SwitchAgent) {
        return;
    }
    const auto before_selection = _agent_registry.snapshot();
    int64_t connected_at = current_unix_milliseconds();
    if (connected_at == 0) {
        const secure_pairing::AgentRecord* existing =
            _agent_registry.find(switch_decision.fingerprint);
        connected_at = existing == nullptr
            ? 0
            : existing->last_connected_at;
    }
    if (_agent_registry.select(
            switch_decision.fingerprint,
            connected_at) !=
            secure_pairing::RegistryResult::Ok ||
        !save_agent_registry()) {
        (void)secure_pairing::AgentRegistry::restore(
            before_selection, _agent_registry);
        set_status("AGENT ERR", kCodexRed);
        show_agent_panel_notice("Couldn't switch", now_ms);
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
    _agent_pairing_expired = false;
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
        int64_t connected_at = current_unix_milliseconds();
        if (connected_at == 0) {
            const secure_pairing::AgentRecord* existing =
                _agent_registry.find(fingerprint);
            connected_at = existing == nullptr
                ? 0
                : existing->last_connected_at;
        }
        if (_agent_registry.select(
                fingerprint,
                connected_at) ==
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
        _pairing_completion = PairingCompletion::None;
        _pairing_completion_transaction = 0;
        _pairing_allow_switch = false;
        _agent_pairing_expired = true;
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
            _agent_pairing_expired = false;
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
        show_agent_panel_notice("Couldn't switch", GetHAL().millis());
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

AppCodexStatus::TaskFocusStateSnapshot
AppCodexStatus::snapshot_task_focus_state()
{
    std::lock_guard<std::mutex> task_lock(_task_state_mutex);
    TaskFocusStateSnapshot snapshot;
    snapshot.deck = _task_focus_deck;
    snapshot.mode = _task_focus_mode;
    snapshot.selected_route = _task_focus_deck.selected_route();
    if (const CodexTaskAlert* selected = _task_focus_deck.selected()) {
        snapshot.selected_task = *selected;
        snapshot.has_selected_task = true;
        if (const CodexTaskDetail* detail =
                _task_detail_store.find(selected->route)) {
            snapshot.detail_text = detail->text;
            snapshot.detail_revision = detail->revision;
            snapshot.has_detail = true;
        }
    }
    return snapshot;
}

void AppCodexStatus::sync_task_edge_exclusion(CodexTaskFocusMode mode)
{
    GetGlobalEdgeControlService().setTouchExclusion(
        mode == CodexTaskFocusMode::Status
            ? std::optional<EdgeControlRect>{kTaskDeckTouchExclusion}
            : std::optional<EdgeControlRect>{kTaskModeTouchExclusion});
}

bool AppCodexStatus::sync_task_detail_credential()
{
    bool content_cleared = false;
    std::lock_guard<std::mutex> task_lock(_task_state_mutex);
    if (_task_detail_crypto == nullptr) {
        _task_detail_crypto =
            std::make_unique<voice_input::crypto::CcmContext>();
    }
    if (!_agent_registry_ready || !_agent_registry.initialized()) {
        if (_task_detail_key_epoch != 0) {
            _task_detail_store.reset();
            _task_detail_crypto->clear();
            _task_detail_key_epoch = 0;
            content_cleared = true;
        }
        return content_cleared;
    }
    const uint32_t key_epoch = _agent_registry.key_epoch();
    if (key_epoch == 0 || key_epoch == _task_detail_key_epoch) {
        return false;
    }
    _task_detail_store.reset_for_credential(key_epoch);
    content_cleared = true;
    if (!_task_detail_crypto->set_key(_agent_registry.master_key())) {
        _task_detail_store.reset();
        _task_detail_key_epoch = 0;
        return content_cleared;
    }
    _task_detail_key_epoch = key_epoch;
    return content_cleared;
}

void AppCodexStatus::sync_task_voice_route()
{
    const TaskFocusStateSnapshot task = snapshot_task_focus_state();
    const uint64_t route =
        task.mode == CodexTaskFocusMode::TaskDetail ? task.selected_route : 0;
    {
        std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
        if (_voice_input != nullptr) {
            (void)_voice_input->set_task_route(route);
        }
    }
    sync_task_voice_readiness();
}

void AppCodexStatus::sync_task_voice_readiness()
{
    std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
    if (_voice_input == nullptr) {
        _task_voice_ready_applied = false;
        return;
    }
    const bool usable = _production_voice_link_usable;
    if (_task_voice_ready_applied &&
        _voice_input->ui_state() == VoiceInputUiState::Active) {
        return;
    }
    if (usable == _task_voice_ready_applied) {
        return;
    }
    _voice_input->set_ready(
        usable, _production_voice_mtu, _production_voice_capabilities);
    _task_voice_ready_applied = usable;
}

VoiceButtonSink* AppCodexStatus::task_focus_voice_sink()
{
    std::lock_guard<std::mutex> controller_lock(_task_controller_mutex);
    return _voice_input != nullptr &&
               (_voice_input->ui_state() == VoiceInputUiState::Active ||
                _production_voice_link_usable)
        ? _voice_input
        : nullptr;
}

bool AppCodexStatus::sync_agent_panel_for_task_mode(
    CodexTaskFocusMode task_mode
)
{
    if (_agent_panel_root == nullptr) {
        return false;
    }
    const AgentPanelSnapshot snapshot = _agent_panel_model.snapshot();
    const bool hide_dormant_panel =
        (task_mode != CodexTaskFocusMode::Status || _current_page != 1) &&
        !snapshot.visible;
    const bool hidden =
        lv_obj_has_flag(_agent_panel_root, LV_OBJ_FLAG_HIDDEN);
    if (hide_dormant_panel) {
        if (!hidden) {
            lv_anim_delete(this, agent_panel_animation_cb);
            lv_obj_add_flag(_agent_panel_root, LV_OBJ_FLAG_HIDDEN);
        }
        _agent_panel_model.cancel_touch();
        _last_agent_touch_state = LV_INDEV_STATE_REL;
        return false;
    }
    if (hidden) {
        lv_obj_clear_flag(_agent_panel_root, LV_OBJ_FLAG_HIDDEN);
        settle_agent_panel(false);
    }
    return true;
}

void AppCodexStatus::sync_task_focus_visibility()
{
    if (_task_focus_view == nullptr) {
        return;
    }
    const TaskFocusStateSnapshot task = snapshot_task_focus_state();
    (void)sync_agent_panel_for_task_mode(task.mode);
    const bool visible =
        (task.mode != CodexTaskFocusMode::Status ||
         _task_deck_drag_active) &&
        _current_page == 1 &&
        !_agent_panel_model.snapshot().visible &&
        !_agent_panel_model.owns_touch();
    _task_focus_view->set_visible(visible);
}

void AppCodexStatus::render_task_focus()
{
    const TaskFocusStateSnapshot task = snapshot_task_focus_state();
    if (_task_focus_view == nullptr ||
        task.mode == CodexTaskFocusMode::Status) {
        return;
    }
    if (task.mode == CodexTaskFocusMode::TaskDetail) {
        render_task_focus_detail();
    } else {
        _task_focus_view->show_deck(
            task.deck,
            _ble_transport != nullptr && _ble_transport->connected());
        sync_task_focus_visibility();
    }
}

void AppCodexStatus::render_task_focus_detail()
{
    if (_task_focus_view == nullptr) {
        return;
    }
    TaskFocusStateSnapshot task = snapshot_task_focus_state();
    if (!task.has_selected_task) {
        {
            std::lock_guard<std::mutex> task_lock(_task_state_mutex);
            _task_focus_mode = CodexTaskFocusMode::TaskDeck;
        }
        sync_task_edge_exclusion(CodexTaskFocusMode::TaskDeck);
        render_task_focus();
        return;
    }
    _task_focus_view->show_detail(
        &task.selected_task,
        task.has_detail ? task.detail_text.c_str() : nullptr,
        task.detail_revision,
        _ble_transport != nullptr && _ble_transport->connected());
    sync_task_focus_visibility();
}

void AppCodexStatus::open_task_deck()
{
    if (_current_page != 1) {
        return;
    }
    _page_swipe.cancel();
    _task_deck_auto_dismiss_latched = false;
    _last_page_touch_state = LV_INDEV_STATE_REL;
    {
        std::lock_guard<std::mutex> task_lock(_task_state_mutex);
        _task_focus_mode = CodexTaskFocusMode::TaskDeck;
    }
    sync_task_edge_exclusion(CodexTaskFocusMode::TaskDeck);
    sync_task_voice_readiness();
    render_task_focus();
}


void AppCodexStatus::set_task_focus_y(int y)
{
    if (_task_focus_view != nullptr) {
        _task_focus_view->set_vertical_offset(y);
    }
}


void AppCodexStatus::settle_task_deck_entry(bool open, bool animate)
{
    if (_task_focus_view == nullptr) {
        _task_deck_drag_active = false;
        return;
    }
    const int target_y = open ? 0 : kTaskDeckHiddenY;
    const int current_y = _task_focus_view->vertical_offset();
    lv_anim_delete(this, task_deck_animation_cb);
    if (!animate || current_y == target_y) {
        set_task_focus_y(target_y);
        _task_deck_drag_active = false;
        if (!open) {
            _task_focus_view->set_visible(false);
        }
        sync_pending_glow();
        return;
    }

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, this);
    lv_anim_set_user_data(&animation, this);
    lv_anim_set_exec_cb(&animation, task_deck_animation_cb);
    lv_anim_set_values(&animation, current_y, target_y);
    lv_anim_set_duration(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(
        &animation, task_deck_animation_completed_cb);
    lv_anim_start(&animation);
    sync_pending_glow();
}


void AppCodexStatus::task_deck_animation_cb(void* target, int32_t y)
{
    auto* self = static_cast<AppCodexStatus*>(target);
    if (self != nullptr) {
        self->set_task_focus_y(y);
    }
}


void AppCodexStatus::task_deck_animation_completed_cb(lv_anim_t* animation)
{
    auto* self = static_cast<AppCodexStatus*>(
        lv_anim_get_user_data(animation));
    if (self == nullptr || self->_task_focus_view == nullptr) {
        return;
    }
    self->_task_deck_drag_active = false;
    if (self->_task_focus_view->vertical_offset() >= kTaskDeckHiddenY) {
        self->_task_focus_view->set_visible(false);
    }
    self->sync_pending_glow();
}

void AppCodexStatus::cancel_task_focus_touch()
{
    if (_task_focus_view != nullptr) {
        _task_focus_view->set_deck_scroll_offset(0);
    }
    _last_task_touch_state = LV_INDEV_STATE_REL;
    _task_touch_start_x = 0;
    _task_touch_start_y = 0;
    _task_touch_last_y = 0;
    _task_touch_moved = false;
    _task_deck_started_on_handle = false;
    _task_detail_started_at_top = false;
    _task_touch_axis =
        codex_task_focus_gesture::TaskFocusGestureAxis::Undecided;
}

void AppCodexStatus::update_task_focus_touch(uint32_t now_ms)
{
    (void)now_ms;
    lv_indev_t* indev = GetHAL().lvTouchpad;
    auto& controls = GetGlobalEdgeControlService();
    if (indev == nullptr || _screen_off || controls.ownsTouch() ||
        controls.lastReleaseConsumed() ||
        GetGlobalDisplayProtectionService().touchInputSuppressed()) {
        cancel_task_focus_touch();
        return;
    }
    const lv_indev_state_t state = lv_indev_get_state(indev);
    const CodexTaskFocusMode task_mode =
        snapshot_task_focus_state().mode;
    lv_point_t point = {};
    lv_indev_get_point(indev, &point);
    if (state == LV_INDEV_STATE_PR) {
        if (_last_task_touch_state == LV_INDEV_STATE_REL) {
            _task_touch_start_x = point.x;
            _task_touch_start_y = point.y;
            _task_touch_last_y = point.y;
            _task_touch_moved = false;
            _task_touch_axis =
                codex_task_focus_gesture::TaskFocusGestureAxis::Undecided;
            _task_deck_started_on_handle =
                task_mode == CodexTaskFocusMode::TaskDeck &&
                codex_task_focus_gesture::is_deck_handle_start(
                    point.x, point.y);
            _task_detail_started_at_top =
                _task_focus_view != nullptr &&
                _task_focus_view->detail_at_top();
        } else {
            const int dy = point.y - _task_touch_last_y;
            _task_touch_last_y = point.y;
            const int total_dx = point.x - _task_touch_start_x;
            const int total_dy = point.y - _task_touch_start_y;
            const auto previous_axis = _task_touch_axis;
            _task_touch_axis = codex_task_focus_gesture::update_axis(
                _task_touch_axis,
                total_dx,
                total_dy,
                kTaskTapTolerance);
            if (std::abs(total_dy) >=
                    kTaskTapTolerance ||
                std::abs(total_dx) >=
                    kTaskTapTolerance) {
                _task_touch_moved = true;
            }
            if (task_mode == CodexTaskFocusMode::TaskDeck &&
                _task_focus_view != nullptr &&
                _task_touch_axis ==
                    codex_task_focus_gesture::TaskFocusGestureAxis::Vertical) {
                if (_task_deck_started_on_handle) {
                    if (!_task_deck_drag_active) {
                        lv_anim_delete(this, task_deck_animation_cb);
                        _task_deck_drag_active = true;
                        sync_pending_glow();
                    }
                    const int drag_offset =
                        codex_task_focus_gesture::
                            task_deck_handle_drag_offset(
                                total_dy, kTaskDeckHiddenY);
                    set_task_focus_y(drag_offset);
                    if (codex_task_focus_gesture::
                            should_auto_dismiss_task_deck(
                                total_dx,
                                total_dy,
                                _task_touch_axis,
                                true,
                                drag_offset,
                                kTaskDeckAutoDismissY)) {
                        _task_deck_auto_dismiss_latched = true;
                        {
                            std::lock_guard<std::mutex> task_lock(
                                _task_state_mutex);
                            _task_focus_mode =
                                CodexTaskFocusMode::Status;
                        }
                        sync_task_edge_exclusion(
                            CodexTaskFocusMode::Status);
                        sync_task_voice_readiness();
                        settle_task_deck_entry(false, true);
                        cancel_task_focus_touch();
                        return;
                    }
                } else {
                    const TaskFocusStateSnapshot task =
                        snapshot_task_focus_state();
                    const bool has_previous =
                        task.deck.focused_index() > 0;
                    const bool has_next =
                        task.deck.focused_index() + 1 < task.deck.count();
                    _task_focus_view->set_deck_scroll_offset(
                        codex_task_focus_gesture::task_stream_drag_offset(
                            total_dy,
                            has_previous,
                            has_next,
                            codex_task_focus_layout::kStreamDragLimit));
                }
            } else if (task_mode == CodexTaskFocusMode::TaskDetail &&
                _task_focus_view != nullptr &&
                _task_touch_axis ==
                    codex_task_focus_gesture::TaskFocusGestureAxis::Vertical) {
                _task_focus_view->detail_scroll_by(
                    previous_axis ==
                            codex_task_focus_gesture::TaskFocusGestureAxis::Undecided
                        ? total_dy
                        : dy);
            }
        }
    } else if (_last_task_touch_state == LV_INDEV_STATE_PR) {
        const int total_dx = point.x - _task_touch_start_x;
        const int total_dy = point.y - _task_touch_start_y;
        _task_touch_axis = codex_task_focus_gesture::update_axis(
            _task_touch_axis,
            total_dx,
            total_dy,
            kTaskTapTolerance);
        if (task_mode == CodexTaskFocusMode::TaskDeck &&
            _task_deck_started_on_handle) {
            if (_task_deck_drag_active) {
                settle_task_deck_entry(true, true);
            }
            cancel_task_focus_touch();
            return;
        } else if (task_mode == CodexTaskFocusMode::TaskDeck &&
            _task_touch_axis ==
                codex_task_focus_gesture::TaskFocusGestureAxis::Vertical &&
            std::abs(total_dy) >= kTaskSwipeThreshold) {
            const int direction = total_dy < 0 ? 1 : -1;
            CodexTaskFocusEvent event = CodexTaskFocusEvent::None;
            {
                std::lock_guard<std::mutex> task_lock(_task_state_mutex);
                event = _task_focus_deck.browse(direction);
            }
            if (event == CodexTaskFocusEvent::StateBoundaryCrossed &&
                GetHAL().getButtonConfig().vibrateEnabled) {
                GetHAL().vibrate(20, 60);
            }
            if (event != CodexTaskFocusEvent::None) {
                render_task_focus();
            }
        } else if (task_mode == CodexTaskFocusMode::TaskDeck &&
                   !_task_touch_moved &&
                   _task_focus_view != nullptr &&
                   _task_focus_view->contains_focus_point(point.x, point.y)) {
            {
                std::lock_guard<std::mutex> task_lock(_task_state_mutex);
                (void)_task_focus_deck.select_focused();
                _task_focus_mode = CodexTaskFocusMode::TaskDetail;
            }
            sync_task_voice_route();
            sync_task_edge_exclusion(CodexTaskFocusMode::TaskDetail);
            sync_task_voice_readiness();
            render_task_focus_detail();
        } else if (task_mode == CodexTaskFocusMode::TaskDetail &&
                   codex_task_focus_gesture::is_detail_return_gesture(
                       total_dx,
                       total_dy,
                       _task_touch_axis,
                       _task_detail_started_at_top,
                       kTaskSwipeThreshold)) {
            {
                std::lock_guard<std::mutex> task_lock(_task_state_mutex);
                _task_focus_mode = CodexTaskFocusMode::TaskDeck;
            }
            sync_task_edge_exclusion(CodexTaskFocusMode::TaskDeck);
            sync_task_voice_readiness();
            render_task_focus();
        }
        cancel_task_focus_touch();
        return;
    }
    _last_task_touch_state = state;
}

void AppCodexStatus::update_page_swipe(uint32_t now_ms)
{
    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr) {
        if (_task_deck_drag_active) {
            settle_task_deck_entry(false, true);
        }
        _page_swipe.cancel();
        _last_page_touch_state = LV_INDEV_STATE_REL;
        return;
    }

    const lv_indev_state_t state = lv_indev_get_state(indev);
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    auto& controls = GetGlobalEdgeControlService();
    const bool display_suppressed =
        GetGlobalDisplayProtectionService().touchInputSuppressed();
    const bool edge_owned = controls.ownsTouch();
    const bool edge_consumed = controls.lastReleaseConsumed();
    if (display_suppressed) {
        if (_task_deck_drag_active) {
            settle_task_deck_entry(false, true);
        }
        _page_swipe.cancel();
        _last_page_touch_state = LV_INDEV_STATE_REL;
        return;
    }
    if (_screen_off ||
        edge_owned || edge_consumed) {
        if (_task_deck_drag_active) {
            settle_task_deck_entry(false, true);
        }
        _page_swipe.cancel();
        _last_page_touch_state = LV_INDEV_STATE_REL;
        return;
    }
    if (state == LV_INDEV_STATE_PR) {
        if (_last_page_touch_state == LV_INDEV_STATE_REL) {
            _page_swipe.press(
                point.x,
                point.y,
                now_ms,
                _current_page == 4 &&
                    _trae_status_page.contains_task_point(point.x, point.y)
            );
        } else {
            _page_swipe.move(point.x, point.y);
            const int task_drag_offset =
                _page_swipe.task_deck_drag_offset(1);
            if (_page_swipe.task_deck_drag_active(1) &&
                _task_focus_view != nullptr) {
                if (!_task_deck_drag_active) {
                    lv_anim_delete(this, task_deck_animation_cb);
                    _task_deck_drag_active = true;
                    sync_pending_glow();
                    _task_deck_drag_start_y = kTaskDeckHiddenY;
                    const TaskFocusStateSnapshot task =
                        snapshot_task_focus_state();
                    _task_focus_view->show_deck(
                        task.deck,
                        _ble_transport != nullptr &&
                            _ble_transport->connected());
                    _task_focus_view->set_visible(true);
                }
                set_task_focus_y(std::clamp(
                    _task_deck_drag_start_y - task_drag_offset,
                    0,
                    kTaskDeckHiddenY));
            }
        }
    } else if (_last_page_touch_state == LV_INDEV_STATE_PR) {
        const StatusPageSwipeResult result =
            _page_swipe.release(now_ms, _current_page);
        if (result == StatusPageSwipeResult::PreviousPage ||
            result == StatusPageSwipeResult::NextPage) {
            const int direction = result == StatusPageSwipeResult::NextPage
                ? 1
                : -1;
            step_page(direction);
        } else if (result == StatusPageSwipeResult::TraeTasksForward) {
            if (_trae_task_carousel.scroll_forward()) {
                render_trae_status_page(current_status_channels());
            }
        } else if (result == StatusPageSwipeResult::TraeTasksBackward) {
            if (_trae_task_carousel.scroll_backward()) {
                render_trae_status_page(current_status_channels());
            }
        } else if (result == StatusPageSwipeResult::OpenTaskDeck) {
            open_task_deck();
            settle_task_deck_entry(true, true);
        } else if (_task_deck_drag_active) {
            settle_task_deck_entry(false, true);
        }
    }
    _last_page_touch_state = state;
}

void AppCodexStatus::step_page(int direction)
{
    const int slot = page_slot_for_logical(_current_page);
    const int next_slot = slot + direction;
    if (slot >= 0 && next_slot >= 0 &&
        next_slot < static_cast<int>(_active_page_count)) {
        set_page(_active_page_order[static_cast<size_t>(next_slot)], true);
    }
}

void AppCodexStatus::set_page(int page, bool animate)
{
    if (_page_strip == nullptr) {
        return;
    }
    const int slot = page_slot_for_logical(page);
    if (slot < 0) {
        return;
    }
    if (snapshot_task_focus_state().mode != CodexTaskFocusMode::Status) {
        return;
    }
    const int target_x = -slot * kPageWidth;
    const int current_x = lv_obj_get_x(_page_strip);
    _page_swipe.cancel();
    cancel_task_focus_touch();
    _last_page_touch_state = LV_INDEV_STATE_REL;
    _current_page = page;
    _trae_status_page.set_active(page == 4);
    sync_pending_glow();
    sync_pet_runtime(GetHAL().millis());
    _pinned_microphone_controller.page_changed(page);
    update_page_indicator();
    (void)sync_agent_panel_for_task_mode(CodexTaskFocusMode::Status);
    if (page == 1) {
        render_alert();
    } else if (page == 2 || page == 3) {
        render_telemetry_pages();
    } else if (page == 4) {
        render_trae_status_page(current_status_channels());
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
    if (_page_dots[0] == nullptr) {
        return;
    }
    const bool geometry_changed = _page_indicator_count != _active_page_count;
    const int active_slot = page_slot_for_logical(_current_page);
    for (size_t index = 0; index < _page_dots.size(); ++index) {
        lv_obj_t* dot = _page_dots[index];
        if (dot == nullptr) {
            continue;
        }
        const bool visible = index < _active_page_count;
        const bool hidden = lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN);
        if (visible && hidden) {
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
        } else if (!visible && !hidden) {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
        if (!visible) {
            continue;
        }
        if (geometry_changed) {
            const int total_width =
                static_cast<int>(_active_page_count - 1) * 16 + 8;
            const int start_x = (kPageWidth - total_width) / 2;
            lv_obj_set_x(dot, start_x + static_cast<int>(index) * 16);
        }
        if (geometry_changed || _page_indicator_slot != active_slot) {
            lv_obj_set_style_bg_opa(
                dot,
                static_cast<int>(index) == active_slot
                    ? LV_OPA_COVER
                    : LV_OPA_TRANSP,
                0);
        }
    }
    _page_indicator_count = _active_page_count;
    _page_indicator_slot = active_slot;
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
    const bool surface_visible =
        _current_page == 1 &&
        !_task_deck_drag_active &&
        snapshot_task_focus_state().mode == CodexTaskFocusMode::Status;
    _pending_glow->set_state(
        _alert_carousel.display_state(), screen_off, surface_visible);
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
    _screen_off_voice_policy.reset();
    if (woke_screen) {
        _suppress_task_navigation = true;
    }
    _page_swipe.cancel();
    cancel_task_focus_touch();
    _last_page_touch_state = LV_INDEV_STATE_REL;
    sync_pending_glow();
    sync_pet_runtime(GetHAL().millis());
    if (_pet_timer != nullptr) {
        if (_screen_off) {
            lv_timer_pause(_pet_timer);
        } else {
            lv_timer_resume(_pet_timer);
        }
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
        set_label_text_if_changed(label, text);
    }
    if (bar != nullptr && lv_bar_get_value(bar) != value) {
        lv_bar_set_value(bar, value, LV_ANIM_OFF);
    }
}

void AppCodexStatus::pet_timer_cb(lv_timer_t* timer)
{
    auto* self = static_cast<AppCodexStatus*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->_pet_image == nullptr ||
        self->_pet_runtime == nullptr) {
        return;
    }

    const uint32_t now_ms = GetHAL().millis();
    self->_pet_runtime->advance(now_ms);
    if (self->_pet_runtime->take_frame_changed()) {
        self->apply_pet_image();
    }
    lv_timer_set_period(
        timer, self->_pet_runtime->next_frame_delay_ms(now_ms));
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
            self->_last_status_payload_ms = 0;
            self->clear_telemetry();
            self->set_status("WAITING", kTextDim);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            self->_ws_connected = false;
            if (self->_ble_transport == nullptr ||
                !self->_ble_transport->connected()) {
                self->_last_status_payload_ms = 0;
            }
            if (self->_ws_client != nullptr) {
                self->set_status("RETRY", kCodexRed);
            }
            if (self->_ble_transport == nullptr ||
                !self->_ble_transport->connected()) {
                self->clear_telemetry();
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            self->_ws_connected = false;
            if (self->_ble_transport == nullptr ||
                !self->_ble_transport->connected()) {
                self->_last_status_payload_ms = 0;
            }
            self->set_status("WS ERR", kCodexRed);
            if (self->_ble_transport == nullptr ||
                !self->_ble_transport->connected()) {
                self->clear_telemetry();
            }
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
