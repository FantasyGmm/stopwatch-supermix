/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_fft.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <runtime/display/global_display_protection_service.h>
#include <smooth_lvgl.hpp>

using namespace mooncake;

AppFft::AppFft()
{
    setAppInfo().name = "Audio.FFT";
    setAppInfo().icon = (void*)&icon_fft;
}

void AppFft::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppFft::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();
    _screen_off_paused =
        GetGlobalDisplayProtectionService().state() ==
        DisplayProtectionState::ScreenOff;
    GetHAL().setAudioSpectrumMonitoring(!_screen_off_paused);

    LvglLockGuard lock;

    _view = std::make_unique<view::FftView>();
    _view->init(lv_screen_active());
}

void AppFft::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    const bool screen_off =
        GetGlobalDisplayProtectionService().state() ==
        DisplayProtectionState::ScreenOff;
    if (screen_off != _screen_off_paused) {
        _screen_off_paused = screen_off;
        GetHAL().setAudioSpectrumMonitoring(!screen_off);
    }
    if (screen_off) {
        return;
    }
    GetHAL().updateAudioSpectrum();

    LvglLockGuard lock;

    if (_view) {
        const auto& spectrum = GetHAL().getAudioSpectrum();
        _view->setSpectrum(spectrum.bands);
        _view->setPeakFrequencyHz(spectrum.peakFrequencyHz);
        _view->update();
    }
}

void AppFft::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();
    GetHAL().setAudioSpectrumMonitoring(false);
    _screen_off_paused = false;

    LvglLockGuard lock;

    _view.reset();
}
