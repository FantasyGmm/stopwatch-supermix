/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_setup.h"
#include <apps/common/edge_control/global_edge_control_service.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>

using namespace mooncake;
using namespace view;
using namespace setup_workers;

AppSetup::AppSetup()
{
    setAppInfo().name = "Settings";
    setAppInfo().icon = (void*)&icon_setup;
}

void AppSetup::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    // open();
}

void AppSetup::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    // Reset state
    _destroy_menu    = false;
    _need_warm_reset = false;
    _magic_count     = 0;

    _menu_sections = {
        {
            "Device",
            {
                {"Brightness",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<BrightnessWorker>();
                 }},
                {"Volume",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<VolumeWorker>();
                 }},
                {"Button",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<ButtonWorker>();
                 }},
                {"Power LED",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<PowerLedWorker>();
                 }},
            },
        },
        {
            "Time & Date",
            {
                {"Set Time",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<SetTimeWorker>();
                 }},
                {"Set Date",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<SetDateWorker>();
                }},
            },
        },
        {
            "WiFi",
            {
                {"WiFi",
                 [&]() {
                     _destroy_menu = true;
                     _worker       = std::make_unique<WifiSettingsWorker>();
                 }},
            },
        },
        {
            "Firmware",
            {
                {fmt::format("Version: {}", common::FirmwareVersion),
                 [&]() {
                     _magic_count++;
                     if (_magic_count >= 10) {
                         _magic_count  = 0;
                         _destroy_menu = true;
                         _worker       = std::make_unique<AboutWorker>();
                     }
                 }},
            },
        },
    };

    LvglLockGuard lock;
    GetGlobalEdgeControlService().setEnabled(false);

    _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
}

void AppSetup::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    bool worker_done = false;
    if (_worker) {
        _worker->updateRuntime();
    }

    {
        LvglLockGuard lock;

        if (_menu_page) {
            _menu_page->update();
        }

        if (_destroy_menu) {
            _menu_page.reset();
            _destroy_menu = false;
        }

        if (_worker) {
            _worker->update();
            worker_done = _worker->isDone();
        }
    }

    if (worker_done) {
        _worker->releaseRuntime();
        LvglLockGuard lock;
        _worker.reset();
        _menu_page = std::make_unique<view::SelectMenuPage>(_menu_sections);
    }
}

void AppSetup::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    if (_worker) {
        _worker->releaseRuntime();
    }

    LvglLockGuard lock;

    _menu_sections.clear();
    _menu_page.reset();
    _worker.reset();
    GetGlobalEdgeControlService().setEnabled(true);

    if (_need_warm_reset) {
        // GetHAL().requestWarmReboot(6);
    }
}
