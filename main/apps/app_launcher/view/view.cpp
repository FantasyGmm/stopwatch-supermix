/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <mooncake_log.h>
#include <assets/assets.h>
#include <functional>
#include <hal/hal.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

/* -------------------------------------------------------------------------- */
/*                               Page indicator                               */
/* -------------------------------------------------------------------------- */
class PageIndicator {
public:
    const int dot_size     = 8;
    const int dot_size_big = 14;
    const int dot_gap      = 16;

    void init(int pageNum, int pageGap, lv_obj_t* parent, int posX, int posY)
    {
        _page_num = pageNum;
        _page_gap = pageGap;

        _panel = std::make_unique<Container>(parent);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_FLOATING);
        _panel->setAlign(LV_ALIGN_CENTER);
        _panel->setPadding(0, 0, 24, 24);
        _panel->setPos(posX, posY);
        _panel->setBorderWidth(0);
        _panel->setHeight(24);
        _panel->setWidth((pageNum * dot_size) + (pageNum - 1) * (dot_gap - dot_size) + 24 * 2);
        _panel->setBgOpa(0);

        for (int i = 0; i < pageNum; i++) {
            _dots.push_back(std::make_unique<Container>(_panel->get()));
            _dots.back()->setAlign(LV_ALIGN_CENTER);
            _dots.back()->setPos(i * dot_gap - (pageNum - 1) * dot_gap / 2, 0);
            _dots.back()->setBgColor(lv_color_hex(0xFFFFFF));
            _dots.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            _dots.back()->setRadius(LV_RADIUS_CIRCLE);
            _dots.back()->setSize(dot_size, dot_size);
            _dots.back()->setBorderWidth(0);
        }

        jumpTo(0);
    }

    void jumpTo(int index)
    {
        if (index < 0 || index >= _page_num) {
            return;
        }
        _current_index = index;
        _last_index    = index;
        update_dots();
    }

    void update(int scrollValue)
    {
        _last_index = _current_index;

        // Calculate absolute index
        int abs_index = (scrollValue + _page_gap / 2) / _page_gap;

        // Map to 0 ~ N-1
        _current_index = abs_index % _page_num;
        if (_current_index < 0) {
            _current_index += _page_num;
        }

        if (_last_index != _current_index) {
            update_dots();
        }
    }

private:
    int _page_num = 0;
    int _page_gap = 0;

    int _current_index = 0;
    int _last_index    = 0;

    std::unique_ptr<Container> _panel;
    std::vector<std::unique_ptr<Container>> _dots;

    void update_dots()
    {
        for (int i = 0; i < _page_num; i++) {
            if (i == _current_index) {
                _dots[i]->setSize(dot_size_big, dot_size_big);
                _dots[i]->setOpa(255);
            } else {
                _dots[i]->setSize(dot_size, dot_size);
                _dots[i]->setOpa(128);
            }
        }
    }
};

/* -------------------------------------------------------------------------- */
/*                             Dynamic icon label                             */
/* -------------------------------------------------------------------------- */
class DynamicIconLabel {
public:
    const int show_range      = 150;
    const int pos_y           = 155;
    const int transition_zone = 80;

    void init(const std::vector<std::string>& iconLabelTexts, int iconGap, lv_obj_t* parent)
    {
        _icon_label_texts = iconLabelTexts;
        _icon_gap         = iconGap;

        // Create floating label
        _label = std::make_unique<Label>(parent);
        _label->setTextColor(lv_color_hex(0xFFFFFF));
        _label->setTextFont(&MontserratSemiBold26);
        _label->setAlign(LV_ALIGN_CENTER);
        _label->addFlag(LV_OBJ_FLAG_FLOATING);
        _label->setOpa(255);

        jumpTo(0);
    }

    void jumpTo(int index)
    {
        if (index < 0 || index >= _icon_label_texts.size()) {
            return;
        }

        _current_index = index;
        _last_index    = index;

        // Update label
        _label->setText(_icon_label_texts[index]);
        _label->setPos(0, pos_y);
        set_opa(255);
    }

