/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "assistant_view.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include <assets/assets.h>

LV_FONT_DECLARE(font_puhui_14_1)

using namespace smooth_ui_toolkit::lvgl_cpp;

namespace {
constexpr int panel_size = 466;
constexpr int safe_text_width = 370;
constexpr int status_y = 34;
constexpr int status_line_height = 22;
constexpr int phase_y = 68;
constexpr int wave_y = 88;
constexpr int wave_height = 132;
constexpr int title_y = 228;
constexpr int divider_y = 276;
constexpr int content_y = 294;
constexpr int content_height = 60;
constexpr int hint_bottom = -74;
constexpr int battery_y = 419;
constexpr int network_dot_x = 102;
constexpr int network_label_x = 116;
constexpr int wave_bar_width = 8;
constexpr int wave_total_width = 222;

constexpr uint32_t background_color = 0x05070A;
constexpr uint32_t primary_color = 0xF7FBFF;
constexpr uint32_t secondary_color = 0xC7D1DD;
constexpr uint32_t inactive_color = 0x637185;
constexpr uint32_t inactive_wave_color = 0x293641;
constexpr uint32_t cyan_color = 0x2BC8FF;
constexpr uint32_t teal_color = 0x3BD6C7;
constexpr uint32_t indigo_color = 0x697CFF;
constexpr uint32_t amber_color = 0xF1C85A;
constexpr uint32_t failure_color = 0xFF6673;
constexpr uint32_t battery_neutral_color = 0x788391;
constexpr uint32_t battery_charging_color = 0x3CB371;
constexpr uint32_t battery_low_color = 0xE74C3C;

using WaveFrame = std::array<int16_t, 15>;
constexpr WaveFrame inactive_wave{16, 18, 16, 24, 18, 32, 22, 44, 22, 32, 18, 24, 16, 18, 16};
constexpr std::array<WaveFrame, 4> active_wave{{
    {18, 28, 42, 58, 76, 66, 88, 104, 88, 66, 76, 58, 42, 28, 18},
    {28, 46, 68, 44, 82, 58, 96, 72, 102, 64, 86, 48, 70, 36, 24},
    {44, 30, 76, 52, 94, 62, 84, 108, 78, 98, 56, 88, 46, 64, 34},
    {22, 40, 58, 80, 50, 92, 68, 100, 74, 88, 54, 76, 48, 34, 20},
}};
constexpr std::array<WaveFrame, 4> thinking_wave{{
    {8, 10, 13, 17, 22, 28, 35, 44, 35, 28, 22, 17, 13, 10, 8},
    {10, 13, 17, 22, 28, 35, 44, 55, 44, 35, 28, 22, 17, 13, 10},
    {13, 17, 22, 28, 35, 44, 54, 66, 54, 44, 35, 28, 22, 17, 13},
    {10, 13, 17, 22, 28, 35, 44, 55, 44, 35, 28, 22, 17, 13, 10},
}};

const char* battery_symbol(uint8_t percent)
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

void configure_raw_object(lv_obj_t* object)
{
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
}

} // namespace

