#include "trae_task_carousel.h"

#include <algorithm>
#include <cstring>

namespace {

constexpr uint32_t kRotationIntervalMs = 3000;
constexpr uint32_t kManualTimeoutMs = 5000;

}  // namespace


bool TraeTaskCarousel::apply(
    const TraeStatusSnapshot& snapshot,
    uint32_t now_ms
)
{
    if (snapshot.revision == _revision) {
        return false;
    }

    std::array<char, 33> current_id{};
    if (current() != nullptr) {
        current_id = current()->id;
    }

    std::array<TraeTask, 4> active_tasks{};
    size_t active_count = 0;
    const size_t snapshot_count = std::min(snapshot.task_count, snapshot.tasks.size());
    for (size_t index = 0; index < snapshot_count; ++index) {
        const TraeTask& candidate = snapshot.tasks[index];
        if (candidate.state == TraeTaskState::Pending ||
            candidate.state == TraeTaskState::Running) {
            active_tasks[active_count++] = candidate;
        }
    }

    _revision = snapshot.revision;
    _tasks = active_tasks;
    _task_count = active_count;
    _index = first_priority_index();
    if (_task_count < 2) {
        _manual_mode = false;
    }
    if (current_id[0] != '\0') {
        for (size_t index = 0; index < _task_count; ++index) {
            if (std::strncmp(
                    _tasks[index].id.data(),
                    current_id.data(),
                    current_id.size()
                ) == 0) {
            _index = index;
            break;
            }
        }
    }
    _last_rotation_ms = now_ms;
    return true;
}


bool TraeTaskCarousel::update(uint32_t now_ms)
{
    if (_manual_mode) {
        if (now_ms - _last_manual_ms < kManualTimeoutMs) {
            return false;
        }

        _manual_mode = false;
        const size_t priority_index = first_priority_index();
        const bool changed = _task_count != 0 && _index != priority_index;
        _index = priority_index;
        _last_rotation_ms = now_ms;
        return changed;
    }

    if (_task_count < 2 ||
        now_ms - _last_rotation_ms < kRotationIntervalMs) {
        return false;
    }

    const size_t priority_index = first_priority_index();
    const TraeTaskState priority_state = _tasks[priority_index].state;
    const bool current_is_priority = _tasks[_index].state == priority_state;
    if (current_is_priority &&
        priority_index == next_priority_index(priority_index)) {
        return false;
    }

    const uint32_t elapsed = now_ms - _last_rotation_ms;
    const size_t steps = elapsed / kRotationIntervalMs;
    size_t new_index = current_is_priority ? _index : priority_index;
    const size_t rotations = current_is_priority ? steps : steps - 1;
    for (size_t step = 0; step < rotations; ++step) {
        new_index = next_priority_index(new_index);
    }
    _last_rotation_ms += static_cast<uint32_t>(steps) * kRotationIntervalMs;
    const bool changed = _index != new_index;
    _index = new_index;
    return changed;
}


bool TraeTaskCarousel::manual_next(uint32_t now_ms)
{
    if (_task_count < 2) {
        return false;
    }

    _index = (_index + 1) % _task_count;
    _last_manual_ms = now_ms;
    _manual_mode = true;
    return true;
}


void TraeTaskCarousel::reset()
{
    _revision = -1;
    _tasks = {};
    _task_count = 0;
    _index = 0;
    _last_rotation_ms = 0;
    _last_manual_ms = 0;
    _manual_mode = false;
}


const TraeTask* TraeTaskCarousel::current() const
{
    return _task_count == 0 ? nullptr : &_tasks[_index];
}


TraeTaskState TraeTaskCarousel::display_state() const
{
    const TraeTask* task = current();
    return task == nullptr ? TraeTaskState::Completed : task->state;
}


size_t TraeTaskCarousel::task_count() const
{
    return _task_count;
}


bool TraeTaskCarousel::manual_mode() const
{
    return _manual_mode;
}


size_t TraeTaskCarousel::first_priority_index() const
{
    for (size_t index = 0; index < _task_count; ++index) {
        if (_tasks[index].state == TraeTaskState::Pending) {
            return index;
        }
    }
    return 0;
}


size_t TraeTaskCarousel::next_priority_index(size_t from) const
{
    if (_task_count == 0) {
        return 0;
    }

    const TraeTaskState priority = _tasks[first_priority_index()].state;
    for (size_t offset = 1; offset < _task_count; ++offset) {
        const size_t index = (from + offset) % _task_count;
        if (_tasks[index].state == priority) {
            return index;
        }
    }
    return from;
}
