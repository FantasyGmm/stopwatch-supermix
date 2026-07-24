#pragma once

#include <array>
#include <cstddef>
#include <cstdint>


enum class TraeTaskState : uint8_t {
    Pending,
    Running,
    Completed,
};


struct TraeTask {
    std::array<char, 33> project{};
    std::array<char, 49> title{};
    std::array<char, 73> detail{};
    std::array<char, 9> time{};
    TraeTaskState state = TraeTaskState::Completed;
};


struct TraeStatusSnapshot {
    bool online = false;
    std::array<char, 65> model{};
    std::array<char, 33> effort{};
    int revision = 0;
    uint32_t received_at_ms = 0;
    std::array<TraeTask, 4> tasks{};
    size_t task_count = 0;
};


bool parse_trae_status_message(
    const char* data,
    size_t length,
    uint32_t received_at_ms,
    TraeStatusSnapshot& snapshot
);
