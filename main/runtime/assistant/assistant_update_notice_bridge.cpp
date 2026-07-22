#include "runtime/assistant/assistant_update_notice_bridge.h"

#include "apps/common/common.h"

#include <cstdlib>
#include <cstring>
#include <utility>

void AssistantUpdateNoticeBridge::beginSession(uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _active_session = session_id;
    do {
        _active_session_token = _next_session_token++;
    } while (_active_session_token == 0);
    _latest.reset();
}

void AssistantUpdateNoticeBridge::endSession(uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_active_session != session_id) {
        return;
    }
    _active_session = 0;
    _active_session_token = 0;
    _latest.reset();
}

AssistantUpdateSessionToken AssistantUpdateNoticeBridge::captureSessionToken()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _active_session_token;
}

void AssistantUpdateNoticeBridge::report(AssistantUpdateSessionToken session_token, const char* version)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (session_token == 0 || session_token != _active_session_token ||
        version == nullptr || version[0] == '\0') {
        return;
    }
    _latest = AssistantUpdateReport{_active_session, version};
}

std::optional<AssistantUpdateReport> AssistantUpdateNoticeBridge::consume()
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto latest = std::move(_latest);
    _latest.reset();
    return latest;
}

AssistantUpdateNoticeBridge& GetAssistantUpdateNoticeBridge()
{
    static AssistantUpdateNoticeBridge bridge;
    return bridge;
}

extern "C" char* stopwatch_duplicate_firmware_version()
{
    const auto version = common::FirmwareVersion;
    auto* duplicate = static_cast<char*>(std::malloc(version.size() + 1));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::memcpy(duplicate, version.data(), version.size());
    duplicate[version.size()] = '\0';
    return duplicate;
}

extern "C" uint64_t stopwatch_capture_firmware_update_session_token()
{
    return GetAssistantUpdateNoticeBridge().captureSessionToken();
}

extern "C" void stopwatch_report_remote_firmware_version(uint64_t session_token, const char* version)
{
    GetAssistantUpdateNoticeBridge().report(session_token, version);
}
