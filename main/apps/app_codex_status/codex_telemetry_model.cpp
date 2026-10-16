#include "codex_telemetry_model.h"

#include <cJSON.h>
#include <cmath>
#include <cstring>
#include <ctime>


namespace {

constexpr size_t kMaxTelemetryJsonBytes = 2048;
constexpr double kMaximumExactJsonInteger = 9007199254740991.0;
constexpr int64_t kMinimumEpochSeconds = 946684800LL;
constexpr int64_t kMaximumEpochSeconds = 4102444800LL;
constexpr double kMaximumCostUsd = 1000000000.0;
constexpr double kMaximumRateKiBPerSecond = 1000000000.0;
constexpr double kMaximumPerformanceEstimate = 1000000000.0;

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

bool exact_integer(cJSON* item, int64_t minimum, int64_t maximum, int64_t& value)
{
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        std::abs(item->valuedouble) > kMaximumExactJsonInteger ||
        item->valuedouble < static_cast<double>(minimum) ||
        item->valuedouble > static_cast<double>(maximum)) {
        return false;
    }
    value = static_cast<int64_t>(item->valuedouble);
    return true;
}

bool required_epoch(cJSON* object, const char* key, int64_t& epoch_seconds)
{
    return exact_integer(
        cJSON_GetObjectItemCaseSensitive(object, key),
        kMinimumEpochSeconds,
        kMaximumEpochSeconds,
        epoch_seconds);
}

bool nullable_epoch(
    cJSON* object,
    const char* key,
    int64_t& epoch_seconds,
    bool& present)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == nullptr) {
        return false;
    }
    if (cJSON_IsNull(item)) {
        present = false;
        epoch_seconds = 0;
        return true;
    }
    present = exact_integer(
        item,
        kMinimumEpochSeconds,
        kMaximumEpochSeconds,
        epoch_seconds);
    return present;
}

bool nullable_percentage(cJSON* object, const char* key, int& percentage)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == nullptr) {
        return false;
    }
    if (cJSON_IsNull(item)) {
        percentage = -1;
        return true;
    }
    int64_t parsed = 0;
    if (!exact_integer(item, 0, 100, parsed)) {
        return false;
    }
    percentage = static_cast<int>(parsed);
    return true;
}

bool nullable_unsigned(cJSON* object, const char* key, uint64_t& value, bool& present)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == nullptr) {
        return false;
    }
    if (cJSON_IsNull(item)) {
        value = 0;
        present = false;
        return true;
    }
    int64_t parsed = 0;
    if (!exact_integer(item, 0, static_cast<int64_t>(kMaximumExactJsonInteger), parsed)) {
        return false;
    }
    value = static_cast<uint64_t>(parsed);
    present = true;
    return true;
}

bool required_unsigned(cJSON* object, const char* key, uint64_t& value)
{
    int64_t parsed = 0;
    if (!exact_integer(cJSON_GetObjectItemCaseSensitive(object, key), 0,
                       static_cast<int64_t>(kMaximumExactJsonInteger), parsed)) {
        return false;
    }
    value = static_cast<uint64_t>(parsed);
    return true;
}

bool required_boolean(cJSON* object, const char* key, bool& value)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    value = cJSON_IsTrue(item);
    return true;
}

bool nullable_nonnegative_number(
    cJSON* object,
    const char* key,
    double maximum,
    double& value,
    bool& present)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == nullptr) {
        return false;
    }
    if (cJSON_IsNull(item)) {
        value = 0.0;
        present = false;
        return true;
    }
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble < 0.0 || item->valuedouble > maximum) {
        return false;
    }
    value = item->valuedouble;
    present = true;
    return true;
}

bool parse_telemetry_status(cJSON* object, const char* key, CodexTelemetryStatus& status)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    if (std::strcmp(item->valuestring, "ok") == 0) {
        status = CodexTelemetryStatus::Ok;
    } else if (std::strcmp(item->valuestring, "stale") == 0) {
        status = CodexTelemetryStatus::Stale;
    } else if (std::strcmp(item->valuestring, "error") == 0) {
        status = CodexTelemetryStatus::Error;
    } else if (std::strcmp(item->valuestring, "unavailable") == 0) {
        status = CodexTelemetryStatus::Unavailable;
    } else {
        return false;
    }
    return true;
}

