#include "edge_control_overlay.h"

#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <cstdio>
#include <smooth_lvgl.hpp>

using smooth_ui_toolkit::lvgl_cpp::Container;

namespace {

static constexpr int _control_panel_size    = 466;
static constexpr int _control_center        = _control_panel_size / 2;
static constexpr int _control_arc_radius    = 197;
static constexpr int _control_arc_width     = 8;
static constexpr uint32_t _brightness_color = 0x2F83FF;
static constexpr uint32_t _volume_color     = 0x36DDE2;

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

EdgeControlOverlay::~EdgeControlOverlay()
{
    shutdown();
}

void EdgeControlOverlay::init(lv_obj_t* parent)
{
    shutdown();

    if (parent == nullptr) {
        return;
    }

    _overlay = std::make_unique<Container>(parent);
    _overlay->align(LV_ALIGN_CENTER, 0, 0);
    _overlay->setSize(_control_panel_size, _control_panel_size);
    _overlay->setRadius(0);
    _overlay->setBorderWidth(0);
    _overlay->setPaddingAll(0);
    _overlay->setBgOpa(LV_OPA_TRANSP);
    _overlay->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _overlay->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _overlay->addFlag(LV_OBJ_FLAG_FLOATING);
    _overlay->setHidden(true);
    lv_obj_add_event_cb(_overlay->get(), draw_event_cb, LV_EVENT_ALL, this);
    lv_obj_refresh_ext_draw_size(_overlay->get());
}

void EdgeControlOverlay::show(EdgeControlSide side, int value)
{
    if (_overlay == nullptr || side == EdgeControlSide::None) {
        return;
    }

    const bool hidden = lv_obj_has_flag(_overlay->get(), LV_OBJ_FLAG_HIDDEN);
    const EdgeControlSide previous_side = _visible_side;
    if (!hidden && previous_side == side && _current_value == value) {
        return;
    }

    _visible_side  = side;
    _current_value = value;
    if (hidden) {
        _overlay->setHidden(false);
        _overlay->moveForeground();
        return;
    }

    if (previous_side != side && previous_side != EdgeControlSide::None) {
        invalidate(previous_side);
    }
    invalidate(side);
}

void EdgeControlOverlay::hide()
{
    if (_overlay != nullptr && !lv_obj_has_flag(_overlay->get(), LV_OBJ_FLAG_HIDDEN)) {
        _overlay->setHidden(true);
    }
    _visible_side = EdgeControlSide::None;
}

void EdgeControlOverlay::shutdown()
{
    if (_overlay != nullptr) {
        lv_obj_remove_event_cb_with_user_data(_overlay->get(), draw_event_cb, this);
        _overlay.reset();
    }
    _visible_side  = EdgeControlSide::None;
    _current_value = 0;
}

void EdgeControlOverlay::invalidate(EdgeControlSide side)
{
    if (_overlay == nullptr || side == EdgeControlSide::None) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(_overlay->get(), &coords);
    const bool brightness = side == EdgeControlSide::Brightness;
    lv_area_t area = {
        static_cast<lv_coord_t>(coords.x1 + (brightness ? 18 : 292)),
        static_cast<lv_coord_t>(coords.y1 + 48),
        static_cast<lv_coord_t>(coords.x1 + (brightness ? 174 : 448)),
        static_cast<lv_coord_t>(coords.y1 + 383),
    };
    lv_obj_invalidate_area(_overlay->get(), &area);
}

void EdgeControlOverlay::draw_event_cb(lv_event_t* event)
{
    auto* overlay = static_cast<EdgeControlOverlay*>(lv_event_get_user_data(event));
    if (overlay == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(event, 16);
        return;
    }

    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(event, LV_COVER_RES_NOT_COVER);
        return;
    }

    const EdgeControlSide side = overlay->_visible_side;
    if (code != LV_EVENT_DRAW_MAIN_BEGIN || side == EdgeControlSide::None) {
        return;
    }

    lv_obj_t* obj     = lv_event_get_target_obj(event);
    lv_layer_t* layer = lv_event_get_layer(event);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    lv_point_t center = {
        static_cast<lv_coord_t>(coords.x1 + _control_center),
        static_cast<lv_coord_t>(coords.y1 + _control_center),
    };

    const int value = overlay->_current_value;
    if (side == EdgeControlSide::Brightness) {
        const int start_angle = 135;
        const int end_angle   = 225;
        float ratio           = static_cast<float>(value - 10) / 90.0f;
        ratio                 = std::clamp(ratio, 0.0f, 1.0f);
        int value_angle = start_angle + static_cast<int>(std::lround((end_angle - start_angle) * ratio));

        draw_arc(layer, center, _control_arc_radius, start_angle, end_angle, _control_arc_width, _brightness_color,
                 LV_OPA_40);
        draw_arc(layer, center, _control_arc_radius, start_angle, value_angle, _control_arc_width, _brightness_color,
                 LV_OPA_COVER);

        const int marker_radius = _control_arc_radius - _control_arc_width / 2;
        lv_point_t knob = point_on_arc(center.x, center.y, marker_radius, static_cast<float>(value_angle));
        draw_circle(layer, knob, 9, _brightness_color, LV_OPA_COVER);
        draw_circle(layer, knob, 4, 0xB7D7FF, LV_OPA_COVER);

        draw_sun_icon(layer, coords.x1, coords.y1);
        draw_percent_label(layer, coords.x1 + 116, coords.y1 + 235, value);
    } else if (side == EdgeControlSide::Volume) {
        const int track_start = 315;
        const int track_end   = 405;
        float ratio           = static_cast<float>(value) / 100.0f;
        ratio                 = std::clamp(ratio, 0.0f, 1.0f);
        int value_angle = track_end - static_cast<int>(std::lround((track_end - track_start) * ratio));

        draw_arc(layer, center, _control_arc_radius, track_start, track_end, _control_arc_width, _volume_color,
                 LV_OPA_30);
        draw_arc(layer, center, _control_arc_radius, value_angle, track_end, _control_arc_width, _volume_color,
                 LV_OPA_COVER);

        const int marker_radius = _control_arc_radius - _control_arc_width / 2;
        lv_point_t knob = point_on_arc(center.x, center.y, marker_radius, static_cast<float>(value_angle));
        draw_circle(layer, knob, 9, _volume_color, LV_OPA_COVER);
        draw_circle(layer, knob, 4, 0xC6FFFF, LV_OPA_COVER);

        draw_speaker_icon(layer, coords.x1, coords.y1);
        draw_percent_label(layer, coords.x1 + 350, coords.y1 + 235, value);
    }
}
