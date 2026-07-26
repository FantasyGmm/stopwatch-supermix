#include "edge_control_logic.h"

#include <algorithm>
#include <cstdlib>

namespace {

constexpr int kTopExclusionPx = 30;
constexpr int kHalfWidthPx = 233;
constexpr int kActivationDistancePx = 24;
constexpr int kDragRangePx = 210;
constexpr uint32_t kLiveApplyPeriodMs = 40;
constexpr uint32_t kHideDelayMs = 700;

} // namespace

void EdgeControlLogic::press(int x, int y, int brightness, int volume, uint32_t now_ms)
{
    reset();
    _start_x = x;
    _start_y = y;
    _candidate = y > kTopExclusionPx
        ? (x < kHalfWidthPx ? EdgeControlSide::Brightness : EdgeControlSide::Volume)
        : EdgeControlSide::None;
    _start_value = normalize_visual(_candidate, _candidate == EdgeControlSide::Brightness ? brightness : volume);
    _current_value = _start_value;
    _applied_value = normalize_hardware(_candidate, _start_value);
    _last_apply_ms = now_ms;
}

EdgeControlUpdate EdgeControlLogic::move(int x, int y, uint32_t now_ms)
{
    EdgeControlUpdate update;
    if (_candidate == EdgeControlSide::None && _active == EdgeControlSide::None) {
        return update;
    }

    const int delta_x = x - _start_x;
    const int delta_y = _start_y - y;
    const int abs_x = std::abs(delta_x);
    const int abs_y = std::abs(delta_y);

    if (_active == EdgeControlSide::None && abs_y > kActivationDistancePx && abs_y > abs_x) {
        _active = _candidate;
        _visible = _active;
        _candidate = EdgeControlSide::None;
        update.activated = true;
    } else if (_active == EdgeControlSide::None && abs_x > kActivationDistancePx && abs_x > abs_y) {
        _candidate = EdgeControlSide::None;
        update.cancelled = true;
        return update;
    }

    if (_active == EdgeControlSide::None) {
        return update;
    }

    const int value_range = _active == EdgeControlSide::Brightness ? 90 : 100;
    _current_value = normalize_visual(_active, _start_value + (delta_y * value_range) / kDragRangePx);

    update.side = _active;
    update.value = _current_value;
    update.hardware_value = normalize_hardware(_active, _current_value);
    update.consumed = true;
    if (now_ms - _last_apply_ms >= kLiveApplyPeriodMs && update.hardware_value != _applied_value) {
        update.apply = true;
        _applied_value = update.hardware_value;
        _last_apply_ms = now_ms;
    }
    return update;
}

EdgeControlUpdate EdgeControlLogic::release(uint32_t now_ms)
{
    EdgeControlUpdate update;
    if (_active == EdgeControlSide::None) {
        _candidate = EdgeControlSide::None;
        return update;
    }

    update.side = _active;
    update.value = _current_value;
    update.hardware_value = normalize_hardware(_active, _current_value);
    update.apply = true;
    update.persist = true;
    update.consumed = true;

    _applied_value = update.hardware_value;
    _active = EdgeControlSide::None;
    _candidate = EdgeControlSide::None;
    _hide_at_ms = now_ms + kHideDelayMs;
    return update;
}

EdgeControlUpdate EdgeControlLogic::tick(uint32_t now_ms)
{
    EdgeControlUpdate update;
    if (_hide_at_ms == 0 || static_cast<int32_t>(now_ms - _hide_at_ms) < 0) {
        return update;
    }

    update.side = _visible;
    update.value = _current_value;
    update.hide = true;
    _visible = EdgeControlSide::None;
    _hide_at_ms = 0;
    return update;
}

int EdgeControlLogic::normalize_visual(EdgeControlSide side, int value)
{
    if (side == EdgeControlSide::Brightness) {
        return std::clamp(value, 10, 100);
    }
    if (side == EdgeControlSide::Volume) {
        return std::clamp(value, 0, 100);
    }
    return value;
}

int EdgeControlLogic::normalize_hardware(EdgeControlSide side, int value)
{
    value = normalize_visual(side, value);
    if (side == EdgeControlSide::Volume) {
        return ((value + 2) / 5) * 5;
    }
    return value;
}

void EdgeControlApplyQueue::push(const EdgeControlUpdate& update)
{
    if (!update.apply || update.side == EdgeControlSide::None) {
        return;
    }
    _pending.side = update.side;
    _pending.value = update.hardware_value;
    _pending.persist = update.persist;
}

EdgeControlHardwareUpdate EdgeControlApplyQueue::take()
{
    EdgeControlHardwareUpdate update = _pending;
    reset();
    return update;
}

void EdgeControlApplyQueue::reset()
{
    _pending = {};
}

bool EdgeControlApplyQueue::pending() const
{
    return _pending.side != EdgeControlSide::None;
}

bool EdgeControlLogic::active() const
{
    return _active != EdgeControlSide::None;
}

int EdgeControlLogic::current_value() const
{
    return _current_value;
}

EdgeControlSide EdgeControlLogic::visible_side() const
{
    return _visible;
}

void EdgeControlLogic::reset()
{
    _candidate = EdgeControlSide::None;
    _active = EdgeControlSide::None;
    _visible = EdgeControlSide::None;
    _start_x = 0;
    _start_y = 0;
    _start_value = 0;
    _current_value = 0;
    _applied_value = -1;
    _last_apply_ms = 0;
    _hide_at_ms = 0;
}
