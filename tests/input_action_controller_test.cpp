#include "main/apps/app_codex_status/input_action_controller.h"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

constexpr voice_input::crypto::Key kKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

input_action::ReadyControl ready(uint8_t first_byte)
{
    input_action::ReadyControl value = {};
    value.connection_nonce = {first_byte, 2, 3, 4, 5, 6, 7, 8};
    return value;
}

input_action::Action decode(
    const input_action::WireAction& wire,
    const input_action::ReadyControl& control,
    voice_input::crypto::CcmContext& crypto)
{
    input_action::Action action = {};
    assert(input_action::decode_action(
        wire.bytes.data(),
        wire.bytes.size(),
        control.connection_nonce,
        crypto,
        action));
    return action;
}

void test_requires_ready_and_sequences_from_one()
{
    voice_input::crypto::CcmContext crypto(kKey);
    InputActionController controller(crypto);
    assert(!controller.enqueue(CodexButtonAction::Submit, 10));
    assert(!controller.enqueue(CodexButtonAction::None, 11));

    const auto control = ready(1);
    controller.set_ready(control);
    assert(controller.enqueue(CodexButtonAction::Submit, 12));
    assert(controller.enqueue(CodexButtonAction::ForceSteer, 13));

    input_action::WireAction wire = {};
    assert(controller.take(wire));
    auto action = decode(wire, control, crypto);
    assert(action.kind == input_action::Kind::Submit);
    assert(action.sequence == 1);
    assert(action.watch_ms == 12);
    controller.record_result(true);

    assert(controller.take(wire));
    action = decode(wire, control, crypto);
    assert(action.kind == input_action::Kind::ForceSteer);
    assert(action.sequence == 2);
    assert(action.watch_ms == 13);
    controller.record_result(true);
    assert(!controller.take(wire));
}

void test_queue_is_bounded_and_dequeued_actions_are_never_retried()
{
    voice_input::crypto::CcmContext crypto(kKey);
    InputActionController controller(crypto);
    const auto control = ready(9);
    controller.set_ready(control);

    for (uint32_t index = 0; index < 4; ++index) {
        assert(controller.enqueue(CodexButtonAction::Submit, 100 + index));
    }
    assert(!controller.enqueue(CodexButtonAction::ForceSteer, 104));

    input_action::WireAction wire = {};
    assert(controller.take(wire));
    assert(decode(wire, control, crypto).sequence == 1);
    controller.record_result(false);

    for (uint32_t sequence = 2; sequence <= 4; ++sequence) {
        assert(controller.take(wire));
        assert(decode(wire, control, crypto).sequence == sequence);
        controller.record_result(false);
    }
    assert(!controller.take(wire));

    assert(controller.enqueue(CodexButtonAction::ForceSteer, 200));
    assert(controller.take(wire));
    const auto next = decode(wire, control, crypto);
    assert(next.kind == input_action::Kind::ForceSteer);
    assert(next.sequence == 5);
}

void test_disconnect_and_new_ready_clear_connection_state()
{
    voice_input::crypto::CcmContext crypto(kKey);
    InputActionController controller(crypto);
    const auto first = ready(1);
    controller.set_ready(first);
    assert(controller.enqueue(CodexButtonAction::Submit, 300));

    controller.disconnect();
    input_action::WireAction wire = {};
    assert(!controller.take(wire));
    assert(!controller.enqueue(CodexButtonAction::Submit, 301));

    const auto second = ready(10);
    controller.set_ready(second);
    assert(controller.enqueue(CodexButtonAction::ForceSteer, 302));
    assert(controller.take(wire));
    const auto action = decode(wire, second, crypto);
    assert(action.sequence == 1);
    assert(action.kind == input_action::Kind::ForceSteer);
}

void test_clear_pending_preserves_ready_connection_and_sequence()
{
    voice_input::crypto::CcmContext crypto(kKey);
    InputActionController controller(crypto);
    const auto control = ready(11);
    controller.set_ready(control);
    assert(controller.enqueue(CodexButtonAction::Submit, 400));
    controller.clear_pending();

    input_action::WireAction wire = {};
    assert(!controller.take(wire));
    assert(controller.enqueue(CodexButtonAction::ForceSteer, 401));
    assert(controller.take(wire));
    assert(decode(wire, control, crypto).sequence == 2);
}

void test_ready_replay_guard_authenticates_once_per_connection()
{
    voice_input::crypto::CcmContext crypto(kKey);
    InputActionReadyReplayGuard guard;
    const auto control = ready(12);
    std::array<uint8_t, input_action::kReadyControlSize> bytes = {};
    assert(input_action::encode_ready(
               control, crypto, bytes.data(), bytes.size()) == bytes.size());

    input_action::ReadyControl decoded = {};
    assert(guard.decode_once(bytes.data(), bytes.size(), crypto, decoded));
    assert(decoded.connection_nonce == control.connection_nonce);
    assert(!guard.decode_once(bytes.data(), bytes.size(), crypto, decoded));

    guard.reset();
    assert(guard.decode_once(bytes.data(), bytes.size(), crypto, decoded));
    guard.reset();
    bytes.back() ^= 1;
    assert(!guard.decode_once(bytes.data(), bytes.size(), crypto, decoded));
}

}  // namespace

int main()
{
    test_requires_ready_and_sequences_from_one();
    test_queue_is_bounded_and_dequeued_actions_are_never_retried();
    test_disconnect_and_new_ready_clear_connection_state();
    test_clear_pending_preserves_ready_connection_and_sequence();
    test_ready_replay_guard_authenticates_once_per_connection();
}
