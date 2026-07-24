#include "codex_status_model.h"

#include <cassert>
#include <cstring>

static CodexMessageKind parse(const char* json, CodexStatusSnapshot& snapshot)
{
    return parse_codex_message(json, std::strlen(json), snapshot);
}

int main()
{
    CodexStatusSnapshot snapshot;
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::TimeSync);
    assert(snapshot.unix_time_ms == 1783872000123LL);
    assert(snapshot.utc_offset_minutes == 480);

    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123.5,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1577836799999,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1577836800000,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::TimeSync);
    assert(parse(
        R"({"type":"time_sync","unix_ms":4102444800000,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::TimeSync);
    assert(parse(
        R"({"type":"time_sync","unix_ms":4102444800001,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":841})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":-841})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":840})",
        snapshot
    ) == CodexMessageKind::TimeSync);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":-840})",
        snapshot
    ) == CodexMessageKind::TimeSync);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":480.5})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1e999,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":1e999})",
        snapshot
    ) == CodexMessageKind::Invalid);

    assert(parse(R"({"codex_model":"gpt-5"})", snapshot) == CodexMessageKind::Status);
    assert(snapshot.model == "gpt-5");
    assert(parse(
        R"({"type":"status","source":"trae","codex_model":"must-not-apply","trae_model":"Model A","trae_tasks":[]})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"chime","agent":"codex"})", snapshot
    ) == CodexMessageKind::Chime);
}
