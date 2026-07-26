/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_assistant.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <hal/audio/stopwatch_audio_backend.h>
#include <mooncake_log.h>
#include <runtime/assistant/assistant_runtime.h>
#include <runtime/assistant/assistant_trace.h>
#include <runtime/display/global_display_protection_service.h>
#include <runtime/runtime_supervisor.h>
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
#include <esp_log.h>
#endif

#include <utility>
#include <vector>

using namespace mooncake;

namespace {
bool isConversationEvent(AssistantEventKind kind)
{
    return kind == AssistantEventKind::ActivationCode ||
           kind == AssistantEventKind::UserText ||
           kind == AssistantEventKind::AgentText ||
           kind == AssistantEventKind::Emotion ||
           kind == AssistantEventKind::WakeDetected ||
           kind == AssistantEventKind::ListeningStatus ||
           kind == AssistantEventKind::SpeakingStatus;
}
} // namespace

AppAssistant::AppAssistant()
{
    setAppInfo().name = "Assistant";
    setAppInfo().icon = (void*)&icon_assistant;
}

void AppAssistant::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppAssistant::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();
    _close_controller.reset();
    _presentation.reset();
    _display_activity.reset();
    GetGlobalDisplayProtectionService().setKeepAwake(
        DisplayKeepAwakeReason::AssistantInteraction, false);
    _last_telemetry_ms = 0;
    _retry_available = false;
    _has_conversation_text = false;
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
    _debug_close_pending = false;
    if (!RegisterDebugAssistantTarget(this)) {
        ESP_LOGE("DebugCommand", "failed to register Assistant debug target");
    }
#endif

    {
        LvglLockGuard lock;
        _view = std::make_unique<AssistantView>();
        _view->init(lv_screen_active());
    }

    auto& runtime = GetRuntimeSupervisor();
    GetAssistantTrace().record({
        .session_id = GetAssistantRuntime().sessionId() + 1,
        .timestamp_ms = GetHAL().millis(),
        .component = AssistantTraceComponent::App,
        .event = AssistantTraceEvent::Open,
        .state = runtime.assistantState(),
        .result = AssistantTraceResult::Accepted,
    });
    runtime.startAssistant();
}

void AppAssistant::onRunning()
{
    const uint32_t now = GetHAL().millis();
    const auto input_levels = GetStopWatchAudioBackend().inputLevels();
    {
        LvglLockGuard lock;
        if (_view) {
            _view->setInputLevels(input_levels);
            _view->update(now);
        }
    }
    updateTelemetry(now);

    auto& runtime = GetRuntimeSupervisor();
    drainRuntimeEvents();
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
    if (_debug_close_pending) {
        _debug_close_pending = false;
        requestGracefulClose(AssistantCloseTrigger::DebugHome);
    }
#endif
    const auto key = _key_manager ? _key_manager->update() : input::KeyEvent::None;

    if (key == input::KeyEvent::GoHome) {
        requestGracefulClose(AssistantCloseTrigger::PhysicalHome);
    }

    if (key == input::KeyEvent::GoNext && _retry_available && !_close_controller.requested() &&
        runtime.assistantState() == AssistantRuntimeState::Stopped) {
        _retry_available = false;
        _has_conversation_text = false;
        _display_activity.observe(AssistantDisplayActivitySignal::Idle, true, now);
        {
            LvglLockGuard lock;
            applyPresentationSignal(AssistantPresentationSignal::Connecting);
        }
        runtime.startAssistant();
    }

    if (key == input::KeyEvent::GoNext &&
        runtime.assistantState() == AssistantRuntimeState::StopTimedOut) {
        GetRuntimeSupervisor().requestStopAssistant();
    }

    syncDisplayKeepAwake(now);
    if (_close_controller.requested() && runtime.assistantState() == AssistantRuntimeState::Stopped) {
        close();
    }
}

