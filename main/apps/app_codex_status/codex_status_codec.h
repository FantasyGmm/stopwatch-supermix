#pragma once

#include "codex_ble_framing.h"
#include "codex_status_model.h"

#include <memory>

struct CodexStatusObjectDeleter {
    void operator()(cJSON* root) const;
};

using CodexStatusObject = std::unique_ptr<cJSON, CodexStatusObjectDeleter>;

struct CodexDecodedStatusPayload {
    CodexStatusObject root;
    CodexTaskDetailEnvelope native_detail;
    bool has_native_detail = false;
};

bool decode_codex_status_payload(
    const char* data,
    size_t length,
    CodexStatusEncoding encoding,
    CodexDecodedStatusPayload& output);
