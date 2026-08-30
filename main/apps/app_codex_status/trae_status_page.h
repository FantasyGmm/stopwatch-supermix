#pragma once

#include "trae_status_model.h"

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <cstdint>


struct TraeChannelState {
    const char* text = "";
    uint32_t dot_color = 0;
    uint32_t text_color = 0;
};


struct TraeChannelPresentation {
    TraeChannelState agent;
    TraeChannelState voice;
};


class TraeStatusPage {
public:
    void init(lv_obj_t* parent);
    void render(
        const TraeStatusSnapshot& snapshot,
        const TraeTask* task,
        const TraeTask* next_task,
        const TraeChannelPresentation& channels,
        bool stale,
        bool bluetooth_connected
    );
    void set_active(bool active);
    bool contains_task_point(int x, int y) const;
    void shutdown();

private:
    struct TaskRow {
        lv_obj_t* project = nullptr;
        lv_obj_t* title = nullptr;
        lv_obj_t* meta = nullptr;
        lv_obj_t* model = nullptr;
        lv_obj_t* effort_badge = nullptr;
        lv_obj_t* effort = nullptr;
    };

    void render_task_row(
        size_t index,
        const TraeTask* task,
        const TraeTask& fallback,
        bool single
    );
    void set_task_row_hidden(size_t index, bool hidden);

    lv_obj_t* _root = nullptr;
    lv_obj_t* _brand_icon = nullptr;
    lv_obj_t* _agent_dot = nullptr;
    lv_obj_t* _agent_status = nullptr;
    lv_obj_t* _voice_dot = nullptr;
    lv_obj_t* _voice_status = nullptr;
    lv_obj_t* _pending_label = nullptr;
    lv_obj_t* _pending_count = nullptr;
    lv_obj_t* _running_label = nullptr;
    lv_obj_t* _running_count = nullptr;
    lv_obj_t* _task_group_label = nullptr;
    lv_obj_t* _task_divider = nullptr;
    lv_obj_t* _running_summary = nullptr;
    lv_obj_t* _empty_state = nullptr;
    std::array<TaskRow, 2> _task_rows{};
    bool _active = false;
};
