#pragma once

#include <cstddef>
#include <cstdint>

struct cJSON;

enum class StatusMessageSource : uint8_t {
    LegacyCodex,
    Codex,
    Trae,
    AgentInventory,
    Unknown,
};


struct AgentInventorySnapshot {
    bool codex = false;
    bool trae = false;
};


StatusMessageSource route_status_message(const char* data, size_t length);
StatusMessageSource route_status_object(cJSON* root);

bool parse_agent_inventory_message(
    const char* data,
    size_t length,
    AgentInventorySnapshot& snapshot);

bool parse_agent_inventory_object(cJSON* root, AgentInventorySnapshot& snapshot);
