#include "main/apps/app_codex_status/codex_task_detail_crypto.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr voice_input::crypto::Key kKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

CodexTaskDetailEnvelope fixed_envelope()
{
    CodexTaskDetailEnvelope envelope;
    envelope.route = UINT64_C(0x0123456789abcdef);
    envelope.route_hex = "0123456789abcdef";
    envelope.revision = 12;
    envelope.index = 1;
    envelope.count = 3;
    envelope.key_epoch = 7;
    envelope.exchange_nonce = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    envelope.sequence = 0x01020304;
    envelope.ciphertext = {
        0xd0, 0xcb, 0x30, 0xba, 0xf5, 0xe4, 0x00, 0xf6, 0xaf, 0xc9,
        0x34, 0xc7, 0xdf, 0xff, 0xc0, 0x88, 0x0b, 0x1f, 0xa3, 0x56,
    };
    return envelope;
}

}  // namespace

int main()
{
    voice_input::crypto::CcmContext context(kKey);
    CodexTaskDetailChunk chunk;
    const auto envelope = fixed_envelope();
    assert(canonical_codex_task_detail_aad(envelope) ==
           R"({"type":"task_detail","version":1,"route":"0123456789abcdef","revision":12,"index":1,"count":3,"epoch":7,"nonce":"1011121314151617","sequence":16909060})");
    assert(decrypt_codex_task_detail(envelope, 7, context, chunk));
    assert(chunk.route == envelope.route);
    assert(chunk.revision == 12);
    assert(chunk.index == 1);
    assert(chunk.count == 3);
    assert(chunk.exchange_nonce == envelope.exchange_nonce);
    assert(chunk.sequence == envelope.sequence);
    assert(chunk.text == "fixed 私密");

    for (int field = 0; field < 9; ++field) {
        auto changed = envelope;
        switch (field) {
            case 0: changed.route = UINT64_C(0xfedcba9876543210); changed.route_hex = "fedcba9876543210"; break;
            case 1: ++changed.revision; break;
            case 2: changed.index = 2; break;
            case 3: changed.count = 4; break;
            case 4: ++changed.key_epoch; break;
            case 5: changed.exchange_nonce[0] ^= 1; break;
            case 6: ++changed.sequence; break;
            case 7: changed.ciphertext[0] ^= 1; break;
            case 8: changed.route_hex = "0123456789abcdee"; break;
        }
        CodexTaskDetailChunk rejected;
        assert(!decrypt_codex_task_detail(changed, changed.key_epoch, context, rejected));
        assert(rejected.text.empty());
    }

    CodexTaskDetailChunk rejected;
    assert(!decrypt_codex_task_detail(envelope, 8, context, rejected));
    auto wrong_key = kKey;
    wrong_key[0] ^= 1;
    voice_input::crypto::CcmContext wrong_context(wrong_key);
    assert(!decrypt_codex_task_detail(envelope, 7, wrong_context, rejected));

    auto invalid_utf8 = envelope;
    invalid_utf8.index = 0;
    invalid_utf8.count = 1;
    invalid_utf8.sequence = 9;
    invalid_utf8.ciphertext.assign(1 + voice_input::crypto::kTagSize, 0);
    const std::string invalid_aad = canonical_codex_task_detail_aad(invalid_utf8);
    const uint8_t invalid_plaintext = 0xff;
    size_t encrypted_size = 0;
    assert(context.encrypt(
        voice_input::crypto::Domain::TaskDetail,
        invalid_utf8.exchange_nonce,
        invalid_utf8.sequence,
        reinterpret_cast<const uint8_t*>(invalid_aad.data()),
        invalid_aad.size(),
        &invalid_plaintext,
        1,
        invalid_utf8.ciphertext.data(),
        invalid_utf8.ciphertext.size(),
        encrypted_size));
    assert(!decrypt_codex_task_detail(invalid_utf8, 7, context, rejected));
}
