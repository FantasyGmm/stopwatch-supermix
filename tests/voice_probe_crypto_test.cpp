#include "main/apps/app_codex_status/voice_probe_crypto.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

namespace {

template <size_t Size>
std::array<uint8_t, Size> from_hex(std::string_view hex)
{
    assert(hex.size() == Size * 2);
    std::array<uint8_t, Size> result = {};
    auto nibble = [](char value) -> uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<uint8_t>(value - '0');
        }
        return static_cast<uint8_t>(value - 'a' + 10);
    };
    for (size_t index = 0; index < Size; ++index) {
        result[index] = static_cast<uint8_t>(
            (nibble(hex[index * 2]) << 4U) | nibble(hex[index * 2 + 1]));
    }
    return result;
}

}  // namespace

int main()
{
    using namespace ble_voice_probe::crypto;

    const Nonce expected_nonce =
        from_hex<kNonceSize>("101112131415161718191a1b1c");
    const std::array<uint8_t, 14> aad =
        from_hex<14>("020207000100000064000000ac00");
    const Plaintext plaintext = {};
    const Ciphertext expected_ciphertext = from_hex<kCiphertextSize>(
        "7ce17041b85ceed4bb5948912f076c812ad355797f979e2a3d4d1ad38eb885430"
        "57a9fde2190a49a3f6ca33e9f7b96618c9ce5c51bb7600d9fe2d79627c3f70f5"
        "c917ad213e94d2ac87c80d85b1770ad8b0d56b6e1cfd63186c90a740972c30e1"
        "f964ef8857931bae5f4d3b9e01eb7fc4437636d71976153bdb1c63f16c8fd5c5"
        "3720cdfa86184af620540f33fe593b122581362678afe9a5c27fd20cafc3fd03a"
        "3a2f12f2ba40f8e73af636");

    Nonce nonce = {};
    assert(derive_nonce(0x1413, 0x18171615, 0x1c1b1a19, nonce));
    assert(nonce == expected_nonce);
    assert(!derive_nonce(0, 1, 1, nonce));

    Ciphertext ciphertext = {};
    uint32_t elapsed_us = 0;
    assert(encrypt(
        aad.data(),
        aad.size(),
        plaintext.data(),
        plaintext.size(),
        expected_nonce,
        ciphertext,
        elapsed_us));
    assert(ciphertext == expected_ciphertext);

    Plaintext decrypted = {};
    assert(decrypt(
        aad.data(),
        aad.size(),
        ciphertext.data(),
        ciphertext.size(),
        expected_nonce,
        decrypted));
    assert(decrypted == plaintext);

    auto tampered_ciphertext = ciphertext;
    tampered_ciphertext[0] ^= 0x01;
    assert(!decrypt(
        aad.data(),
        aad.size(),
        tampered_ciphertext.data(),
        tampered_ciphertext.size(),
        expected_nonce,
        decrypted));

    auto tampered_tag = ciphertext;
    tampered_tag.back() ^= 0x01;
    assert(!decrypt(
        aad.data(),
        aad.size(),
        tampered_tag.data(),
        tampered_tag.size(),
        expected_nonce,
        decrypted));

    auto tampered_nonce = expected_nonce;
    tampered_nonce.back() ^= 0x01;
    assert(!decrypt(
        aad.data(),
        aad.size(),
        ciphertext.data(),
        ciphertext.size(),
        tampered_nonce,
        decrypted));

    auto tampered_aad = aad;
    tampered_aad.back() ^= 0x01;
    assert(!decrypt(
        tampered_aad.data(),
        tampered_aad.size(),
        ciphertext.data(),
        ciphertext.size(),
        expected_nonce,
        decrypted));

    const Nonce zero_nonce = {};
    assert(!encrypt(
        aad.data(),
        aad.size(),
        plaintext.data(),
        plaintext.size(),
        zero_nonce,
        ciphertext,
        elapsed_us));
    assert(!encrypt(
        aad.data(),
        aad.size(),
        plaintext.data(),
        plaintext.size() - 1,
        expected_nonce,
        ciphertext,
        elapsed_us));
    assert(!decrypt(
        aad.data(),
        aad.size(),
        ciphertext.data(),
        ciphertext.size() - 1,
        expected_nonce,
        decrypted));
}
