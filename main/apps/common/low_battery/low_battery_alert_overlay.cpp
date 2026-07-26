#include "low_battery_alert_overlay.h"

#include "low_battery_alert_layout.h"

#include <assets/assets.h>
#include <cstdio>
#include <smooth_lvgl.hpp>

LV_FONT_DECLARE(font_puhui_14_1)

using smooth_ui_toolkit::lvgl_cpp::Container;
using smooth_ui_toolkit::lvgl_cpp::Label;

LowBatteryAlertOverlay::~LowBatteryAlertOverlay()
{
    shutdown();
}

bool LowBatteryAlertOverlay::init(lv_obj_t* parent)
{
    shutdown();

    if (parent == nullptr) {
        return false;
    }

    _root = std::make_unique<Container>(parent);
    _root->align(LV_ALIGN_CENTER, 0, 0);
    _root->setSize(low_battery_alert_layout::screenSize, low_battery_alert_layout::screenSize);
    _root->setRadius(0);
    _root->setBorderWidth(0);
    _root->setPaddingAll(0);
    _root->setBgOpa(LV_OPA_TRANSP);
    _root->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _root->addFlag(LV_OBJ_FLAG_FLOATING);
    _root->setHidden(true);

    _banner = std::make_unique<Container>(_root->get());
    _banner->setSize(low_battery_alert_layout::bannerWidth, low_battery_alert_layout::bannerHeight);
    _banner->align(LV_ALIGN_CENTER, 0, low_battery_alert_layout::centerYOffset);
    _banner->setRadius(low_battery_alert_layout::cornerRadius);
    _banner->setBorderWidth(1);
    _banner->setBorderColor(lv_color_hex(0xF05A3C));
    _banner->setBgColor(lv_color_hex(0x171717));
    _banner->setBgOpa(LV_OPA_COVER);
    _banner->setPaddingAll(0);
    _banner->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _banner->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title = std::make_unique<Label>(_banner->get());
    _title->setTextFont(&font_puhui_14_1);
    _title->align(LV_ALIGN_TOP_LEFT, 22, 18);
    _title->setText("电量低");

    _percent = std::make_unique<Label>(_banner->get());
    _percent->setTextFont(&MontserratSemiBold26);
    _percent->setTextColor(lv_color_hex(0xFF8066));
    _percent->align(LV_ALIGN_TOP_RIGHT, -22, 10);
    _percent->setText("");

    _message = std::make_unique<Label>(_banner->get());
    _message->setTextFont(&font_puhui_14_1);
    _message->align(LV_ALIGN_BOTTOM_MID, 0, -15);
    _message->setText("请及时充电");

    return true;
}

void LowBatteryAlertOverlay::show(int battery_percent)
{
    if (_root == nullptr || _percent == nullptr) {
        return;
    }

    char text[16] = {};
    std::snprintf(text, sizeof(text), "%d%%", battery_percent);
    _percent->setText(text);
    _root->setHidden(false);
    _root->moveForeground();
}

void LowBatteryAlertOverlay::hide()
{
    if (_root != nullptr) {
        _root->setHidden(true);
    }
}

void LowBatteryAlertOverlay::shutdown()
{
    _title.reset();
    _percent.reset();
    _message.reset();
    _banner.reset();
    _root.reset();
}

bool LowBatteryAlertOverlay::visible() const
{
    return _root != nullptr && !lv_obj_has_flag(_root->get(), LV_OBJ_FLAG_HIDDEN);
}
