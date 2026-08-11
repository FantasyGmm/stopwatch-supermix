#include "main/apps/app_codex_status/voice_input_controller.h"

#include <cassert>
#include <cstdint>
#include <functional>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr voice_input::crypto::Key kKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

class FakeBackend final : public VoiceCaptureBackend {
public:
    bool acquireCapture() override
    {
        ++acquire_calls;
        if (!acquire_ok || capture_owned) {
            return false;
        }
        capture_owned = true;
        return true;
    }

    void releaseCapture() override
    {
        ++release_calls;
        capture_owned = false;
    }

    bool openRecorder() override
    {
        ++open_recorder_calls;
        return recorder_ok;
    }

    void closeRecorder() override
    {
        ++close_recorder_calls;
    }

    size_t readRecorder(
        int16_t* samples,
        size_t sample_count,
        uint32_t timeout_ms) override
    {
        ++read_calls;
        assert(timeout_ms == VoiceCapturePipeline::kCaptureReadTimeoutMs);
        for (size_t index = 0; index < sample_count; ++index) {
            samples[index] = static_cast<int16_t>(index);
        }
        if (during_read) {
            during_read();
        }
        return read_ok ? sample_count * sizeof(*samples) : 0;
    }

    bool openResampler(void*& handle) override
    {
        handle = resampler_ok ? this : nullptr;
        return resampler_ok;
    }

    bool maxOutputSamples(void*, size_t, uint32_t& output_samples) override
    {
        output_samples = 320;
        return resampler_ok;
    }

    bool resample(
        void*,
        const int16_t*,
        size_t,
        int16_t* output,
        uint32_t& output_samples) override
    {
        if (!resample_ok) {
            return false;
        }
        output_samples = 320;
        for (size_t index = 0; index < output_samples; ++index) {
            output[index] = static_cast<int16_t>(index * 2);
        }
        return true;
    }

    void closeResampler(void*) override
    {
        ++close_resampler_calls;
    }

    size_t encodeAdpcm(
        const int16_t*,
        size_t sample_count,
        uint8_t* output,
        size_t capacity) override
    {
        if (!encode_ok || sample_count != 320 || capacity != 164) {
            return 0;
        }
        for (size_t index = 0; index < capacity; ++index) {
            output[index] = static_cast<uint8_t>(index);
        }
        return capacity;
    }

    bool acquire_ok = true;
    bool recorder_ok = true;
    bool resampler_ok = true;
    bool resample_ok = true;
    bool read_ok = true;
    bool encode_ok = true;
    bool capture_owned = false;
    std::atomic<int> acquire_calls{0};
    std::atomic<int> release_calls{0};
    std::atomic<int> open_recorder_calls{0};
    std::atomic<int> close_recorder_calls{0};
    std::atomic<int> close_resampler_calls{0};
    std::atomic<int> read_calls{0};
    std::function<void()> during_read;
};

class FakeEntropy final : public VoiceInputEntropy {
public:
    uint32_t random32() override
    {
        if (index < values.size()) {
            return values[index++];
        }
        return static_cast<uint32_t>(++index * 0x01010101U);
    }

    std::vector<uint32_t> values = {0, 7, 0x04030201, 0x08070605};
    size_t index = 0;
};

class FakeVoiceButtonSink final : public VoiceButtonSink {
public:
    void handle_button(bool pressed, uint32_t now_ms) override
    {
        events.push_back(pressed ? "press" : "release");
        times.push_back(now_ms);
    }

    void cancel_for_home(uint32_t now_ms) override
    {
        events.push_back("cancel");
        times.push_back(now_ms);
    }

    std::vector<const char*> events;
    std::vector<uint32_t> times;
};

class FakeCodexButtonActionSink final : public CodexButtonActionSink {
public:
    void handle_action(CodexButtonAction action, uint32_t now_ms) override
    {
        actions.push_back(action);
        times.push_back(now_ms);
    }

    std::vector<CodexButtonAction> actions;
    std::vector<uint32_t> times;
};

VoiceButtonDispatchDecision dispatch(
    VoiceButtonDispatcher& dispatcher,
    FakeVoiceButtonSink& sink,
    bool left_pressed,
    bool right_pressed,
    bool woke_screen,
    bool go_home,
    uint32_t now_ms)
{
    return dispatcher.dispatch(
        left_pressed,
        right_pressed,
        woke_screen,
        go_home,
        now_ms,
        &sink);
}

