#include "runtime/assistant/assistant_update_notice_policy.h"
#include "runtime/assistant/assistant_update_notice_bridge.h"

#include <cassert>
#include <cstdlib>
#include <optional>
#include <string>

int main()
{
    assert(NormalizeAssistantFirmwareVersion("V0.5") == std::optional<std::string>("0.5.0"));
    assert(NormalizeAssistantFirmwareVersion("v1.2.3") == std::optional<std::string>("1.2.3"));
    assert(NormalizeAssistantFirmwareVersion("1") == std::optional<std::string>("1.0.0"));
    assert(!NormalizeAssistantFirmwareVersion("1.2.3.4"));
    assert(!NormalizeAssistantFirmwareVersion("1.beta.3"));
    assert(!NormalizeAssistantFirmwareVersion(""));

    AssistantUpdateNoticeModel model("V0.5");
    assert(model.restore(std::optional<std::string>("0.4.9")) ==
           AssistantUpdateStorageAction::Clear);
    assert(!model.hasPending());
    assert(model.observe("v0.6") == AssistantUpdateStorageAction::Save);
    assert(model.pendingVersion() == "0.6.0");
    assert(model.displayText() == "发现新版本 v0.6.0");
    assert(model.observe("0.5.0") == AssistantUpdateStorageAction::None);
    assert(model.observe("invalid") == AssistantUpdateStorageAction::None);
    assert(model.pendingVersion() == "0.6.0");
    assert(model.restore(std::nullopt) == AssistantUpdateStorageAction::None);
    assert(model.pendingVersion() == "0.6.0");
    assert(model.observe("0.7.1") == AssistantUpdateStorageAction::Save);
    assert(model.pendingVersion() == "0.7.1");

    AssistantUpdateNoticeModel after_usb_upgrade("V0.7.1");
    assert(after_usb_upgrade.restore(std::optional<std::string>("v0.7.1")) ==
           AssistantUpdateStorageAction::Clear);
    assert(!after_usb_upgrade.hasPending());

    AssistantUpdateNoticeModel offline_reopen("V0.5");
    assert(offline_reopen.restore(std::optional<std::string>("v0.6")) ==
           AssistantUpdateStorageAction::None);
    assert(offline_reopen.displayText() == "发现新版本 v0.6.0");

    AssistantUpdateNoticeModel malformed_local("not-a-version");
    assert(malformed_local.restore(std::optional<std::string>("v0.6")) ==
           AssistantUpdateStorageAction::None);
    assert(malformed_local.pendingVersion() == "0.6.0");

    AssistantUpdateNoticeModel unavailable_local("");
    assert(unavailable_local.restore(std::optional<std::string>("v0.6")) ==
           AssistantUpdateStorageAction::None);
    assert(unavailable_local.pendingVersion() == "0.6.0");

    AssistantUpdateNoticeModel no_local_remote_notice("");
    assert(no_local_remote_notice.observe("v9.9.9") == AssistantUpdateStorageAction::None);
    assert(!no_local_remote_notice.hasPending());

    auto& bridge = GetAssistantUpdateNoticeBridge();
    bridge.beginSession(7);
    auto session_token = stopwatch_capture_firmware_update_session_token();
    assert(session_token != 0);
    stopwatch_report_remote_firmware_version(session_token, "v0.6.0");
    auto report = bridge.consume();
    assert(report.has_value());
    assert(report->session_id == 7);
    assert(report->version == "v0.6.0");

    bridge.endSession(7);
    stopwatch_report_remote_firmware_version(session_token, "v9.9.9");
    assert(!bridge.consume());

    bridge.beginSession(8);
    session_token = stopwatch_capture_firmware_update_session_token();
    stopwatch_report_remote_firmware_version(session_token, nullptr);
    stopwatch_report_remote_firmware_version(session_token, "");
    assert(!bridge.consume());
    stopwatch_report_remote_firmware_version(session_token, "v0.6.0");
    stopwatch_report_remote_firmware_version(session_token, "v0.7.0");
    report = bridge.consume();
    assert(report.has_value());
    assert(report->session_id == 8);
    assert(report->version == "v0.7.0");
    assert(!bridge.consume());

    stopwatch_report_remote_firmware_version(session_token, "v0.8.0");
    bridge.endSession(9);
    report = bridge.consume();
    assert(report.has_value());
    assert(report->session_id == 8);
    bridge.endSession(8);

    bridge.beginSession(10);
    session_token = stopwatch_capture_firmware_update_session_token();
    stopwatch_report_remote_firmware_version(session_token, "v1.0.0");
    bridge.endSession(10);
    assert(!bridge.consume());

    bridge.beginSession(11);
    const auto stale_session_token = stopwatch_capture_firmware_update_session_token();
    bridge.beginSession(12);
    const auto active_session_token = stopwatch_capture_firmware_update_session_token();
    assert(stale_session_token != active_session_token);
    stopwatch_report_remote_firmware_version(stale_session_token, "v8.8.8");
    assert(!bridge.consume());
    stopwatch_report_remote_firmware_version(active_session_token, "v0.9.0");
    report = bridge.consume();
    assert(report.has_value());
    assert(report->session_id == 12);
    assert(report->version == "v0.9.0");
    bridge.endSession(12);

    char* local = stopwatch_duplicate_firmware_version();
    assert(local != nullptr);
    assert(std::string(local) == "V0.5");
    std::free(local);
    return 0;
}