    void update(int scrollValue)
    {
        if (_icon_label_texts.empty()) {
            return;
        }

        _last_index = _current_index;

        // Calculate current icon index and distance to icon center
        _current_index        = (scrollValue + _icon_gap / 2) / _icon_gap;
        int icon_center_pos_x = _current_index * _icon_gap;
        int distance_to_icon  = std::abs(scrollValue - icon_center_pos_x);

        // Clamp index
        if (_current_index < 0) {
            _current_index = 0;
        }
        if (_current_index >= _icon_label_texts.size()) {
            _current_index = _icon_label_texts.size() - 1;
        }

        // Check if label should be visible
        bool should_be_visible = (distance_to_icon <= show_range);

        // If index changed, update label text
        if (_last_index != _current_index) {
            _label->setText(_icon_label_texts[_current_index]);
        }

        // Update opacity based on distance when in transition zone
        if (should_be_visible && distance_to_icon > (show_range - transition_zone)) {
            // Fade out as approaching edge
            float fade_ratio = 1.0f - (float)(distance_to_icon - (show_range - transition_zone)) / transition_zone;
            set_opa((static_cast<int>(255 * fade_ratio) / 16) * 16);
        } else if (should_be_visible) {
            set_opa(255);
        }
    }

private:
    std::vector<std::string> _icon_label_texts;
    int _icon_gap      = 0;
    int _current_index = 0;
    int _last_index    = 0;
    bool _is_visible   = false;
    int _last_opa      = -1;

    std::unique_ptr<Label> _label;

    void set_opa(int opa)
    {
        opa = std::max(0, std::min(opa, 255));
        if (_last_opa != opa) {
            _label->setOpa(opa);
            _last_opa = opa;
        }
    }
};

static std::string _tag        = "LauncherView";
static constexpr int _icon_gap = 466;
// Create 3 copies: [0:Buffer] [1:Main] [2:Buffer]
static constexpr int _loop_copies       = 3;
static constexpr int _center_copy_index = 1;

static int _last_clicked_icon_pos_x = -1;
static std::unique_ptr<PageIndicator> _page_indicator;
static std::unique_ptr<DynamicIconLabel> _dynamic_icon_label;

namespace {

static constexpr int _visible_icon_range        = 2;
static constexpr int _fast_swipe_min_distance   = 36;
static constexpr int _drag_swipe_min_distance   = 100;
static constexpr uint32_t _fast_swipe_max_ms    = 260;
static constexpr uint32_t _hold_scroll_initial_delay_ms = 300;
static constexpr uint32_t _hold_scroll_repeat_period_ms = 170;

}  // namespace

LauncherView::~LauncherView()
{
    _icon_images.clear();
    _icon_image_by_panel.clear();
    _icon_panels.clear();
    _lr_indicators_images.clear();
    _lr_indicator_panels.clear();
    _panel.reset();
    _page_indicator.reset();
    _dynamic_icon_label.reset();
}