voice_input::VoiceNotification decode(
    const voice_input::Notification& wire,
    voice_input::crypto::CcmContext& crypto,
    const voice_input::crypto::ExchangeNonce* nonce)
{
    voice_input::VoiceNotification decoded = {};
    assert(voice_input::decode_notification(
        wire.bytes.data(), wire.size, crypto, nonce, decoded));
    return decoded;
}

void test_requires_ready_subscription_and_audio_mtu()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);

    controller.handle_button(true, 10);
    assert(backend.acquire_calls == 0);
    controller.handle_button(false, 11);

    controller.set_ready(true, 188);
    controller.handle_button(true, 20);
    assert(backend.acquire_calls == 0);
    assert(controller.ui_state() == VoiceInputUiState::Off);

    controller.set_ready(true, 189);
    controller.handle_button(false, 21);
    controller.handle_button(true, 22);
    assert(backend.acquire_calls == 1);
    assert(controller.ui_state() == VoiceInputUiState::Active);
}

void test_begin_audio_end_order_and_counters()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 100);

    voice_input::Notification wire = {};
    assert(controller.take_notification(wire));
    auto begin = decode(wire, crypto, nullptr);
    assert(begin.kind == voice_input::NotificationKind::PttBegin);
    assert(begin.sequence == 0);
    assert(begin.session == 1);
    const auto nonce = begin.session_nonce;
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 101);

    controller.poll(120);
    assert(controller.take_notification(wire));
    auto audio = decode(wire, crypto, &nonce);
    assert(audio.kind == voice_input::NotificationKind::Audio);
    assert(audio.sequence == 1);
    assert(audio.payload_size == voice_input::kAudioPlaintextSize);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 121);

    controller.handle_button(false, 130);
    controller.poll(131);
    assert(controller.take_notification(wire));
    auto end = decode(wire, crypto, &nonce);
    assert(end.kind == voice_input::NotificationKind::PttEnd);
    assert(end.sequence == 2);
    voice_input::EndSummary summary = {};
    assert(voice_input::decode_end_summary(
        end.payload.data(), end.payload_size, summary));
    assert(summary.generated_packets == 1);
    assert(summary.accepted_packets == 1);
    assert(summary.queue_overflows == 0);
    assert(summary.encode_failures == 0);
    assert(summary.captured_samples == 320);
    assert(summary.negotiated_mtu == 256);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 132);
    controller.poll(133);
    assert(controller.ui_state() == VoiceInputUiState::Ready);
}

void test_release_during_read_keeps_final_complete_block()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 200);

    voice_input::Notification wire = {};
    assert(controller.take_notification(wire));
    auto begin = decode(wire, crypto, nullptr);
    const auto nonce = begin.session_nonce;
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 201);

    backend.during_read = [&controller] { controller.handle_button(false, 220); };
    controller.poll(220);
    assert(controller.take_notification(wire));
    auto audio = decode(wire, crypto, &nonce);
    assert(audio.kind == voice_input::NotificationKind::Audio);
    assert(audio.sequence == 1);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 221);
    controller.poll(222);
    assert(controller.take_notification(wire));
    auto end = decode(wire, crypto, &nonce);
    assert(end.kind == voice_input::NotificationKind::PttEnd);
    assert(end.sequence == 2);
    assert(backend.read_calls == 1);
}

void test_duplicate_edges_disconnect_home_and_capture_failure_are_bounded()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 300);
    controller.handle_button(true, 301);
    assert(backend.acquire_calls == 1);
    controller.cancel_for_home(302);
    controller.cancel_for_home(303);
    assert(controller.ui_state() == VoiceInputUiState::Ready);
    assert(backend.release_calls == 1);

    controller.handle_button(false, 304);
    controller.handle_button(true, 305);
    controller.handle_disconnect();
    controller.handle_disconnect();
    assert(controller.ui_state() == VoiceInputUiState::Off);
    assert(backend.release_calls == 2);

    FakeBackend failed_backend;
    failed_backend.acquire_ok = false;
    FakeEntropy failed_entropy;
    VoiceInputController failed(failed_backend, crypto, failed_entropy);
    failed.set_ready(true, 256);
    failed.handle_button(true, 400);
    assert(failed.ui_state() == VoiceInputUiState::Error);
    failed.handle_button(false, 401);
}

