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

    auto& bridge = GetAssistantUpdateNoticeBridge();
    bridge.beginSession(7);
    stopwatch_report_remote_firmware_version("v0.6.0");
    auto report = bridge.consume();
    assert(report.has_value());
    assert(report->session_id == 7);
    assert(report->version == "v0.6.0");

    bridge.endSession(7);
    stopwatch_report_remote_firmware_version("v9.9.9");
    assert(!bridge.consume());

    bridge.beginSession(8);
    stopwatch_report_remote_firmware_version(nullptr);
    stopwatch_report_remote_firmware_version("");
    assert(!bridge.consume());
    stopwatch_report_remote_firmware_version("v0.6.0");
    stopwatch_report_remote_firmware_version("v0.7.0");
    report = bridge.consume();
    assert(report.has_value());
    assert(report->session_id == 8);
    assert(report->version == "v0.7.0");
    assert(!bridge.consume());

    stopwatch_report_remote_firmware_version("v0.8.0");
    bridge.endSession(9);
    report = bridge.consume();
    assert(report.has_value());
    assert(report->session_id == 8);
    bridge.endSession(8);

    bridge.beginSession(10);
    stopwatch_report_remote_firmware_version("v1.0.0");
    bridge.endSession(10);
    assert(!bridge.consume());

    char* local = stopwatch_duplicate_firmware_version();
    assert(local != nullptr);
    assert(std::string(local) == "V0.5");
    std::free(local);
    return 0;
}