void AssistantView::init(lv_obj_t* parent)
{
    destroy();

    _panel = std::make_unique<Container>(parent);
    _panel->setSize(panel_size, panel_size);
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _panel->setBgColor(lv_color_hex(background_color));
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _network_dot = lv_obj_create(_panel->get());
    configure_raw_object(_network_dot);
    lv_obj_set_size(_network_dot, 8, 8);
    lv_obj_set_pos(_network_dot, network_dot_x, status_y + 7);
    lv_obj_set_style_radius(_network_dot, LV_RADIUS_CIRCLE, 0);

    _network_label = std::make_unique<Label>(_panel->get());
    _network_label->setWidth(84);
    _network_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
    _network_label->setTextFont(&font_puhui_14_1);
    _network_label->align(LV_ALIGN_TOP_LEFT, network_label_x, status_y);

    _update_label = std::make_unique<Label>(_panel->get());
    _update_label->setSize(160, status_line_height);
    _update_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _update_label->setTextFont(&font_puhui_14_1);
    _update_label->setTextColor(lv_color_hex(amber_color));
    lv_label_set_long_mode(_update_label->get(), LV_LABEL_LONG_MODE_DOTS);
    _update_label->align(LV_ALIGN_TOP_RIGHT, -112, status_y);
    _update_label->setText("");

    _battery_label = lv_label_create(_panel->get());
    lv_obj_set_style_text_font(_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_battery_label, 150);
    lv_obj_set_style_text_align(_battery_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_battery_label, LV_ALIGN_TOP_MID, 0, battery_y);

    _phase_label = std::make_unique<Label>(_panel->get());
    _phase_label->setWidth(safe_text_width);
    _phase_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _phase_label->setTextFont(&font_puhui_14_1);
    _phase_label->align(LV_ALIGN_TOP_MID, 0, phase_y);

    _wave_container = lv_obj_create(_panel->get());
    configure_raw_object(_wave_container);
    lv_obj_set_size(_wave_container, panel_size, wave_height);
    lv_obj_set_pos(_wave_container, 0, wave_y);
    const int wave_left = (panel_size - wave_total_width) / 2;
    for (size_t index = 0; index < _wave_bars.size(); ++index) {
        _wave_bars[index] = lv_obj_create(_wave_container);
        configure_raw_object(_wave_bars[index]);
        lv_obj_set_width(_wave_bars[index], wave_bar_width);
        lv_obj_set_style_radius(_wave_bars[index], wave_bar_width / 2, 0);
        const int bar_x = wave_left +
                          static_cast<int>(index) * (wave_total_width - wave_bar_width) /
                              static_cast<int>(_wave_bars.size() - 1);
        lv_obj_set_pos(_wave_bars[index], bar_x, 0);
    }

    _title_label = std::make_unique<Label>(_panel->get());
    _title_label->setWidth(safe_text_width);
    _title_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _title_label->setTextFont(&lv_font_montserrat_24);
    _title_label->setTextColor(lv_color_hex(primary_color));
    _title_label->align(LV_ALIGN_TOP_MID, 0, title_y);

    _divider = lv_obj_create(_panel->get());
    configure_raw_object(_divider);
    lv_obj_set_size(_divider, 346, 1);
    lv_obj_set_pos(_divider, 60, divider_y);
    lv_obj_set_style_bg_color(_divider, lv_color_hex(inactive_color), 0);
    lv_obj_set_style_bg_opa(_divider, LV_OPA_40, 0);

    _content_label = std::make_unique<Label>(_panel->get());
    lv_obj_set_size(_content_label->get(), safe_text_width, content_height);
    _content_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _content_label->setTextFont(&font_puhui_14_1);
    _content_label->setTextColor(lv_color_hex(secondary_color));
    _content_label->align(LV_ALIGN_TOP_MID, 0, content_y);
    lv_label_set_long_mode(_content_label->get(), LV_LABEL_LONG_MODE_DOTS);

    _hint_label = std::make_unique<Label>(_panel->get());
    _hint_label->setWidth(safe_text_width);
    _hint_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _hint_label->setTextFont(&font_puhui_14_1);
    _hint_label->setTextColor(lv_color_hex(inactive_color));
    _hint_label->align(LV_ALIGN_BOTTOM_MID, 0, hint_bottom);

    setConnected(false);
    setBattery(0, false);
    setState(AssistantVisualState::Connecting);
}

void AssistantView::update(uint32_t now_ms)
{
    if (!_panel) {
        return;
    }

    uint32_t interval_ms = 0;
    switch (_state) {
        case AssistantVisualState::Listening:
            interval_ms = 60;
            break;
        case AssistantVisualState::Speaking:
            interval_ms = 120;
            break;
        case AssistantVisualState::Connecting:
        case AssistantVisualState::Activating:
        case AssistantVisualState::Thinking:
        case AssistantVisualState::Closing:
            interval_ms = 170;
            break;
        case AssistantVisualState::WaitingWake:
        case AssistantVisualState::Failed:
        case AssistantVisualState::StopTimedOut:
            return;
    }

    if (now_ms - _last_animation_ms < interval_ms) {
        return;
    }
    _last_animation_ms = now_ms;
    if (_state == AssistantVisualState::Listening) {
        applyWaveHeights(_input_waveform.update(_input_levels));
        return;
    }
    _frame_index = static_cast<uint8_t>((_frame_index + 1) % active_wave.size());
    applyWaveFrame(_frame_index);
}

void AssistantView::setInputLevels(const AssistantInputWaveform::InputLevels& levels)
{
    _input_levels = levels;
}

void AssistantView::setState(AssistantVisualState state)
{
    if (!_panel) {
        return;
    }
    _state = state;
    _last_animation_ms = 0;
    _frame_index = 0;
    _input_levels.fill(0);
    _input_waveform.reset();
    applyDefaultCopy();
    applyWaveFrame(_frame_index);
}

void AssistantView::setConversationText(const char* text)
{
    if (_content_label) {
        _content_label->setText(text != nullptr ? text : "");
    }
}

void AssistantView::setUpdateVersion(const char* canonical_version)
{
    if (!_update_label) {
        return;
    }
    if (canonical_version == nullptr || canonical_version[0] == '\0') {
        _update_label->setText("");
        return;
    }
    const std::string text = std::string("v") + canonical_version + " 可用";
    _update_label->setText(text);
}

void AssistantView::setBattery(uint8_t percent, bool charging)
{
    if (_battery_label == nullptr) {
        return;
    }

    const uint8_t clamped = std::min<uint8_t>(percent, 100);
    char text[32] = {};
    if (charging) {
        std::snprintf(text, sizeof(text), LV_SYMBOL_CHARGE " %u%%", static_cast<unsigned>(clamped));
    } else {
        std::snprintf(text, sizeof(text), "%s %u%%", battery_symbol(clamped), static_cast<unsigned>(clamped));
    }
    uint32_t color = battery_neutral_color;
    if (charging) {
        color = battery_charging_color;
    } else if (clamped < 15) {
        color = battery_low_color;
    }
    lv_label_set_text(_battery_label, text);
    lv_obj_set_style_text_color(_battery_label, lv_color_hex(color), 0);
}