void test_queue_overflow_emits_error_and_cleans_capture()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 500);
    for (int index = 0; index < 20; ++index) {
        controller.poll(static_cast<uint32_t>(501 + index));
    }
    assert(controller.ui_state() == VoiceInputUiState::Error);
    assert(backend.release_calls == 1);
    assert(controller.queued_audio_ms_for_test() <= 300);
    voice_input::Notification wire = {};
    assert(controller.take_notification(wire));
    const auto begin = decode(wire, crypto, nullptr);
    assert(begin.kind == voice_input::NotificationKind::PttBegin);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 521);
    assert(controller.take_notification(wire));
    const auto error = decode(wire, crypto, &begin.session_nonce);
    assert(error.kind == voice_input::NotificationKind::VoiceError);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 522);
    assert(!controller.take_notification(wire));
}

void test_consecutive_sessions_do_not_reuse_identity()
{
    FakeBackend backend;
    FakeEntropy entropy;
    entropy.values = {
        7, 0x04030201, 0x08070605,
        7, 8, 0x04030201, 0x08070605, 0x14131211, 0x18171615,
    };
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);

    controller.handle_button(true, 600);
    voice_input::Notification wire = {};
    assert(controller.take_notification(wire));
    const auto first = decode(wire, crypto, nullptr);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 601);
    controller.handle_button(false, 602);
    controller.poll(603);
    assert(controller.take_notification(wire));
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 604);

    controller.handle_button(true, 605);
    assert(controller.take_notification(wire));
    const auto second = decode(wire, crypto, nullptr);
    assert(first.session == 1);
    assert(second.session == 2);
    assert(second.session_nonce != first.session_nonce);

    controller.handle_connection_epoch(2);
    controller.set_ready(true, 256);
    controller.handle_button(false, 606);
    controller.handle_button(true, 607);
    assert(controller.take_notification(wire));
    const auto after_reconnect = decode(wire, crypto, nullptr);
    assert(after_reconnect.session == 1);
}

void test_session_exhaustion_requires_new_connection_epoch()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.set_next_connection_session_for_test(UINT16_MAX);

    controller.handle_button(true, 610);
    voice_input::Notification wire = {};
    assert(controller.take_notification(wire));
    auto begin = decode(wire, crypto, nullptr);
    assert(begin.session == UINT16_MAX);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 611);
    controller.handle_button(false, 612);
    controller.poll(613);
    assert(controller.take_notification(wire));
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 614);

    controller.handle_button(true, 615);
    assert(controller.ui_state() == VoiceInputUiState::Error);
    assert(!controller.take_notification(wire));
    controller.handle_button(false, 616);
    controller.handle_connection_epoch(3);
    controller.set_ready(true, 256);
    controller.handle_button(true, 617);
    assert(controller.take_notification(wire));
    begin = decode(wire, crypto, nullptr);
    assert(begin.session == 1);
}

void test_notification_failure_stops_capture_before_error()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 700);

    voice_input::Notification wire = {};
    assert(controller.take_notification(wire));
    const auto begin = decode(wire, crypto, nullptr);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 701);
    controller.poll(702);
    assert(controller.take_notification(wire));
    const auto audio = decode(wire, crypto, &begin.session_nonce);
    assert(audio.kind == voice_input::NotificationKind::Audio);

    controller.record_notification_result(
        wire, VoiceNotificationSendResult::PermanentFailure, 703);
    assert(controller.ui_state() == VoiceInputUiState::Error);
    assert(backend.release_calls == 1);
    assert(controller.take_notification(wire));
    const auto error = decode(wire, crypto, &begin.session_nonce);
    assert(error.kind == voice_input::NotificationKind::VoiceError);
    assert(error.sequence == 1);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 704);
    assert(!controller.take_notification(wire));
}

void test_rejected_begin_does_not_emit_undecodable_error()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 800);

    voice_input::Notification wire = {};
    assert(controller.take_notification(wire));
    assert(wire.kind == voice_input::NotificationKind::PttBegin);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::PermanentFailure, 801);
    assert(controller.ui_state() == VoiceInputUiState::Error);
    assert(backend.release_calls == 1);
    assert(!controller.take_notification(wire));
}

