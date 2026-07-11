#include "codex_status_model.h"

#include <cJSON.h>
#include <cstring>


namespace {

int json_int_or(cJSON* root, const char* key, int fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) ? static_cast<int>(item->valuedouble) : fallback;
}


std::string json_string_or_empty(cJSON* root, const char* key)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
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
    if (type == "chime") {
        const std::string agent = json_string_or_empty(root, "agent");
        cJSON_Delete(root);
        return agent == "codex" ? CodexMessageKind::Chime : CodexMessageKind::Invalid;
    }

    snapshot.model = json_string_or_empty(root, "codex_model");
    snapshot.effort = json_string_or_empty(root, "codex_effort");
    snapshot.five_hour_left_pct = json_int_or(root, "codex_5h_left_pct", -1);
    snapshot.week_left_pct = json_int_or(root, "codex_week_left_pct", -1);
    snapshot.today_minutes = json_int_or(root, "codex_today_min", -1);
    snapshot.week_minutes = json_int_or(root, "codex_week_min", -1);
    cJSON_Delete(root);

    const bool has_status = !snapshot.model.empty() || !snapshot.effort.empty() ||
                            snapshot.five_hour_left_pct >= 0 || snapshot.week_left_pct >= 0 ||
                            snapshot.today_minutes >= 0 || snapshot.week_minutes >= 0;
    return has_status ? CodexMessageKind::Status : CodexMessageKind::Invalid;
}
