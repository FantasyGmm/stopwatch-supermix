#include "main/apps/app_codex_status/input_action_protocol.h"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

constexpr voice_input::crypto::Key kTestKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
constexpr voice_input::crypto::ExchangeNonce kTestNonce = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};
constexpr std::array<uint8_t, input_action::kReadyControlSize> kReadyVector = {
    0x01, 0x01, 0x00, 0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0xfc, 0x73, 0xd2, 0x42, 0xa0, 0x22, 0x66, 0x7e,
};
constexpr std::array<uint8_t, input_action::kWireActionSize> kSubmitVector = {
    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
    0x08, 0x00, 0x90, 0x85, 0xa9, 0x57, 0x1d, 0x1c, 0xff, 0x3c,
};
constexpr std::array<uint8_t, input_action::kWireActionSize> kForceSteerVector = {
    0x01, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x08, 0x00, 0x7b, 0x42, 0x11, 0x63, 0x5b, 0xc8, 0xf5, 0xca,
};

}  // namespace

int main()
{
    using namespace input_action;

    voice_input::crypto::CcmContext context(kTestKey);
    ReadyControl ready = {kTestNonce};
    std::array<uint8_t, kReadyControlSize> ready_bytes = {};
    assert(encode_ready(ready, context, ready_bytes.data(), ready_bytes.size()) ==
           ready_bytes.size());
    assert(ready_bytes == kReadyVector);
    ReadyControl decoded_ready = {};
    assert(decode_ready(
        kReadyVector.data(), kReadyVector.size(), context, decoded_ready));
    assert(decoded_ready.connection_nonce == kTestNonce);

    std::array<uint8_t, kWireActionSize> action_bytes = {};
    const Action submit = {Kind::Submit, 1, 0x04030201};
    assert(encode_action(
               submit,
               kTestNonce,
               context,
               action_bytes.data(),
               action_bytes.size()) == action_bytes.size());
    assert(action_bytes == kSubmitVector);
    Action decoded_action = {};
    assert(decode_action(
        kSubmitVector.data(),
        kSubmitVector.size(),
        kTestNonce,
        context,
        decoded_action));
    assert(decoded_action.kind == Kind::Submit);
    assert(decoded_action.sequence == 1);
    assert(decoded_action.watch_ms == 0x04030201);

    const Action force_steer = {Kind::ForceSteer, UINT32_MAX, UINT32_MAX};
    assert(encode_action(
               force_steer,
               kTestNonce,
               context,
               action_bytes.data(),
               action_bytes.size()) == action_bytes.size());
    assert(action_bytes == kForceSteerVector);
    assert(decode_action(
        action_bytes.data(),
        action_bytes.size(),
        kTestNonce,
        context,
        decoded_action));
    assert(decoded_action.kind == Kind::ForceSteer);
    assert(decoded_action.sequence == UINT32_MAX);
    assert(decoded_action.watch_ms == UINT32_MAX);

    for (const size_t index : {size_t{0}, size_t{1}, size_t{2}, size_t{6},
                               size_t{10}, kSubmitVector.size() - 1}) {
        auto changed = kSubmitVector;
        changed[index] ^= index == 1 ? 0x7f : 1;
        assert(!decode_action(
            changed.data(), changed.size(), kTestNonce, context, decoded_action));
    }
    auto zero_sequence = kSubmitVector;
    zero_sequence[2] = 0;
    assert(!decode_action(
        zero_sequence.data(),
        zero_sequence.size(),
        kTestNonce,
        context,
        decoded_action));
    auto wrong_nonce = kTestNonce;
    wrong_nonce.back() ^= 1;
    assert(!decode_action(
        kSubmitVector.data(),
        kSubmitVector.size(),
        wrong_nonce,
        context,
        decoded_action));
    const voice_input::crypto::ExchangeNonce zero_nonce = {};
    assert(!decode_action(
        kSubmitVector.data(),
        kSubmitVector.size(),
        zero_nonce,
        context,
        decoded_action));
    assert(!decode_action(
        kSubmitVector.data(),
        kSubmitVector.size() - 1,
        kTestNonce,
        context,
        decoded_action));
    assert(!decode_action(
        kSubmitVector.data(),
        kSubmitVector.size() + 1,
        kTestNonce,
        context,
        decoded_action));

    for (const size_t index : {size_t{0}, size_t{1}, size_t{2}, size_t{3},
                               size_t{4}, kReadyVector.size() - 1}) {
        auto changed = kReadyVector;
        changed[index] ^= 1;
        assert(!decode_ready(
            changed.data(), changed.size(), context, decoded_ready));
    }
    auto ready_zero_nonce = kReadyVector;
    for (size_t index = 4; index < 12; ++index) {
        ready_zero_nonce[index] = 0;
    }
    assert(!decode_ready(
        ready_zero_nonce.data(), ready_zero_nonce.size(), context, decoded_ready));
    assert(!decode_ready(
        kReadyVector.data(), kReadyVector.size() - 1, context, decoded_ready));
    assert(!decode_ready(
        kReadyVector.data(), kReadyVector.size() + 1, context, decoded_ready));

    ReadyControl invalid_ready = {};
    assert(encode_ready(
               invalid_ready,
               context,
               ready_bytes.data(),
               ready_bytes.size()) == 0);
    assert(encode_ready(ready, context, nullptr, ready_bytes.size()) == 0);
    assert(encode_ready(
               ready,
               context,
               ready_bytes.data(),
               ready_bytes.size() - 1) == 0);
    assert(encode_action(
               Action{static_cast<Kind>(99), 1, 0},
               kTestNonce,
               context,
               action_bytes.data(),
               action_bytes.size()) == 0);
    assert(encode_action(
               Action{Kind::Submit, 0, 0},
               kTestNonce,
               context,
               action_bytes.data(),
               action_bytes.size()) == 0);
    assert(encode_action(
               submit,
               zero_nonce,
               context,
               action_bytes.data(),
               action_bytes.size()) == 0);
    assert(encode_action(
               submit, kTestNonce, context, nullptr, action_bytes.size()) == 0);
    assert(encode_action(
               submit,
               kTestNonce,
               context,
               action_bytes.data(),
               action_bytes.size() - 1) == 0);

    return 0;
}