void test_congestion_retries_identical_notification_until_accepted()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 820);

    voice_input::Notification first = {};
    voice_input::Notification retry = {};
    assert(controller.take_notification(first));
    controller.record_notification_result(
        first, VoiceNotificationSendResult::Congested, 821);
    assert(controller.take_notification(retry));
    assert(retry.kind == first.kind);
    assert(retry.sequence == first.sequence);
    assert(retry.size == first.size);
    assert(std::equal(first.bytes.begin(), first.bytes.begin() + first.size,
                      retry.bytes.begin()));
    controller.record_notification_result(
        retry, VoiceNotificationSendResult::Accepted, 822);
    controller.poll(823);
    assert(controller.take_notification(retry));
    assert(retry.kind == voice_input::NotificationKind::Audio);
    assert(retry.sequence == 1);
}

void test_repeated_congestion_replaces_unsent_audio_with_contiguous_error()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 824);

    voice_input::Notification wire = {};
    assert(controller.take_notification(wire));
    const auto begin = decode(wire, crypto, nullptr);
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Accepted, 825);
    controller.poll(826);
    assert(controller.take_notification(wire));
    const auto audio = decode(wire, crypto, &begin.session_nonce);
    assert(audio.sequence == 1);

    for (int retry = 0; retry < 3; ++retry) {
        controller.record_notification_result(
            wire, VoiceNotificationSendResult::Congested,
            static_cast<uint32_t>(827 + retry));
        voice_input::Notification same = {};
        assert(controller.take_notification(same));
        assert(same.sequence == 1);
        wire = same;
    }
    controller.record_notification_result(
        wire, VoiceNotificationSendResult::Congested, 830);
    assert(controller.take_notification(wire));
    const auto error = decode(wire, crypto, &begin.session_nonce);
    assert(error.kind == voice_input::NotificationKind::VoiceError);
    assert(error.sequence == 1);
}

void test_release_after_failed_attempt_clears_error_state()
{
    FakeBackend backend;
    backend.acquire_ok = false;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 830);
    assert(controller.ui_state() == VoiceInputUiState::Error);
    controller.handle_button(false, 831);
    assert(controller.ui_state() == VoiceInputUiState::Ready);

    controller.handle_button(true, 832);
    controller.handle_disconnect();
    controller.handle_button(false, 833);
    assert(controller.ui_state() == VoiceInputUiState::Off);
}

void test_production_and_probe_pipeline_exclude_each_other()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController production(backend, crypto, entropy);
    VoiceCapturePipeline diagnostic(backend);
    VoiceCaptureError error = VoiceCaptureError::None;

    production.set_ready(true, 256);
    production.handle_button(true, 900);
    assert(!diagnostic.open(error));
    assert(error == VoiceCaptureError::CaptureBusy);

    production.cancel_for_home(901);
    error = VoiceCaptureError::None;
    assert(diagnostic.open(error));
    diagnostic.close();
}

void test_control_authentication_and_replay_guard()
{
    voice_input::crypto::CcmContext crypto(kKey);
    voice_input::ControlReplayGuard guard;
    voice_input::VoiceControl ready = {};
    ready.kind = voice_input::ControlKind::Ready;
    ready.exchange_nonce = {1, 2, 3, 4, 5, 6, 7, 8};
    ready.target_bps = voice_input::kTargetBitrate;
    std::array<uint8_t, voice_input::kControlSize> bytes = {};
    assert(voice_input::encode_control(
               ready, crypto, bytes.data(), bytes.size()) == bytes.size());
    voice_input::VoiceControl decoded = {};
    assert(guard.decode_once(bytes.data(), bytes.size(), crypto, decoded));
    assert(decoded.kind == voice_input::ControlKind::Ready);
    assert(!guard.decode_once(bytes.data(), bytes.size(), crypto, decoded));
    guard.reset();
    bytes.back() ^= 1;
    assert(!guard.decode_once(bytes.data(), bytes.size(), crypto, decoded));
    bytes.back() ^= 1;
    bytes[0] = 1;
    assert(!guard.decode_once(bytes.data(), bytes.size(), crypto, decoded));
}

void test_simultaneous_buttons_are_reserved_for_home()
{
    VoiceButtonEdgeGate gate;
    gate.reset(false);
    assert(gate.update(true, true, false, false, 10) == VoiceButtonAction::None);
    assert(gate.update(true, true, false, true, 11) == VoiceButtonAction::CancelHome);
    assert(gate.update(false, false, false, false, 12) == VoiceButtonAction::None);
}

