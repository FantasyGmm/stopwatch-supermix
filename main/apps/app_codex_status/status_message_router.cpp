#include "status_message_router.h"

#include <array>
#include <cJSON.h>
#include <cstring>


StatusMessageSource route_status_message(const char* data, size_t length)
{
    if (data == nullptr || length == 0) {
        return StatusMessageSource::Unknown;
    }
    cJSON* root = cJSON_ParseWithLength(data, length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return StatusMessageSource::Unknown;
    }

    const StatusMessageSource result = route_status_object(root);
    cJSON_Delete(root);
    return result;
}


StatusMessageSource route_status_object(cJSON* root)
{
    if (!cJSON_IsObject(root)) {
        return StatusMessageSource::Unknown;
    }

    cJSON* source = cJSON_GetObjectItemCaseSensitive(root, "source");
    StatusMessageSource result = StatusMessageSource::Unknown;
    if (source == nullptr) {
        result = StatusMessageSource::LegacyCodex;
    } else if (cJSON_IsString(source) && source->valuestring != nullptr) {
        if (std::strcmp(source->valuestring, "codex") == 0) {
            result = StatusMessageSource::Codex;
        } else if (std::strcmp(source->valuestring, "trae") == 0) {
            result = StatusMessageSource::Trae;
        } else if (std::strcmp(source->valuestring, "agent_inventory") == 0) {
            result = StatusMessageSource::AgentInventory;
        }
    }
    return result;
}


bool parse_agent_inventory_message(
    const char* data,
    size_t length,
    AgentInventorySnapshot& snapshot)
{
    if (data == nullptr || length == 0) {
        return false;
    }

    cJSON* root = cJSON_ParseWithLength(data, length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const bool result = parse_agent_inventory_object(root, snapshot);
    cJSON_Delete(root);
    return result;
}


bool parse_agent_inventory_object(cJSON* root, AgentInventorySnapshot& snapshot)
{
    if (!cJSON_IsObject(root)) {
        return false;
    }

    const cJSON* source = cJSON_GetObjectItemCaseSensitive(root, "source");
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON* agents = cJSON_GetObjectItemCaseSensitive(root, "agents");
    if (!cJSON_IsString(source) || source->valuestring == nullptr ||
        std::strcmp(source->valuestring, "agent_inventory") != 0 ||
        !cJSON_IsNumber(version) || version->valuedouble != 1.0 ||
        !cJSON_IsArray(agents) || cJSON_GetArraySize(agents) > 8) {
        return false;
    }

    AgentInventorySnapshot parsed{};
    std::array<const char*, 8> seen_ids{};
    size_t seen_count = 0;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, agents) {
        if (!cJSON_IsString(item) || item->valuestring == nullptr) {
            return false;
        }
        const size_t id_length = std::strlen(item->valuestring);
        if (id_length == 0 || id_length > 16) {
            return false;
        }
        for (size_t index = 0; index < id_length; ++index) {
            const unsigned char byte = static_cast<unsigned char>(
                item->valuestring[index]);
            if (byte < 0x21U || byte > 0x7eU) {
                return false;
            }
        }
        for (size_t index = 0; index < seen_count; ++index) {
            if (std::strcmp(seen_ids[index], item->valuestring) == 0) {
                return false;
            }
        }
        seen_ids[seen_count++] = item->valuestring;
        if (std::strcmp(item->valuestring, "codex") == 0) {
            parsed.codex = true;
        } else if (std::strcmp(item->valuestring, "trae") == 0) {
            parsed.trae = true;
        }
    }

    snapshot = parsed;
    return true;
}
