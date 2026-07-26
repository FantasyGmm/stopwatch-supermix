/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "display_protection_settings_model.h"
#include "workers.h"

#include <mooncake_log.h>
#include <runtime/display/global_display_protection_service.h>

#include <cstdio>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static const std::string_view _tag = "Setup-DisplayProtection";

namespace setup_workers {

class DisplayProtectionWorker::DisplayProtectionConfigView {
public:
    explicit DisplayProtectionConfigView(DisplayProtectionConfig initial_config)
        : _model(initial_config)
    {
        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setSize(466, 466);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_hex(0x000000));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        createChargingRow(24, "Charging Protection");

        _dim_label = std::make_unique<Label>(_panel->get());
        _dim_label->setSize(374, 28);
        _dim_label->align(LV_ALIGN_TOP_MID, 0, 110);
        _dim_label->setTextFont(&lv_font_montserrat_20);
        _dim_label->setTextColor(lv_color_hex(0xFFFFFF));
        _dim_label->setTextAlign(LV_TEXT_ALIGN_CENTER);

        _dim_slider = std::make_unique<Slider>(_panel->get());
        _dim_slider->align(LV_ALIGN_TOP_MID, 0, 150);
        _dim_slider->setSize(374, 20);
        _dim_slider->setRange(0, 5, false);
        applySliderStyle(*_dim_slider);
        _dim_slider->onValueChanged().connect([this](int32_t value) {
            if (_syncing_controls) {
                return;
            }

            _model.setDimChoice(static_cast<int>(value));
            syncControls();
        });

        _screen_off_label = std::make_unique<Label>(_panel->get());
        _screen_off_label->setSize(374, 28);
        _screen_off_label->align(LV_ALIGN_TOP_MID, 0, 205);
        _screen_off_label->setTextFont(&lv_font_montserrat_20);
        _screen_off_label->setTextColor(lv_color_hex(0xFFFFFF));
        _screen_off_label->setTextAlign(LV_TEXT_ALIGN_CENTER);

        _screen_off_slider = std::make_unique<Slider>(_panel->get());
        _screen_off_slider->align(LV_ALIGN_TOP_MID, 0, 245);
        _screen_off_slider->setSize(374, 20);
        _screen_off_slider->setRange(0, 4, false);
        applySliderStyle(*_screen_off_slider);
        _screen_off_slider->onValueChanged().connect([this](int32_t value) {
            if (_syncing_controls) {
                return;
            }

            _model.setScreenOffChoice(static_cast<int>(value));
            syncControls();
        });

        syncControls();

        _ok_button = std::make_unique<Button>(_panel->get());
        _ok_button->align(LV_ALIGN_CENTER, 0, 175);
        _ok_button->setSize(374, 130);
        _ok_button->setRadius(77);
        _ok_button->setBorderWidth(0);
        _ok_button->setShadowWidth(0);
        _ok_button->setBgColor(lv_color_hex(0x4AD78C));
        _ok_button->label().setText("OK");
        _ok_button->label().setTextFont(&lv_font_montserrat_28);
        _ok_button->label().setTextColor(lv_color_hex(0x0F5831));
        _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
        _ok_button->onClick().connect([this]() { _save_requested = true; });
    }

    const DisplayProtectionConfig& config() const
    {
        return _model.config();
    }