void test_b_then_a_inside_guard_is_reserved_for_home()
{
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;
    VoiceButtonEdgeGate gate;
    gate.reset(false);
    assert(gate.update(false, true, false, false, 30) == VoiceButtonAction::None);
    assert(gate.update(true, true, false, false, 30 + guard - 1) ==
           VoiceButtonAction::None);
    assert(gate.update(true, true, false, true, 30 + guard) ==
           VoiceButtonAction::CancelHome);
    assert(gate.update(false, false, false, false, 31 + guard) ==
           VoiceButtonAction::None);
}

void test_a_then_b_is_reserved_for_home()
{
    VoiceButtonEdgeGate gate;
    gate.reset(false);
    assert(gate.update(true, false, false, false, 40) == VoiceButtonAction::None);
    assert(gate.update(true, true, false, false, 41) == VoiceButtonAction::None);
    assert(gate.update(true, true, false, true, 42) == VoiceButtonAction::CancelHome);
    assert(gate.update(false, false, false, false, 43) == VoiceButtonAction::None);
}

void test_short_b_tap_does_not_start_voice()
{
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;
    VoiceButtonEdgeGate gate;
    gate.reset(false);
    assert(gate.update(false, true, false, false, 100) == VoiceButtonAction::None);
    assert(gate.update(false, false, false, false, 100 + guard - 1) ==
           VoiceButtonAction::ShortClick);
}

void test_b_hold_starts_once_at_guard_boundary()
{
    static_assert(VoiceButtonEdgeGate::kChordGuardMs <= 100);
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;
    VoiceButtonEdgeGate gate;
    gate.reset(false);
    assert(gate.update(false, true, false, false, 200) == VoiceButtonAction::None);
    assert(gate.update(false, true, false, false, 200 + guard - 1) ==
           VoiceButtonAction::None);
    assert(gate.update(false, true, false, false, 200 + guard) ==
           VoiceButtonAction::Press);
    assert(gate.update(false, true, false, false, 201 + guard) ==
           VoiceButtonAction::None);
}

void test_b_release_is_only_sent_after_press()
{
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;
    VoiceButtonEdgeGate gate;
    gate.reset(false);
    assert(gate.update(false, true, false, false, 200) == VoiceButtonAction::None);
    assert(gate.update(false, true, false, false, 200 + guard) ==
           VoiceButtonAction::Press);
    assert(gate.update(false, false, false, false, 202 + guard) ==
           VoiceButtonAction::Release);

    gate.reset(false);
    assert(gate.update(false, true, false, false, 300) == VoiceButtonAction::None);
    assert(gate.update(false, false, false, false, 301) == VoiceButtonAction::ShortClick);
}

void test_late_a_cancels_active_voice_before_home()
{
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;
    VoiceButtonEdgeGate gate;
    gate.reset(false);
    assert(gate.update(false, true, false, false, 400) == VoiceButtonAction::None);
    assert(gate.update(false, true, false, false, 400 + guard) ==
           VoiceButtonAction::Press);
    assert(gate.update(true, true, false, false, 401 + guard) ==
           VoiceButtonAction::CancelVoice);
    assert(gate.update(true, true, false, true, 402 + guard) ==
           VoiceButtonAction::CancelHome);
    assert(gate.update(false, false, false, false, 403 + guard) ==
           VoiceButtonAction::None);
}

void test_wake_consumed_b_is_suppressed_through_release()
{
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;
    VoiceButtonEdgeGate gate;
    gate.reset(false);
    assert(gate.update(false, true, true, false, 500) == VoiceButtonAction::None);
    assert(gate.update(false, true, false, false, 500 + guard) ==
           VoiceButtonAction::None);
    assert(gate.update(false, false, false, false, 501 + guard) ==
           VoiceButtonAction::None);
}

void test_b_held_on_entry_requires_release_and_new_press()
{
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;
    VoiceButtonEdgeGate gate;
    gate.reset(true);
    assert(gate.update(false, true, false, false, 600 + guard) ==
           VoiceButtonAction::None);
    assert(gate.update(false, false, false, false, 601 + guard) ==
           VoiceButtonAction::None);
    assert(gate.update(false, true, false, false, 700) == VoiceButtonAction::None);
    assert(gate.update(false, true, false, false, 700 + guard) ==
           VoiceButtonAction::Press);
}

void test_dispatcher_suppresses_wake_b_until_release()
{
    FakeVoiceButtonSink sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;

    assert(dispatch(dispatcher, sink, false, true, true, false, 10) ==
           VoiceButtonDispatchDecision::Continue);
    assert(dispatch(dispatcher, sink, false, true, false, false, 10 + guard) ==
           VoiceButtonDispatchDecision::Continue);
    assert(dispatch(dispatcher, sink, false, false, false, false, 11 + guard) ==
           VoiceButtonDispatchDecision::Continue);
    assert(sink.events.empty());
}

