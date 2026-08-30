#pragma once

#include "trae_status_model.h"

#include <array>
#include <cstddef>
#include <cstdint>


class TraeTaskCarousel {
public:
    bool apply(const TraeStatusSnapshot& snapshot, uint32_t now_ms);
    bool update(uint32_t now_ms);
    bool scroll_forward();
    bool scroll_backward();
    void reset();

    const TraeTask* current() const;
    const TraeTask* next() const;
    TraeTaskState display_state() const;
    size_t task_count() const;

private:
    size_t first_priority_index() const;

    int _revision = -1;
    std::array<TraeTask, 4> _tasks{};
    size_t _task_count = 0;
    size_t _index = 0;
};