bool parse_cost_status(cJSON* object, CodexCostStatus& status)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, "cs");
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    if (std::strcmp(item->valuestring, "ok") == 0) {
        status = CodexCostStatus::Ok;
    } else if (std::strcmp(item->valuestring, "partial") == 0) {
        status = CodexCostStatus::Partial;
    } else if (std::strcmp(item->valuestring, "unavailable") == 0) {
        status = CodexCostStatus::Unavailable;
    } else {
        return false;
    }
    return true;
}

bool valid_day(const char* day)
{
    if (day == nullptr || std::strlen(day) != 10 || day[4] != '-' || day[7] != '-') {
        return false;
    }
    for (size_t index = 0; index < 10; ++index) {
        if (index == 4 || index == 7) {
            continue;
        }
        if (day[index] < '0' || day[index] > '9') {
            return false;
        }
    }
    const int year = (day[0] - '0') * 1000 + (day[1] - '0') * 100 +
                     (day[2] - '0') * 10 + (day[3] - '0');
    const int month = (day[5] - '0') * 10 + (day[6] - '0');
    const int date = (day[8] - '0') * 10 + (day[9] - '0');
    if (year < 2000 || month < 1 || month > 12 || date < 1) {
        return false;
    }
    constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maximum_date = kDaysPerMonth[month - 1];
    const bool leap_year = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (month == 2 && leap_year) {
        ++maximum_date;
    }
    return date <= maximum_date;
}

bool recent_epoch(int64_t timestamp, int64_t now, uint32_t ttl_seconds)
{
    return timestamp > 0 && now >= timestamp &&
           static_cast<uint64_t>(now - timestamp) <= ttl_seconds;
}

void clear_usage_cost(CodexUsageTelemetry& usage)
{
    usage.today_total_tokens = 0;
    usage.has_today_total_tokens = false;
    usage.estimated_api_equivalent_usd = 0.0;
    usage.has_estimated_cost = false;
    usage.day.fill('\0');
    usage.details = {};
}

bool same_local_day(const CodexUsageTelemetry& usage, int64_t now)
{
    const time_t seconds = static_cast<time_t>(now);
    std::tm local = {};
    char day[11] = {};
    return now > 0 && localtime_r(&seconds, &local) != nullptr &&
           std::strftime(day, sizeof(day), "%Y-%m-%d", &local) != 0 &&
           std::strcmp(day, usage.day.data()) == 0;
}

void clear_usage_quota(CodexUsageTelemetry& usage)
{
    usage.five_hour_remaining_pct = -1;
    usage.week_remaining_pct = -1;
    usage.five_hour_reset_epoch_seconds = 0;
    usage.week_reset_epoch_seconds = 0;
    usage.has_five_hour_reset = false;
    usage.has_week_reset = false;
    usage.providers = {};
    usage.provider_count = 0;
}

void clear_host_values(CodexHostTelemetry& host)
{
    host.cpu_pct = 0.0;
    host.memory_pct = 0.0;
    host.disk_pct = 0.0;
    host.network_up_kib_s = 0.0;
    host.network_down_kib_s = 0.0;
    host.has_cpu = false;
    host.has_memory = false;
    host.has_disk = false;
    host.has_network_up = false;
    host.has_network_down = false;
}

