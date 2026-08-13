#pragma once

#include "codex_task_detail_crypto.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

struct CodexTaskDetail {
    int32_t revision = 0;
    std::string text;
};

class CodexTaskDetailStore {
public:
    bool accept(const CodexTaskDetailChunk& chunk);
    const CodexTaskDetail* find(uint64_t route) const;
    void retain_routes(const std::vector<uint64_t>& routes);
    void reset_for_credential(uint32_t key_epoch);
    void reset_connection();
    void reset();

private:
    struct Pending {
        int32_t revision = 0;
        size_t count = 0;
        std::vector<std::string> chunks;
        std::vector<bool> received;
    };

    bool accept_sequence(
        const voice_input::crypto::ExchangeNonce& nonce,
        uint32_t sequence);
    void retire_active_nonce();

    uint32_t _key_epoch = 0;
    bool _has_active_nonce = false;
    voice_input::crypto::ExchangeNonce _active_nonce = {};
    uint32_t _highest_sequence = 0;
    std::deque<voice_input::crypto::ExchangeNonce> _recent_nonces;
    std::map<uint64_t, Pending> _pending;
    std::map<uint64_t, CodexTaskDetail> _published;
};
