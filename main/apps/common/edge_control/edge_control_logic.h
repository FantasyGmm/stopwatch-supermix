#pragma once

#include <cstdint>

enum class EdgeControlSide : uint8_t {
    None,
    Brightness,
    Volume,
};

struct EdgeControlUpdate {
    EdgeControlSide side = EdgeControlSide::None;
    int value = 0;
    int hardware_value = 0;
    bool activated = false;
    bool apply = false;
    bool persist = false;
    bool consumed = false;
    bool cancelled = false;
    bool hide = false;
};

struct EdgeControlHardwareUpdate {
    EdgeControlSide side = EdgeControlSide::None;
    int value = 0;
    bool persist = false;
};

struct EdgeControlRect {
    int min_x = 0;
    int min_y = 0;
    int max_x = -1;
    int max_y = -1;

    bool contains(int x, int y) const;
};

class EdgeControlApplyQueue {
public:
    void push(const EdgeControlUpdate& update);
    EdgeControlHardwareUpdate take();
    void reset();
    bool pending() const;

private:
    EdgeControlHardwareUpdate _pending;
};

class EdgeControlLogic {
public:
    void press(
        int x,
        int y,
        int brightness,
        int volume,
        uint32_t now_ms,
        const EdgeControlRect* exclusion = nullptr
    );
    EdgeControlUpdate move(int x, int y, uint32_t now_ms);
    EdgeControlUpdate release(uint32_t now_ms);
    EdgeControlUpdate tick(uint32_t now_ms);
    void reset();
    void cancelCandidate();

    bool active() const;
    bool candidate() const;
    int current_value() const;
    EdgeControlSide visible_side() const;

private:
    static int normalize_visual(EdgeControlSide side, int value);
    static int normalize_hardware(EdgeControlSide side, int value);

    EdgeControlSide _candidate = EdgeControlSide::None;
    EdgeControlSide _active = EdgeControlSide::None;
    EdgeControlSide _visible = EdgeControlSide::None;
    int _start_x = 0;
    int _start_y = 0;
    int _start_value = 0;
    int _current_value = 0;
    int _applied_value = -1;
    uint32_t _last_apply_ms = 0;
    uint32_t _hide_at_ms = 0;
};
