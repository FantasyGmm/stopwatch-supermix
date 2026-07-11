/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_assistant.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_app_format.h>
#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;

AppAssistant::AppAssistant()
{
    setAppInfo().name = "Assistant";
    setAppInfo().icon = (void*)&icon_assistant;
}

void AppAssistant::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppAssistant::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager    = std::make_unique<input::KeyManager>();
    _switch_started = true;
    _switch_at_ms   = 0;

    {
        LvglLockGuard lock;
        create_switch_view();
    }

    // Switch immediately from onOpen(). Waiting for a later onRunning() tick can
    // leave the launcher-side transition screen visible if the app update loop
    // is interrupted by the OTA restart path.
    switch_to_assistant();
}

void AppAssistant::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (!_switch_started && GetHAL().millis() >= _switch_at_ms) {
        _switch_started = true;
        switch_to_assistant();
    }
}

void AppAssistant::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;
    _message_label.reset();
    _title_label.reset();
    _panel.reset();
}

void AppAssistant::create_switch_view()
{
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setSize(466, 466);
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _panel->setBgColor(lv_color_hex(0x000000));
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title_label = std::make_unique<Label>(_panel->get());
    _title_label->setTextFont(&lv_font_montserrat_28);
    _title_label->setTextColor(lv_color_hex(0xFFFFFF));
    _title_label->align(LV_ALIGN_CENTER, 0, -28);
    _title_label->setText("Assistant");

    _message_label = std::make_unique<Label>(_panel->get());
    _message_label->setTextFont(&lv_font_montserrat_16);
    _message_label->setTextColor(lv_color_hex(0xAEB7C8));
    _message_label->align(LV_ALIGN_CENTER, 0, 18);
    _message_label->setText("Starting Xiaozhi runtime ...");
}

void AppAssistant::set_message(const char* message)
{
    LvglLockGuard lock;
    if (_message_label) {
        _message_label->setText(message);
    }
}

void AppAssistant::switch_to_assistant()
{
    const esp_partition_t* assistant_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    if (assistant_partition == nullptr) {
        mclog::tagError(getAppInfo().name, "assistant ota_1 partition missing");
        set_message("Assistant partition missing");
        return;
    }

    esp_app_desc_t assistant_desc = {};
    esp_err_t err = esp_ota_get_partition_description(assistant_partition, &assistant_desc);
    if (err != ESP_OK) {
        mclog::tagError(getAppInfo().name, "assistant firmware invalid: {}", esp_err_to_name(err));
        set_message("Assistant firmware is not installed");
        return;
    }

    mclog::tagInfo(getAppInfo().name, "boot assistant from {}, version {}", assistant_partition->label,
                   assistant_desc.version);
    set_message("Switching to Xiaozhi ...");

    err = esp_ota_set_boot_partition(assistant_partition);
    if (err != ESP_OK) {
        mclog::tagError(getAppInfo().name, "failed to set assistant boot partition: {}", esp_err_to_name(err));
        set_message("Unable to start assistant");
        return;
    }

    GetHAL().delay(80);
    esp_restart();
}
