/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_clock.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <runtime/display/global_display_protection_service.h>

using namespace mooncake;

AppClock::AppClock()
{
    setAppInfo().name = "Clock";
    setAppInfo().icon = (void*)&icon_clock;
}

void AppClock::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    _stopwatch = std::make_unique<model::Stopwatch>();
    _alarm_clock = std::make_unique<model::AlarmClock>();
    _alarm_ready = _alarm_clock->init();
    if (!_alarm_ready) {
        mclog::tagError(getAppInfo().name, "failed to init alarm clock model");
        return;
    }

    _alarm_clock->onTriggered().connect([this](const model::AlarmClock::AlarmTriggeredEvent& event) {
        auto& protection = GetGlobalDisplayProtectionService();
        protection.setKeepAwake(DisplayKeepAwakeReason::AlarmRinging, true);
        protection.applyPendingHardwareUpdate();
        {
            LvglLockGuard lock;
            _alarm_trigger_view = std::make_unique<view::AlarmTriggerView>();
            _alarm_trigger_view->init(event.time);
        }

        GetHAL().startAlarm();
    });
}

void AppClock::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    transitionTo(Feature::Selection);
}

ClockNavigation AppClock::readNavigation() const
{
    return resolve_clock_navigation(
        GetHAL().btnA.isHolding() && GetHAL().btnB.isHolding(),
        GetHAL().btnA.wasHold(),
        GetHAL().btnA.wasClicked(),
        GetHAL().btnB.wasClicked()
    );
}

void AppClock::onRunning()
{
    if (updateAlarmTrigger()) {
        return;
    }

    GetHAL().updateButtonStates();
    ClockNavigation navigation = readNavigation();
    if (navigation == ClockNavigation::Home) {
        close();
        return;
    }
    if (navigation == ClockNavigation::FeatureMenu && _feature != Feature::Selection) {
        transitionTo(Feature::Selection);
        return;
    }

    if (_alarm_ready && _alarm_clock) {
        _alarm_clock->update();
        if (updateAlarmTrigger()) {
            return;
        }
    }

    clock_view::Selection selection = clock_view::Selection::None;
    {
        LvglLockGuard lock;
        switch (_feature) {
            case Feature::Selection:
                selection = _selection_view->consumeSelection();
                break;
            case Feature::Alarm:
                updateAlarmFeature();
                break;
            case Feature::WatchFace:
                if (navigation == ClockNavigation::Previous) {
                    _watch_face_manager->goPrevious();
                } else if (navigation == ClockNavigation::Next) {
                    _watch_face_manager->goNext();
                }
                _watch_face_manager->update();
                break;
            case Feature::Stopwatch:
                _stopwatch_view->update(false);
                break;
        }
    }
    syncStopwatchKeepAwake();

    switch (selection) {
        case clock_view::Selection::Alarm:
            if (_alarm_ready) {
                transitionTo(Feature::Alarm);
            }
            break;
        case clock_view::Selection::WatchFace:
            transitionTo(Feature::WatchFace);
            break;
        case clock_view::Selection::Stopwatch:
            transitionTo(Feature::Stopwatch);
            break;
        default:
            break;
    }
}

void AppClock::onSleeping()
{
    if (_alarm_ready && _alarm_clock) {
        _alarm_clock->update();
    }
    syncStopwatchKeepAwake();
    updateAlarmTrigger();
}

void AppClock::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    clearAlarmTrigger();
    _key_manager.reset();
    syncStopwatchKeepAwake();
    LvglLockGuard lock;
    destroyFeatureUi();
}

void AppClock::onDestroy()
{
    mclog::tagInfo(getAppInfo().name, "on destroy");
    _alarm_clock.reset();
    _stopwatch.reset();
    GetGlobalDisplayProtectionService().setKeepAwake(
        DisplayKeepAwakeReason::StopwatchRunning, false);
    GetGlobalDisplayProtectionService().setKeepAwake(
        DisplayKeepAwakeReason::AlarmRinging, false);
}

bool AppClock::updateAlarmTrigger()
{
    if (!_alarm_trigger_view) {
        return false;
    }

    GetHAL().startAlarm();

    bool confirmed = false;
    {
        LvglLockGuard lock;
        confirmed = _alarm_trigger_view->isConfirmed();
        if (confirmed) {
            _alarm_trigger_view.reset();
        }
    }
    if (confirmed) {
        GetHAL().stopAlarm();
        GetGlobalDisplayProtectionService().setKeepAwake(
            DisplayKeepAwakeReason::AlarmRinging, false);
    }
    return !confirmed;
}

void AppClock::clearAlarmTrigger()
{
    bool was_active = false;
    {
        LvglLockGuard lock;
        was_active = _alarm_trigger_view != nullptr;
        _alarm_trigger_view.reset();
    }
    if (was_active) {
        GetHAL().stopAlarm();
        GetGlobalDisplayProtectionService().setKeepAwake(
            DisplayKeepAwakeReason::AlarmRinging, false);
    }
}

void AppClock::transitionTo(Feature feature)
{
    _key_manager = std::make_unique<input::KeyManager>();
    LvglLockGuard lock;
    destroyFeatureUi();
    _feature = feature;
    switch (_feature) {
        case Feature::Selection:
            _selection_view = std::make_unique<clock_view::ClockSelectionView>(_alarm_ready);
            break;
        case Feature::Alarm:
            _alarm_list_view = std::make_unique<view::AlarmListView>(*_alarm_clock);
            _alarm_list_view->init(lv_screen_active());
            break;
        case Feature::WatchFace:
            _watch_face_manager = std::make_unique<view::WatchFaceManager>();
            _watch_face_manager->init();
            break;
        case Feature::Stopwatch:
            _stopwatch_view = std::make_unique<view::StopwatchView>();
            _stopwatch_view->init(lv_screen_active(), *_stopwatch);
            break;
    }
    syncStopwatchKeepAwake();
}

void AppClock::destroyFeatureUi()
{
    _selection_view.reset();
    _alarm_list_view.reset();
    _alarm_add_view.reset();
    _watch_face_manager.reset();
    _stopwatch_view.reset();
}

void AppClock::updateAlarmFeature()
{
    if (_alarm_list_view) {
        _alarm_list_view->update();
        if (_alarm_list_view->consumeAddRequested()) {
            _alarm_list_view.reset();
            _alarm_add_view = std::make_unique<view::AlarmAddView>();
            _alarm_add_view->init(lv_screen_active());
        }
    }

    if (_alarm_add_view && _alarm_add_view->isConfirmed()) {
        _alarm_clock->addAlarm(_alarm_add_view->selectedTime(), true);
        _alarm_add_view.reset();
        _alarm_list_view = std::make_unique<view::AlarmListView>(*_alarm_clock);
        _alarm_list_view->init(lv_screen_active());
    }
}

void AppClock::syncStopwatchKeepAwake()
{
    const bool running = _stopwatch &&
        _stopwatch->getState() == model::Stopwatch::State_t::Running;
    GetGlobalDisplayProtectionService().setKeepAwake(
        DisplayKeepAwakeReason::StopwatchRunning, running);
}
