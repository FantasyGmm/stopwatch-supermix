#pragma once

#include "trae_status_model.h"

#include <array>
#include <cstddef>
#include <cstdint>


class TraeTaskCarousel {
public:
    bool apply(const TraeStatusSnapshot& snapshot, uint32_t now_ms);
    bool update(uint32_t now_ms);
    bool manual_next(uint32_t now_ms);
    void reset();

    const TraeTask* current() const;
    TraeTaskState display_state() const;
    size_t task_count() const;
    bool manual_mode() const;

private:
    size_t first_priority_index() const;
    size_t next_priority_index(size_t from) const;

    int _revision = -1;
    std::array<TraeTask, 4> _tasks{};
    size_t _task_count = 0;
    size_t _index = 0;
    uint32_t _last_rotation_ms = 0;
    uint32_t _last_manual_ms = 0;
    bool _manual_mode = false;
};