bool parse_provider(cJSON* object, CodexProviderQuota& provider)
{
    if (!cJSON_IsObject(object)) {
        return false;
    }
    cJSON* name = cJSON_GetObjectItemCaseSensitive(object, "n");
    if (!cJSON_IsString(name) || name->valuestring == nullptr) {
        return false;
    }
    if (std::strcmp(name->valuestring, "claude") == 0) {
        provider.provider = CodexTelemetryProvider::Claude;
    } else if (std::strcmp(name->valuestring, "opencode") == 0) {
        provider.provider = CodexTelemetryProvider::OpenCode;
    } else {
        return false;
    }
    if (!parse_telemetry_status(object, "s", provider.status) ||
        !nullable_percentage(object, "h", provider.five_hour_remaining_pct) ||
        !nullable_percentage(object, "w", provider.week_remaining_pct) ||
        !nullable_epoch(
            object,
            "hr",
            provider.five_hour_reset_epoch_seconds,
            provider.has_five_hour_reset) ||
        !nullable_epoch(
            object,
            "wr",
            provider.week_reset_epoch_seconds,
            provider.has_week_reset)) {
        return false;
    }
    provider.present = true;
    return true;
}

bool parse_model_usage(cJSON* object, CodexModelUsage& model)
{
    if (!cJSON_IsObject(object)) {
        return false;
    }
    cJSON* name = cJSON_GetObjectItemCaseSensitive(object, "m");
    if (!cJSON_IsString(name) || name->valuestring == nullptr) {
        return false;
    }
    const size_t length = std::strlen(name->valuestring);
    if (length == 0 || length > kCodexUsageModelNameBytes) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char character = name->valuestring[index];
        if (character < 0x20 || character > 0x7e) {
            return false;
        }
    }
    std::memcpy(model.model.data(), name->valuestring, length + 1);
    if (!required_unsigned(object, "n", model.requests) ||
        !required_unsigned(object, "t", model.total_tokens) ||
        !required_unsigned(object, "i", model.fresh_input_tokens) ||
        !required_unsigned(object, "o", model.output_tokens) ||
        !required_unsigned(object, "r", model.cache_read_tokens) ||
        !required_unsigned(object, "w", model.cache_write_tokens) ||
        !nullable_nonnegative_number(object, "c", kMaximumCostUsd,
                                    model.estimated_api_equivalent_usd,
                                    model.has_estimated_cost) ||
        !parse_cost_status(object, model.cost_status) ||
        !required_boolean(object, "p", model.tokens_partial) ||
        !nullable_nonnegative_number(object, "ts", kMaximumPerformanceEstimate,
                                    model.estimated_tps, model.has_estimated_tps) ||
        !nullable_nonnegative_number(object, "ft", kMaximumPerformanceEstimate,
                                    model.estimated_ttft_ms,
                                    model.has_estimated_ttft) ||
        !required_unsigned(object, "tn", model.tps_samples) ||
        !required_unsigned(object, "fn", model.ttft_samples)) {
        return false;
    }
    return model.total_tokens == model.fresh_input_tokens + model.output_tokens +
               model.cache_read_tokens + model.cache_write_tokens &&
           model.tps_samples <= model.requests &&
           model.ttft_samples <= model.requests &&
           model.has_estimated_tps == (model.tps_samples != 0) &&
           model.has_estimated_ttft == (model.ttft_samples != 0) &&
           (!model.has_estimated_tps || model.estimated_tps > 0.0) &&
           (!model.has_estimated_ttft || model.estimated_ttft_ms > 0.0);
}

