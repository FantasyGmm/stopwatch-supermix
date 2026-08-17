#include "global_edge_control_service.h"

#include <hal/hal.h>
#include <runtime/display/global_display_protection_service.h>
#include <runtime/runtime_supervisor.h>

namespace {
GlobalEdgeControlService global_edge_control_service;
}

GlobalEdgeControlService& GetGlobalEdgeControlService()
{
    return global_edge_control_service;
}

bool GlobalEdgeControlService::init()
{
    shutdown();

    lv_obj_t* top_layer = lv_layer_top();
    if (top_layer == nullptr || GetHAL().lvTouchpad == nullptr) {
        return false;
    }

    _overlay.init(top_layer);
    _initialized = true;
    _enabled = true;
    return true;
}

void GlobalEdgeControlService::update()
{
    if (!_initialized || !_enabled) {
        return;
    }

    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr) {
        return;
    }

    if (GetGlobalDisplayProtectionService().touchInputSuppressed()) {
        _logic.reset();
        _apply_queue.reset();
        _overlay.hide();
        _last_touch_state = LV_INDEV_STATE_REL;
        _owns_touch = false;
        _last_release_consumed = false;
        return;
    }

    if (GetGlobalDisplayProtectionService().state() ==
            DisplayProtectionState::ScreenOff ||
        GetHAL().getBackLightBrightness() <= 0) {
        suspendForScreenOff(indev);
        return;
    }

    const lv_indev_state_t state = lv_indev_get_state(indev);
    const uint32_t now_ms = GetHAL().millis();

    if (state == LV_INDEV_STATE_PR) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);

        if (_last_touch_state == LV_INDEV_STATE_REL) {
            _last_release_consumed = false;
            _overlay.hide();
            _logic.press(point.x, point.y, GetHAL().getUserBackLightBrightness(),
                         GetHAL().getSpeakerVolume(), now_ms,
                         _touch_exclusion ? &*_touch_exclusion : nullptr);
        }

        apply(_logic.move(point.x, point.y, now_ms), indev);
    } else if (_last_touch_state == LV_INDEV_STATE_PR) {
        apply(_logic.release(now_ms), indev);
    } else {
        apply(_logic.tick(now_ms), indev);
    }

    _last_touch_state = state;
}

void GlobalEdgeControlService::applyPendingHardwareUpdate()
{
    const EdgeControlHardwareUpdate update = _apply_queue.take();
    if (update.side == EdgeControlSide::Brightness) {
        GetGlobalDisplayProtectionService().applyUserBrightness(
            update.value, update.persist);
        _brightness_revision++;
    } else if (update.side == EdgeControlSide::Volume) {
        GetHAL().setSpeakerVolume(update.value, update.persist);
        _volume_revision++;
        if (update.persist) {
            GetRuntimeSupervisor().requestAssistantVolumeSync(update.value);
        }
    }
}

void GlobalEdgeControlService::shutdown()
{
    _overlay.shutdown();
    _logic.reset();
    _apply_queue.reset();
    _last_touch_state = LV_INDEV_STATE_REL;
    _initialized = false;
    _enabled = false;
    _owns_touch = false;
    _last_release_consumed = false;
    _touch_exclusion.reset();
}

void GlobalEdgeControlService::setEnabled(bool enabled)
{
    if (!_initialized || _enabled == enabled) {
        return;
    }

    _logic.reset();
    _apply_queue.reset();
    _overlay.hide();
    _owns_touch = false;
    _last_release_consumed = false;
    _enabled = enabled;

    lv_indev_t* indev = GetHAL().lvTouchpad;
    auto& protection = GetGlobalDisplayProtectionService();
    _last_touch_state =
        indev == nullptr || protection.touchInputSuppressed()
        ? LV_INDEV_STATE_REL
        : lv_indev_get_state(indev);
}

void GlobalEdgeControlService::setTouchExclusion(
    std::optional<EdgeControlRect> exclusion
)
{
    _touch_exclusion = exclusion;
    if (!_logic.active()) {
        _logic.cancelCandidate();
    }
}

bool GlobalEdgeControlService::ownsTouch() const
{
    return _owns_touch;
}

bool GlobalEdgeControlService::lastReleaseConsumed() const
{
    return _last_release_consumed;
}

uint32_t GlobalEdgeControlService::brightnessRevision() const
{
    return _brightness_revision;
}

uint32_t GlobalEdgeControlService::volumeRevision() const
{
    return _volume_revision;
}

void GlobalEdgeControlService::apply(const EdgeControlUpdate& update, lv_indev_t* indev)
{
    if (update.activated) {
        _owns_touch = true;
        lv_indev_wait_release(indev);
        lv_indev_reset(indev, nullptr);
    }

    if (update.side != EdgeControlSide::None &&
        (update.activated || update.consumed)) {
        _overlay.show(update.side, update.value);
    }

    _apply_queue.push(update);

    if (update.persist && update.consumed) {
        _last_release_consumed = true;
        _owns_touch = false;
    }

    if (update.hide) {
        _overlay.hide();
    }
}

void GlobalEdgeControlService::suspendForScreenOff(lv_indev_t* indev)
{
    const bool was_active = _logic.active();
    if (was_active) {
        apply(_logic.release(GetHAL().millis()), indev);
    }

    _logic.reset();
    _overlay.hide();
    _last_touch_state = LV_INDEV_STATE_REL;
    _owns_touch = false;
}
