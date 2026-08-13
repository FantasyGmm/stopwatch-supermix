#include "codex_task_focus_deck.h"

#include <algorithm>
#include <utility>


namespace {

const CodexTaskAlert* find_route(
    const std::vector<CodexTaskAlert>& tasks,
    uint64_t route
)
{
    const auto found = std::find_if(
        tasks.begin(),
        tasks.end(),
        [route](const CodexTaskAlert& task) { return task.route == route; }
    );
    return found == tasks.end() ? nullptr : &*found;
}


size_t route_index(const std::vector<CodexTaskAlert>& tasks, uint64_t route)
{
    return static_cast<size_t>(std::distance(
        tasks.begin(),
        std::find_if(
            tasks.begin(),
            tasks.end(),
            [route](const CodexTaskAlert& task) { return task.route == route; }
        )
    ));
}

}  // namespace


bool CodexTaskFocusDeck::apply(const CodexStatusSnapshot& snapshot)
{
    if (!snapshot.alerts_valid) {
        return false;
    }

    size_t active_count = 0;
    for (size_t index = 0; index < snapshot.alerts.size(); ++index) {
        if (snapshot.alerts[index].state == CodexTaskState::Completed) {
            continue;
        }
        ++active_count;
        const uint64_t route = snapshot.alerts[index].route;
        if (route == 0) {
            return false;
        }
        for (size_t other = index + 1; other < snapshot.alerts.size(); ++other) {
            if (snapshot.alerts[other].state != CodexTaskState::Completed &&
                snapshot.alerts[other].route == route) {
                return false;
            }
        }
    }
    if (active_count > kMaxTasks) {
        return false;
    }

    std::vector<CodexTaskAlert> next;
    next.reserve(active_count);
    for (const auto& task : snapshot.alerts) {
        if (task.state == CodexTaskState::Pending) {
            next.push_back(task);
        }
    }
    for (const auto& task : snapshot.alerts) {
        if (task.state == CodexTaskState::Running) {
            next.push_back(task);
        }
    }

    const size_t old_focus = _focused_index;
    const uint64_t old_selection = _selected_route;
    const bool selection_survives =
        old_selection != 0 && find_route(next, old_selection) != nullptr;

    uint64_t next_selection = old_selection;
    if (!selection_survives) {
        next_selection = next.empty() ? 0 : next.front().route;
    }

    size_t next_focus = 0;
    if (!next.empty()) {
        if (selection_survives || old_selection == 0) {
            next_focus = route_index(next, next_selection);
        } else {
            next_focus = std::min(old_focus, next.size() - 1);
        }
    }

    _tasks = std::move(next);
    _focused_index = next_focus;
    _selected_route = next_selection;
    return true;
}


CodexTaskFocusEvent CodexTaskFocusDeck::browse(int direction)
{
    if ((direction != -1 && direction != 1) || _tasks.empty()) {
        return CodexTaskFocusEvent::None;
    }
    if ((direction < 0 && _focused_index == 0) ||
        (direction > 0 && _focused_index + 1 >= _tasks.size())) {
        return CodexTaskFocusEvent::None;
    }

    const CodexTaskState previous_state = _tasks[_focused_index].state;
    if (direction < 0) {
        --_focused_index;
    } else {
        ++_focused_index;
    }
    return previous_state == _tasks[_focused_index].state
        ? CodexTaskFocusEvent::FocusChanged
        : CodexTaskFocusEvent::StateBoundaryCrossed;
}


CodexTaskFocusEvent CodexTaskFocusDeck::select_focused()
{
    if (_tasks.empty() || _selected_route == _tasks[_focused_index].route) {
        return CodexTaskFocusEvent::None;
    }
    _selected_route = _tasks[_focused_index].route;
    return CodexTaskFocusEvent::SelectionChanged;
}


void CodexTaskFocusDeck::reset()
{
    _tasks.clear();
    _focused_index = 0;
    _selected_route = 0;
}


const std::vector<CodexTaskAlert>& CodexTaskFocusDeck::tasks() const
{
    return _tasks;
}


size_t CodexTaskFocusDeck::count() const
{
    return _tasks.size();
}


size_t CodexTaskFocusDeck::focused_index() const
{
    return _focused_index;
}


uint64_t CodexTaskFocusDeck::selected_route() const
{
    return _selected_route;
}


const CodexTaskAlert* CodexTaskFocusDeck::focused() const
{
    return _tasks.empty() ? nullptr : &_tasks[_focused_index];
}


const CodexTaskAlert* CodexTaskFocusDeck::selected() const
{
    return find_route(_tasks, _selected_route);
}
