/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "assistant_view.h"

#include <string>

#include <assets/assets.h>

LV_FONT_DECLARE(font_puhui_14_1)

using namespace smooth_ui_toolkit::lvgl_cpp;

namespace {
constexpr int panel_size = 466;
constexpr int text_width = 382;
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
    _panel->setBgColor(lv_color_hex(0x000000));
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title_label = std::make_unique<Label>(_panel->get());
    _title_label->setText("Assistant");
    _title_label->setTextFont(&lv_font_montserrat_28);
    _title_label->setTextColor(lv_color_hex(0xFFFFFF));
    _title_label->align(LV_ALIGN_TOP_MID, 0, 72);

    _primary_label = std::make_unique<Label>(_panel->get());
    _primary_label->setWidth(text_width);
    _primary_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _primary_label->setTextFont(&lv_font_montserrat_20);
    _primary_label->setTextColor(lv_color_hex(0xFFFFFF));
    _primary_label->align(LV_ALIGN_CENTER, 0, -26);

    _secondary_label = std::make_unique<Label>(_panel->get());
    _secondary_label->setWidth(text_width);
    _secondary_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _secondary_label->setTextFont(&font_puhui_14_1);
    _secondary_label->setTextColor(lv_color_hex(0xAEB7C8));
    _secondary_label->align(LV_ALIGN_CENTER, 0, 28);

    _hint_label = std::make_unique<Label>(_panel->get());
    _hint_label->setWidth(text_width);
    _hint_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _hint_label->setTextFont(&lv_font_montserrat_14);
    _hint_label->setTextColor(lv_color_hex(0x768197));
    _hint_label->align(LV_ALIGN_BOTTOM_MID, 0, -66);

    _update_label = std::make_unique<Label>(_panel->get());
    _update_label->setWidth(text_width);
    _update_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _update_label->setTextFont(&font_puhui_14_1);
    _update_label->setTextColor(lv_color_hex(0xAEB7C8));
    _update_label->align(LV_ALIGN_BOTTOM_MID, 0, -38);
    _update_label->setText("");

    setState(AssistantViewState::Connecting);
}

void AssistantView::setState(AssistantViewState state)
{
    switch (state) {
        case AssistantViewState::Connecting:
            setPrimaryText("Connecting to Wi-Fi");
            setSecondaryText("Preparing Xiaozhi...");
            _hint_label->setText("Home: close");
            break;
        case AssistantViewState::Activating:
            setPrimaryText("Activating");
            setSecondaryText("Waiting for Xiaozhi...");
            _hint_label->setText("Home: close");
            break;
        case AssistantViewState::Listening:
            setPrimaryText("Listening");
            setSecondaryText("Say \"Hello Xiaozhi\"");
            _hint_label->setText("Home: close");
            break;
        case AssistantViewState::Thinking:
            setPrimaryText("Thinking");
            _hint_label->setText("Home: close");
            break;
        case AssistantViewState::Speaking:
            setPrimaryText("Speaking");
            _hint_label->setText("Home: close");
            break;
        case AssistantViewState::Failed:
            setPrimaryText("Assistant unavailable");
            setSecondaryText("Press Next to retry");
            _hint_label->setText("Next: retry   Home: close");
            break;
        case AssistantViewState::Closing:
            setPrimaryText("Closing");
            setSecondaryText("Waiting for Assistant to stop...");
            _hint_label->setText("");
            break;
        case AssistantViewState::StopTimedOut:
            setPrimaryText("Close timed out");
            setSecondaryText("Resources remain protected");
            _hint_label->setText("Next: stop again");
            break;
    }
}

void AssistantView::setPrimaryText(const char* text)
{
    if (_primary_label) {
        _primary_label->setText(text != nullptr ? text : "");
    }
}

void AssistantView::setSecondaryText(const char* text)
{
    if (_secondary_label) {
        _secondary_label->setText(text != nullptr ? text : "");
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
    const std::string text = std::string("发现新版本 v") + canonical_version;
    _update_label->setText(text);
}

void AssistantView::destroy()
{
    _update_label.reset();
    _hint_label.reset();
    _secondary_label.reset();
    _primary_label.reset();
    _title_label.reset();
    _panel.reset();
}
