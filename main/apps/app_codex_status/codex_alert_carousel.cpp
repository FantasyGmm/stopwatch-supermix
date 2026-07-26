#include "codex_alert_carousel.h"

#include <algorithm>
#include <cstdio>


namespace {

constexpr uint32_t kRotationIntervalMs = 3000;

}  // namespace


bool CodexAlertCarousel::apply(const CodexStatusSnapshot& snapshot, uint32_t now_ms)
{
    if (!snapshot.alerts_valid || snapshot.alert_revision == _revision) {
        return false;
    }

    _revision = snapshot.alert_revision;
    _pending_count = snapshot.pending_count;
    _running_count = snapshot.running_count;
    _completed_count = snapshot.completed_count;
    _alerts = snapshot.alerts;
    _index = 0;
    const CodexTaskState selected_state = display_state();
    const auto selected = std::find_if(
        _alerts.begin(),
        _alerts.end(),
        [selected_state](const CodexTaskAlert& alert) {
            return alert.state == selected_state;
        }
    );
    if (selected != _alerts.end()) {
        _index = static_cast<size_t>(std::distance(_alerts.begin(), selected));
    }
    _last_rotation_ms = now_ms;
    return true;
}


bool CodexAlertCarousel::update(uint32_t now_ms)
{
    const CodexTaskState selected_state = display_state();
    const size_t available = static_cast<size_t>(std::count_if(
        _alerts.begin(),
        _alerts.end(),
        [selected_state](const CodexTaskAlert& alert) {
            return alert.state == selected_state;
        }
    ));
    if (available < 2 || now_ms - _last_rotation_ms < kRotationIntervalMs) {
        return false;
    }

    const uint32_t elapsed = now_ms - _last_rotation_ms;
    const size_t steps = elapsed / kRotationIntervalMs;
    const size_t rotations = steps % available;
    for (size_t step = 0; step < rotations; ++step) {
        do {
            _index = (_index + 1) % _alerts.size();
        } while (_alerts[_index].state != selected_state);
    }
    _last_rotation_ms += static_cast<uint32_t>(steps) * kRotationIntervalMs;
    return rotations != 0;
}


void CodexAlertCarousel::reset()
{
    _revision = -1;
    _pending_count = 0;
    _running_count = 0;
    _completed_count = 0;
    _alerts.clear();
    _index = 0;
    _last_rotation_ms = 0;
}


const CodexTaskAlert* CodexAlertCarousel::current() const
{
    return _alerts.empty() ? nullptr : &_alerts[_index];
}


CodexTaskState CodexAlertCarousel::display_state() const
{
    if (_pending_count > 0) {
        return CodexTaskState::Pending;
    }
    if (_running_count > 0) {
        return CodexTaskState::Running;
    }
    return CodexTaskState::Completed;
}


int CodexAlertCarousel::pending_count() const
{
    return _pending_count;
}


int CodexAlertCarousel::running_count() const
{
    return _running_count;
}


bool CodexAlertCarousel::completed() const
{
    return !_alerts.empty() && display_state() == CodexTaskState::Completed;
}


std::string CodexAlertCarousel::heading() const
{
    if (_alerts.empty()) {
        return "NO ALERTS";
    }
    const CodexTaskState selected_state = display_state();
    const int total = static_cast<int>(std::count_if(
        _alerts.begin(),
        _alerts.end(),
        [selected_state](const CodexTaskAlert& alert) {
            return alert.state == selected_state;
        }
    ));
    if (total == 0) {
        return "NO ALERTS";
    }

    int position = 0;
    for (size_t i = 0; i <= _index; ++i) {
        if (_alerts[i].state == selected_state) {
            ++position;
        }
    }
    char text[32];
    std::snprintf(text, sizeof(text), "TASK %d/%d", position, total);
    return text;
}
