#pragma once

#include "edge_control_logic.h"
#include "edge_control_overlay.h"

#include <cstdint>
#include <lvgl.h>

class GlobalEdgeControlService {
public:
    bool init();
    void update();
    void applyPendingHardwareUpdate();
    void shutdown();
    void setEnabled(bool enabled);

    bool ownsTouch() const;
    bool lastReleaseConsumed() const;
    uint32_t brightnessRevision() const;
    uint32_t volumeRevision() const;

private:
    EdgeControlLogic _logic;
    EdgeControlApplyQueue _apply_queue;
    EdgeControlOverlay _overlay;
    lv_indev_state_t _last_touch_state = LV_INDEV_STATE_REL;
    bool _initialized = false;
    bool _enabled = false;
    bool _owns_touch = false;
    bool _last_release_consumed = false;
    uint32_t _brightness_revision = 0;
    uint32_t _volume_revision = 0;

    void apply(const EdgeControlUpdate& update, lv_indev_t* indev);
    void suspendForScreenOff(lv_indev_t* indev);
};

GlobalEdgeControlService& GetGlobalEdgeControlService();