bool AppAssistant::requestGracefulClose(AssistantCloseTrigger trigger)
{
    auto& runtime = GetRuntimeSupervisor();
    const uint64_t session_id = GetAssistantRuntime().sessionId();
    if (!_close_controller.request(trigger)) {
        GetAssistantTrace().record({
            .session_id = session_id,
            .timestamp_ms = GetHAL().millis(),
            .component = AssistantTraceComponent::App,
            .event = AssistantTraceEvent::CloseRequest,
            .state = runtime.assistantState(),
            .detail = trigger == AssistantCloseTrigger::PhysicalHome
                          ? AssistantTraceDetail::PhysicalHome
                          : AssistantTraceDetail::DebugHome,
            .result = AssistantTraceResult::Duplicate,
        });
        return false;
    }

    _display_activity.observe(AssistantDisplayActivitySignal::Idle, true,
                              GetHAL().millis());

    {
        LvglLockGuard lock;
        applyPresentationSignal(AssistantPresentationSignal::Closing);
    }

    // Record the user-visible close intent before requestStopAssistant() can
    // synchronously emit the Stopping transition. This keeps the trace in
    // causal order and distinguishes app-level de-duplication from a runtime
    // rejection.
    GetAssistantTrace().record({
        .session_id = session_id,
        .timestamp_ms = GetHAL().millis(),
        .component = AssistantTraceComponent::App,
        .event = AssistantTraceEvent::CloseRequest,
        .state = runtime.assistantState(),
        .detail = trigger == AssistantCloseTrigger::PhysicalHome
                      ? AssistantTraceDetail::PhysicalHome
                      : AssistantTraceDetail::DebugHome,
        .result = AssistantTraceResult::Accepted,
    });

    const bool accepted = runtime.requestStopAssistant();
    if (!accepted) {
        GetAssistantTrace().record({
            .session_id = session_id,
            .timestamp_ms = GetHAL().millis(),
            .component = AssistantTraceComponent::App,
            .event = AssistantTraceEvent::Error,
            .state = runtime.assistantState(),
            .detail = AssistantTraceDetail::Stop,
            .result = AssistantTraceResult::Rejected,
        });
    }
    return accepted;
}

void AppAssistant::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
    UnregisterDebugAssistantTarget(this);
    _debug_close_pending = false;
#endif

    AssistantTraceDetail close_detail = AssistantTraceDetail::ForceHome;
    if (_close_controller.trigger() == AssistantCloseTrigger::PhysicalHome) {
        close_detail = AssistantTraceDetail::PhysicalHome;
    } else if (_close_controller.trigger() == AssistantCloseTrigger::DebugHome) {
        close_detail = AssistantTraceDetail::DebugHome;
    }

    GetAssistantTrace().record({
        .session_id = GetAssistantRuntime().sessionId(),
        .timestamp_ms = GetHAL().millis(),
        .component = AssistantTraceComponent::App,
        .event = AssistantTraceEvent::Closed,
        .state = GetRuntimeSupervisor().assistantState(),
        .detail = close_detail,
        .result = AssistantTraceResult::Ok,
    });

    // Mooncake can close the app without going through the physical-key path
    // (for example, the USB debug launcher). Keep shutdown idempotent and let
    // the global runtime pump finish the asynchronous stop after the UI is gone.
    GetRuntimeSupervisor().requestStopAssistant();

    _key_manager.reset();
    _last_telemetry_ms = 0;
    _has_conversation_text = false;
    _display_activity.reset();
    GetGlobalDisplayProtectionService().setKeepAwake(
        DisplayKeepAwakeReason::AssistantInteraction, false);

    LvglLockGuard lock;
    if (_view) {
        _view->destroy();
        _view.reset();
    }
}

#if CONFIG_STOPWATCH_DEBUG_COMMANDS
bool AppAssistant::requestDebugGracefulHome()
{
    if (_debug_close_pending || _close_controller.requested()) {
        return false;
    }
    _debug_close_pending = true;
    return true;
}

#endif

void AppAssistant::drainRuntimeEvents()
{
    auto& runtime = GetRuntimeSupervisor();
    std::vector<AssistantEvent> events;
    AssistantEvent event;
    while (runtime.pollAssistantEvent(event)) {
        events.push_back(std::move(event));
    }
    if (events.empty()) {
        return;
    }

    const auto runtime_state = runtime.assistantState();
    LvglLockGuard lock;
    for (const auto& queued_event : events) {
        applyRuntimeEvent(queued_event, runtime_state);
    }
}

