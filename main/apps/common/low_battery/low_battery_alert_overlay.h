#pragma once

#include <lvgl.h>
#include <memory>

namespace smooth_ui_toolkit::lvgl_cpp {
class Container;
class Label;
}

class LowBatteryAlertOverlay {
public:
    ~LowBatteryAlertOverlay();

    bool init(lv_obj_t* parent);
    void show(int battery_percent);
    void hide();
    void shutdown();
    bool visible() const;

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _banner;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _percent;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _message;
};
