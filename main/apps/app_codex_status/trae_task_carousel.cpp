#include "trae_task_carousel.h"

#include <algorithm>
#include <cstring>

bool TraeTaskCarousel::apply(
    const TraeStatusSnapshot& snapshot,
    uint32_t now_ms
)
{
    (void)now_ms;
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
    if (_task_count >= 2 && _index + 1 >= _task_count) {
        _index = _task_count - 2;
    }
    return true;
}


bool TraeTaskCarousel::update(uint32_t now_ms)
{
    (void)now_ms;
    return false;
}


bool TraeTaskCarousel::scroll_forward()
{
    if (_task_count < 2 || _index + 2 >= _task_count) {
        return false;
    }
    ++_index;
    return true;
}


bool TraeTaskCarousel::scroll_backward()
{
    if (_index == 0 || _task_count == 0) {
        return false;
    }
    --_index;
    return true;
}


void TraeTaskCarousel::reset()
{
    _revision = -1;
    _tasks = {};
    _task_count = 0;
    _index = 0;
}


const TraeTask* TraeTaskCarousel::current() const
{
    return _task_count == 0 ? nullptr : &_tasks[_index];
}


const TraeTask* TraeTaskCarousel::next() const
{
    if (_index + 1 >= _task_count) {
        return nullptr;
    }
    return &_tasks[_index + 1];
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


size_t TraeTaskCarousel::first_priority_index() const
{
    for (size_t index = 0; index < _task_count; ++index) {
        if (_tasks[index].state == TraeTaskState::Pending) {
            return index;
        }
    }
    return 0;
}