void test_dispatcher_reserves_all_home_chord_orders_without_press()
{
    const auto verify_home = [](bool start_left, bool start_right, bool add_left, bool add_right) {
        FakeVoiceButtonSink sink;
        VoiceButtonDispatcher dispatcher;
        dispatcher.reset(false);
        assert(dispatch(dispatcher, sink, start_left, start_right, false, false, 20) ==
               VoiceButtonDispatchDecision::Continue);
        assert(dispatch(dispatcher, sink, add_left, add_right, false, true, 21) ==
               VoiceButtonDispatchDecision::Close);
        assert(sink.events.size() == 1);
        assert(std::string(sink.events[0]) == "cancel");
    };

    verify_home(true, true, true, true);
    verify_home(false, true, true, true);
    verify_home(true, false, true, true);
}

void test_dispatcher_normal_hold_presses_and_releases_once()
{
    FakeVoiceButtonSink sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;

    assert(dispatch(dispatcher, sink, false, true, false, false, 100) ==
           VoiceButtonDispatchDecision::Continue);
    assert(dispatch(dispatcher, sink, false, true, false, false, 100 + guard) ==
           VoiceButtonDispatchDecision::Continue);
    assert(dispatch(dispatcher, sink, false, true, false, false, 101 + guard) ==
           VoiceButtonDispatchDecision::Continue);
    assert(dispatch(dispatcher, sink, false, false, false, false, 102 + guard) ==
           VoiceButtonDispatchDecision::Continue);
    assert(sink.events.size() == 2);
    assert(std::string(sink.events[0]) == "press");
    assert(std::string(sink.events[1]) == "release");
}

void test_dispatcher_late_a_cancels_before_close()
{
    FakeVoiceButtonSink sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;

    dispatch(dispatcher, sink, false, true, false, false, 200);
    dispatch(dispatcher, sink, false, true, false, false, 200 + guard);
    assert(dispatch(dispatcher, sink, true, true, false, false, 201 + guard) ==
           VoiceButtonDispatchDecision::Continue);
    assert(dispatch(dispatcher, sink, true, true, false, true, 202 + guard) ==
           VoiceButtonDispatchDecision::Close);
    assert(sink.events.size() == 3);
    assert(std::string(sink.events[0]) == "press");
    assert(std::string(sink.events[1]) == "cancel");
    assert(std::string(sink.events[2]) == "cancel");
}

void test_dispatcher_entry_held_and_short_tap_do_not_start()
{
    FakeVoiceButtonSink sink;
    VoiceButtonDispatcher dispatcher;
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;
    dispatcher.reset(true);
    dispatch(dispatcher, sink, false, true, false, false, 300 + guard);
    dispatch(dispatcher, sink, false, false, false, false, 301 + guard);
    dispatch(dispatcher, sink, false, true, false, false, 400);
    dispatch(dispatcher, sink, false, false, false, false, 400 + guard - 1);
    assert(sink.events.empty());
}

void test_dispatcher_delays_single_click_and_emits_submit_once()
{
    FakeVoiceButtonSink voice_sink;
    FakeCodexButtonActionSink action_sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);

    dispatcher.dispatch(false, true, false, false, 100, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 120, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 419, &voice_sink, &action_sink);
    assert(action_sink.actions.empty());
    dispatcher.dispatch(false, false, false, false, 420, &voice_sink, &action_sink);

    assert(voice_sink.events.empty());
    assert(action_sink.actions.size() == 1);
    assert(action_sink.actions[0] == CodexButtonAction::Submit);
    assert(action_sink.times[0] == 420);
}

void test_dispatcher_double_click_emits_only_force_steer()
{
    FakeVoiceButtonSink voice_sink;
    FakeCodexButtonActionSink action_sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);

    dispatcher.dispatch(false, true, false, false, 500, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 520, &voice_sink, &action_sink);
    dispatcher.dispatch(false, true, false, false, 600, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 620, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 900, &voice_sink, &action_sink);

    assert(voice_sink.events.empty());
    assert(action_sink.actions.size() == 1);
    assert(action_sink.actions[0] == CodexButtonAction::ForceSteer);
    assert(action_sink.times[0] == 620);
}