void LauncherView::init(std::vector<mooncake::AppProps_t> appPorps)
{
    mclog::tagInfo(_tag, "init");

    _key_manager = std::make_unique<input::KeyManager>();

    /* ------------------------------ Screen setup ------------------------------ */
    ScreenActive screen;
    screen.removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    /* ---------------------------------- Panel --------------------------------- */
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setSize(466, 466);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _panel->setBgColor(lv_color_hex(0x000000));
    _panel->addFlag(LV_OBJ_FLAG_SCROLL_ONE);
    _panel->setScrollDir(LV_DIR_HOR);
    _panel->setPaddingAll(0);
    lv_obj_set_scroll_snap_x(_panel->get(), LV_SCROLL_SNAP_CENTER);

    /* ---------------------------------- Icons --------------------------------- */
    int icon_x = 0;
    int icon_y = -15;
    std::vector<std::string> icon_label_texts;
    std::vector<uint32_t> step_colors;

    // Loop multiple times to create fake infinite scroll
    for (int loop = 0; loop < _loop_copies; loop++) {
        for (const auto& props : appPorps) {
            // Icon panel
            _icon_panels.push_back(std::make_unique<Container>(_panel->get()));
            _icon_panels.back()->setAlign(LV_ALIGN_CENTER);
            _icon_panels.back()->setSize(200, 200);
            _icon_panels.back()->setPos(icon_x, icon_y);
            _icon_panels.back()->setBorderWidth(0);
            _icon_panels.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            _icon_panels.back()->setBgOpa(0);

            // Icon click callback
            auto app_id = props.appID;
            auto pos_x  = icon_x;
            _icon_panels.back()->onClick().connect([&, app_id, pos_x]() {
                if (GetHAL().millis() < _suppress_click_until_ms) {
                    return;
                }
                _clicked_app_id          = app_id;
                _last_clicked_icon_pos_x = pos_x;
            });

            // Keep track of data for helpers
            icon_label_texts.push_back(props.info.name);

            uint32_t color = 0xDADADA;
            if (props.info.userData != nullptr) {
                color = *(uint32_t*)props.info.userData;
            }
            step_colors.push_back(color);

            // Icon image
            _icon_image_by_panel.push_back(nullptr);
            if (props.info.icon != nullptr) {
                _icon_images.push_back(std::make_unique<Image>(_icon_panels.back()->get()));
                _icon_images.back()->setSrc(props.info.icon);
                _icon_images.back()->setAlign(LV_ALIGN_CENTER);
                _icon_image_by_panel.back() = _icon_images.back().get();
            }

            icon_x += _icon_gap;
        }
    }

    /* ------------------------------ LR indicators ----------------------------- */
    // Go left indicator
    _lr_indicator_panels.push_back(std::make_unique<Container>(_panel->get()));
    _lr_indicator_panels.back()->setAlign(LV_ALIGN_CENTER);
    _lr_indicator_panels.back()->setSize(52, 160);
    _lr_indicator_panels.back()->setPos(-200, 0);
    _lr_indicator_panels.back()->setBorderWidth(0);
    _lr_indicator_panels.back()->addFlag(LV_OBJ_FLAG_FLOATING);
    _lr_indicator_panels.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _lr_indicator_panels.back()->setBgOpa(0);
    _lr_indicator_panels.back()->addEventCb(
        [](lv_event_t* e) {
            auto* view = static_cast<LauncherView*>(lv_event_get_user_data(e));
            if (view != nullptr) {
                view->handle_lr_indicator_event(-1, lv_event_get_code(e));
            }
        },
        LV_EVENT_ALL, this);

    _lr_indicators_images.push_back(std::make_unique<Image>(_lr_indicator_panels.back()->get()));
    _lr_indicators_images.back()->setSrc(&icon_indicator_left);
    _lr_indicators_images.back()->align(LV_ALIGN_CENTER, 0, 0);

    // Go right indicator
    _lr_indicator_panels.push_back(std::make_unique<Container>(_panel->get()));
    _lr_indicator_panels.back()->setAlign(LV_ALIGN_CENTER);
    _lr_indicator_panels.back()->setSize(52, 160);
    _lr_indicator_panels.back()->setPos(200, 0);
    _lr_indicator_panels.back()->setBorderWidth(0);
    _lr_indicator_panels.back()->addFlag(LV_OBJ_FLAG_FLOATING);
    _lr_indicator_panels.back()->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _lr_indicator_panels.back()->setBgOpa(0);
    _lr_indicator_panels.back()->addEventCb(
        [](lv_event_t* e) {
            auto* view = static_cast<LauncherView*>(lv_event_get_user_data(e));
            if (view != nullptr) {
                view->handle_lr_indicator_event(1, lv_event_get_code(e));
            }
        },
        LV_EVENT_ALL, this);

    _lr_indicators_images.push_back(std::make_unique<Image>(_lr_indicator_panels.back()->get()));
    _lr_indicators_images.back()->setSrc(&icon_indicator_right);
    _lr_indicators_images.back()->align(LV_ALIGN_CENTER, 0, 0);

    /* ------------------------------ Page indicator ---------------------------- */
    _page_indicator = std::make_unique<PageIndicator>();
    _page_indicator->init(appPorps.size(), _icon_gap, _panel->get(), 0, 200);

    /* --------------------------- Dynamic icon label --------------------------- */
    _dynamic_icon_label = std::make_unique<DynamicIconLabel>();
    _dynamic_icon_label->init(icon_label_texts, _icon_gap, _panel->get());

    /* ---------------------------------- Clock --------------------------------- */
    _clock = std::make_unique<view::ArcTopClock>(_panel->get());
    _clock->init();
    _clock->align(LV_ALIGN_TOP_MID, 0, 4);
    _clock->addFlag(LV_OBJ_FLAG_FLOATING);

    /* ----------------------------- History restore ---------------------------- */
    bool need_restore      = false;
    int restore_icon_pos_x = -1;

    // Normal start pos (Center of the repeated sets)
    int base_offset_rounds = _center_copy_index * appPorps.size();
    int default_start_x    = base_offset_rounds * _icon_gap;

    // // If warm boot was requested
    // if (GetHAL().getWarmRebootTarget() >= 0) {
    //     auto app_index = GetHAL().getWarmRebootTarget();
    //     mclog::tagInfo(_tag, "warm boot was requested, app index: {}", app_index);
    //     app_index = uitk::clamp(app_index, 0, static_cast<int>(appPorps.size()) - 1);

    //     // Restore to center set
    //     restore_icon_pos_x = (base_offset_rounds + app_index) * _icon_gap;
    //     need_restore       = true;
    //     GetHAL().clearWarmRebootRequest();
    // }

    if (_last_clicked_icon_pos_x != -1) {
        // Just restore where they left off, it should be in a valid range
        // mclog::tagInfo(_tag, "navigate to last clicked icon, pos x: {}", _last_clicked_icon_pos_x);
        restore_icon_pos_x       = _last_clicked_icon_pos_x;
        need_restore             = true;
        _last_clicked_icon_pos_x = -1;
    }

    if (!need_restore) {
        restore_icon_pos_x = default_start_x;
        need_restore       = true;
    }

    if (need_restore) {
        _panel->scrollBy(-restore_icon_pos_x, 0, LV_ANIM_OFF);

        int restore_index = restore_icon_pos_x / _icon_gap;
        if (!appPorps.empty()) {
            _page_indicator->jumpTo(restore_index % appPorps.size());
        }
        _dynamic_icon_label->jumpTo(restore_index);
        update_visible_icon_images(restore_icon_pos_x);

        _state = STATE_NORMAL;
    }

    // Destory boot logo label
    GetHAL().bootLogo.reset();
}

