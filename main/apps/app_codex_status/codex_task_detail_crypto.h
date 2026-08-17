#pragma once

#include "codex_status_model.h"
#include "voice_input_crypto.h"

#include <cstddef>
#include <cstdint>
#include <string>

struct CodexTaskDetailChunk {
    uint64_t route = 0;
    int32_t revision = 0;
    size_t index = 0;
    size_t count = 0;
    voice_input::crypto::ExchangeNonce exchange_nonce = {};
    uint32_t sequence = 0;
    std::string text;
};

std::string canonical_codex_task_detail_aad(const CodexTaskDetailEnvelope& envelope);

bool decrypt_codex_task_detail(
    const CodexTaskDetailEnvelope& envelope,
    uint32_t expected_key_epoch,
    voice_input::crypto::CcmContext& context,
    CodexTaskDetailChunk& output);
