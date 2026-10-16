#include "apps/app_codex_status/codex_telemetry_model.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>


static CodexTelemetryParseResult parse(
    const char* json,
    CodexTelemetrySnapshot& snapshot)
{
    return parse_codex_telemetry_message(json, std::strlen(json), snapshot);
}


static void assert_usage_and_host_fields_are_bounded_and_copied()
{
    const char* json =
        R"({"telemetry_v":1,"usage":{"s":"ok","q":"ok","at":1791950000,"qa":1791949990,"h":72,"w":48,"hr":1791960000,"wr":1792050000,"t":123456,"c":0.42,"cs":"partial","day":"2026-10-14","p":[{"n":"claude","s":"ok","h":80,"w":42,"hr":1791960000,"wr":1792050000},{"n":"opencode","s":"unavailable","h":null,"w":null,"hr":null,"wr":null}]},"host":{"s":"ok","at":1791950000,"cpu":32.5,"mem":66,"disk":51,"up":12.75,"down":99.5}})";
    CodexTelemetrySnapshot snapshot;
    assert(parse(json, snapshot) == CodexTelemetryParseResult::Parsed);
    assert(snapshot.version_supported);
    assert(snapshot.usage.present);
    assert(snapshot.usage.status == CodexTelemetryStatus::Ok);
    assert(snapshot.usage.quota_status == CodexTelemetryStatus::Ok);
    assert(snapshot.usage.cost_status == CodexCostStatus::Partial);
    assert(snapshot.usage.five_hour_remaining_pct == 72);
    assert(snapshot.usage.week_remaining_pct == 48);
    assert(snapshot.usage.today_total_tokens == 123456);
    assert(snapshot.usage.has_estimated_cost);
    assert(snapshot.usage.estimated_api_equivalent_usd == 0.42);
    assert(std::strcmp(snapshot.usage.day.data(), "2026-10-14") == 0);
    assert(snapshot.usage.provider_count == 2);
    assert(snapshot.usage.providers[0].provider == CodexTelemetryProvider::Claude);
    assert(snapshot.usage.providers[1].provider == CodexTelemetryProvider::OpenCode);
    assert(snapshot.usage.providers[1].five_hour_remaining_pct == -1);
    assert(!snapshot.usage.providers[1].has_five_hour_reset);
    assert(snapshot.host.present);
    assert(snapshot.host.has_cpu && snapshot.host.cpu_pct == 32.5);
    assert(snapshot.host.has_network_down && snapshot.host.network_down_kib_s == 99.5);
}


static void assert_absent_and_unknown_versions_do_not_mutate_snapshot()
{
    CodexTelemetrySnapshot snapshot;
    snapshot.version_supported = true;
    snapshot.usage.present = true;
    snapshot.usage.five_hour_remaining_pct = 37;

    assert(parse(R"({"source":"codex"})", snapshot) ==
           CodexTelemetryParseResult::NoTelemetry);
    assert(snapshot.usage.five_hour_remaining_pct == 37);
    assert(parse(R"({"telemetry_v":2,"usage":{}})", snapshot) ==
           CodexTelemetryParseResult::UnsupportedVersion);
    assert(snapshot.usage.five_hour_remaining_pct == 37);
}


static void assert_malformed_or_oversized_telemetry_is_rejected_atomically()
{
    CodexTelemetrySnapshot snapshot;
    snapshot.version_supported = true;
    snapshot.usage.five_hour_remaining_pct = 37;

    assert(parse(R"({"telemetry_v":1,"usage":{"h":101}})", snapshot) ==
           CodexTelemetryParseResult::Invalid);
    assert(snapshot.usage.five_hour_remaining_pct == 37);
    assert(parse(R"({"telemetry_v":1,"usage":{}} trailing)", snapshot) ==
           CodexTelemetryParseResult::Invalid);

    std::vector<char> oversized(2049, ' ');
    oversized.front() = '{';
    oversized.back() = '}';
    assert(parse_codex_telemetry_message(
               oversized.data(), oversized.size(), snapshot) ==
           CodexTelemetryParseResult::Invalid);
    assert(snapshot.usage.five_hour_remaining_pct == 37);
}


static void assert_provider_count_and_cost_values_are_validated()
{
    CodexTelemetrySnapshot snapshot;
    assert(parse(
               R"({"telemetry_v":1,"usage":{"s":"ok","q":"ok","at":1791950000,"qa":1791949990,"h":1,"w":1,"hr":null,"wr":null,"t":0,"c":-0.1,"cs":"ok","day":"2026-10-14"}})",
               snapshot) == CodexTelemetryParseResult::Invalid);
    assert(parse(
               R"({"telemetry_v":1,"usage":{"s":"ok","q":"ok","at":1791950000,"qa":1791949990,"h":1,"w":1,"hr":null,"wr":null,"t":0,"c":0,"cs":"ok","day":"2026-10-14","p":[{"n":"claude","s":"ok","h":1,"w":1,"hr":null,"wr":null},{"n":"opencode","s":"ok","h":1,"w":1,"hr":null,"wr":null},{"n":"claude","s":"ok","h":1,"w":1,"hr":null,"wr":null}]}})",
               snapshot) == CodexTelemetryParseResult::Invalid);
}


static void assert_usage_quota_and_host_expire_independently()
{
    const char* json =
        R"({"telemetry_v":1,"usage":{"s":"ok","q":"ok","at":1791950000,"qa":1791949800,"h":72,"w":48,"hr":1791960000,"wr":1792050000,"t":123456,"c":0.42,"cs":"ok","day":"2026-10-14"},"host":{"s":"ok","at":1791950000,"cpu":32.5,"mem":66,"disk":51,"up":12.75,"down":99.5}})";
    CodexTelemetrySnapshot snapshot;
    assert(parse(json, snapshot) == CodexTelemetryParseResult::Parsed);

    expire_codex_telemetry(snapshot, 1791950010);
    assert(snapshot.usage.status == CodexTelemetryStatus::Ok);
    assert(snapshot.usage.quota_status == CodexTelemetryStatus::Stale);
    assert(snapshot.usage.has_estimated_cost);
    assert(snapshot.usage.has_today_total_tokens);
    assert(snapshot.usage.five_hour_remaining_pct == -1);
    assert(snapshot.host.status == CodexTelemetryStatus::Ok);
    assert(snapshot.host.has_cpu);

    assert(parse(json, snapshot) == CodexTelemetryParseResult::Parsed);
    expire_codex_telemetry(snapshot, 1791950121);
    assert(snapshot.usage.status == CodexTelemetryStatus::Stale);
    assert(!snapshot.usage.has_today_total_tokens);
    assert(snapshot.host.status == CodexTelemetryStatus::Stale);
    assert(!snapshot.host.has_cpu);
}


int main()
{
    assert_usage_and_host_fields_are_bounded_and_copied();
    assert_absent_and_unknown_versions_do_not_mutate_snapshot();
    assert_malformed_or_oversized_telemetry_is_rejected_atomically();
    assert_provider_count_and_cost_values_are_validated();
    assert_usage_quota_and_host_expire_independently();
}
