#pragma once

#include "codex_ble_framing.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>


class CodexBleTransport {
public:
    bool start(const std::string& device_name);
    void stop();
    void poll(uint32_t now_ms);
    bool connected() const;
    bool take_message(CodexBleMessage& message);

    bool enqueue_frame_from_ble(const uint8_t* data, size_t length);
    void handle_gap_connected(uint16_t connection_handle);
    void handle_gap_disconnected();
    bool stopping() const;

private:
    void* _frame_queue = nullptr;
    CodexBleReassembler _reassembler;
    std::deque<CodexBleMessage> _messages;
    std::atomic<bool> _connected{false};
    std::atomic<bool> _stopping{false};
    bool _initialized = false;
    std::atomic<uint16_t> _connection_handle{0xffff};
};
