#pragma once

#include "trae_status_model.h"

#include <lvgl.h>

#include <array>


class TraeStatusPage {
public:
    void init(lv_obj_t* parent);
    void render(
        const TraeStatusSnapshot& snapshot,
        const TraeTask* task,
        const TraeTask* next_task,
        bool stale,
        bool bluetooth_connected
    );
    void set_active(bool active);
    bool contains_task_point(int x, int y) const;
    void shutdown();

private:
    static void robot_timer_cb(lv_timer_t* timer);
    void set_robot_action(bool action);

    lv_obj_t* _root = nullptr;
    lv_obj_t* _brand_icon = nullptr;
    lv_obj_t* _bt_dot = nullptr;
    lv_obj_t* _bt_status = nullptr;
    lv_obj_t* _process_dot = nullptr;
    lv_obj_t* _process_status = nullptr;
    lv_obj_t* _robot = nullptr;
    lv_obj_t* _model = nullptr;
    lv_obj_t* _effort = nullptr;
    lv_obj_t* _pending_label = nullptr;
    lv_obj_t* _pending_count = nullptr;
    lv_obj_t* _running_label = nullptr;
    lv_obj_t* _running_count = nullptr;
    lv_obj_t* _current_project = nullptr;
    lv_obj_t* _current_task = nullptr;
    lv_obj_t* _next_caption = nullptr;
    lv_obj_t* _next_task = nullptr;
    lv_timer_t* _robot_timer = nullptr;
    std::array<char, 33> _last_task_id{};
    bool _active = false;
    bool _robot_action = false;
};
