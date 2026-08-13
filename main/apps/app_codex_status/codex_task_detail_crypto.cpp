#include "codex_task_detail_crypto.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

bool nonzero(const voice_input::crypto::ExchangeNonce& nonce)
{
    return std::any_of(nonce.begin(), nonce.end(), [](uint8_t byte) {
        return byte != 0;
    });
}

bool valid_route_hex(const CodexTaskDetailEnvelope& envelope)
{
    if (envelope.route == 0 || envelope.route_hex.size() != 16) {
        return false;
    }
    uint64_t parsed = 0;
    for (char character : envelope.route_hex) {
        uint8_t nibble = 0;
        if (character >= '0' && character <= '9') {
            nibble = static_cast<uint8_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            nibble = static_cast<uint8_t>(character - 'a' + 10);
        } else {
            return false;
        }
        parsed = (parsed << 4U) | nibble;
    }
    return parsed == envelope.route;
}

bool valid_envelope(const CodexTaskDetailEnvelope& envelope)
{
    return valid_route_hex(envelope) && envelope.revision >= 0 &&
           envelope.count >= 1 && envelope.count <= 16 &&
           envelope.index < envelope.count && envelope.key_epoch != 0 &&
           nonzero(envelope.exchange_nonce) && envelope.sequence != 0 &&
           envelope.ciphertext.size() >= voice_input::crypto::kTagSize &&
           envelope.ciphertext.size() <= 1300 + voice_input::crypto::kTagSize;
}

bool valid_utf8(const uint8_t* data, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        const uint8_t first = data[offset];
        if (first < 0x80U) {
            ++offset;
            continue;
        }
        size_t continuation = 0;
        uint32_t codepoint = 0;
        if ((first & 0xe0U) == 0xc0U) {
            continuation = 1;
            codepoint = first & 0x1fU;
        } else if ((first & 0xf0U) == 0xe0U) {
            continuation = 2;
            codepoint = first & 0x0fU;
        } else if ((first & 0xf8U) == 0xf0U) {
            continuation = 3;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (offset + continuation >= size) {
            return false;
        }
        for (size_t index = 1; index <= continuation; ++index) {
            const uint8_t next = data[offset + index];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((continuation == 1 && codepoint < 0x80U) ||
            (continuation == 2 && codepoint < 0x800U) ||
            (continuation == 3 && codepoint < 0x10000U) ||
            codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
        offset += continuation + 1;
    }
    return true;
}

std::string nonce_hex(const voice_input::crypto::ExchangeNonce& nonce)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result(16, '0');
    for (size_t index = 0; index < nonce.size(); ++index) {
        result[index * 2] = kHex[nonce[index] >> 4U];
        result[index * 2 + 1] = kHex[nonce[index] & 0x0fU];
    }
    return result;
}

}  // namespace

std::string canonical_codex_task_detail_aad(const CodexTaskDetailEnvelope& envelope)
{
    if (!valid_envelope(envelope)) {
        return {};
    }
    char suffix[160];
    const int written = std::snprintf(
        suffix,
        sizeof(suffix),
        "\",\"revision\":%d,\"index\":%u,\"count\":%u,\"epoch\":%u,"
        "\"nonce\":\"%s\",\"sequence\":%u}",
        envelope.revision,
        envelope.index,
        envelope.count,
        envelope.key_epoch,
        nonce_hex(envelope.exchange_nonce).c_str(),
        envelope.sequence);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(suffix)) {
        return {};
    }
    return std::string("{\"type\":\"task_detail\",\"version\":1,\"route\":\"") +
           envelope.route_hex + suffix;
}

bool decrypt_codex_task_detail(
    const CodexTaskDetailEnvelope& envelope,
    uint32_t expected_key_epoch,
    voice_input::crypto::CcmContext& context,
    CodexTaskDetailChunk& output)
{
    output = {};
    if (expected_key_epoch == 0 || envelope.key_epoch != expected_key_epoch) {
        return false;
    }
    const std::string aad = canonical_codex_task_detail_aad(envelope);
    if (aad.empty()) {
        return false;
    }
    const size_t plaintext_capacity = envelope.ciphertext.size() - voice_input::crypto::kTagSize;
    std::vector<uint8_t> plaintext(std::max<size_t>(plaintext_capacity, 1));
    size_t plaintext_size = 0;
    if (!context.decrypt(
            voice_input::crypto::Domain::TaskDetail,
            envelope.exchange_nonce,
            envelope.sequence,
            reinterpret_cast<const uint8_t*>(aad.data()),
            aad.size(),
            envelope.ciphertext.data(),
            envelope.ciphertext.size(),
            plaintext.data(),
            plaintext_capacity,
            plaintext_size)) {
        return false;
    }
    if (!valid_utf8(plaintext.data(), plaintext_size)) {
        std::fill_n(plaintext.data(), plaintext_size, uint8_t{0});
        return false;
    }
    CodexTaskDetailChunk parsed;
    parsed.route = envelope.route;
    parsed.revision = envelope.revision;
    parsed.index = envelope.index;
    parsed.count = envelope.count;
    parsed.exchange_nonce = envelope.exchange_nonce;
    parsed.sequence = envelope.sequence;
    parsed.text.assign(reinterpret_cast<const char*>(plaintext.data()), plaintext_size);
    std::fill_n(plaintext.data(), plaintext_size, uint8_t{0});
    output = std::move(parsed);
    return true;
}
