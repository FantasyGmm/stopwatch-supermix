#pragma once

#include "agent_panel_model.h"

#include <cstddef>
#include <cstdint>

namespace agent_panel_presentation {

constexpr int64_t kMinimumUnixMilliseconds = 1577836800000LL;

void format_last_connected(
    AgentPanelRowState state,
    int64_t last_connected_at,
    int64_t now,
    char* output,
    size_t output_size);

}  // namespace agent_panel_presentation
