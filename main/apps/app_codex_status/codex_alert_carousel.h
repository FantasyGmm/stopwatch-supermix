#pragma once

#include "codex_status_model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>


class CodexAlertCarousel {
public:
    bool apply(const CodexStatusSnapshot& snapshot, uint32_t now_ms);
    bool update(uint32_t now_ms);
    void reset();

    const CodexTaskAlert* current() const;
    std::string heading() const;
    CodexTaskState display_state() const;
    bool completed() const;

private:
    int _revision = -1;
    int _pending_count = 0;
    int _running_count = 0;
    int _completed_count = 0;
    std::vector<CodexTaskAlert> _alerts;
    size_t _index = 0;
    uint32_t _last_rotation_ms = 0;
};
