#pragma once

#include "edge_control_logic.h"

#include <functional>
#include <lvgl.h>
#include <memory>

namespace smooth_ui_toolkit::lvgl_cpp {
class Container;
}

class EdgeControlOverlay {
public:
    using ActiveChanged = std::function<void(bool)>;

    EdgeControlOverlay() = default;
    ~EdgeControlOverlay();

    void init(lv_obj_t* parent, ActiveChanged on_active_changed = {});
    void update();
    void shutdown();
    bool last_release_consumed() const;

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _overlay;
    EdgeControlLogic _logic;
    ActiveChanged _on_active_changed;
    lv_indev_state_t _last_touch_state = LV_INDEV_STATE_REL;
    bool _last_release_consumed = false;

    void apply(const EdgeControlUpdate& update);
    void invalidate();
    static void draw_event_cb(lv_event_t* event);
};