    bool consumeSaveRequested()
    {
        const bool requested = _save_requested;
        _save_requested      = false;
        return requested;
    }

private:
    void createChargingRow(int y, const char* title)
    {
        _charging_row = std::make_unique<Container>(_panel->get());
        _charging_row->setSize(374, 60);
        _charging_row->align(LV_ALIGN_TOP_MID, 0, y);
        _charging_row->setBgOpa(LV_OPA_TRANSP);
        _charging_row->setBorderWidth(0);
        _charging_row->setShadowWidth(0);
        _charging_row->setPaddingAll(0);
        _charging_row->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _charging_label = std::make_unique<Label>(_charging_row->get());
        _charging_label->setText(title);
        _charging_label->setTextFont(&lv_font_montserrat_22);
        _charging_label->setTextColor(lv_color_hex(0xFFFFFF));
        _charging_label->align(LV_ALIGN_LEFT_MID, 0, 0);

        _charging_switch = std::make_unique<Switch>(_charging_row->get());
        _charging_switch->setSize(80, 44);
        _charging_switch->align(LV_ALIGN_RIGHT_MID, 0, 0);
        _charging_switch->setValue(_model.config().protect_while_charging);
        _charging_switch->setBgColor(lv_color_hex(0x343434), LV_PART_MAIN);
        _charging_switch->setBgOpa(LV_OPA_COVER, LV_PART_MAIN);
        _charging_switch->setBorderWidth(0, LV_PART_MAIN);
        _charging_switch->setRadius(LV_RADIUS_CIRCLE, LV_PART_MAIN);
        _charging_switch->setBgColor(lv_color_hex(0x4AD78C),
                                     LV_PART_INDICATOR | LV_STATE_CHECKED);
        _charging_switch->setBgOpa(LV_OPA_COVER,
                                   LV_PART_INDICATOR | LV_STATE_CHECKED);
        _charging_switch->setBorderWidth(0,
                                        LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(_charging_switch->get(), lv_color_hex(0xFFFFFF),
                                  LV_PART_KNOB);
        lv_obj_set_style_bg_opa(_charging_switch->get(), LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_width(_charging_switch->get(), 0, LV_PART_KNOB);
        lv_obj_set_style_radius(_charging_switch->get(), LV_RADIUS_CIRCLE,
                                LV_PART_KNOB);
        _charging_switch->onValueChanged().connect(
            [this](bool enabled) { _model.setProtectWhileCharging(enabled); });
    }

    void applySliderStyle(Slider& slider)
    {
        slider.setBgColor(lv_color_hex(0x343434), LV_PART_MAIN);
        slider.setBgOpa(LV_OPA_COVER, LV_PART_MAIN);
        slider.setBorderWidth(0, LV_PART_MAIN);
        slider.setRadius(LV_RADIUS_CIRCLE, LV_PART_MAIN);
        slider.setBgColor(lv_color_hex(0x4AD78C), LV_PART_INDICATOR);
        slider.setBgOpa(LV_OPA_COVER, LV_PART_INDICATOR);
        slider.setBorderWidth(0, LV_PART_INDICATOR);
        slider.setRadius(LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider.get(), lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(slider.get(), LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_width(slider.get(), 0, LV_PART_KNOB);
        lv_obj_set_style_radius(slider.get(), LV_RADIUS_CIRCLE, LV_PART_KNOB);
    }

    void syncControls()
    {
        _syncing_controls = true;
        _dim_slider->setValue(_model.config().dim_choice);
        _screen_off_slider->setValue(_model.config().screen_off_choice);
        updateLabels();
        _syncing_controls = false;
    }

    void updateLabels()
    {
        const auto dim_label = displayProtectionDimLabel(_model.config().dim_choice);
        std::snprintf(_dim_label_text, sizeof(_dim_label_text), "Dim After: %.*s",
                      static_cast<int>(dim_label.size()), dim_label.data());
        _dim_label->setText(_dim_label_text);

        const auto screen_off_label =
            displayProtectionScreenOffLabel(_model.config().screen_off_choice);
        std::snprintf(_screen_off_label_text, sizeof(_screen_off_label_text),
                      "Screen Off After: %.*s",
                      static_cast<int>(screen_off_label.size()),
                      screen_off_label.data());
        _screen_off_label->setText(_screen_off_label_text);
    }

    DisplayProtectionSettingsModel _model;
    std::unique_ptr<Container> _panel;
    std::unique_ptr<Container> _charging_row;
    std::unique_ptr<Label> _charging_label;
    std::unique_ptr<Switch> _charging_switch;
    std::unique_ptr<Label> _dim_label;
    std::unique_ptr<Slider> _dim_slider;
    std::unique_ptr<Label> _screen_off_label;
    std::unique_ptr<Slider> _screen_off_slider;
    std::unique_ptr<Button> _ok_button;
    char _dim_label_text[32]        = {};
    char _screen_off_label_text[40] = {};
    bool _syncing_controls = false;
    bool _save_requested   = false;
};

}  // namespace setup_workers

DisplayProtectionWorker::DisplayProtectionWorker()
{
    mclog::tagInfo(_tag, "start display protection worker");

    _view = std::make_unique<DisplayProtectionConfigView>(
        GetGlobalDisplayProtectionService().config());
}

void DisplayProtectionWorker::update()
{
    if (_view && _view->consumeSaveRequested()) {
        GetGlobalDisplayProtectionService().applyConfig(_view->config(), true);
        _is_done = true;
    }
}

DisplayProtectionWorker::~DisplayProtectionWorker()
{
}
