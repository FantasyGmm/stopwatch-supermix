#include "voice_probe_crypto.h"

#include <mbedtls/ccm.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <mutex>

#if defined(ESP_PLATFORM)
#include <esp_timer.h>
#endif

namespace ble_voice_probe::crypto {
namespace {

constexpr std::array<uint8_t, 16> kDiagnosticKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
constexpr std::array<uint8_t, 3> kNonceDomain = {0x10, 0x11, 0x12};

uint32_t g_key_schedule_initializations = 0;

class DiagnosticCcmContext {
public:
    class Operation {
    public:
        explicit Operation(DiagnosticCcmContext& owner)
            : _owner(owner),
              _lock(owner._mutex)
        {
            ++_owner._active_operations;
            _owner._max_concurrent_operations = std::max(
                _owner._max_concurrent_operations,
                _owner._active_operations);
        }

        ~Operation()
        {
            --_owner._active_operations;
        }

        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;

    private:
        DiagnosticCcmContext& _owner;
        std::unique_lock<std::mutex> _lock;
    };

    DiagnosticCcmContext()
    {
        mbedtls_ccm_init(&_context);
        ++g_key_schedule_initializations;
        _ready = mbedtls_ccm_setkey(
                     &_context,
                     MBEDTLS_CIPHER_ID_AES,
                     kDiagnosticKey.data(),
                     kDiagnosticKey.size() * 8) == 0;
    }

    ~DiagnosticCcmContext()
    {
        mbedtls_ccm_free(&_context);
    }

    bool ready() const
    {
        return _ready;
    }

    mbedtls_ccm_context* get()
    {
        return &_context;
    }

    Operation begin_operation()
    {
        return Operation(*this);
    }

    uint32_t max_concurrent_operations()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _max_concurrent_operations;
    }

private:
    mbedtls_ccm_context _context = {};
    bool _ready = false;
    std::mutex _mutex;
    uint32_t _active_operations = 0;
    uint32_t _max_concurrent_operations = 0;
};

DiagnosticCcmContext& diagnostic_context()
{
    static DiagnosticCcmContext context;
    return context;
}

void write_u16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

bool is_valid_nonce(const Nonce& nonce)
{
    return std::any_of(nonce.begin(), nonce.end(), [](uint8_t value) {
        return value != 0;
    });
}

uint64_t now_us()
{
#if defined(ESP_PLATFORM)
    return static_cast<uint64_t>(esp_timer_get_time());
#else
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
#endif
}

uint32_t elapsed_u32(uint64_t started_us)
{
    return static_cast<uint32_t>(std::min<uint64_t>(
        now_us() - started_us,
        std::numeric_limits<uint32_t>::max()));
}

}  // namespace

bool derive_nonce(
    uint16_t session,
    uint32_t sequence,
    uint32_t watch_ms,
    Nonce& output
)
{
    if (session == 0) {
        output = {};
        return false;
    }

    std::copy(kNonceDomain.begin(), kNonceDomain.end(), output.begin());
    write_u16(output.data() + 3, session);
    write_u32(output.data() + 5, sequence);
    write_u32(output.data() + 9, watch_ms);
    return true;
}

bool encrypt(
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* plaintext,
    size_t plaintext_len,
    const Nonce& nonce,
    Ciphertext& output,
    uint32_t& elapsed_us
)
{
    output = {};
    elapsed_us = 0;
    if (aad == nullptr || aad_len != kAadSize ||
        plaintext == nullptr || plaintext_len != kPlaintextSize ||
        !is_valid_nonce(nonce)) {
        return false;
    }

    const uint64_t started_us = now_us();
    auto& context = diagnostic_context();
    auto operation = context.begin_operation();
    if (!context.ready()) {
        elapsed_us = elapsed_u32(started_us);
        return false;
    }

    Ciphertext encrypted = {};
    const int result = mbedtls_ccm_encrypt_and_tag(
        context.get(),
        plaintext_len,
        nonce.data(),
        nonce.size(),
        aad,
        aad_len,
        plaintext,
        encrypted.data(),
        encrypted.data() + kPlaintextSize,
        kTagSize);
    elapsed_us = elapsed_u32(started_us);
    if (result != 0) {
        return false;
    }

    output = encrypted;
    return true;
}

bool decrypt(
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* ciphertext,
    size_t ciphertext_len,
    const Nonce& nonce,
    Plaintext& output
)
{
    output = {};
    if (aad == nullptr || aad_len != kAadSize ||
        ciphertext == nullptr || ciphertext_len != kCiphertextSize ||
        !is_valid_nonce(nonce)) {
        return false;
    }

    auto& context = diagnostic_context();
    auto operation = context.begin_operation();
    if (!context.ready()) {
        return false;
    }

    Plaintext decrypted = {};
    const int result = mbedtls_ccm_auth_decrypt(
        context.get(),
        kPlaintextSize,
        nonce.data(),
        nonce.size(),
        aad,
        aad_len,
        ciphertext,
        decrypted.data(),
        ciphertext + kPlaintextSize,
        kTagSize);
    if (result != 0) {
        return false;
    }

    output = decrypted;
    return true;
}

bool encrypt_synthetic(
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* plaintext,
    size_t plaintext_len,
    const Nonce& nonce,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_len,
    uint32_t& elapsed_us
)
{
    output_len = 0;
    elapsed_us = 0;
    if (aad == nullptr || aad_len != kAadSize ||
        plaintext == nullptr || plaintext_len == 0 ||
        output == nullptr || output_capacity < plaintext_len + kTagSize ||
        !is_valid_nonce(nonce)) {
        return false;
    }

    const uint64_t started_us = now_us();
    auto& context = diagnostic_context();
    auto operation = context.begin_operation();
    if (!context.ready()) {
        elapsed_us = elapsed_u32(started_us);
        return false;
    }

    const int result = mbedtls_ccm_encrypt_and_tag(
        context.get(),
        plaintext_len,
        nonce.data(),
        nonce.size(),
        aad,
        aad_len,
        plaintext,
        output,
        output + plaintext_len,
        kTagSize);
    elapsed_us = elapsed_u32(started_us);
    if (result != 0) {
        return false;
    }

    output_len = plaintext_len + kTagSize;
    return true;
}

bool decrypt_synthetic(
    const uint8_t* aad,
    size_t aad_len,
    const uint8_t* ciphertext,
    size_t ciphertext_len,
    const Nonce& nonce,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_len
)
{
    output_len = 0;
    if (aad == nullptr || aad_len != kAadSize ||
        ciphertext == nullptr || ciphertext_len <= kTagSize ||
        output == nullptr || output_capacity < ciphertext_len - kTagSize ||
        !is_valid_nonce(nonce)) {
        return false;
    }

    const size_t plaintext_len = ciphertext_len - kTagSize;
    auto& context = diagnostic_context();
    auto operation = context.begin_operation();
    if (!context.ready()) {
        return false;
    }

    const int result = mbedtls_ccm_auth_decrypt(
        context.get(),
        plaintext_len,
        nonce.data(),
        nonce.size(),
        aad,
        aad_len,
        ciphertext,
        output,
        ciphertext + plaintext_len,
        kTagSize);
    if (result != 0) {
        return false;
    }

    output_len = plaintext_len;
    return true;
}

#if defined(STOPWATCH_HOST_TEST)
uint32_t key_schedule_initializations()
{
    return g_key_schedule_initializations;
}

uint32_t max_concurrent_operations()
{
    return diagnostic_context().max_concurrent_operations();
}
#endif

}  // namespace ble_voice_probe::crypto
