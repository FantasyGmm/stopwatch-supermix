#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct cJSON;

enum class TraeTaskState : uint8_t {
    Pending,
    Running,
    Completed,
};


enum class TraeProcessState : uint8_t {
    Unknown,
    Live,
    Off,
};


struct TraeTask {
    std::array<char, 33> id{};
    std::array<char, 33> project{};
    std::array<char, 49> title{};
    std::array<char, 73> detail{};
    std::array<char, 9> time{};
    std::array<char, 65> model{};
    std::array<char, 33> effort{};
    TraeTaskState state = TraeTaskState::Completed;
};


struct TraeStatusSnapshot {
    bool online = false;
    TraeProcessState process_state = TraeProcessState::Unknown;
    std::array<char, 65> model{};
    std::array<char, 33> effort{};
    int revision = 0;
    uint32_t received_at_ms = 0;
    uint16_t pending_count = 0;
    uint16_t running_count = 0;
    bool tasks_truncated = false;
    std::array<TraeTask, 4> tasks{};
    size_t task_count = 0;
};


bool parse_trae_status_message(
    const char* data,
    size_t length,
    uint32_t received_at_ms,
    TraeStatusSnapshot& snapshot
);

bool parse_trae_status_object(
    cJSON* root,
    uint32_t received_at_ms,
    TraeStatusSnapshot& snapshot);
