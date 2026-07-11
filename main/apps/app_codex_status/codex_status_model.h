#pragma once

#include <cstddef>
#include <string>


struct CodexStatusSnapshot {
    std::string model;
    std::string effort;
    int five_hour_left_pct = -1;
    int week_left_pct = -1;
    int today_minutes = -1;
    int week_minutes = -1;
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
