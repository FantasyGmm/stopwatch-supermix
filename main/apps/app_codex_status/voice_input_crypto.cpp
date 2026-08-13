#include "voice_input_crypto.h"

#include <mbedtls/ccm.h>

#include <algorithm>
#include <mutex>

namespace voice_input::crypto {
namespace {

bool valid_domain(Domain domain)
{
    switch (domain) {
        case Domain::Ready:
        case Domain::Unavailable:
        case Domain::PttBegin:
        case Domain::Audio:
        case Domain::PttEnd:
        case Domain::VoiceError:
        case Domain::TaskDetail:
            return true;
    }
    return false;
}

bool nonzero_nonce(const ExchangeNonce& nonce)
{
    return std::any_of(nonce.begin(), nonce.end(), [](uint8_t value) {
        return value != 0;
    });
}

}  // namespace

struct CcmContext::Impl {
    mbedtls_ccm_context context = {};
    bool ready = false;
    mutable std::mutex mutex;
    uint32_t key_setup_count = 0;
    uint32_t active_operations = 0;
    uint32_t max_concurrent_operations = 0;
};

bool derive_nonce(
    Domain domain,
    const ExchangeNonce& exchange_nonce,
    uint32_t sequence,
    Nonce& output)
{
    output = {};
    if (!valid_domain(domain) || !nonzero_nonce(exchange_nonce)) {
        return false;
    }
    output[0] = static_cast<uint8_t>(domain);
    std::copy(exchange_nonce.begin(), exchange_nonce.end(), output.begin() + 1);
    output[9] = static_cast<uint8_t>(sequence & 0xffU);
    output[10] = static_cast<uint8_t>((sequence >> 8U) & 0xffU);
    output[11] = static_cast<uint8_t>((sequence >> 16U) & 0xffU);
    output[12] = static_cast<uint8_t>((sequence >> 24U) & 0xffU);
    return true;
}

CcmContext::CcmContext() : _impl(std::make_unique<Impl>())
{
    mbedtls_ccm_init(&_impl->context);
}

CcmContext::CcmContext(const Key& key) : CcmContext()
{
    set_key(key);
}

CcmContext::~CcmContext()
{
    mbedtls_ccm_free(&_impl->context);
}

bool CcmContext::ready() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->ready;
}

bool CcmContext::set_key(const Key& key)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    mbedtls_ccm_free(&_impl->context);
    mbedtls_ccm_init(&_impl->context);
    ++_impl->key_setup_count;
    _impl->ready = mbedtls_ccm_setkey(
                       &_impl->context,
                       MBEDTLS_CIPHER_ID_AES,
                       key.data(),
                       key.size() * 8U) == 0;
    return _impl->ready;
}

void CcmContext::clear()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    mbedtls_ccm_free(&_impl->context);
    mbedtls_ccm_init(&_impl->context);
    _impl->ready = false;
}

bool CcmContext::encrypt(
    Domain domain,
    const ExchangeNonce& exchange_nonce,
    uint32_t sequence,
    const uint8_t* aad,
    size_t aad_size,
    const uint8_t* plaintext,
    size_t plaintext_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    output_size = 0;
    Nonce nonce = {};
    if (!derive_nonce(domain, exchange_nonce, sequence, nonce) ||
        (aad_size != 0 && aad == nullptr) ||
        (plaintext_size != 0 && plaintext == nullptr) || output == nullptr ||
        output_capacity < plaintext_size + kTagSize) {
        return false;
    }

    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->ready) {
        return false;
    }
    ++_impl->active_operations;
    _impl->max_concurrent_operations = std::max(
        _impl->max_concurrent_operations, _impl->active_operations);
    const int result = mbedtls_ccm_encrypt_and_tag(
        &_impl->context,
        plaintext_size,
        nonce.data(),
        nonce.size(),
        aad,
        aad_size,
        plaintext,
        output,
        output + plaintext_size,
        kTagSize);
    --_impl->active_operations;
    if (result != 0) {
        return false;
    }
    output_size = plaintext_size + kTagSize;
    return true;
}

bool CcmContext::decrypt(
    Domain domain,
    const ExchangeNonce& exchange_nonce,
    uint32_t sequence,
    const uint8_t* aad,
    size_t aad_size,
    const uint8_t* ciphertext,
    size_t ciphertext_size,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    output_size = 0;
    Nonce nonce = {};
    if (!derive_nonce(domain, exchange_nonce, sequence, nonce) ||
        (aad_size != 0 && aad == nullptr) || ciphertext == nullptr ||
        ciphertext_size < kTagSize || output == nullptr ||
        output_capacity < ciphertext_size - kTagSize) {
        return false;
    }
    const size_t plaintext_size = ciphertext_size - kTagSize;

    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->ready) {
        return false;
    }
    ++_impl->active_operations;
    _impl->max_concurrent_operations = std::max(
        _impl->max_concurrent_operations, _impl->active_operations);
    const int result = mbedtls_ccm_auth_decrypt(
        &_impl->context,
        plaintext_size,
        nonce.data(),
        nonce.size(),
        aad,
        aad_size,
        ciphertext,
        output,
        ciphertext + plaintext_size,
        kTagSize);
    --_impl->active_operations;
    if (result != 0) {
        std::fill_n(output, plaintext_size, uint8_t{0});
        return false;
    }
    output_size = plaintext_size;
    return true;
}

uint32_t CcmContext::key_setup_count() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->key_setup_count;
}

uint32_t CcmContext::max_concurrent_operations() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->max_concurrent_operations;
}

}  // namespace voice_input::crypto
