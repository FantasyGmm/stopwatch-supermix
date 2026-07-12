/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "clock_navigation.h"
#include "view/selection.h"
#include <apps/app_alarm_clock/model/alarm_clock.h>
#include <apps/app_alarm_clock/view/view.h>
#include <apps/app_stopwatch/view/view.h>
#include <apps/app_watch_face/view/watch_face.h>
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>

class AppClock : public mooncake::AppAbility {
public:
    AppClock();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onSleeping() override;
    void onClose() override;
    void onDestroy() override;

private:
    enum class Feature {
        Selection,
        Alarm,
        WatchFace,
        Stopwatch,
    };

    Feature _feature = Feature::Selection;
    bool _alarm_ready = false;
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<model::AlarmClock> _alarm_clock;
    std::unique_ptr<clock_view::ClockSelectionView> _selection_view;
    std::unique_ptr<view::AlarmListView> _alarm_list_view;
    std::unique_ptr<view::AlarmAddView> _alarm_add_view;
    std::unique_ptr<view::WatchFaceManager> _watch_face_manager;
    std::unique_ptr<view::StopwatchView> _stopwatch_view;

    ClockNavigation readNavigation() const;
    void transitionTo(Feature feature);
    void destroyFeatureUi();
    void updateAlarmFeature();
};
