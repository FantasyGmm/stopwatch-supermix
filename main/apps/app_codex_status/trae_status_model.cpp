#include "trae_status_model.h"

#include <cJSON.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>


namespace {

template <size_t Size>
bool copy_json_string(cJSON* root, const char* key, std::array<char, Size>& output)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    const size_t length = std::min(std::strlen(item->valuestring), Size - 1);
    std::memcpy(output.data(), item->valuestring, length);
    output[length] = '\0';
    return true;
}


template <size_t Size>
bool copy_optional_json_string(
    cJSON* root,
    const char* key,
    std::array<char, Size>& output
)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (item == nullptr) {
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    const size_t length = std::min(std::strlen(item->valuestring), Size - 1);
    std::memcpy(output.data(), item->valuestring, length);
    output[length] = '\0';
    return true;
}


bool parse_revision(cJSON* root, int& revision)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "trae_revision");
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < 0 || item->valuedouble > 2147483647.0) {
        return false;
    }
    revision = static_cast<int>(item->valuedouble);
    return true;
}


bool parse_optional_count(cJSON* root, const char* key, uint16_t& count, bool& present)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    present = item != nullptr;
    if (!present) {
        return true;
    }
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < 0 ||
        item->valuedouble > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    count = static_cast<uint16_t>(item->valuedouble);
    return true;
}


bool parse_process_state(cJSON* root, bool online, TraeProcessState& process_state)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "trae_process_state");
    if (item == nullptr) {
        process_state = online ? TraeProcessState::Live : TraeProcessState::Off;
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    if (std::strcmp(item->valuestring, "live") == 0) {
        process_state = TraeProcessState::Live;
    } else if (std::strcmp(item->valuestring, "off") == 0) {
        process_state = TraeProcessState::Off;
    } else if (std::strcmp(item->valuestring, "unknown") == 0) {
        process_state = TraeProcessState::Unknown;
    } else {
        return false;
    }
    return true;
}


bool parse_optional_truncation(cJSON* root, bool& tasks_truncated)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "trae_tasks_truncated");
    if (item == nullptr) {
        return true;
    }
    if (!cJSON_IsBool(item)) {
        return false;
    }
    tasks_truncated = cJSON_IsTrue(item);
    return true;
}


bool parse_task(cJSON* item, size_t index, TraeTask& task)
{
    if (!cJSON_IsObject(item)) {
        return false;
    }
    cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (!copy_json_string(item, "project", task.project) ||
        !copy_json_string(item, "title", task.title) ||
        !copy_json_string(item, "detail", task.detail) ||
        !copy_json_string(item, "time", task.time) ||
        !copy_optional_json_string(item, "id", task.id) ||
        !copy_optional_json_string(item, "model", task.model) ||
        !copy_optional_json_string(item, "effort", task.effort)) {
        return false;
    }
    if (id == nullptr) {
        std::snprintf(task.id.data(), task.id.size(), "legacy-%zu", index);
    }
    cJSON* state = cJSON_GetObjectItemCaseSensitive(item, "state");
    if (!cJSON_IsString(state) || state->valuestring == nullptr) {
        return false;
    }
    if (std::strcmp(state->valuestring, "pending") == 0) {
        task.state = TraeTaskState::Pending;
    } else if (std::strcmp(state->valuestring, "running") == 0) {
        task.state = TraeTaskState::Running;
    } else if (std::strcmp(state->valuestring, "completed") == 0) {
        task.state = TraeTaskState::Completed;
    } else {
        return false;
    }
    return true;
}

}  // namespace


bool parse_trae_status_message(
    const char* data,
    size_t length,
    uint32_t received_at_ms,
    TraeStatusSnapshot& snapshot
)
{
    if (data == nullptr || length == 0) {
        return false;
    }
    cJSON* root = cJSON_ParseWithLength(data, length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    TraeStatusSnapshot parsed;
    cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON* source = cJSON_GetObjectItemCaseSensitive(root, "source");
    cJSON* online = cJSON_GetObjectItemCaseSensitive(root, "trae_online");
    cJSON* tasks = cJSON_GetObjectItemCaseSensitive(root, "trae_tasks");
    bool valid =
        cJSON_IsString(type) && type->valuestring != nullptr &&
        std::strcmp(type->valuestring, "status") == 0 &&
        cJSON_IsString(source) && source->valuestring != nullptr &&
        std::strcmp(source->valuestring, "trae") == 0 &&
        cJSON_IsBool(online) &&
        copy_json_string(root, "trae_model", parsed.model) &&
        copy_json_string(root, "trae_effort", parsed.effort) &&
        parse_revision(root, parsed.revision) &&
        cJSON_IsArray(tasks);

    if (valid) {
        parsed.online = cJSON_IsTrue(online);
        parsed.received_at_ms = received_at_ms;
        bool pending_count_present = false;
        bool running_count_present = false;
        valid =
            parse_process_state(root, parsed.online, parsed.process_state) &&
            parse_optional_count(
                root,
                "trae_pending_count",
                parsed.pending_count,
                pending_count_present
            ) &&
            parse_optional_count(
                root,
                "trae_running_count",
                parsed.running_count,
                running_count_present
            ) &&
            parse_optional_truncation(root, parsed.tasks_truncated);

        uint16_t derived_pending_count = 0;
        uint16_t derived_running_count = 0;
        cJSON* item = nullptr;
        size_t index = 0;
        cJSON_ArrayForEach(item, tasks) {
            TraeTask task;
            if (!parse_task(item, index, task)) {
                valid = false;
                break;
            }
            if (task.state == TraeTaskState::Pending &&
                derived_pending_count < std::numeric_limits<uint16_t>::max()) {
                ++derived_pending_count;
            } else if (task.state == TraeTaskState::Running &&
                       derived_running_count < std::numeric_limits<uint16_t>::max()) {
                ++derived_running_count;
            }
            if (parsed.task_count < parsed.tasks.size()) {
                parsed.tasks[parsed.task_count++] = task;
            }
            ++index;
        }
        if (valid) {
            if (!pending_count_present) {
                parsed.pending_count = derived_pending_count;
            }
            if (!running_count_present) {
                parsed.running_count = derived_running_count;
            }
        }
    }

    cJSON_Delete(root);
    if (!valid) {
        return false;
    }
    snapshot = parsed;
    return true;
}
