/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>

namespace clock_view {

enum class Selection {
    None,
    Alarm,
    WatchFace,
    Stopwatch,
};

class ClockSelectionView {
public:
    explicit ClockSelectionView(bool alarmEnabled);
    ~ClockSelectionView();

    Selection consumeSelection();

private:
    lv_obj_t* _panel           = nullptr;
    lv_obj_t* _alarm_button    = nullptr;
    lv_obj_t* _watch_button    = nullptr;
    lv_obj_t* _stopwatch_button = nullptr;
    Selection _selection      = Selection::None;

    lv_obj_t* createButton(const char* label, int y, bool enabled = true);
    static void handleButtonClicked(lv_event_t* event);
};

}  // namespace clock_view
