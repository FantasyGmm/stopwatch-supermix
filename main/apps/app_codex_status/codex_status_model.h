#pragma once

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
};

struct CodexStatusSnapshot {
    std::string model;
    std::string effort;
    int five_hour_left_pct = -1;
    int week_left_pct = -1;
    int pending_count = 0;
    int running_count = 0;
    int completed_count = 0;
    int alert_revision = 0;
    std::vector<CodexTaskAlert> alerts;
    bool alerts_valid = false;
};


enum class CodexMessageKind {
    Invalid,
    Status,
    Chime,
};


CodexMessageKind parse_codex_message(
    const char* data,
    size_t length,
    CodexStatusSnapshot& snapshot
);
