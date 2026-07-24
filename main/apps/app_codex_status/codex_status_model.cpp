#include "codex_status_model.h"

#include <cJSON.h>
#include <cmath>
#include <cstring>


namespace {

constexpr size_t kMaxAlerts = 8;
constexpr double kMaximumExactJsonInteger = 9007199254740991.0;
constexpr int64_t kMinimumUnixMs = 1577836800000LL;
constexpr int64_t kMaximumUnixMs = 4102444800000LL;
constexpr int kMaximumUtcOffsetMinutes = 14 * 60;

bool json_exact_int64(cJSON* root, const char* key, int64_t& value)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        std::abs(item->valuedouble) > kMaximumExactJsonInteger) {
        return false;
    }
    value = static_cast<int64_t>(item->valuedouble);
    return true;
}

int json_int_or(cJSON* root, const char* key, int fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) ? static_cast<int>(item->valuedouble) : fallback;
}


int json_nonnegative_int_or(cJSON* root, const char* key, int fallback)
{
    const int value = json_int_or(root, key, fallback);
    return value >= 0 ? value : fallback;
}


std::string json_string_or_empty(cJSON* root, const char* key)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}


bool parse_alerts(cJSON* root, std::vector<CodexTaskAlert>& alerts)
{
    cJSON* array = cJSON_GetObjectItemCaseSensitive(root, "codex_alerts");
    if (!cJSON_IsArray(array)) {
        return false;
    }

    std::vector<CodexTaskAlert> parsed;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsObject(item)) {
            return false;
        }
        cJSON* project = cJSON_GetObjectItemCaseSensitive(item, "project");
        cJSON* title = cJSON_GetObjectItemCaseSensitive(item, "title");
        cJSON* time = cJSON_GetObjectItemCaseSensitive(item, "time");
        cJSON* state = cJSON_GetObjectItemCaseSensitive(item, "state");
        if (!cJSON_IsString(project) || !cJSON_IsString(title) ||
            !cJSON_IsString(time) || !cJSON_IsString(state) ||
            project->valuestring == nullptr || title->valuestring == nullptr ||
            time->valuestring == nullptr || state->valuestring == nullptr) {
            return false;
        }

        const std::string state_text = state->valuestring;
        CodexTaskState parsed_state = CodexTaskState::Completed;
        if (state_text == "pending") {
            parsed_state = CodexTaskState::Pending;
        } else if (state_text == "running") {
            parsed_state = CodexTaskState::Running;
        } else if (state_text == "completed") {
            parsed_state = CodexTaskState::Completed;
        } else {
            return false;
        }
        if (parsed.size() < kMaxAlerts) {
            parsed.push_back(
                {project->valuestring, title->valuestring, time->valuestring, parsed_state}
            );
        }
    }

    alerts = std::move(parsed);
    return true;
}

}  // namespace


CodexMessageKind parse_codex_message(const char* data, size_t length, CodexStatusSnapshot& snapshot)
{
    if (data == nullptr || length == 0) {
        return CodexMessageKind::Invalid;
    }

    cJSON* root = cJSON_ParseWithLength(data, length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return CodexMessageKind::Invalid;
    }

    const std::string type = json_string_or_empty(root, "type");
    cJSON* source = cJSON_GetObjectItemCaseSensitive(root, "source");
    if (source != nullptr &&
        (!cJSON_IsString(source) || source->valuestring == nullptr ||
         std::strcmp(source->valuestring, "codex") != 0)) {
        cJSON_Delete(root);
        return CodexMessageKind::Invalid;
    }
    if (type == "chime") {
        const std::string agent = json_string_or_empty(root, "agent");
        cJSON_Delete(root);
        return agent == "codex" ? CodexMessageKind::Chime : CodexMessageKind::Invalid;
    }

    if (type == "time_sync") {
        int64_t unix_time_ms = 0;
        int64_t offset_minutes = 0;
        if (!json_exact_int64(root, "unix_ms", unix_time_ms) ||
            !json_exact_int64(root, "utc_offset_minutes", offset_minutes) ||
            unix_time_ms < kMinimumUnixMs || unix_time_ms > kMaximumUnixMs ||
            offset_minutes < -kMaximumUtcOffsetMinutes ||
            offset_minutes > kMaximumUtcOffsetMinutes) {
            cJSON_Delete(root);
            return CodexMessageKind::Invalid;
        }
        snapshot = CodexStatusSnapshot{};
        snapshot.unix_time_ms = unix_time_ms;
        snapshot.utc_offset_minutes = static_cast<int>(offset_minutes);
        cJSON_Delete(root);
        return CodexMessageKind::TimeSync;
    }

    snapshot = CodexStatusSnapshot{};
    snapshot.model = json_string_or_empty(root, "codex_model");
    snapshot.effort = json_string_or_empty(root, "codex_effort");
    snapshot.five_hour_left_pct = json_int_or(root, "codex_5h_left_pct", -1);
    snapshot.week_left_pct = json_int_or(root, "codex_week_left_pct", -1);
    snapshot.pending_count = json_nonnegative_int_or(root, "codex_alert_pending", 0);
    snapshot.running_count = json_nonnegative_int_or(root, "codex_alert_running", 0);
    snapshot.completed_count = json_nonnegative_int_or(root, "codex_alert_completed", 0);
    snapshot.alert_revision = json_nonnegative_int_or(root, "codex_alert_revision", 0);
    snapshot.alerts_valid = parse_alerts(root, snapshot.alerts);
    cJSON_Delete(root);

    const bool has_status = !snapshot.model.empty() || !snapshot.effort.empty() ||
                            snapshot.five_hour_left_pct >= 0 || snapshot.week_left_pct >= 0 ||
                            snapshot.alerts_valid;
    return has_status ? CodexMessageKind::Status : CodexMessageKind::Invalid;
}
