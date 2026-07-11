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
#include <cstdio>
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

namespace view {
void launcher_control_overlay_draw_event_cb(lv_event_t* e);
}

namespace {

static constexpr int _control_panel_size        = 466;
static constexpr int _control_center            = _control_panel_size / 2;
static constexpr int _control_arc_radius        = 197;
static constexpr int _control_arc_width         = 8;
static constexpr int _control_activate_distance = 24;
static constexpr int _control_drag_range_px     = 210;
static constexpr int _visible_icon_range        = 2;
static constexpr int _fast_swipe_min_distance   = 36;
static constexpr int _drag_swipe_min_distance   = 100;
static constexpr uint32_t _fast_swipe_max_ms    = 260;
static constexpr uint32_t _control_apply_period_ms = 40;
static constexpr uint32_t _hold_scroll_initial_delay_ms = 300;
static constexpr uint32_t _hold_scroll_repeat_period_ms = 170;
static constexpr uint32_t _brightness_color     = 0x2F83FF;
static constexpr uint32_t _volume_color         = 0x36DDE2;

int clamp_int(int value, int min_value, int max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

lv_point_t point_on_arc(int center_x, int center_y, int radius, float degrees)
{
    static constexpr float pi = 3.14159265358979323846f;
    float radians             = degrees * pi / 180.0f;
    return {
        static_cast<lv_coord_t>(std::lround(center_x + std::cos(radians) * radius)),
        static_cast<lv_coord_t>(std::lround(center_y + std::sin(radians) * radius)),
    };
}

void draw_arc(lv_layer_t* layer, lv_point_t center, int radius, int start_deg, int end_deg, int width, uint32_t color,
              lv_opa_t opa)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center      = center;
    dsc.radius      = radius;
    dsc.start_angle = start_deg;
    dsc.end_angle   = end_deg;
    dsc.width       = width;
    dsc.rounded     = 1;
    dsc.color       = lv_color_hex(color);
    dsc.opa         = opa;
    lv_draw_arc(layer, &dsc);
}

void draw_circle(lv_layer_t* layer, lv_point_t center, int radius, uint32_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa   = opa;
    dsc.radius   = LV_RADIUS_CIRCLE;

    lv_area_t area = {
        static_cast<lv_coord_t>(center.x - radius),
        static_cast<lv_coord_t>(center.y - radius),
        static_cast<lv_coord_t>(center.x + radius),
        static_cast<lv_coord_t>(center.y + radius),
    };
    lv_draw_rect(layer, &dsc, &area);
}

void draw_line(lv_layer_t* layer, int x1, int y1, int x2, int y2, int width, uint32_t color, lv_opa_t opa)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1.x        = x1;
    dsc.p1.y        = y1;
    dsc.p2.x        = x2;
    dsc.p2.y        = y2;
    dsc.width       = width;
    dsc.color       = lv_color_hex(color);
    dsc.opa         = opa;
    dsc.round_start = 1;
    dsc.round_end   = 1;
    lv_draw_line(layer, &dsc);
}

void draw_percent_label(lv_layer_t* layer, int x, int y, int value)
{
    static char text[8] = {};
    std::snprintf(text, sizeof(text), "%d%%", value);

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = lv_color_hex(0xFFFFFF);
    dsc.font  = &MontserratSemiBold26;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    dsc.opa   = LV_OPA_COVER;
    dsc.text  = text;

    lv_area_t area = {
        static_cast<lv_coord_t>(x - 54),
        static_cast<lv_coord_t>(y - 20),
        static_cast<lv_coord_t>(x + 54),
        static_cast<lv_coord_t>(y + 28),
    };
    lv_draw_label(layer, &dsc, &area);
}