void AppAssistant::applyRuntimeEvent(const AssistantEvent& event,
                                     AssistantRuntimeState runtime_state)
{
    if (!_view) {
        return;
    }
    if (isConversationEvent(event.kind) && _close_controller.requested()) {
        return;
    }

    switch (event.kind) {
        case AssistantEventKind::State:
            if (event.text == "Stopped" && _retry_available && !_close_controller.requested()) {
                // A synchronous start failure publishes Error followed by Stopped. Keep the
                // actionable failure page visible instead of replacing it with a normal state.
                return;
            }
            if (_close_controller.requested() && event.text != "Stopping" && event.text != "Stopped") {
                return;
            }
            if (event.text == "Starting" || event.text == "Connecting") {
                _display_activity.observe(AssistantDisplayActivitySignal::Idle, true,
                                          GetHAL().millis());
                _has_conversation_text = false;
                applyPresentationSignal(AssistantPresentationSignal::Connecting);
            } else if (event.text == "Activating" || event.text == "Activated") {
                applyPresentationSignal(AssistantPresentationSignal::Activating);
            } else if (event.text == "Running") {
                _display_activity.observe(AssistantDisplayActivitySignal::Idle, true,
                                          GetHAL().millis());
                _has_conversation_text = false;
                applyPresentationSignal(AssistantPresentationSignal::Running);
            } else if (event.text == "Stopping") {
                _display_activity.observe(AssistantDisplayActivitySignal::Idle, true,
                                          GetHAL().millis());
                applyPresentationSignal(AssistantPresentationSignal::Closing);
            } else if (event.text == "Stopped") {
                _display_activity.observe(AssistantDisplayActivitySignal::Idle, true,
                                          GetHAL().millis());
            }
            break;
        case AssistantEventKind::ActivationCode:
            applyPresentationSignal(AssistantPresentationSignal::Activating);
            _view->setConversationText(event.text.c_str());
            break;
        case AssistantEventKind::Error:
            _display_activity.observe(AssistantDisplayActivitySignal::Idle, true,
                                      GetHAL().millis());
            if (runtime_state == AssistantRuntimeState::StopTimedOut) {
                applyPresentationSignal(AssistantPresentationSignal::StopTimedOut);
            } else if (_close_controller.requested()) {
                applyPresentationSignal(AssistantPresentationSignal::Closing);
            } else {
                applyPresentationSignal(AssistantPresentationSignal::Failed);
                _retry_available = runtime_state == AssistantRuntimeState::Stopped;
            }
            _view->setConversationText(event.text.c_str());
            break;
        case AssistantEventKind::UserText:
            _display_activity.observe(AssistantDisplayActivitySignal::UserText, true,
                                      GetHAL().millis());
            _has_conversation_text = true;
            applyPresentationSignal(AssistantPresentationSignal::UserText);
            _view->setConversationText(event.text.c_str());
            break;
        case AssistantEventKind::AgentText:
            _display_activity.observe(AssistantDisplayActivitySignal::AgentText, true,
                                      GetHAL().millis());
            _has_conversation_text = true;
            applyPresentationSignal(AssistantPresentationSignal::AgentText);
            _view->setConversationText(event.text.c_str());
            break;
        case AssistantEventKind::Emotion:
            if (!event.text.empty()) {
                _display_activity.observe(AssistantDisplayActivitySignal::Emotion, true,
                                          GetHAL().millis());
            }
            if (!_has_conversation_text && !event.text.empty()) {
                _view->setConversationText(event.text.c_str());
            }
            break;
        case AssistantEventKind::UpdateAvailable:
            _view->setUpdateVersion(event.text.c_str());
            break;
        case AssistantEventKind::WakeDetected:
            _display_activity.observe(AssistantDisplayActivitySignal::WakeDetected, true,
                                      GetHAL().millis());
            applyPresentationSignal(AssistantPresentationSignal::WakeDetected);
            break;
        case AssistantEventKind::ListeningStatus:
            _display_activity.observe(AssistantDisplayActivitySignal::Listening,
                                      event.active, GetHAL().millis());
            applyPresentationSignal(AssistantPresentationSignal::ListeningChanged, event.active);
            break;
        case AssistantEventKind::SpeakingStatus: {
            _display_activity.observe(AssistantDisplayActivitySignal::Speaking,
                                      event.active, GetHAL().millis());
            const AssistantVisualState previous_state = _presentation.state();
            applyPresentationSignal(AssistantPresentationSignal::SpeakingChanged, event.active);
            if (!event.active &&
                previous_state == AssistantVisualState::Speaking &&
                _presentation.state() == AssistantVisualState::WaitingWake) {
                _has_conversation_text = false;
            }
            break;
        }
    }
}

void AppAssistant::syncDisplayKeepAwake(uint32_t now_ms)
{
    GetGlobalDisplayProtectionService().setKeepAwake(
        DisplayKeepAwakeReason::AssistantInteraction,
        _display_activity.active(now_ms));
}

void AppAssistant::applyPresentationSignal(AssistantPresentationSignal signal, bool active)
{
    const AssistantVisualState previous_state = _presentation.state();
    _presentation.apply(signal, active);
    if (!_view) {
        return;
    }
    if (previous_state != _presentation.state()) {
        _view->setState(_presentation.state());
    }
    if (signal == AssistantPresentationSignal::Connecting) {
        _view->setConnected(false);
    } else if (signal == AssistantPresentationSignal::Activating ||
               signal == AssistantPresentationSignal::Running) {
        _view->setConnected(true);
    }
}

void AppAssistant::updateTelemetry(uint32_t now_ms)
{
    if (_view == nullptr ||
        static_cast<uint32_t>(now_ms - _last_telemetry_ms) < 1000) {
        return;
    }
    _last_telemetry_ms = now_ms;
    const uint8_t battery = GetHAL().getBatteryLevel();
    const bool charging = GetHAL().isBatteryCharging();
    LvglLockGuard lock;
    if (_view) {
        _view->setBattery(battery, charging);
    }
}
