#include "global_edge_control_service.h"

#include <hal/hal.h>

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
    return true;
}

void GlobalEdgeControlService::update()
{
    if (!_initialized) {
        return;
    }

    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr) {
        return;
    }

    if (GetHAL().getBackLightBrightness() <= 0) {
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
            _logic.press(point.x, point.y, GetHAL().getBackLightBrightness(),
                         GetHAL().getSpeakerVolume(), now_ms);
        }

        apply(_logic.move(point.x, point.y, now_ms), indev);
    } else if (_last_touch_state == LV_INDEV_STATE_PR) {
        apply(_logic.release(now_ms), indev);
    } else {
        apply(_logic.tick(now_ms), indev);
    }

    _last_touch_state = state;
}

void GlobalEdgeControlService::shutdown()
{
    _overlay.shutdown();
    _logic.reset();
    _last_touch_state = LV_INDEV_STATE_REL;
    _initialized = false;
    _owns_touch = false;
    _last_release_consumed = false;
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

    if (update.apply && update.side == EdgeControlSide::Brightness) {
        GetHAL().setBackLightBrightness(update.value, update.persist);
        _brightness_revision++;
    } else if (update.apply && update.side == EdgeControlSide::Volume) {
        GetHAL().setSpeakerVolume(update.value, update.persist);
        _volume_revision++;
    }

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
        GetHAL().setBackLightBrightness(0, false);
    }

    _logic.reset();
    _overlay.hide();
    _last_touch_state = LV_INDEV_STATE_REL;
    _owns_touch = false;
}
