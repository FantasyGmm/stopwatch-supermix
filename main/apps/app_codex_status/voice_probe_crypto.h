#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ble_voice_probe::crypto {

constexpr size_t kNonceSize = 13;
constexpr size_t kAadSize = 14;
constexpr size_t kPlaintextSize = 164;
constexpr size_t kTagSize = 8;
constexpr size_t kCiphertextSize = kPlaintextSize + kTagSize;

using Nonce = std::array<uint8_t, kNonceSize>;
using Plaintext = std::array<uint8_t, kPlaintextSize>;
using Ciphertext = std::array<uint8_t, kCiphertextSize>;

bool derive_nonce(uint16_t session, uint32_t sequence, uint32_t watch_ms,
                  Nonce& output);
bool encrypt(const uint8_t* aad, size_t aad_len,
             const uint8_t* plaintext, size_t plaintext_len,
             const Nonce& nonce, Ciphertext& output,
             uint32_t& elapsed_us);
bool decrypt(const uint8_t* aad, size_t aad_len,
             const uint8_t* ciphertext, size_t ciphertext_len,
             const Nonce& nonce, Plaintext& output);
bool encrypt_synthetic(const uint8_t* aad, size_t aad_len,
                       const uint8_t* plaintext, size_t plaintext_len,
                       const Nonce& nonce,
                       uint8_t* output, size_t output_capacity,
                       size_t& output_len, uint32_t& elapsed_us);
bool decrypt_synthetic(const uint8_t* aad, size_t aad_len,
                       const uint8_t* ciphertext, size_t ciphertext_len,
                       const Nonce& nonce,
                       uint8_t* output, size_t output_capacity,
                       size_t& output_len);

}  // namespace ble_voice_probe::crypto