void LauncherView::update()
{
    if (_key_manager) {
        auto key_event = _key_manager->update();
        update_button_hold_scroll(key_event);

        switch (key_event) {
            case input::KeyEvent::GoPrevious:
                if (_ignore_next_button_click) {
                    _ignore_next_button_click = false;
                } else {
                    scroll_to_nearby_icon(-1);
                }
                break;
            case input::KeyEvent::GoNext:
                if (_ignore_next_button_click) {
                    _ignore_next_button_click = false;
                } else {
                    scroll_to_nearby_icon(1);
                }
                break;
            default:
                break;
        }
    }

    switch (_state) {
        case STATE_STARTUP:
            handle_state_startup();
            break;
        case STATE_NORMAL:
            handle_state_normal();
            break;
        default:
            break;
    }
}

void LauncherView::scroll_to_nearby_icon(int direction)
{
    auto current_scroll_x = _panel->getScrollX();
    int current_index     = (current_scroll_x + _icon_gap / 2) / _icon_gap;
    scroll_to_icon_index(current_index + direction);
}

void LauncherView::scroll_to_icon_index(int targetIndex)
{
    int target_x = targetIndex * _icon_gap;
    _panel->scrollToX(target_x, LV_ANIM_ON);
    _suppress_click_until_ms = GetHAL().millis() + 350;
    update_visible_icon_images(target_x);
}

void LauncherView::handle_lr_indicator_event(int direction, lv_event_code_t code)
{
    switch (code) {
        case LV_EVENT_PRESSED:
            start_hold_scroll(direction, HOLD_SCROLL_TOUCH);
            break;
        case LV_EVENT_RELEASED: {
            bool short_press = _hold_scroll_source == HOLD_SCROLL_TOUCH && !_hold_scroll_repeated;
            stop_hold_scroll(HOLD_SCROLL_TOUCH);
            _ignore_next_lr_click = true;
            if (short_press) {
                scroll_to_nearby_icon(direction);
            }
            break;
        }
        case LV_EVENT_PRESS_LOST:
            stop_hold_scroll(HOLD_SCROLL_TOUCH);
            _ignore_next_lr_click = false;
            break;
        case LV_EVENT_CLICKED:
            if (_ignore_next_lr_click) {
                _ignore_next_lr_click = false;
            } else {
                scroll_to_nearby_icon(direction);
            }
            break;
        default:
            break;
    }
}

