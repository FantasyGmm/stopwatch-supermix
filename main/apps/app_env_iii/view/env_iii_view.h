/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <hal/env_sensor_snapshot.h>
#include <lvgl.h>
#include <cstdint>

namespace view {

class EnvIIIView {
public:
    ~EnvIIIView();

    void init(lv_obj_t* parent);
    void update(const EnvSnapshot& snapshot, std::uint32_t now_ms);

private:
    lv_obj_t* _panel = nullptr;
    lv_obj_t* _temperature_label = nullptr;
    lv_obj_t* _temperature_caption = nullptr;
    lv_obj_t* _humidity_label = nullptr;
    lv_obj_t* _pressure_label = nullptr;
    lv_obj_t* _pressure_caption = nullptr;
    std::uint32_t _temperature_caption_color = 0;
    std::uint32_t _pressure_caption_color = 0;
    bool _has_temperature_caption_color = false;
    bool _has_pressure_caption_color = false;
};

}  // namespace view
