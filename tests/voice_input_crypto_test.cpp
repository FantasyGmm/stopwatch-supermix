#include "main/apps/app_codex_status/voice_input_crypto.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

constexpr voice_input::crypto::Key kTestKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
constexpr voice_input::crypto::ExchangeNonce kTestNonce = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};

}  // namespace

int main()
{
    using namespace voice_input::crypto;

    static_assert(static_cast<uint8_t>(Domain::Ready) == 0x01);
    static_assert(static_cast<uint8_t>(Domain::Unavailable) == 0x02);
    static_assert(static_cast<uint8_t>(Domain::PttBegin) == 0x11);
    static_assert(static_cast<uint8_t>(Domain::Audio) == 0x12);
    static_assert(static_cast<uint8_t>(Domain::PttEnd) == 0x13);
    static_assert(static_cast<uint8_t>(Domain::VoiceError) == 0x14);

    for (const Domain domain : {
             Domain::Ready,
             Domain::Unavailable,
             Domain::PttBegin,
             Domain::Audio,
             Domain::PttEnd,
             Domain::VoiceError,
         }) {
        Nonce nonce = {};
        assert(derive_nonce(domain, kTestNonce, 0x04030201U, nonce));
        assert(nonce[0] == static_cast<uint8_t>(domain));
        assert(std::equal(kTestNonce.begin(), kTestNonce.end(), nonce.begin() + 1));
        assert(nonce[9] == 0x01);
        assert(nonce[10] == 0x02);
        assert(nonce[11] == 0x03);
        assert(nonce[12] == 0x04);
    }

    Nonce rejected = {};
    assert(!derive_nonce(static_cast<Domain>(0x10), kTestNonce, 1, rejected));
    assert(!derive_nonce(Domain::Audio, ExchangeNonce{}, 1, rejected));

    CcmContext context(kTestKey);
    assert(context.ready());
    assert(context.key_setup_count() == 1);

    const std::array<uint8_t, 6> aad = {3, 2, 7, 0, 1, 0};
    const std::array<uint8_t, 5> plaintext = {'a', 'u', 'd', 'i', 'o'};
    std::array<uint8_t, plaintext.size() + kTagSize> encrypted = {};
    size_t encrypted_size = 0;
    assert(context.encrypt(
        Domain::Audio,
        kTestNonce,
        1,
        aad.data(),
        aad.size(),
        plaintext.data(),
        plaintext.size(),
        encrypted.data(),
        encrypted.size(),
        encrypted_size));
    assert(encrypted_size == encrypted.size());

    std::array<uint8_t, plaintext.size()> decrypted = {};
    size_t decrypted_size = 0;
    assert(context.decrypt(
        Domain::Audio,
        kTestNonce,
        1,
        aad.data(),
        aad.size(),
        encrypted.data(),
        encrypted.size(),
        decrypted.data(),
        decrypted.size(),
        decrypted_size));
    assert(decrypted_size == plaintext.size());
    assert(decrypted == plaintext);

    for (size_t changed_index : {size_t{0}, encrypted.size() - 1}) {
        auto tampered = encrypted;
        tampered[changed_index] ^= 1;
        assert(!context.decrypt(
            Domain::Audio,
            kTestNonce,
            1,
            aad.data(),
            aad.size(),
            tampered.data(),
            tampered.size(),
            decrypted.data(),
            decrypted.size(),
            decrypted_size));
    }
    auto changed_aad = aad;
    changed_aad[0] ^= 1;
    assert(!context.decrypt(
        Domain::Audio,
        kTestNonce,
        1,
        changed_aad.data(),
        changed_aad.size(),
        encrypted.data(),
        encrypted.size(),
        decrypted.data(),
        decrypted.size(),
        decrypted_size));
    auto changed_nonce = kTestNonce;
    changed_nonce[0] ^= 1;
    assert(!context.decrypt(
        Domain::Audio,
        changed_nonce,
        1,
        aad.data(),
        aad.size(),
        encrypted.data(),
        encrypted.size(),
        decrypted.data(),
        decrypted.size(),
        decrypted_size));

    Key wrong_key = kTestKey;
    wrong_key[0] ^= 1;
    CcmContext wrong_key_context(wrong_key);
    assert(!wrong_key_context.decrypt(
        Domain::Audio,
        kTestNonce,
        1,
        aad.data(),
        aad.size(),
        encrypted.data(),
        encrypted.size(),
        decrypted.data(),
        decrypted.size(),
        decrypted_size));
    assert(!context.decrypt(
        Domain::PttBegin,
        kTestNonce,
        1,
        aad.data(),
        aad.size(),
        encrypted.data(),
        encrypted.size(),
        decrypted.data(),
        decrypted.size(),
        decrypted_size));
    assert(!context.decrypt(
        Domain::Audio,
        kTestNonce,
        2,
        aad.data(),
        aad.size(),
        encrypted.data(),
        encrypted.size(),
        decrypted.data(),
        decrypted.size(),
        decrypted_size));

    std::vector<std::thread> workers;
    for (int worker = 0; worker < 8; ++worker) {
        workers.emplace_back([&context, &aad, &plaintext]() {
            for (uint32_t sequence = 1; sequence <= 64; ++sequence) {
                std::array<uint8_t, plaintext.size() + kTagSize> output = {};
                size_t output_size = 0;
                assert(context.encrypt(
                    Domain::Audio,
                    kTestNonce,
                    sequence,
                    aad.data(),
                    aad.size(),
                    plaintext.data(),
                    plaintext.size(),
                    output.data(),
                    output.size(),
                    output_size));
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    assert(context.key_setup_count() == 1);
    assert(context.max_concurrent_operations() == 1);
}
