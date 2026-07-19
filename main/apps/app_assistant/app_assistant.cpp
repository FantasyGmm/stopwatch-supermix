/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_assistant.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <runtime/assistant/assistant_runtime.h>
#include <runtime/assistant/assistant_trace.h>
#include <runtime/runtime_supervisor.h>
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
#include <esp_log.h>
#endif

#include <utility>
#include <vector>
#include <cinttypes>

using namespace mooncake;

namespace {
bool isConversationEvent(AssistantEventKind kind)
{
    return kind == AssistantEventKind::ActivationCode ||
           kind == AssistantEventKind::UserText ||
           kind == AssistantEventKind::AgentText ||
           kind == AssistantEventKind::Emotion;
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
    _retry_available = false;
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
        {
            LvglLockGuard lock;
            _view->setState(AssistantViewState::Connecting);
        }
        runtime.startAssistant();
    }

    if (key == input::KeyEvent::GoNext &&
        runtime.assistantState() == AssistantRuntimeState::StopTimedOut) {
        GetRuntimeSupervisor().requestStopAssistant();
    }

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

    {
        LvglLockGuard lock;
        _view->setState(AssistantViewState::Closing);
    }
    const bool accepted = runtime.requestStopAssistant();
    GetAssistantTrace().record({
        .session_id = session_id,
        .timestamp_ms = GetHAL().millis(),
        .component = AssistantTraceComponent::App,
        .event = AssistantTraceEvent::CloseRequest,
        .state = runtime.assistantState(),
        .detail = trigger == AssistantCloseTrigger::PhysicalHome
                      ? AssistantTraceDetail::PhysicalHome
                      : AssistantTraceDetail::DebugHome,
        .result = accepted ? AssistantTraceResult::Accepted : AssistantTraceResult::Rejected,
    });
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

void AppAssistant::logDebugAssistantStatus()
{
    const auto snapshot = GetRuntimeSupervisor().diagnostics();
    ESP_LOGI("DebugCommand",
             "ASTATUS sid=%" PRIu64 " state=%s bindings=%u subscriptions=%u wifi=%u capture=%u "
             "playback=%u ap=%u blocked=%u internal=%u psram=%u tasks=%u",
             GetAssistantRuntime().sessionId(), AssistantTraceName(snapshot.assistant_state),
             static_cast<unsigned>(snapshot.assistant_bindings),
             static_cast<unsigned>(snapshot.assistant_subscriptions),
             static_cast<unsigned>(snapshot.resources.wifi_lease_count),
             static_cast<unsigned>(snapshot.resources.capture_owner),
             static_cast<unsigned>(snapshot.resources.playback_owner),
             static_cast<unsigned>(snapshot.resources.wifi_ap_active),
             static_cast<unsigned>(snapshot.resources.blocked),
             static_cast<unsigned>(snapshot.internal_free_heap),
             static_cast<unsigned>(snapshot.psram_free),
             static_cast<unsigned>(snapshot.task_count));
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
            if (event.text == "Starting") {
                _view->setState(AssistantViewState::Connecting);
            } else if (event.text == "Activating" || event.text == "Activated") {
                _view->setState(AssistantViewState::Activating);
            } else if (event.text == "Running") {
                _view->setState(AssistantViewState::Listening);
            } else if (event.text == "Stopping") {
                _view->setState(AssistantViewState::Closing);
            }
            break;
        case AssistantEventKind::ActivationCode:
            _view->setState(AssistantViewState::Activating);
            _view->setPrimaryText("Activation code");
            _view->setSecondaryText(event.text.c_str());
            break;
        case AssistantEventKind::Error:
            if (runtime_state == AssistantRuntimeState::StopTimedOut) {
                _view->setState(AssistantViewState::StopTimedOut);
            } else if (_close_controller.requested()) {
                _view->setState(AssistantViewState::Closing);
                _view->setSecondaryText(event.text.c_str());
            } else {
                _view->setState(AssistantViewState::Failed);
                _view->setSecondaryText(event.text.c_str());
                _retry_available = runtime_state == AssistantRuntimeState::Stopped;
            }
            break;
        case AssistantEventKind::UserText:
            _view->setState(AssistantViewState::Thinking);
            _view->setSecondaryText(event.text.c_str());
            break;
        case AssistantEventKind::AgentText:
            _view->setState(AssistantViewState::Speaking);
            _view->setSecondaryText(event.text.c_str());
            break;
        case AssistantEventKind::Emotion:
            _view->setPrimaryText("Emotion");
            _view->setSecondaryText(event.text.c_str());
            break;
    }
}
