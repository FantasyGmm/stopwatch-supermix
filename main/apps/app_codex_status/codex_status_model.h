#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>


enum class CodexTaskState : uint8_t {
    Pending,
    Running,
    Completed,
};


struct CodexTaskAlert {
    std::string project;
    std::string title;
    std::string time;
    CodexTaskState state = CodexTaskState::Completed;
    uint64_t route = 0;
    std::string model;
    std::string effort;
};

struct CodexTaskDetailEnvelope {
    uint64_t route = 0;
    std::string route_hex;
    int32_t revision = 0;
    uint32_t index = 0;
    uint32_t count = 0;
    uint32_t key_epoch = 0;
    std::array<uint8_t, 8> exchange_nonce = {};
    uint32_t sequence = 0;
    std::vector<uint8_t> ciphertext;
};

struct CodexStatusSnapshot {
    std::string model;
    std::string effort;
    int64_t unix_time_ms = 0;
    int utc_offset_minutes = 0;
    int five_hour_left_pct = -1;
    int week_left_pct = -1;
    int pending_count = 0;
    int running_count = 0;
    int completed_count = 0;
    int alert_revision = 0;
    std::vector<CodexTaskAlert> alerts;
    bool alerts_valid = false;
    CodexTaskDetailEnvelope task_detail;
};


enum class CodexMessageKind {
    Invalid,
    Status,
    Chime,
    TimeSync,
    TaskDetail,
};


CodexMessageKind parse_codex_message(
    const char* data,
    size_t length,
    CodexStatusSnapshot& snapshot
);
