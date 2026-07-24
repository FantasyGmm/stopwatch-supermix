#pragma once

#include "trae_status_model.h"

#include <array>
#include <cstddef>
#include <cstdint>


class TraeTaskCarousel {
public:
    bool apply(const TraeStatusSnapshot& snapshot, uint32_t now_ms);
    bool update(uint32_t now_ms);
    void reset();

    const TraeTask* current() const;
    TraeTaskState display_state() const;

private:
    int _revision = -1;
    std::array<TraeTask, 4> _tasks{};
    size_t _task_count = 0;
    size_t _index = 0;
    uint32_t _last_rotation_ms = 0;
};