bool parse_usage_details(cJSON* object, CodexUsageDetails& details)
{
    int64_t version = 0;
    if (!cJSON_IsObject(object) ||
        !exact_integer(cJSON_GetObjectItemCaseSensitive(object, "v"), 1, 1, version) ||
        !required_boolean(object, "p", details.tokens_partial) ||
        !required_unsigned(object, "more", details.more_models)) {
        return false;
    }
    cJSON* source = cJSON_GetObjectItemCaseSensitive(object, "src");
    if (!cJSON_IsString(source) || source->valuestring == nullptr) {
        return false;
    }
    if (std::strcmp(source->valuestring, "ccs_session") == 0) {
        details.source = CodexUsageDetailSource::CcsSession;
    } else if (std::strcmp(source->valuestring, "rollout") == 0) {
        details.source = CodexUsageDetailSource::Rollout;
    } else {
        return false;
    }
    cJSON* models = cJSON_GetObjectItemCaseSensitive(object, "m");
    if (!cJSON_IsArray(models) ||
        cJSON_GetArraySize(models) > static_cast<int>(kCodexUsageModelLimit)) {
        return false;
    }
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, models) {
        CodexModelUsage& model = details.models[details.model_count];
        if (!parse_model_usage(item, model) ||
            (details.source == CodexUsageDetailSource::Rollout &&
             (model.has_estimated_tps || model.has_estimated_ttft))) {
            return false;
        }
        for (size_t index = 0; index < details.model_count; ++index) {
            if (std::strcmp(details.models[index].model.data(), model.model.data()) == 0) {
                return false;
            }
        }
        if (details.model_count > 0 &&
            details.models[details.model_count - 1].total_tokens < model.total_tokens) {
            return false;
        }
        ++details.model_count;
    }
    details.present = true;
    return true;
}

bool parse_usage(cJSON* object, CodexUsageTelemetry& usage)
{
    if (!cJSON_IsObject(object) ||
        !parse_telemetry_status(object, "s", usage.status) ||
        !parse_telemetry_status(object, "q", usage.quota_status) ||
        !required_epoch(object, "at", usage.at_epoch_seconds) ||
        !nullable_epoch(
            object,
            "qa",
            usage.quota_at_epoch_seconds,
            usage.has_quota_timestamp) ||
        !nullable_percentage(object, "h", usage.five_hour_remaining_pct) ||
        !nullable_percentage(object, "w", usage.week_remaining_pct) ||
        !nullable_epoch(
            object,
            "hr",
            usage.five_hour_reset_epoch_seconds,
            usage.has_five_hour_reset) ||
        !nullable_epoch(
            object,
            "wr",
            usage.week_reset_epoch_seconds,
            usage.has_week_reset) ||
        !nullable_unsigned(
            object,
            "t",
            usage.today_total_tokens,
            usage.has_today_total_tokens) ||
        !nullable_nonnegative_number(
            object,
            "c",
            kMaximumCostUsd,
            usage.estimated_api_equivalent_usd,
            usage.has_estimated_cost) ||
        !parse_cost_status(object, usage.cost_status)) {
        return false;
    }

    cJSON* day = cJSON_GetObjectItemCaseSensitive(object, "day");
    if (!cJSON_IsString(day) || !valid_day(day->valuestring)) {
        return false;
    }
    std::memcpy(usage.day.data(), day->valuestring, usage.day.size());

    cJSON* providers = cJSON_GetObjectItemCaseSensitive(object, "p");
    if (providers != nullptr) {
        if (!cJSON_IsArray(providers) || cJSON_GetArraySize(providers) > 2) {
            return false;
        }
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, providers) {
            CodexProviderQuota parsed;
            if (!parse_provider(item, parsed)) {
                return false;
            }
            for (size_t index = 0; index < usage.provider_count; ++index) {
                if (usage.providers[index].provider == parsed.provider) {
                    return false;
                }
            }
            usage.providers[usage.provider_count++] = parsed;
        }
    }
    // Optional extensions are isolated: malformed/unknown details must not hide
    // a valid quota, daily total, or Host snapshot.
    CodexUsageDetails details;
    if (parse_usage_details(cJSON_GetObjectItemCaseSensitive(object, "d"), details)) {
        usage.details = details;
    }
    usage.present = true;
    return true;
}

bool parse_host(cJSON* object, CodexHostTelemetry& host)
{
    if (!cJSON_IsObject(object) ||
        !parse_telemetry_status(object, "s", host.status) ||
        !required_epoch(object, "at", host.at_epoch_seconds) ||
        !nullable_nonnegative_number(
            object, "cpu", 100.0, host.cpu_pct, host.has_cpu) ||
        !nullable_nonnegative_number(
            object, "mem", 100.0, host.memory_pct, host.has_memory) ||
        !nullable_nonnegative_number(
            object, "disk", 100.0, host.disk_pct, host.has_disk) ||
        !nullable_nonnegative_number(
            object,
            "up",
            kMaximumRateKiBPerSecond,
            host.network_up_kib_s,
            host.has_network_up) ||
        !nullable_nonnegative_number(
            object,
            "down",
            kMaximumRateKiBPerSecond,
            host.network_down_kib_s,
            host.has_network_down)) {
        return false;
    }
    host.present = true;
    return true;
}

}  // namespace


