/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "env_iii_view.h"

#include "../assets/env_fonts.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kPanelSize = 466;

constexpr std::uint32_t kBackground = 0x080D13;
constexpr std::uint32_t kPrimary = 0xEEF7FF;
constexpr std::uint32_t kSecondary = 0x91A2B6;
constexpr std::uint32_t kWarning = 0xE8B881;
constexpr std::uint32_t kRule = 0x273545;

struct SensorDisplayState {
    bool live = false;
    bool stale = false;
    bool read_error = false;
    bool offline = false;
    bool unavailable = false;
};

bool has_fresh_sample(const EnvSensorStatus& status, float value, std::uint32_t now_ms)
{
    return status.state == EnvSensorState::Ready && status.has_sample && std::isfinite(value) &&
           static_cast<std::uint32_t>(now_ms - status.sampled_at_ms) <= EnvSnapshot::stale_after_ms;
}

SensorDisplayState get_sensor_display_state(
    const EnvSensorStatus& status,
    bool value_is_finite,
    std::uint32_t now_ms)
{
    SensorDisplayState result;
    result.live = status.state == EnvSensorState::Ready && status.has_sample && value_is_finite &&
                  static_cast<std::uint32_t>(now_ms - status.sampled_at_ms) <= EnvSnapshot::stale_after_ms;
    result.stale = status.state == EnvSensorState::Ready && status.has_sample &&
                   static_cast<std::uint32_t>(now_ms - status.sampled_at_ms) > EnvSnapshot::stale_after_ms;
    if (result.live || result.stale) {
        return result;
    }

    switch (status.state) {
    case EnvSensorState::ReadError:
        result.read_error = true;
        break;
    case EnvSensorState::Initializing:
    case EnvSensorState::Ready:
        break;
    case EnvSensorState::NotDetected:
        result.offline = true;
        break;
    case EnvSensorState::Unavailable:
        result.unavailable = true;
        break;
    }
    return result;
}

lv_obj_t* make_label(
    lv_obj_t* parent,
    int x,
    int y,
    int width,
    int height,
    const lv_font_t* font,
    std::uint32_t color,
    const char* text)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(label, text);
    return label;
}

const char* missing_sensor_description(const SensorDisplayState& state)
{
    if (state.stale) {
        return "数据已过期";
    }
    if (state.read_error) {
        return "读取失败";
    }
    if (state.unavailable) {
        return "不可用";
    }
    if (state.offline) {
        return "离线";
    }
    return "正在检测";
}

void set_label_text(lv_obj_t* label, const char* text)
{
    if (label != nullptr) {
        const char* current_text = lv_label_get_text(label);
        if (current_text != nullptr && std::strcmp(current_text, text) == 0) {
            return;
        }
        lv_label_set_text(label, text);
    }
}

void set_label_color(
    lv_obj_t* label,
    std::uint32_t color,
    std::uint32_t& previous_color,
    bool& has_previous_color)
{
    if (!has_previous_color || previous_color != color) {
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        previous_color = color;
        has_previous_color = true;
    }
}

}  // namespace

namespace view {

EnvIIIView::~EnvIIIView()
{
    if (_panel != nullptr) {
        lv_obj_delete(_panel);
        _panel = nullptr;
    }
}

void EnvIIIView::init(lv_obj_t* parent)
{
    _panel = lv_obj_create(parent);
    lv_obj_set_size(_panel, kPanelSize, kPanelSize);
    lv_obj_align(_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(_panel, 0, 0);
    lv_obj_set_style_border_width(_panel, 0, 0);
    lv_obj_set_style_outline_width(_panel, 0, 0);
    lv_obj_set_style_shadow_width(_panel, 0, 0);
    lv_obj_set_style_pad_all(_panel, 0, 0);
    lv_obj_set_style_bg_color(_panel, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Rasterize at the final size: scaling the shared 2-bit fonts magnifies their
    // edge steps. Keep the approved centers and baselines without a transform.
    _temperature_label = make_label(
        _panel, 55, 100, 356, 70, &EnvValue91, kPrimary, "--");

    _temperature_caption = make_label(_panel, 100, 207, 266, 20, &EnvCaption16, kSecondary, "温度 / °C");

    lv_obj_t* divider = lv_obj_create(_panel);
    lv_obj_set_pos(divider, 70, 250);
    lv_obj_set_size(divider, 326, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(kRule), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    _humidity_label = make_label(_panel, 61, 278, 170, 36, &EnvValue42, kPrimary, "--");
    _pressure_label = make_label(_panel, 235, 278, 170, 36, &EnvValue42, kPrimary, "--");
    make_label(_panel, 61, 328, 170, 20, &EnvCaption16, kSecondary, "湿度 / %");
    _pressure_caption = make_label(_panel, 227, 328, 186, 20, &EnvCaption16, kSecondary, "气压 / hPa");
}

void EnvIIIView::update(const EnvSnapshot& snapshot, std::uint32_t now_ms)
{
    const bool climate_live = has_fresh_sample(snapshot.climate, snapshot.temperature_c, now_ms) &&
                              std::isfinite(snapshot.humidity_percent);
    const bool pressure_live = has_fresh_sample(snapshot.pressure, snapshot.pressure_hpa, now_ms);
    const SensorDisplayState climate_state = get_sensor_display_state(
        snapshot.climate, std::isfinite(snapshot.temperature_c) && std::isfinite(snapshot.humidity_percent), now_ms);
    const SensorDisplayState pressure_state = get_sensor_display_state(
        snapshot.pressure, std::isfinite(snapshot.pressure_hpa), now_ms);

    char temperature[20] = "--";
    char humidity[20] = "--";
    char pressure[20] = "--";
    if (climate_live) {
        std::snprintf(temperature, sizeof(temperature), "%.1f", static_cast<double>(snapshot.temperature_c));
        std::snprintf(humidity, sizeof(humidity), "%.1f", static_cast<double>(snapshot.humidity_percent));
    }
    if (pressure_live) {
        std::snprintf(pressure, sizeof(pressure), "%.1f", static_cast<double>(snapshot.pressure_hpa));
    }
    set_label_text(_temperature_label, temperature);
    set_label_text(_humidity_label, humidity);
    set_label_text(_pressure_label, pressure);

    char temperature_caption[64] = {};
    char pressure_caption[64] = {};
    if (climate_live) {
        std::snprintf(temperature_caption, sizeof(temperature_caption), "温度 / °C");
    } else {
        std::snprintf(
            temperature_caption, sizeof(temperature_caption), "温度 / °C · %s", missing_sensor_description(climate_state));
    }
    if (pressure_live) {
        std::snprintf(pressure_caption, sizeof(pressure_caption), "气压 / hPa");
    } else {
        std::snprintf(
            pressure_caption, sizeof(pressure_caption), "气压 / hPa · %s", missing_sensor_description(pressure_state));
    }
    set_label_text(_temperature_caption, temperature_caption);
    set_label_text(_pressure_caption, pressure_caption);

    set_label_color(
        _temperature_caption, climate_live ? kSecondary : kWarning, _temperature_caption_color, _has_temperature_caption_color);
    set_label_color(
        _pressure_caption, pressure_live ? kSecondary : kWarning, _pressure_caption_color, _has_pressure_caption_color);
}

}  // namespace view
