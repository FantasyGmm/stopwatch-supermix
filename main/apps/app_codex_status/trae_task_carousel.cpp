#include "trae_task_carousel.h"


namespace {

constexpr uint32_t kRotationIntervalMs = 3000;

}  // namespace


bool TraeTaskCarousel::apply(
    const TraeStatusSnapshot& snapshot,
    uint32_t now_ms
)
{
    if (snapshot.revision == _revision) {
        return false;
    }
    _revision = snapshot.revision;
    _task_count = snapshot.task_count;
    _tasks = snapshot.tasks;
    _index = 0;
    const TraeTaskState selected = display_state();
    for (size_t index = 0; index < _task_count; ++index) {
        if (_tasks[index].state == selected) {
            _index = index;
            break;
        }
    }
    _last_rotation_ms = now_ms;
    return true;
}


bool TraeTaskCarousel::update(uint32_t now_ms)
{
    if (_task_count < 2 ||
        now_ms - _last_rotation_ms < kRotationIntervalMs) {
        return false;
    }
    const TraeTaskState selected = display_state();
    size_t available = 0;
    for (size_t index = 0; index < _task_count; ++index) {
        if (_tasks[index].state == selected) {
            ++available;
        }
    }
    if (available < 2) {
        return false;
    }

    const uint32_t elapsed = now_ms - _last_rotation_ms;
    const size_t steps = elapsed / kRotationIntervalMs;
    const size_t rotations = steps % available;
    for (size_t step = 0; step < rotations; ++step) {
        do {
            _index = (_index + 1) % _task_count;
        } while (_tasks[_index].state != selected);
    }
    _last_rotation_ms += static_cast<uint32_t>(steps) * kRotationIntervalMs;
    return rotations != 0;
}


void TraeTaskCarousel::reset()
{
    _revision = -1;
    _tasks = {};
    _task_count = 0;
    _index = 0;
    _last_rotation_ms = 0;
}


const TraeTask* TraeTaskCarousel::current() const
{
    return _task_count == 0 ? nullptr : &_tasks[_index];
}


TraeTaskState TraeTaskCarousel::display_state() const
{
    for (size_t index = 0; index < _task_count; ++index) {
        if (_tasks[index].state == TraeTaskState::Pending) {
            return TraeTaskState::Pending;
        }
    }
    for (size_t index = 0; index < _task_count; ++index) {
        if (_tasks[index].state == TraeTaskState::Running) {
            return TraeTaskState::Running;
        }
    }
    return TraeTaskState::Completed;
}