void LauncherView::update_button_hold_scroll(input::KeyEvent keyEvent)
{
    bool go_home_pressed = GetHAL().btnA.isPressed() && GetHAL().btnB.isPressed();
    if (go_home_pressed) {
        stop_hold_scroll(HOLD_SCROLL_BUTTON);
        return;
    }

    int direction = 0;
    if (GetHAL().btnA.isPressed()) {
        direction = -1;
    } else if (GetHAL().btnB.isPressed()) {
        direction = 1;
    }

    if (direction == 0) {
        if (_hold_scroll_source == HOLD_SCROLL_BUTTON && _hold_scroll_repeated &&
            (keyEvent == input::KeyEvent::GoPrevious || keyEvent == input::KeyEvent::GoNext)) {
            _ignore_next_button_click = true;
        }
        stop_hold_scroll(HOLD_SCROLL_BUTTON);
        return;
    }

    if (_hold_scroll_source == HOLD_SCROLL_NONE || _hold_scroll_source == HOLD_SCROLL_BUTTON) {
        if (_hold_scroll_source != HOLD_SCROLL_BUTTON || _hold_scroll_direction != direction) {
            start_hold_scroll(direction, HOLD_SCROLL_BUTTON);
        }
    }

    _ignore_next_button_click = false;
}

void LauncherView::start_hold_scroll(int direction, HoldScrollSource source)
{
    if (!lv_obj_has_flag(_panel->get(), LV_OBJ_FLAG_SCROLLABLE)) {
        return;
    }

    uint32_t now = GetHAL().millis();
    _hold_scroll_source       = source;
    _hold_scroll_direction    = direction;
    _hold_scroll_target_index = (_panel->getScrollX() + _icon_gap / 2) / _icon_gap;
    _hold_scroll_repeated     = false;
    _next_hold_scroll_ms      = now + _hold_scroll_initial_delay_ms;
}

void LauncherView::stop_hold_scroll(HoldScrollSource source)
{
    if (_hold_scroll_source != source) {
        return;
    }

    _hold_scroll_source    = HOLD_SCROLL_NONE;
    _hold_scroll_direction = 0;
    _next_hold_scroll_ms   = 0;
}

void LauncherView::update_hold_scroll()
{
    if (_hold_scroll_source == HOLD_SCROLL_NONE || _hold_scroll_direction == 0 ||
        !lv_obj_has_flag(_panel->get(), LV_OBJ_FLAG_SCROLLABLE)) {
        return;
    }

    uint32_t now = GetHAL().millis();
    if (now < _next_hold_scroll_ms) {
        return;
    }

    normalize_hold_scroll_target();
    _hold_scroll_target_index += _hold_scroll_direction;
    normalize_hold_scroll_target();
    scroll_to_icon_index(_hold_scroll_target_index);

    _hold_scroll_repeated = true;
    _next_hold_scroll_ms  = now + _hold_scroll_repeat_period_ms;
}

void LauncherView::normalize_hold_scroll_target()
{
    int total_icons = _icon_panels.size();
    if (total_icons == 0 || _loop_copies == 0) {
        return;
    }

    int icons_per_set = total_icons / _loop_copies;
    if (icons_per_set <= 0) {
        return;
    }

    int set_width_px = icons_per_set * _icon_gap;
    int left_limit   = (_center_copy_index - 1) * icons_per_set + (icons_per_set / 2);
    int right_limit  = (_center_copy_index + 1) * icons_per_set + (icons_per_set / 2);

    if (_hold_scroll_target_index < left_limit) {
        _panel->scrollBy(-set_width_px, 0, LV_ANIM_OFF);
        _hold_scroll_target_index += icons_per_set;
        _last_scroll_x = -1;
        update_visible_icon_images(_panel->getScrollX());
    } else if (_hold_scroll_target_index > right_limit) {
        _panel->scrollBy(set_width_px, 0, LV_ANIM_OFF);
        _hold_scroll_target_index -= icons_per_set;
        _last_scroll_x = -1;
        update_visible_icon_images(_panel->getScrollX());
    }
}

bool LauncherView::handle_horizontal_swipe_release(const lv_point_t& point)
{
    int dx     = point.x - _swipe_start_point.x;
    int dy     = point.y - _swipe_start_point.y;
    int abs_x  = std::abs(dx);
    int abs_y  = std::abs(dy);
    uint32_t elapsed_ms = GetHAL().millis() - _swipe_start_ms;

    bool fast_swipe = abs_x >= _fast_swipe_min_distance && elapsed_ms <= _fast_swipe_max_ms &&
                      abs_x * 10 > abs_y * 12;
    bool drag_swipe = abs_x >= _drag_swipe_min_distance && abs_x > abs_y;
    if (!fast_swipe && !drag_swipe) {
        return false;
    }

    int start_index = (_swipe_start_scroll_x + _icon_gap / 2) / _icon_gap;
    int direction   = dx < 0 ? 1 : -1;
    scroll_to_icon_index(start_index + direction);
    return true;
}

