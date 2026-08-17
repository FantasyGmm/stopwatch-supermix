#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace voice_input::crypto {

constexpr size_t kKeySize = 16;
constexpr size_t kExchangeNonceSize = 8;
constexpr size_t kNonceSize = 13;
constexpr size_t kTagSize = 8;

using Key = std::array<uint8_t, kKeySize>;
using ExchangeNonce = std::array<uint8_t, kExchangeNonceSize>;
using Nonce = std::array<uint8_t, kNonceSize>;

enum class Domain : uint8_t {
    Ready = 0x01,
    Unavailable = 0x02,
    PttBegin = 0x11,
    Audio = 0x12,
    PttEnd = 0x13,
    VoiceError = 0x14,
    InputReady = 0x21,
    InputSubmit = 0x22,
    InputForceSteer = 0x23,
    TaskDetail = 0x24,
};

bool derive_nonce(
    Domain domain,
    const ExchangeNonce& exchange_nonce,
    uint32_t sequence,
    Nonce& output);

class CcmContext {
public:
    CcmContext();
    explicit CcmContext(const Key& key);
    ~CcmContext();

    CcmContext(const CcmContext&) = delete;
    CcmContext& operator=(const CcmContext&) = delete;

    bool ready() const;
    bool set_key(const Key& key);
    void clear();
    bool encrypt(
        Domain domain,
        const ExchangeNonce& exchange_nonce,
        uint32_t sequence,
        const uint8_t* aad,
        size_t aad_size,
        const uint8_t* plaintext,
        size_t plaintext_size,
        uint8_t* output,
        size_t output_capacity,
        size_t& output_size);
    bool decrypt(
        Domain domain,
        const ExchangeNonce& exchange_nonce,
        uint32_t sequence,
        const uint8_t* aad,
        size_t aad_size,
        const uint8_t* ciphertext,
        size_t ciphertext_size,
        uint8_t* output,
        size_t output_capacity,
        size_t& output_size);

    uint32_t key_setup_count() const;
    uint32_t max_concurrent_operations() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace voice_input::crypto
