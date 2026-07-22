#pragma once

#include "edge_control_logic.h"

#include <lvgl.h>
#include <memory>

namespace smooth_ui_toolkit::lvgl_cpp {
class Container;
}

class EdgeControlOverlay {
public:
    EdgeControlOverlay() = default;
    ~EdgeControlOverlay();

    void init(lv_obj_t* parent);
    void show(EdgeControlSide side, int value);
    void hide();
    void shutdown();

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _overlay;
    EdgeControlSide _visible_side = EdgeControlSide::None;
    int _current_value = 0;

    void invalidate();
    static void draw_event_cb(lv_event_t* event);
};