void AssistantView::setConnected(bool connected)
{
    if (!_network_dot || !_network_label) {
        return;
    }

    const uint32_t color = connected ? cyan_color : inactive_color;
    lv_obj_set_style_bg_color(_network_dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(_network_dot, LV_OPA_COVER, 0);
    _network_label->setText(connected ? "在线" : "连接中");
    _network_label->setTextColor(lv_color_hex(color));
}

void AssistantView::destroy()
{
    _hint_label.reset();
    _content_label.reset();
    _title_label.reset();
    _phase_label.reset();
    _update_label.reset();
    _network_label.reset();
    _wave_bars.fill(nullptr);
    _wave_container = nullptr;
    _divider = nullptr;
    _network_dot = nullptr;
    _battery_label = nullptr;
    _input_levels.fill(0);
    _input_waveform.reset();
    _panel.reset();
}

void AssistantView::applyDefaultCopy()
{
    const char* phase = "";
    const char* title = "";
    const char* content = "";
    const char* hint = "双键长按返回";
    uint32_t wave_color = inactive_wave_color;
    bool show_hint = true;

    switch (_state) {
        case AssistantVisualState::Connecting:
            phase = "正在连接";
            title = "CONNECTING";
            content = "正在连接 Wi-Fi...";
            wave_color = cyan_color;
            break;
        case AssistantVisualState::Activating:
            phase = "正在激活";
            title = "XIAOZHI";
            content = "正在激活...";
            wave_color = cyan_color;
            break;
        case AssistantVisualState::WaitingWake:
            phase = "等待唤醒";
            title = "HELLO XIAOZHI";
            content = "说“你好小智”开始对话";
            wave_color = inactive_wave_color;
            break;
        case AssistantVisualState::Listening:
            phase = "正在聆听";
            title = "LISTENING";
            content = "请说";
            wave_color = cyan_color;
            break;
        case AssistantVisualState::Thinking:
            phase = "正在思考";
            title = "THINKING";
            content = "";
            wave_color = indigo_color;
            break;
        case AssistantVisualState::Speaking:
            phase = "正在播报";
            title = "XIAOZHI";
            content = "";
            wave_color = teal_color;
            break;
        case AssistantVisualState::Failed:
            phase = "暂不可用";
            title = "UNAVAILABLE";
            content = "助手启动失败";
            hint = "右键重试  双键返回";
            wave_color = failure_color;
            break;
        case AssistantVisualState::Closing:
            phase = "正在关闭";
            title = "CLOSING";
            content = "等待助手释放资源...";
            wave_color = inactive_color;
            show_hint = false;
            break;
        case AssistantVisualState::StopTimedOut:
            phase = "关闭超时";
            title = "PROTECTED";
            content = "关闭等待超时";
            hint = "右键再次停止";
            wave_color = failure_color;
            break;
    }

    _phase_label->setText(phase);
    _phase_label->setTextColor(lv_color_hex(wave_color));
    _title_label->setText(title);
    _content_label->setText(content);
    _hint_label->setText(hint);
    _hint_label->setHidden(!show_hint);
    applyWaveColor(wave_color);
}

void AssistantView::applyWaveFrame(uint8_t frame_index)
{
    const WaveFrame* frame = &inactive_wave;
    switch (_state) {
        case AssistantVisualState::Listening:
            applyWaveHeights(_input_waveform.update(_input_levels));
            return;
        case AssistantVisualState::Speaking:
            frame = &active_wave[frame_index % active_wave.size()];
            break;
        case AssistantVisualState::Connecting:
        case AssistantVisualState::Activating:
        case AssistantVisualState::Thinking:
        case AssistantVisualState::Closing:
            frame = &thinking_wave[frame_index % thinking_wave.size()];
            break;
        case AssistantVisualState::WaitingWake:
        case AssistantVisualState::Failed:
        case AssistantVisualState::StopTimedOut:
            break;
    }

    applyWaveHeights(*frame);
}

void AssistantView::applyWaveHeights(const AssistantInputWaveform::Frame& frame)
{
    for (size_t index = 0; index < _wave_bars.size(); ++index) {
        if (_wave_bars[index] == nullptr) {
            continue;
        }
        const int height = frame[index];
        lv_obj_set_height(_wave_bars[index], height);
        lv_obj_set_y(_wave_bars[index], (wave_height - height) / 2);
    }
}

void AssistantView::applyWaveColor(uint32_t color)
{
    for (lv_obj_t* bar : _wave_bars) {
        if (bar == nullptr) {
            continue;
        }
        lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    }
}
