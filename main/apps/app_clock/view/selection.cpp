/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "selection.h"
#include <assets/assets.h>

namespace clock_view {

ClockSelectionView::ClockSelectionView(bool alarmEnabled)
{
    _panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(_panel, 466, 466);
    lv_obj_set_pos(_panel, 0, 0);
    lv_obj_set_style_bg_color(_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_panel, 0, 0);
    lv_obj_set_style_radius(_panel, 0, 0);
    lv_obj_set_style_pad_all(_panel, 0, 0);
    lv_obj_clear_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(_panel);
    lv_label_set_text(title, "CLOCK");
    lv_obj_set_style_text_font(title, &MontserratSemiBold26, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xA0A0A0), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 55);

    _alarm_button     = createButton("Alarm", 108, alarmEnabled);
    _watch_button     = createButton("Watch Face", 194);
    _stopwatch_button = createButton("Stopwatch", 280);
}

ClockSelectionView::~ClockSelectionView()
{
    if (_panel != nullptr) {
        lv_obj_delete(_panel);
        _panel = nullptr;
    }
}

Selection ClockSelectionView::consumeSelection()
{
    Selection result = _selection;
    _selection       = Selection::None;
    return result;
}

lv_obj_t* ClockSelectionView::createButton(const char* label, int y, bool enabled)
{
    lv_obj_t* button = lv_button_create(_panel);
    lv_obj_set_size(button, 330, 66);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(enabled ? 0x353535 : 0x202020), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_radius(button, 33, 0);

    lv_obj_t* text = lv_label_create(button);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(enabled ? 0xFFFFFF : 0x666666), 0);
    lv_obj_center(text);

    if (enabled) {
        lv_obj_add_event_cb(button, handleButtonClicked, LV_EVENT_CLICKED, this);
    } else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
    return button;
}

void ClockSelectionView::handleButtonClicked(lv_event_t* event)
{
    auto* self = static_cast<ClockSelectionView*>(lv_event_get_user_data(event));
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (target == self->_alarm_button) {
        self->_selection = Selection::Alarm;
    } else if (target == self->_watch_button) {
        self->_selection = Selection::WatchFace;
    } else if (target == self->_stopwatch_button) {
        self->_selection = Selection::Stopwatch;
    }
}

}  // namespace clock_view