void draw_sun_icon(lv_layer_t* layer, int x_ofs, int y_ofs)
{
    lv_point_t center = {
        static_cast<lv_coord_t>(x_ofs + 111),
        static_cast<lv_coord_t>(y_ofs + 72),
    };
    draw_circle(layer, center, 5, 0xFFFFFF, LV_OPA_COVER);

    for (int i = 0; i < 8; ++i) {
        float deg       = static_cast<float>(i * 45);
        lv_point_t from = point_on_arc(center.x, center.y, 9, deg);
        lv_point_t to   = point_on_arc(center.x, center.y, 15, deg);
        draw_line(layer, from.x, from.y, to.x, to.y, 2, 0xFFFFFF, LV_OPA_COVER);
    }
}

void draw_speaker_icon(lv_layer_t* layer, int x_ofs, int y_ofs)
{
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(0xFFFFFF);
    rect_dsc.bg_opa   = LV_OPA_COVER;
    rect_dsc.radius   = 1;

    lv_area_t box = {
        static_cast<lv_coord_t>(x_ofs + 341),
        static_cast<lv_coord_t>(y_ofs + 67),
        static_cast<lv_coord_t>(x_ofs + 349),
        static_cast<lv_coord_t>(y_ofs + 77),
    };
    lv_draw_rect(layer, &rect_dsc, &box);

    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.color  = lv_color_hex(0xFFFFFF);
    tri_dsc.opa    = LV_OPA_COVER;
    tri_dsc.p[0].x = x_ofs + 349;
    tri_dsc.p[0].y = y_ofs + 67;
    tri_dsc.p[1].x = x_ofs + 361;
    tri_dsc.p[1].y = y_ofs + 57;
    tri_dsc.p[2].x = x_ofs + 361;
    tri_dsc.p[2].y = y_ofs + 87;
    lv_draw_triangle(layer, &tri_dsc);

    draw_arc(layer, {static_cast<lv_coord_t>(x_ofs + 359), static_cast<lv_coord_t>(y_ofs + 72)}, 9, 318, 402, 2,
             0xFFFFFF, LV_OPA_COVER);
    draw_arc(layer, {static_cast<lv_coord_t>(x_ofs + 359), static_cast<lv_coord_t>(y_ofs + 72)}, 14, 318, 402, 2,
             0xFFFFFF, LV_OPA_COVER);
}

}  // namespace

