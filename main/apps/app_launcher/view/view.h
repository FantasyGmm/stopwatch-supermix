/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/arc_top_clock/arc_top_clock.h>
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <functional>
#include <vector>
#include <memory>

namespace view {

/**
 * @brief
 *
 */
class LauncherView {
public:
    ~LauncherView();

    enum State_t {
        STATE_STARTUP,
        STATE_NORMAL,
    };

    std::function<void(int appID)> onAppClicked;

    void init(std::vector<mooncake::AppProps_t> appPorps);
    void update();

private:
    enum ControlSide {
        CONTROL_NONE,
        CONTROL_BRIGHTNESS,
        CONTROL_VOLUME,
    };

    enum HoldScrollSource {
        HOLD_SCROLL_NONE,
        HOLD_SCROLL_TOUCH,
        HOLD_SCROLL_BUTTON,
    };

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _icon_panels;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Image>> _icon_images;
    std::vector<uitk::lvgl_cpp::Image*> _icon_image_by_panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _lr_indicator_panels;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Image>> _lr_indicators_images;
    std::unique_ptr<uitk::lvgl_cpp::Container> _control_overlay;
    std::unique_ptr<view::ArcTopClock> _clock;
    std::unique_ptr<input::KeyManager> _key_manager;

    int _clicked_app_id = -1;
    State_t _state      = STATE_STARTUP;

    lv_indev_state_t _last_touch_state = LV_INDEV_STATE_REL;
    lv_point_t _control_start_point    = {0, 0};
    lv_point_t _control_last_point     = {0, 0};
    lv_point_t _swipe_start_point      = {0, 0};
    ControlSide _control_candidate     = CONTROL_NONE;
    ControlSide _active_control        = CONTROL_NONE;
    ControlSide _visible_control       = CONTROL_NONE;
    int _control_start_value           = 0;
    int _control_current_value         = 0;
    int _control_applied_value         = -1;
    int _swipe_start_scroll_x          = 0;
    int _last_scroll_x                 = -1;
    int _last_visible_icon_index       = -1;
    int _hold_scroll_direction         = 0;
    int _hold_scroll_target_index      = 0;
    uint32_t _swipe_start_ms           = 0;
    uint32_t _last_control_apply_ms    = 0;
    uint32_t _control_hide_at_ms       = 0;
    uint32_t _suppress_click_until_ms  = 0;
    uint32_t _next_hold_scroll_ms      = 0;
    HoldScrollSource _hold_scroll_source = HOLD_SCROLL_NONE;
    bool _hold_scroll_repeated         = false;
    bool _ignore_next_lr_click         = false;
    bool _ignore_next_button_click     = false;

    void scroll_to_nearby_icon(int direction);
    void scroll_to_icon_index(int targetIndex);
    void handle_lr_indicator_event(int direction, lv_event_code_t code);
    void update_button_hold_scroll(input::KeyEvent keyEvent);
    void start_hold_scroll(int direction, HoldScrollSource source);
    void stop_hold_scroll(HoldScrollSource source);
    void update_hold_scroll();
    void normalize_hold_scroll_target();
    void handle_state_startup();
    void handle_state_normal();
    void handle_scroll_in_loop();
    void init_control_overlay();
    void update_control_gesture();
    bool handle_horizontal_swipe_release(const lv_point_t& point);
    void activate_control(ControlSide side, const lv_point_t& point);
    void update_active_control(const lv_point_t& point);
    void finish_active_control();
    void apply_control_value(bool saveToSettings);
    void refresh_control_overlay();
    void set_lr_indicators_hidden(bool hidden);
    void update_visible_icon_images(int scrollX);
    int normalize_control_value(ControlSide side, int value) const;

    friend void launcher_control_overlay_draw_event_cb(lv_event_t* e);
};

/**
 * @brief
 *
 */
class GuidePage {
public:
    GuidePage();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _img;
};

}  // namespace view
