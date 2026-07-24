#pragma once

#include "trae_status_model.h"

#include <lvgl.h>


class TraeStatusPage {
public:
    void init(lv_obj_t* parent);
    void render(
        const TraeStatusSnapshot& snapshot,
        const TraeTask* task,
        bool stale
    );
    void shutdown();

private:
    lv_obj_t* _root = nullptr;
    lv_obj_t* _status = nullptr;
    lv_obj_t* _model = nullptr;
    lv_obj_t* _effort = nullptr;
    lv_obj_t* _state = nullptr;
    lv_obj_t* _project = nullptr;
    lv_obj_t* _title = nullptr;
    lv_obj_t* _detail = nullptr;
};