void test_dispatcher_second_click_hold_cancels_pending_submit_and_starts_voice()
{
    FakeVoiceButtonSink voice_sink;
    FakeCodexButtonActionSink action_sink;
    VoiceButtonDispatcher dispatcher;
    constexpr uint32_t guard = VoiceButtonEdgeGate::kChordGuardMs;
    dispatcher.reset(false);

    dispatcher.dispatch(false, true, false, false, 1000, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 1020, &voice_sink, &action_sink);
    dispatcher.dispatch(false, true, false, false, 1100, &voice_sink, &action_sink);
    dispatcher.dispatch(
        false, true, false, false, 1100 + guard, &voice_sink, &action_sink);
    dispatcher.dispatch(
        false, false, false, false, 1101 + guard, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 1400, &voice_sink, &action_sink);

    assert(action_sink.actions.empty());
    assert(voice_sink.events.size() == 2);
    assert(std::string(voice_sink.events[0]) == "press");
    assert(std::string(voice_sink.events[1]) == "release");
}

void test_dispatcher_wake_and_home_clear_pending_click()
{
    FakeVoiceButtonSink voice_sink;
    FakeCodexButtonActionSink action_sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);

    dispatcher.dispatch(false, true, false, false, 1500, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 1520, &voice_sink, &action_sink);
    dispatcher.dispatch(false, true, true, false, 1600, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 1900, &voice_sink, &action_sink);
    assert(action_sink.actions.empty());

    dispatcher.dispatch(false, true, false, false, 2000, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 2020, &voice_sink, &action_sink);
    assert(dispatcher.dispatch(
               true, true, false, true, 2100, &voice_sink, &action_sink) ==
           VoiceButtonDispatchDecision::Close);
    assert(action_sink.actions.empty());
}

void test_dispatcher_a_plus_b_cancels_expiring_single_before_home_event()
{
    FakeVoiceButtonSink voice_sink;
    FakeCodexButtonActionSink action_sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);

    dispatcher.dispatch(false, true, false, false, 0, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 20, &voice_sink, &action_sink);
    dispatcher.dispatch(true, true, false, false, 320, &voice_sink, &action_sink);

    assert(voice_sink.events.empty());
    assert(action_sink.actions.empty());
}

void test_dispatcher_independent_a_click_preserves_pending_b_submit()
{
    FakeVoiceButtonSink voice_sink;
    FakeCodexButtonActionSink action_sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);

    dispatcher.dispatch(false, true, false, false, 0, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 20, &voice_sink, &action_sink);
    dispatcher.dispatch(true, false, false, false, 100, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 120, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 320, &voice_sink, &action_sink);

    assert(voice_sink.events.empty());
    assert(action_sink.actions.size() == 1);
    assert(action_sink.actions[0] == CodexButtonAction::Submit);
    assert(action_sink.times[0] == 320);
}

void test_dispatcher_cross_release_suppression_clears_latched_second_click()
{
    FakeVoiceButtonSink voice_sink;
    FakeCodexButtonActionSink action_sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);

    dispatcher.dispatch(false, true, false, false, 0, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 20, &voice_sink, &action_sink);
    dispatcher.dispatch(false, true, false, false, 100, &voice_sink, &action_sink);
    dispatcher.dispatch(true, false, false, false, 120, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 140, &voice_sink, &action_sink);

    dispatcher.dispatch(false, true, false, false, 1100, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 1120, &voice_sink, &action_sink);
    assert(action_sink.actions.empty());
    dispatcher.dispatch(false, false, false, false, 1420, &voice_sink, &action_sink);

    assert(voice_sink.events.empty());
    assert(action_sink.actions.size() == 1);
    assert(action_sink.actions[0] == CodexButtonAction::Submit);
    assert(action_sink.times[0] == 1420);
}

void test_dispatcher_global_input_suppression_clears_pending_gestures()
{
    FakeVoiceButtonSink voice_sink;
    FakeCodexButtonActionSink action_sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);

    dispatcher.dispatch(false, true, false, false, 0, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 20, &voice_sink, &action_sink);
    dispatcher.dispatch(
        false, false, false, false, 100, &voice_sink, &action_sink, true);
    dispatcher.dispatch(
        false, false, false, false, 320, &voice_sink, &action_sink, false);
    assert(action_sink.actions.empty());

    dispatcher.dispatch(false, true, false, false, 1000, &voice_sink, &action_sink);
    dispatcher.dispatch(false, false, false, false, 1020, &voice_sink, &action_sink);
    dispatcher.dispatch(false, true, false, false, 1100, &voice_sink, &action_sink);
    dispatcher.dispatch(
        false, true, false, false, 1120, &voice_sink, &action_sink, true);
    dispatcher.dispatch(
        false, false, false, false, 1140, &voice_sink, &action_sink, false);
    dispatcher.dispatch(
        false, false, false, false, 1500, &voice_sink, &action_sink, false);

    assert(voice_sink.events.empty());
    assert(action_sink.actions.empty());
}