LauncherView::~LauncherView()
{
    _control_overlay.reset();
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

    /* -------------------------- Quick control overlay ------------------------- */
    init_control_overlay();

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
    if (_active_control != CONTROL_NONE) {
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
    if (_hold_scroll_source == HOLD_SCROLL_NONE || _hold_scroll_direction == 0 || _active_control != CONTROL_NONE) {
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

void LauncherView::init_control_overlay()
{
    _control_overlay = std::make_unique<Container>(_panel->get());
    _control_overlay->align(LV_ALIGN_CENTER, 0, 0);
    _control_overlay->setSize(_control_panel_size, _control_panel_size);
    _control_overlay->setRadius(0);
    _control_overlay->setBorderWidth(0);
    _control_overlay->setPaddingAll(0);
    _control_overlay->setBgOpa(LV_OPA_TRANSP);
    _control_overlay->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _control_overlay->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _control_overlay->addFlag(LV_OBJ_FLAG_FLOATING);
    _control_overlay->setHidden(true);
    lv_obj_add_event_cb(_control_overlay->get(), view::launcher_control_overlay_draw_event_cb, LV_EVENT_ALL, this);
    lv_obj_refresh_ext_draw_size(_control_overlay->get());
}

void LauncherView::update_control_gesture()
{
    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr || _control_overlay == nullptr) {
        return;
    }

    lv_indev_state_t state = lv_indev_get_state(indev);

    if (state == LV_INDEV_STATE_REL) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);

        if (_last_touch_state == LV_INDEV_STATE_PR) {
            if (_active_control != CONTROL_NONE) {
                finish_active_control();
            } else {
                handle_horizontal_swipe_release(point);
            }
            _control_candidate = CONTROL_NONE;
        }

        if (_active_control == CONTROL_NONE && _control_hide_at_ms != 0 && GetHAL().millis() >= _control_hide_at_ms) {
            _control_overlay->setHidden(true);
            set_lr_indicators_hidden(false);
            _visible_control   = CONTROL_NONE;
            _control_hide_at_ms = 0;
        }

        _last_touch_state = state;
        return;
    }

    if (_active_control == CONTROL_NONE && _control_candidate == CONTROL_NONE &&
        lv_indev_get_scroll_obj(indev) == _panel->get() && lv_indev_get_scroll_dir(indev) == LV_DIR_HOR) {
        _last_touch_state = state;
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (state == LV_INDEV_STATE_PR && _last_touch_state == LV_INDEV_STATE_REL) {
        _control_start_point = point;
        _control_last_point  = point;
        _swipe_start_point    = point;
        _swipe_start_scroll_x = _panel->getScrollX();
        _swipe_start_ms       = GetHAL().millis();

        if (point.y > 30) {
            _control_candidate = point.x < (_control_panel_size / 2) ? CONTROL_BRIGHTNESS : CONTROL_VOLUME;
        } else {
            _control_candidate = CONTROL_NONE;
        }
    } else if (state == LV_INDEV_STATE_PR) {
        _control_last_point = point;

        if (_active_control == CONTROL_NONE && _control_candidate != CONTROL_NONE) {
            int delta_x = point.x - _control_start_point.x;
            int delta_y = point.y - _control_start_point.y;
            int abs_x   = std::abs(delta_x);
            int abs_y   = std::abs(delta_y);

            if (abs_y > _control_activate_distance && abs_y > abs_x) {
                activate_control(_control_candidate, point);
            } else if (abs_x > _control_activate_distance && abs_x > abs_y) {
                _control_candidate = CONTROL_NONE;
                _last_touch_state  = state;
                return;
            }
        }

        if (_active_control != CONTROL_NONE) {
            update_active_control(point);
        }
    }

    _last_touch_state = state;
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

void LauncherView::activate_control(ControlSide side, const lv_point_t& point)
{
    _active_control = side;
    _visible_control = side;
    _control_start_point = point;
    _control_hide_at_ms = 0;

    if (side == CONTROL_BRIGHTNESS) {
        _control_start_value = GetHAL().getBackLightBrightness();
    } else {
        _control_start_value = GetHAL().getSpeakerVolume();
    }
    _control_current_value = _control_start_value;
    _control_applied_value = _control_start_value;
    _last_control_apply_ms = GetHAL().millis();

    _suppress_click_until_ms = GetHAL().millis() + 350;
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    set_lr_indicators_hidden(true);
    _control_overlay->setHidden(false);
    _control_overlay->moveForeground();
    refresh_control_overlay();
}

void LauncherView::update_active_control(const lv_point_t& point)
{
    int delta_y = _control_start_point.y - point.y;
    int value_range = _active_control == CONTROL_BRIGHTNESS ? 90 : 100;
    int raw_value = _control_start_value + (delta_y * value_range) / _control_drag_range_px;
    int value = normalize_control_value(_active_control, raw_value);

    if (value != _control_current_value) {
        _control_current_value = value;
        refresh_control_overlay();
    }

    uint32_t now = GetHAL().millis();
    if (now - _last_control_apply_ms >= _control_apply_period_ms) {
        apply_control_value(false);
    }
}

void LauncherView::finish_active_control()
{
    apply_control_value(true);

    _active_control = CONTROL_NONE;
    _panel->addFlag(LV_OBJ_FLAG_SCROLLABLE);
    _suppress_click_until_ms = GetHAL().millis() + 350;
    _control_hide_at_ms = GetHAL().millis() + 700;
    refresh_control_overlay();
}

void LauncherView::apply_control_value(bool saveToSettings)
{
    if (_active_control == CONTROL_NONE) {
        return;
    }

    if (!saveToSettings && _control_current_value == _control_applied_value) {
        return;
    }

    if (_active_control == CONTROL_BRIGHTNESS) {
        GetHAL().setBackLightBrightness(_control_current_value, saveToSettings);
    } else if (_active_control == CONTROL_VOLUME) {
        GetHAL().setSpeakerVolume(_control_current_value, saveToSettings);
    }

    _control_applied_value = _control_current_value;
    _last_control_apply_ms = GetHAL().millis();
}

void LauncherView::refresh_control_overlay()
{
    if (_control_overlay) {
        lv_obj_invalidate(_control_overlay->get());
    }
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

int LauncherView::normalize_control_value(ControlSide side, int value) const
{
    if (side == CONTROL_BRIGHTNESS) {
        return clamp_int(value, 10, 100);
    }

    value = clamp_int(value, 0, 100);
    return ((value + 2) / 5) * 5;
}

void LauncherView::handle_state_startup()
{
    _state = STATE_NORMAL;
}

void LauncherView::handle_state_normal()
{
    update_control_gesture();
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

void view::launcher_control_overlay_draw_event_cb(lv_event_t* e)
{
    auto* view = static_cast<LauncherView*>(lv_event_get_user_data(e));
    if (view == nullptr) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(e, 16);
        return;
    }

    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_NOT_COVER);
        return;
    }

    if (code != LV_EVENT_DRAW_MAIN_BEGIN || view->_visible_control == LauncherView::CONTROL_NONE) {
        return;
    }

    lv_obj_t* obj     = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    lv_point_t center = {
        static_cast<lv_coord_t>(coords.x1 + _control_center),
        static_cast<lv_coord_t>(coords.y1 + _control_center),
    };

    if (view->_visible_control == LauncherView::CONTROL_BRIGHTNESS) {
        const int start_angle = 135;
        const int end_angle   = 225;
        float ratio = static_cast<float>(view->_control_current_value - 10) / 90.0f;
        ratio       = std::clamp(ratio, 0.0f, 1.0f);
        int value_angle = start_angle + static_cast<int>(std::lround((end_angle - start_angle) * ratio));

        draw_arc(layer, center, _control_arc_radius, start_angle, end_angle, _control_arc_width, _brightness_color,
                 LV_OPA_40);
        draw_arc(layer, center, _control_arc_radius, start_angle, value_angle, _control_arc_width, _brightness_color,
                 LV_OPA_COVER);

        lv_point_t knob = point_on_arc(center.x, center.y, _control_arc_radius, static_cast<float>(value_angle));
        draw_circle(layer, knob, 9, _brightness_color, LV_OPA_COVER);
        draw_circle(layer, knob, 4, 0xB7D7FF, LV_OPA_COVER);

        draw_sun_icon(layer, coords.x1, coords.y1);
        draw_percent_label(layer, coords.x1 + 116, coords.y1 + 235, view->_control_current_value);
    } else if (view->_visible_control == LauncherView::CONTROL_VOLUME) {
        const int track_start = 315;
        const int track_end   = 405;
        float ratio = static_cast<float>(view->_control_current_value) / 100.0f;
        ratio       = std::clamp(ratio, 0.0f, 1.0f);
        int value_angle = track_end - static_cast<int>(std::lround((track_end - track_start) * ratio));

        draw_arc(layer, center, _control_arc_radius, track_start, track_end, _control_arc_width, _volume_color,
                 LV_OPA_30);
        draw_arc(layer, center, _control_arc_radius, value_angle, track_end, _control_arc_width, _volume_color,
                 LV_OPA_COVER);

        lv_point_t knob = point_on_arc(center.x, center.y, _control_arc_radius, static_cast<float>(value_angle));
        draw_circle(layer, knob, 9, _volume_color, LV_OPA_COVER);
        draw_circle(layer, knob, 4, 0xC6FFFF, LV_OPA_COVER);

        draw_speaker_icon(layer, coords.x1, coords.y1);
        draw_percent_label(layer, coords.x1 + 350, coords.y1 + 235, view->_control_current_value);
    }
}
