#pragma once

#include "codex_status_model.h"

#include <cstddef>
#include <cstdint>
#include <vector>


enum class CodexTaskFocusEvent {
    None,
    FocusChanged,
    StateBoundaryCrossed,
    SelectionChanged,
};


class CodexTaskFocusDeck {
public:
    static constexpr size_t kMaxTasks = 8;

    bool apply(const CodexStatusSnapshot& snapshot);
    CodexTaskFocusEvent browse(int direction);
    CodexTaskFocusEvent select_focused();
    void reset();

    const std::vector<CodexTaskAlert>& tasks() const;
    size_t count() const;
    size_t focused_index() const;
    uint64_t selected_route() const;
    const CodexTaskAlert* focused() const;
    const CodexTaskAlert* selected() const;

private:
    std::vector<CodexTaskAlert> _tasks;
    size_t _focused_index = 0;
    uint64_t _selected_route = 0;
};