void test_dispatcher_guard_handles_uint32_wraparound()
{
    FakeVoiceButtonSink sink;
    VoiceButtonDispatcher dispatcher;
    dispatcher.reset(false);
    constexpr uint32_t start = UINT32_MAX - 44;

    dispatch(dispatcher, sink, false, true, false, false, start);
    dispatch(dispatcher, sink, false, true, false, false, 44);
    assert(sink.events.empty());
    dispatch(dispatcher, sink, false, true, false, false, 45);
    assert(sink.events.size() == 1);
    assert(std::string(sink.events[0]) == "press");
    assert(sink.times[0] == 45);
}

void test_cancel_waits_for_inflight_producer_cleanup()
{
    FakeBackend backend;
    FakeEntropy entropy;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_ready(true, 256);
    controller.handle_button(true, 950);

    std::mutex mutex;
    std::condition_variable condition;
    bool first_read_entered = false;
    bool release_first_read = false;
    std::atomic<int> read_entries{0};
    backend.during_read = [&] {
        if (read_entries.fetch_add(1) != 0) {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        first_read_entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return release_first_read; });
    };

    std::thread producer([&] { controller.poll(951); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return first_read_entered; });
    }
    std::thread cancel([&] { controller.cancel_for_home(952); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(backend.release_calls.load() == 0);
    assert(read_entries.load() == 1);
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first_read = true;
        condition.notify_all();
    }
    producer.join();
    cancel.join();
    assert(backend.release_calls.load() == 1);
}

}  // namespace

int main()
{
    test_requires_ready_subscription_and_audio_mtu();
    test_begin_audio_end_order_and_counters();
    test_release_during_read_keeps_final_complete_block();
    test_duplicate_edges_disconnect_home_and_capture_failure_are_bounded();
    test_queue_overflow_emits_error_and_cleans_capture();
    test_consecutive_sessions_do_not_reuse_identity();
    test_session_exhaustion_requires_new_connection_epoch();
    test_notification_failure_stops_capture_before_error();
    test_rejected_begin_does_not_emit_undecodable_error();
    test_congestion_retries_identical_notification_until_accepted();
    test_repeated_congestion_replaces_unsent_audio_with_contiguous_error();
    test_release_after_failed_attempt_clears_error_state();
    test_production_and_probe_pipeline_exclude_each_other();
    test_control_authentication_and_replay_guard();
    test_simultaneous_buttons_are_reserved_for_home();
    test_b_then_a_inside_guard_is_reserved_for_home();
    test_a_then_b_is_reserved_for_home();
    test_short_b_tap_does_not_start_voice();
    test_b_hold_starts_once_at_guard_boundary();
    test_b_release_is_only_sent_after_press();
    test_late_a_cancels_active_voice_before_home();
    test_wake_consumed_b_is_suppressed_through_release();
    test_b_held_on_entry_requires_release_and_new_press();
    test_dispatcher_suppresses_wake_b_until_release();
    test_dispatcher_reserves_all_home_chord_orders_without_press();
    test_dispatcher_normal_hold_presses_and_releases_once();
    test_dispatcher_late_a_cancels_before_close();
    test_dispatcher_entry_held_and_short_tap_do_not_start();
    test_dispatcher_delays_single_click_and_emits_submit_once();
    test_dispatcher_double_click_emits_only_force_steer();
    test_dispatcher_second_click_hold_cancels_pending_submit_and_starts_voice();
    test_dispatcher_wake_and_home_clear_pending_click();
    test_dispatcher_a_plus_b_cancels_expiring_single_before_home_event();
    test_dispatcher_independent_a_click_preserves_pending_b_submit();
    test_dispatcher_cross_release_suppression_clears_latched_second_click();
    test_dispatcher_global_input_suppression_clears_pending_gestures();
    test_dispatcher_guard_handles_uint32_wraparound();
    test_cancel_waits_for_inflight_producer_cleanup();
}
