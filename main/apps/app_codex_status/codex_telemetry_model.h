#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct cJSON;
constexpr uint32_t kCodexUsageTtlSeconds = 120;
constexpr uint32_t kCodexQuotaTtlSeconds = 120;
constexpr uint32_t kCodexHostTtlSeconds = 15;


enum class CodexTelemetryStatus : uint8_t {
    Unknown,
    Ok,
    Stale,
    Error,
    Unavailable,
};

enum class CodexCostStatus : uint8_t {
    Unknown,
    Ok,
    Partial,
    Unavailable,
};

enum class CodexTelemetryProvider : uint8_t {
    Claude,
    OpenCode,
};

struct CodexProviderQuota {
    bool present = false;
    CodexTelemetryProvider provider = CodexTelemetryProvider::Claude;
    CodexTelemetryStatus status = CodexTelemetryStatus::Unknown;
    int five_hour_remaining_pct = -1;
    int week_remaining_pct = -1;
    int64_t five_hour_reset_epoch_seconds = 0;
    int64_t week_reset_epoch_seconds = 0;
    bool has_five_hour_reset = false;
    bool has_week_reset = false;
};

constexpr size_t kCodexUsageModelLimit = 6;
constexpr size_t kCodexUsageModelNameBytes = 48;

enum class CodexUsageDetailSource : uint8_t {
    CcsSession,
    Rollout,
};

struct CodexModelUsage {
    std::array<char, kCodexUsageModelNameBytes + 1> model{};
    uint64_t requests = 0;
    uint64_t total_tokens = 0;
    uint64_t fresh_input_tokens = 0;
    uint64_t output_tokens = 0;
    uint64_t cache_read_tokens = 0;
    uint64_t cache_write_tokens = 0;
    double estimated_api_equivalent_usd = 0.0;
    CodexCostStatus cost_status = CodexCostStatus::Unknown;
    bool has_estimated_cost = false;
    bool tokens_partial = false;
    double estimated_tps = 0.0;
    double estimated_ttft_ms = 0.0;
    bool has_estimated_tps = false;
    bool has_estimated_ttft = false;
    uint64_t tps_samples = 0;
    uint64_t ttft_samples = 0;
};

struct CodexUsageDetails {
    bool present = false;
    CodexUsageDetailSource source = CodexUsageDetailSource::CcsSession;
    bool tokens_partial = false;
    std::array<CodexModelUsage, kCodexUsageModelLimit> models{};
    size_t model_count = 0;
    uint64_t more_models = 0;
};

struct CodexUsageTelemetry {
    bool present = false;
    CodexTelemetryStatus status = CodexTelemetryStatus::Unknown;
    CodexTelemetryStatus quota_status = CodexTelemetryStatus::Unknown;
    CodexCostStatus cost_status = CodexCostStatus::Unknown;
    int64_t at_epoch_seconds = 0;
    int64_t quota_at_epoch_seconds = 0;
    bool has_quota_timestamp = false;
    int five_hour_remaining_pct = -1;
    int week_remaining_pct = -1;
    int64_t five_hour_reset_epoch_seconds = 0;
    int64_t week_reset_epoch_seconds = 0;
    bool has_five_hour_reset = false;
    bool has_week_reset = false;
    uint64_t today_total_tokens = 0;
    bool has_today_total_tokens = false;
    double estimated_api_equivalent_usd = 0.0;
    bool has_estimated_cost = false;
    std::array<char, 11> day{};
    std::array<CodexProviderQuota, 2> providers{};
    size_t provider_count = 0;
    CodexUsageDetails details;
};

struct CodexHostTelemetry {
    bool present = false;
    CodexTelemetryStatus status = CodexTelemetryStatus::Unknown;
    int64_t at_epoch_seconds = 0;
    double cpu_pct = 0.0;
    double memory_pct = 0.0;
    double disk_pct = 0.0;
    double network_up_kib_s = 0.0;
    double network_down_kib_s = 0.0;
    bool has_cpu = false;
    bool has_memory = false;
    bool has_disk = false;
    bool has_network_up = false;
    bool has_network_down = false;
};

struct CodexTelemetrySnapshot {
    bool version_supported = false;
    CodexUsageTelemetry usage;
    CodexHostTelemetry host;
};

enum class CodexTelemetryParseResult : uint8_t {
    NoTelemetry,
    UnsupportedVersion,
    Invalid,
    Parsed,
};


CodexTelemetryParseResult parse_codex_telemetry_message(
    const char* data,
    size_t length,
    CodexTelemetrySnapshot& snapshot);

CodexTelemetryParseResult parse_codex_telemetry_object(
    cJSON* root,
    CodexTelemetrySnapshot& snapshot);

void expire_codex_telemetry(CodexTelemetrySnapshot& snapshot, int64_t now_epoch_seconds);
