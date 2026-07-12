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
    _handled_count = snapshot.handled_count;
    _alerts = snapshot.alerts;
    _index = 0;
    if (_pending_count > 0) {
        const auto pending = std::find_if(
            _alerts.begin(),
            _alerts.end(),
            [](const CodexTaskAlert& alert) { return alert.pending; }
        );
        if (pending != _alerts.end()) {
            _index = static_cast<size_t>(std::distance(_alerts.begin(), pending));
        }
    }
    _last_rotation_ms = now_ms;
    return true;
}


bool CodexAlertCarousel::update(uint32_t now_ms)
{
    const size_t available_pending = static_cast<size_t>(std::count_if(
        _alerts.begin(),
        _alerts.end(),
        [](const CodexTaskAlert& alert) { return alert.pending; }
    ));
    if (available_pending < 2 || now_ms - _last_rotation_ms < kRotationIntervalMs) {
        return false;
    }

    const uint32_t elapsed = now_ms - _last_rotation_ms;
    const size_t steps = elapsed / kRotationIntervalMs;
    const size_t rotations = steps % available_pending;
    for (size_t step = 0; step < rotations; ++step) {
        do {
            _index = (_index + 1) % _alerts.size();
        } while (!_alerts[_index].pending);
    }
    _last_rotation_ms += static_cast<uint32_t>(steps) * kRotationIntervalMs;
    return rotations != 0;
}


void CodexAlertCarousel::reset()
{
    _revision = -1;
    _pending_count = 0;
    _handled_count = 0;
    _alerts.clear();
    _index = 0;
    _last_rotation_ms = 0;
}


const CodexTaskAlert* CodexAlertCarousel::current() const
{
    return _alerts.empty() ? nullptr : &_alerts[_index];
}


bool CodexAlertCarousel::all_handled() const
{
    return !_alerts.empty() && _pending_count == 0;
}


std::string CodexAlertCarousel::heading() const
{
    if (_alerts.empty()) {
        return "NO ALERTS";
    }
    if (_pending_count == 0) {
        return "COMPLETED";
    }

    int position = 0;
    for (size_t i = 0; i <= _index; ++i) {
        if (_alerts[i].pending) {
            ++position;
        }
    }
    char text[32];
    std::snprintf(text, sizeof(text), "PENDING %d/%d",
                  position, std::max(_pending_count, 1));
    return text;
}
