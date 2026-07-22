#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct AssistantUpdateReport {
    uint64_t session_id;
    std::string version;
};

class AssistantUpdateNoticeBridge {
public:
    void beginSession(uint64_t session_id);
    void endSession(uint64_t session_id);
    void report(const char* version);
    std::optional<AssistantUpdateReport> consume();

private:
    std::mutex _mutex;
    uint64_t _active_session = 0;
    std::optional<AssistantUpdateReport> _latest;
};

AssistantUpdateNoticeBridge& GetAssistantUpdateNoticeBridge();

extern "C" char* stopwatch_duplicate_firmware_version();
extern "C" void stopwatch_report_remote_firmware_version(const char* version);