CodexTelemetryParseResult parse_codex_telemetry_message(
    const char* data,
    size_t length,
    CodexTelemetrySnapshot& snapshot)
{
    if (data == nullptr || length == 0 || length > kMaxTelemetryJsonBytes) {
        return CodexTelemetryParseResult::Invalid;
    }

    const char* parse_end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(data, length, &parse_end, 0);
    if (root == nullptr || !cJSON_IsObject(root) || parse_end == nullptr ||
        parse_end < data || parse_end > data + length ||
        !only_json_whitespace(parse_end, data + length)) {
        cJSON_Delete(root);
        return CodexTelemetryParseResult::Invalid;
    }

    const CodexTelemetryParseResult result = parse_codex_telemetry_object(root, snapshot);
    cJSON_Delete(root);
    return result;
}


CodexTelemetryParseResult parse_codex_telemetry_object(
    cJSON* root,
    CodexTelemetrySnapshot& snapshot)
{
    if (!cJSON_IsObject(root)) {
        return CodexTelemetryParseResult::Invalid;
    }

    cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "telemetry_v");
    if (version == nullptr) {
        return CodexTelemetryParseResult::NoTelemetry;
    }
    int64_t parsed_version = 0;
    if (!exact_integer(version, 1, 1, parsed_version)) {
        return CodexTelemetryParseResult::UnsupportedVersion;
    }

    CodexTelemetrySnapshot parsed;
    parsed.version_supported = true;
    bool valid = true;
    cJSON* usage = cJSON_GetObjectItemCaseSensitive(root, "usage");
    cJSON* host = cJSON_GetObjectItemCaseSensitive(root, "host");
    if (usage != nullptr) {
        valid = parse_usage(usage, parsed.usage);
    }
    if (valid && host != nullptr) {
        valid = parse_host(host, parsed.host);
    }
    if (!valid) {
        return CodexTelemetryParseResult::Invalid;
    }
    snapshot = parsed;
    return CodexTelemetryParseResult::Parsed;
}


void expire_codex_telemetry(CodexTelemetrySnapshot& snapshot, int64_t now_epoch_seconds)
{
    CodexUsageTelemetry& usage = snapshot.usage;
    if (usage.present) {
        if (usage.status == CodexTelemetryStatus::Ok &&
            (!same_local_day(usage, now_epoch_seconds) || !recent_epoch(
                usage.at_epoch_seconds,
                now_epoch_seconds,
                kCodexUsageTtlSeconds))) {
            usage.status = CodexTelemetryStatus::Stale;
        }
        if (usage.status != CodexTelemetryStatus::Ok) {
            clear_usage_cost(usage);
        }

        if (usage.quota_status == CodexTelemetryStatus::Ok &&
            (!usage.has_quota_timestamp ||
             !recent_epoch(
                 usage.quota_at_epoch_seconds,
                 now_epoch_seconds,
                 kCodexQuotaTtlSeconds))) {
            usage.quota_status = CodexTelemetryStatus::Stale;
        }
        if (usage.quota_status != CodexTelemetryStatus::Ok) {
            clear_usage_quota(usage);
        }
    }

    CodexHostTelemetry& host = snapshot.host;
    if (host.present) {
        if (host.status == CodexTelemetryStatus::Ok &&
            !recent_epoch(
                host.at_epoch_seconds,
                now_epoch_seconds,
                kCodexHostTtlSeconds)) {
            host.status = CodexTelemetryStatus::Stale;
        }
        if (host.status != CodexTelemetryStatus::Ok) {
            clear_host_values(host);
        }
    }
}
