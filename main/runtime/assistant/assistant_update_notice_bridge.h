#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct AssistantUpdateReport {
    uint64_t session_id;
    std::string version;
};

using AssistantUpdateSessionToken = uint64_t;

class AssistantUpdateNoticeBridge {
public:
    void beginSession(uint64_t session_id);
    void endSession(uint64_t session_id);
    AssistantUpdateSessionToken captureSessionToken();
    void report(AssistantUpdateSessionToken session_token, const char* version);
    std::optional<AssistantUpdateReport> consume();

private:
    std::mutex _mutex;
    uint64_t _active_session = 0;
    AssistantUpdateSessionToken _active_session_token = 0;
    AssistantUpdateSessionToken _next_session_token = 1;
    std::optional<AssistantUpdateReport> _latest;
};

AssistantUpdateNoticeBridge& GetAssistantUpdateNoticeBridge();

extern "C" char* stopwatch_duplicate_firmware_version();
extern "C" uint64_t stopwatch_capture_firmware_update_session_token();
extern "C" void stopwatch_report_remote_firmware_version(uint64_t session_token, const char* version);
