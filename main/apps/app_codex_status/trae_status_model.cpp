#include "trae_status_model.h"

#include <cJSON.h>
#include <algorithm>
#include <cmath>
#include <cstring>


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


bool parse_task(cJSON* item, TraeTask& task)
{
    if (!cJSON_IsObject(item) ||
        !copy_json_string(item, "project", task.project) ||
        !copy_json_string(item, "title", task.title) ||
        !copy_json_string(item, "detail", task.detail) ||
        !copy_json_string(item, "time", task.time)) {
        return false;
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
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, tasks) {
            TraeTask task;
            if (!parse_task(item, task)) {
                valid = false;
                break;
            }
            if (parsed.task_count < parsed.tasks.size()) {
                parsed.tasks[parsed.task_count++] = task;
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
