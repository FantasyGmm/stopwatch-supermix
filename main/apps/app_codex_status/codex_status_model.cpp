#include "codex_status_model.h"

#include <cJSON.h>
#include <cmath>
#include <cstring>


namespace {

constexpr size_t kMaxAlerts = 8;
constexpr size_t kMaxStatusJsonBytes = 2048;
constexpr uint32_t kMaxDetailChunks = 16;
constexpr size_t kMaxDetailCiphertextBytes = 1300 + 8;
constexpr size_t kMaxProjectCodepoints = 32;
constexpr size_t kMaxTitleCodepoints = 48;
constexpr size_t kMaxModelCodepoints = 64;
constexpr size_t kMaxEffortCodepoints = 32;
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

bool object_has_exact_fields(cJSON* object, const char* const* fields, size_t field_count)
{
    if (!cJSON_IsObject(object) || cJSON_GetArraySize(object) != static_cast<int>(field_count)) {
        return false;
    }
    for (size_t index = 0; index < field_count; ++index) {
        if (cJSON_GetObjectItemCaseSensitive(object, fields[index]) == nullptr) {
            return false;
        }
    }
    return true;
}

bool only_json_whitespace(const char* begin, const char* end)
{
    while (begin < end) {
        if (*begin != ' ' && *begin != '\t' && *begin != '\r' && *begin != '\n') {
            return false;
        }
        ++begin;
    }
    return true;
}

bool parse_hex64(const char* text, uint64_t& value, std::array<uint8_t, 8>* bytes = nullptr)
{
    if (text == nullptr || std::strlen(text) != 16) {
        return false;
    }
    uint64_t parsed = 0;
    std::array<uint8_t, 8> parsed_bytes = {};
    for (size_t index = 0; index < 16; ++index) {
        const char character = text[index];
        uint8_t nibble = 0;
        if (character >= '0' && character <= '9') {
            nibble = static_cast<uint8_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            nibble = static_cast<uint8_t>(character - 'a' + 10);
        } else {
            return false;
        }
        parsed = (parsed << 4U) | nibble;
        parsed_bytes[index / 2] = static_cast<uint8_t>(
            (parsed_bytes[index / 2] << 4U) | nibble);
    }
    value = parsed;
    if (bytes != nullptr) {
        *bytes = parsed_bytes;
    }
    return true;
}

bool valid_utf8_within(const char* text, size_t maximum_codepoints)
{
    if (text == nullptr) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(text);
    const size_t size = std::strlen(text);
    size_t offset = 0;
    size_t codepoints = 0;
    while (offset < size) {
        if (++codepoints > maximum_codepoints) {
            return false;
        }
        const uint8_t first = bytes[offset];
        if (first < 0x80U) {
            ++offset;
            continue;
        }
        size_t continuation = 0;
        uint32_t codepoint = 0;
        if ((first & 0xe0U) == 0xc0U) {
            continuation = 1;
            codepoint = first & 0x1fU;
        } else if ((first & 0xf0U) == 0xe0U) {
            continuation = 2;
            codepoint = first & 0x0fU;
        } else if ((first & 0xf8U) == 0xf0U) {
            continuation = 3;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (offset + continuation >= size) {
            return false;
        }
        for (size_t index = 1; index <= continuation; ++index) {
            const uint8_t next = bytes[offset + index];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((continuation == 1 && codepoint < 0x80U) ||
            (continuation == 2 && codepoint < 0x800U) ||
            (continuation == 3 && codepoint < 0x10000U) ||
            codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
        offset += continuation + 1;
    }
    return true;
}

int base64_value(char character)
{
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

bool decode_base64(const char* text, std::vector<uint8_t>& output)
{
    if (text == nullptr) {
        return false;
    }
    const size_t length = std::strlen(text);
    if (length == 0 || length % 4 != 0 || length > ((kMaxDetailCiphertextBytes + 2) / 3) * 4) {
        return false;
    }
    std::vector<uint8_t> decoded;
    decoded.reserve(length / 4 * 3);
    for (size_t offset = 0; offset < length; offset += 4) {
        const bool last = offset + 4 == length;
        const int a = base64_value(text[offset]);
        const int b = base64_value(text[offset + 1]);
        const int c = text[offset + 2] == '=' ? -2 : base64_value(text[offset + 2]);
        const int d = text[offset + 3] == '=' ? -2 : base64_value(text[offset + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (c == -2 && d != -2) || ((!last) && (c == -2 || d == -2))) {
            return false;
        }
        decoded.push_back(static_cast<uint8_t>((a << 2) | (b >> 4)));
        if (c >= 0) {
            decoded.push_back(static_cast<uint8_t>((b << 4) | (c >> 2)));
            if (d >= 0) {
                decoded.push_back(static_cast<uint8_t>((c << 6) | d));
            } else if ((c & 0x03) != 0) {
                return false;
            }
        } else if ((b & 0x0f) != 0) {
            return false;
        }
    }
    if (decoded.size() < 8 || decoded.size() > kMaxDetailCiphertextBytes) {
        return false;
    }
    output = std::move(decoded);
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
        static constexpr const char* kLegacyFields[] = {
            "route", "project", "title", "state",
        };
        static constexpr const char* kMetadataFields[] = {
            "route", "project", "title", "state", "model", "effort",
        };
        const bool has_metadata =
            object_has_exact_fields(item, kMetadataFields, 6);
        if ((!has_metadata && !object_has_exact_fields(item, kLegacyFields, 4)) ||
            parsed.size() >= kMaxAlerts) {
            return false;
        }
        cJSON* route = cJSON_GetObjectItemCaseSensitive(item, "route");
        cJSON* project = cJSON_GetObjectItemCaseSensitive(item, "project");
        cJSON* title = cJSON_GetObjectItemCaseSensitive(item, "title");
        cJSON* state = cJSON_GetObjectItemCaseSensitive(item, "state");
        cJSON* model = cJSON_GetObjectItemCaseSensitive(item, "model");
        cJSON* effort = cJSON_GetObjectItemCaseSensitive(item, "effort");
        if (!cJSON_IsString(route) || !cJSON_IsString(project) || !cJSON_IsString(title) ||
            !cJSON_IsString(state) || route->valuestring == nullptr ||
            project->valuestring == nullptr || title->valuestring == nullptr ||
            state->valuestring == nullptr ||
            !valid_utf8_within(project->valuestring, kMaxProjectCodepoints) ||
            !valid_utf8_within(title->valuestring, kMaxTitleCodepoints) ||
            (has_metadata &&
             (!cJSON_IsString(model) || model->valuestring == nullptr ||
              !cJSON_IsString(effort) || effort->valuestring == nullptr ||
              !valid_utf8_within(model->valuestring, kMaxModelCodepoints) ||
              !valid_utf8_within(effort->valuestring, kMaxEffortCodepoints)))) {
            return false;
        }

        uint64_t parsed_route = 0;
        if (!parse_hex64(route->valuestring, parsed_route) || parsed_route == 0) {
            return false;
        }

        const std::string state_text = state->valuestring;
        CodexTaskState parsed_state = CodexTaskState::Completed;
        if (state_text == "pending") {
            parsed_state = CodexTaskState::Pending;
        } else if (state_text == "running") {
            parsed_state = CodexTaskState::Running;
        } else {
            return false;
        }
        CodexTaskAlert alert;
        alert.project = project->valuestring;
        alert.title = title->valuestring;
        alert.state = parsed_state;
        alert.route = parsed_route;
        if (has_metadata) {
            alert.model = model->valuestring;
            alert.effort = effort->valuestring;
        }
        parsed.push_back(std::move(alert));
    }

    alerts = std::move(parsed);
    return true;
}

bool parse_task_detail(cJSON* root, CodexTaskDetailEnvelope& output)
{
    static constexpr const char* kFields[] = {
        "type", "route", "revision", "index", "count", "epoch", "nonce", "sequence", "ciphertext",
    };
    if (!object_has_exact_fields(root, kFields, 9)) {
        return false;
    }
    cJSON* route = cJSON_GetObjectItemCaseSensitive(root, "route");
    cJSON* nonce = cJSON_GetObjectItemCaseSensitive(root, "nonce");
    cJSON* ciphertext = cJSON_GetObjectItemCaseSensitive(root, "ciphertext");
    int64_t revision = 0;
    int64_t index = 0;
    int64_t count = 0;
    int64_t epoch = 0;
    int64_t sequence = 0;
    CodexTaskDetailEnvelope parsed;
    uint64_t nonce_value = 0;
    if (!cJSON_IsString(route) || route->valuestring == nullptr ||
        !parse_hex64(route->valuestring, parsed.route) || parsed.route == 0 ||
        !json_exact_int64(root, "revision", revision) || revision < 0 || revision > INT32_MAX ||
        !json_exact_int64(root, "index", index) || index < 0 ||
        !json_exact_int64(root, "count", count) || count < 1 || count > kMaxDetailChunks || index >= count ||
        !json_exact_int64(root, "epoch", epoch) || epoch < 1 || epoch > UINT32_MAX ||
        !cJSON_IsString(nonce) || nonce->valuestring == nullptr ||
        !parse_hex64(nonce->valuestring, nonce_value, &parsed.exchange_nonce) || nonce_value == 0 ||
        !json_exact_int64(root, "sequence", sequence) || sequence < 1 || sequence > UINT32_MAX ||
        !cJSON_IsString(ciphertext) || ciphertext->valuestring == nullptr ||
        !decode_base64(ciphertext->valuestring, parsed.ciphertext)) {
        return false;
    }
    parsed.route_hex = route->valuestring;
    parsed.revision = static_cast<int32_t>(revision);
    parsed.index = static_cast<uint32_t>(index);
    parsed.count = static_cast<uint32_t>(count);
    parsed.key_epoch = static_cast<uint32_t>(epoch);
    parsed.sequence = static_cast<uint32_t>(sequence);
    output = std::move(parsed);
    return true;
}

}  // namespace


CodexMessageKind parse_codex_message(const char* data, size_t length, CodexStatusSnapshot& snapshot)
{
    if (data == nullptr || length == 0 || length > kMaxStatusJsonBytes) {
        return CodexMessageKind::Invalid;
    }

    const char* parse_end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(data, length, &parse_end, 0);
    if (root == nullptr || !cJSON_IsObject(root) || parse_end == nullptr ||
        parse_end < data || parse_end > data + length ||
        !only_json_whitespace(parse_end, data + length)) {
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

    if (type == "task_detail") {
        CodexTaskDetailEnvelope detail;
        if (!parse_task_detail(root, detail)) {
            cJSON_Delete(root);
            return CodexMessageKind::Invalid;
        }
        CodexStatusSnapshot parsed;
        parsed.task_detail = std::move(detail);
        snapshot = std::move(parsed);
        cJSON_Delete(root);
        return CodexMessageKind::TaskDetail;
    }

    CodexStatusSnapshot parsed;
    parsed.model = json_string_or_empty(root, "codex_model");
    parsed.effort = json_string_or_empty(root, "codex_effort");
    parsed.five_hour_left_pct = json_int_or(root, "codex_5h_left_pct", -1);
    parsed.week_left_pct = json_int_or(root, "codex_week_left_pct", -1);
    parsed.pending_count = json_nonnegative_int_or(root, "codex_alert_pending", 0);
    parsed.running_count = json_nonnegative_int_or(root, "codex_alert_running", 0);
    parsed.completed_count = json_nonnegative_int_or(root, "codex_alert_completed", 0);
    parsed.alert_revision = json_nonnegative_int_or(root, "codex_alert_revision", 0);
    cJSON* alerts = cJSON_GetObjectItemCaseSensitive(root, "codex_alerts");
    parsed.alerts_valid = alerts != nullptr && parse_alerts(root, parsed.alerts);
    const bool malformed_alerts = alerts != nullptr && !parsed.alerts_valid;
    const bool has_status = !parsed.model.empty() || !parsed.effort.empty() ||
                            parsed.five_hour_left_pct >= 0 || parsed.week_left_pct >= 0 ||
                            parsed.alerts_valid;
    cJSON_Delete(root);
    if (malformed_alerts || !has_status) {
        return CodexMessageKind::Invalid;
    }
    snapshot = std::move(parsed);
    return CodexMessageKind::Status;
}
