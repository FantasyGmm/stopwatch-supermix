#include "codex_status_model.h"

#include <cassert>
#include <cstring>
#include <string>

static CodexMessageKind parse(const char* json, CodexStatusSnapshot& snapshot)
{
    return parse_codex_message(json, std::strlen(json), snapshot);
}

static std::string alert_status(const std::string& project, const std::string& title)
{
    return std::string(
        R"({"codex_model":"bounded","codex_alert_revision":99,"codex_alerts":[{"route":"0123456789abcdef","project":")") +
        project + R"(","title":")" + title + R"(","state":"pending"}]})";
}

static std::string repeated(const char* text, size_t count)
{
    std::string result;
    for (size_t index = 0; index < count; ++index) {
        result += text;
    }
    return result;
}

int main()
{
    CodexStatusSnapshot snapshot;
    std::string trailing_whitespace = R"({"codex_model":"whitespace"})";
    trailing_whitespace += " \t\r\n";
    assert(parse_codex_message(
        trailing_whitespace.data(), trailing_whitespace.size(), snapshot
    ) == CodexMessageKind::Status);
    assert(snapshot.model == "whitespace");
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
    assert(snapshot.five_hour_left_pct == -1);
    assert(snapshot.week_left_pct == -1);

    assert(parse(
        R"({"codex_model":"gpt-5","codex_5h_left_pct":null,"codex_week_left_pct":null})",
        snapshot
    ) == CodexMessageKind::Status);
    assert(snapshot.five_hour_left_pct == -1);
    assert(snapshot.week_left_pct == -1);
    assert(parse(
        R"({"codex_model":"gpt-5","codex_5h_left_pct":101,"codex_week_left_pct":83.5})",
        snapshot
    ) == CodexMessageKind::Status);
    assert(snapshot.five_hour_left_pct == -1);
    assert(snapshot.week_left_pct == -1);
    assert(parse(
        R"({"codex_model":"gpt-5","codex_5h_left_pct":100,"codex_week_left_pct":0})",
        snapshot
    ) == CodexMessageKind::Status);
    assert(snapshot.five_hour_left_pct == 100);
    assert(snapshot.week_left_pct == 0);

    assert(parse(
        R"({"codex_model":"gpt-5","codex_effort":"high","codex_5h_left_pct":91,"codex_week_left_pct":83,"codex_alert_pending":1,"codex_alert_running":1,"codex_alert_completed":7,"codex_alert_revision":12,"codex_alerts":[{"route":"0123456789abcdef","project":"StopWatch","title":"Waiting","state":"pending"},{"route":"fedcba9876543210","project":"Agent","title":"Working","state":"running"}]})",
        snapshot
    ) == CodexMessageKind::Status);
    assert(snapshot.alerts_valid);
    assert(snapshot.alerts.size() == 2);
    assert(snapshot.alerts[0].route == UINT64_C(0x0123456789abcdef));
    assert(snapshot.alerts[0].state == CodexTaskState::Pending);
    assert(snapshot.alerts[1].route == UINT64_C(0xfedcba9876543210));
    assert(snapshot.alerts[1].state == CodexTaskState::Running);
    assert(snapshot.alerts[0].model.empty());
    assert(snapshot.alerts[0].effort.empty());
    assert(snapshot.completed_count == 7);

    assert(parse(
        R"({"codex_alerts":[{"route":"0123456789abcdef","project":"StopWatch","title":"Waiting","state":"pending","model":"gpt-5.6-sol","effort":"high"}]})",
        snapshot
    ) == CodexMessageKind::Status);
    assert(snapshot.alerts.size() == 1);
    assert(snapshot.alerts[0].model == "gpt-5.6-sol");
    assert(snapshot.alerts[0].effort == "high");

    const CodexStatusSnapshot valid_snapshot = snapshot;

    const std::string ascii_boundary = alert_status(std::string(32, 'p'), std::string(48, 't'));
    assert(parse(ascii_boundary.c_str(), snapshot) == CodexMessageKind::Status);
    assert(snapshot.alerts[0].project.size() == 32);
    assert(snapshot.alerts[0].title.size() == 48);

    const std::string multibyte_boundary = alert_status(
        repeated("项", 32),
        repeated("🚀", 48));
    assert(parse(multibyte_boundary.c_str(), snapshot) == CodexMessageKind::Status);
    assert(snapshot.alerts[0].project == repeated("项", 32));
    assert(snapshot.alerts[0].title == repeated("🚀", 48));

    const CodexStatusSnapshot bounded_snapshot = snapshot;
    std::string invalid_project_utf8(1, static_cast<char>(0xff));
    const std::string invalid_label_messages[] = {
        alert_status(std::string(33, 'p'), "title"),
        alert_status("project", std::string(49, 't')),
        alert_status(repeated("项", 33), "title"),
        alert_status("project", repeated("🚀", 49)),
        alert_status(invalid_project_utf8, "title"),
        alert_status("project", std::string("bad") + static_cast<char>(0xc0) + static_cast<char>(0x80)),
        alert_status(std::string("bad") + static_cast<char>(0xed) + static_cast<char>(0xa0) + static_cast<char>(0x80), "title"),
        alert_status("project", std::string("bad") + static_cast<char>(0xf4) + static_cast<char>(0x90) + static_cast<char>(0x80) + static_cast<char>(0x80)),
    };
    for (const std::string& invalid : invalid_label_messages) {
        assert(parse_codex_message(invalid.data(), invalid.size(), snapshot) == CodexMessageKind::Invalid);
        assert(snapshot.model == bounded_snapshot.model);
        assert(snapshot.alert_revision == bounded_snapshot.alert_revision);
        assert(snapshot.alerts.size() == bounded_snapshot.alerts.size());
        assert(snapshot.alerts[0].project == bounded_snapshot.alerts[0].project);
        assert(snapshot.alerts[0].title == bounded_snapshot.alerts[0].title);
    }

    snapshot = valid_snapshot;
    const std::string invalid_trailing_status[] = {
        R"({"codex_model":"bad"}x)",
        R"({"codex_model":"bad"}{"codex_model":"second"})",
        std::string(R"({"codex_model":"bad"})") + '\0' + "garbage",
    };
    for (const std::string& invalid : invalid_trailing_status) {
        assert(parse_codex_message(invalid.data(), invalid.size(), snapshot) == CodexMessageKind::Invalid);
        assert(snapshot.model == valid_snapshot.model);
        assert(snapshot.alert_revision == valid_snapshot.alert_revision);
    }
    const char* invalid_alerts[] = {
        R"({"codex_alerts":[{"route":"0000000000000000","project":"p","title":"t","state":"pending"}]})",
        R"({"codex_alerts":[{"route":"0123456789abcde","project":"p","title":"t","state":"pending"}]})",
        R"({"codex_alerts":[{"route":"0123456789abcdeF","project":"p","title":"t","state":"pending"}]})",
        R"({"codex_alerts":[{"route":"0123456789abcdef","project":"p","title":"t","state":"completed"}]})",
        R"({"codex_alerts":[{"route":"0123456789abcdef","project":"p","title":"t","state":"unknown"}]})",
        R"({"codex_alerts":[{"route":"0123456789abcdef","project":"p","title":"t","state":"pending","time":"now"}]})",
        R"({"codex_alerts":[{"route":"0123456789abcdef","project":"p","state":"pending"}]})",
    };
    for (const char* invalid : invalid_alerts) {
        assert(parse(invalid, snapshot) == CodexMessageKind::Invalid);
        assert(snapshot.model == valid_snapshot.model);
        assert(snapshot.alert_revision == valid_snapshot.alert_revision);
        assert(snapshot.alerts.size() == valid_snapshot.alerts.size());
        assert(snapshot.alerts[0].route == valid_snapshot.alerts[0].route);
    }
    const std::string invalid_metadata[] = {
        R"({"codex_alerts":[{"route":"0123456789abcdef","project":"p","title":"t","state":"pending","model":56,"effort":"high"}]})",
        R"({"codex_alerts":[{"route":"0123456789abcdef","project":"p","title":"t","state":"pending","model":"gpt-5.6-sol"}]})",
        std::string(
            R"({"codex_alerts":[{"route":"0123456789abcdef","project":"p","title":"t","state":"pending","model":")") +
            std::string(65, 'm') + R"(","effort":"high"}]})",
        std::string(
            R"({"codex_alerts":[{"route":"0123456789abcdef","project":"p","title":"t","state":"pending","model":"gpt-5.6-sol","effort":")") +
            std::string(33, 'e') + R"("}]})",
    };
    for (const std::string& invalid : invalid_metadata) {
        assert(parse(invalid.c_str(), snapshot) == CodexMessageKind::Invalid);
        assert(snapshot.model == valid_snapshot.model);
        assert(snapshot.alert_revision == valid_snapshot.alert_revision);
    }

    std::string too_many = R"({"codex_alerts":[)";
    for (int index = 0; index < 9; ++index) {
        if (index != 0) {
            too_many += ',';
        }
        too_many += R"({"route":"0123456789abcdef","project":"p","title":"t","state":"pending"})";
    }
    too_many += "]}";
    assert(parse(too_many.c_str(), snapshot) == CodexMessageKind::Invalid);
    assert(snapshot.alerts.size() == valid_snapshot.alerts.size());

    const char* detail =
        R"({"type":"task_detail","route":"0123456789abcdef","revision":12,"index":1,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})";
    assert(parse(detail, snapshot) == CodexMessageKind::TaskDetail);
    assert(snapshot.task_detail.route == UINT64_C(0x0123456789abcdef));
    assert(snapshot.task_detail.route_hex == "0123456789abcdef");
    assert(snapshot.task_detail.revision == 12);
    assert(snapshot.task_detail.index == 1);
    assert(snapshot.task_detail.count == 3);
    assert(snapshot.task_detail.key_epoch == 7);
    assert(snapshot.task_detail.exchange_nonce[0] == 0x10);
    assert(snapshot.task_detail.sequence == 16909060U);
    assert(snapshot.task_detail.ciphertext.size() == 20);

    const CodexStatusSnapshot valid_detail = snapshot;
    std::string detail_whitespace = detail;
    detail_whitespace += " \n\t";
    assert(parse_codex_message(
        detail_whitespace.data(), detail_whitespace.size(), snapshot
    ) == CodexMessageKind::TaskDetail);
    const std::string invalid_trailing_detail[] = {
        std::string(detail) + "garbage",
        std::string(detail) + detail,
        std::string(detail) + '\0' + "garbage",
    };
    for (const std::string& invalid : invalid_trailing_detail) {
        assert(parse_codex_message(invalid.data(), invalid.size(), snapshot) == CodexMessageKind::Invalid);
        assert(snapshot.task_detail.route == valid_detail.task_detail.route);
        assert(snapshot.task_detail.ciphertext == valid_detail.task_detail.ciphertext);
    }
    snapshot = valid_detail;
    const char* invalid_details[] = {
        R"({"type":"task_detail","route":"0123456789abcdef","revision":12,"index":1,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y=","text":"plaintext"})",
        R"({"type":"task_detail","version":1,"route":"0123456789abcdef","revision":12,"index":1,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})",
        R"({"type":"task_detail","route":"0123456789abcdef","revision":true,"index":1,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})",
        R"({"type":"task_detail","route":"0123456789abcdef","revision":2147483648,"index":1,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})",
        R"({"type":"task_detail","route":"0123456789abcdef","revision":12,"index":3,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})",
        R"({"type":"task_detail","route":"0123456789abcdef","revision":12,"index":0,"count":17,"epoch":7,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})",
        R"({"type":"task_detail","route":"0123456789abcdef","revision":12,"index":1,"count":3,"epoch":0,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})",
        R"({"type":"task_detail","route":"0123456789abcdef","revision":12,"index":1,"count":3,"epoch":7,"nonce":"0000000000000000","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})",
        R"({"type":"task_detail","route":"0123456789abcdef","revision":12,"index":1,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":0,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})",
        R"({"type":"task_detail","route":"0123456789abcdef","revision":12,"index":1,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"***"})",
    };
    for (const char* invalid : invalid_details) {
        assert(parse(invalid, snapshot) == CodexMessageKind::Invalid);
        assert(snapshot.task_detail.route == valid_detail.task_detail.route);
        assert(snapshot.task_detail.ciphertext == valid_detail.task_detail.ciphertext);
    }
    const char* duplicate_detail =
        R"({"type":"task_detail","route":"0123456789abcdef","route":"fedcba9876543210","revision":12,"index":1,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":16909060,"ciphertext":"0MswuvXkAPavyTTH3//AiAsfo1Y="})";
    assert(parse(duplicate_detail, snapshot) == CodexMessageKind::Invalid);
    assert(snapshot.task_detail.route == valid_detail.task_detail.route);

    std::string oversize_status = R"({"codex_model":"gpt-5","padding":")";
    oversize_status.append(2048, 'x');
    oversize_status += R"("})";
    assert(parse(oversize_status.c_str(), snapshot) == CodexMessageKind::Invalid);
    assert(snapshot.task_detail.route == valid_detail.task_detail.route);
    assert(parse(
        R"({"type":"status","source":"trae","codex_model":"must-not-apply","trae_model":"Model A","trae_tasks":[]})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"chime","agent":"codex"})", snapshot
    ) == CodexMessageKind::Chime);
}