void LauncherView::set_lr_indicators_hidden(bool hidden)
{
    for (auto& panel : _lr_indicator_panels) {
        panel->setHidden(hidden);
    }
}

void LauncherView::update_visible_icon_images(int scrollX)
{
    int current_index = (scrollX + _icon_gap / 2) / _icon_gap;
    if (current_index == _last_visible_icon_index) {
        return;
    }

    _last_visible_icon_index = current_index;
    for (int i = 0; i < static_cast<int>(_icon_image_by_panel.size()); ++i) {
        if (_icon_image_by_panel[i] != nullptr) {
            _icon_image_by_panel[i]->setHidden(std::abs(i - current_index) > _visible_icon_range);
        }
    }
}

void LauncherView::handle_state_startup()
{
    _state = STATE_NORMAL;
}

void LauncherView::handle_state_normal()
{
    auto& controls = GetGlobalEdgeControlService();
    const bool edge_active = controls.ownsTouch();
    if (edge_active != _edge_control_active) {
        _edge_control_active = edge_active;
        _suppress_click_until_ms = GetHAL().millis() + 350;
        if (edge_active) {
            _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
            set_lr_indicators_hidden(true);
            _clicked_app_id = -1;
            stop_hold_scroll(HOLD_SCROLL_TOUCH);
        } else {
            _panel->addFlag(LV_OBJ_FLAG_SCROLLABLE);
            set_lr_indicators_hidden(false);
        }
    }

    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev != nullptr) {
        const lv_indev_state_t state = lv_indev_get_state(indev);
        if (controls.ownsTouch()) {
            _last_swipe_touch_state = state;
        } else if (state != _last_swipe_touch_state) {
            lv_point_t point;
            lv_indev_get_point(indev, &point);

            if (state == LV_INDEV_STATE_PR) {
                _swipe_start_point    = point;
                _swipe_start_scroll_x = _panel->getScrollX();
                _swipe_start_ms       = GetHAL().millis();
            } else if (!controls.lastReleaseConsumed()) {
                handle_horizontal_swipe_release(point);
            }
            _last_swipe_touch_state = state;
        }
    }

    update_hold_scroll();

    if (_clicked_app_id != -1) {
        if (onAppClicked) {
            onAppClicked(_clicked_app_id);
        }
        _clicked_app_id = -1;
    }

    handle_scroll_in_loop();

    int scroll_x = _panel->getScrollX();
    // mclog::tagInfo(_tag, "scroll x: {}", scroll_x);

    if (scroll_x != _last_scroll_x) {
        update_visible_icon_images(scroll_x);
        _page_indicator->update(scroll_x);
        _dynamic_icon_label->update(scroll_x);
        _last_scroll_x = scroll_x;
    }

    if (_clock) {
        _clock->update();
    }
}

void LauncherView::handle_scroll_in_loop()
{
    // We get total size from underlying icons count / copies
    int total_icons   = _icon_panels.size();
    int icons_per_set = total_icons / _loop_copies;
    int set_width_px  = icons_per_set * _icon_gap;

    // Keep the scroll position near the center copy.

    int current_scroll_x = _panel->getScrollX();

    // Thresholds: midpoint of Wrap sets
    int left_trigger_limit  = (_center_copy_index - 1) * set_width_px + (set_width_px / 2);
    int right_trigger_limit = (_center_copy_index + 1) * set_width_px + (set_width_px / 2);

    // Wrap-around Logic
    // Only perform teleport if we are NOT in an automated scroll animation
    // (To avoid interrupting the snap/scroll-to animation which would leave us stuck between icons)
    // However, if the user is manually dragging (PRESSED), we MUST teleport to allow infinite drag.
    bool is_auto_scrolling = lv_obj_is_scrolling(_panel->get()) && !lv_obj_has_state(_panel->get(), LV_STATE_PRESSED);

    if (!is_auto_scrolling) {
        if (current_scroll_x < left_trigger_limit) {
            _panel->scrollBy(-set_width_px, 0, LV_ANIM_OFF);
            _last_scroll_x = -1;
            update_visible_icon_images(_panel->getScrollX());
        } else if (current_scroll_x > right_trigger_limit) {
            _panel->scrollBy(set_width_px, 0, LV_ANIM_OFF);
            _last_scroll_x = -1;
            update_visible_icon_images(_panel->getScrollX());
        }
    }
}
