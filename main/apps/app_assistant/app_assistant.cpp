/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_assistant.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <runtime/runtime_supervisor.h>

#include <utility>
#include <vector>

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
    _close_requested = false;
    _retry_available = false;

    {
        LvglLockGuard lock;
        _view = std::make_unique<AssistantView>();
        _view->init(lv_screen_active());
    }

    GetRuntimeSupervisor().startAssistant();
}

void AppAssistant::onRunning()
{
    auto& runtime = GetRuntimeSupervisor();
    runtime.update(GetHAL().millis());
    drainRuntimeEvents();
    const auto key = _key_manager ? _key_manager->update() : input::KeyEvent::None;

    if (key == input::KeyEvent::GoHome && !_close_requested) {
        _close_requested = true;
        {
            LvglLockGuard lock;
            _view->setState(AssistantViewState::Closing);
        }
        GetRuntimeSupervisor().requestStopAssistant();
    }

    if (key == input::KeyEvent::GoNext && _retry_available && !_close_requested &&
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

    if (_close_requested && runtime.assistantState() == AssistantRuntimeState::Stopped) {
        close();
    }
}

void AppAssistant::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;
    if (_view) {
        _view->destroy();
        _view.reset();
    }
}

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
    if (isConversationEvent(event.kind) && _close_requested) {
        return;
    }

    switch (event.kind) {
        case AssistantEventKind::State:
            if (event.text == "Stopped" && _retry_available && !_close_requested) {
                // A synchronous start failure publishes Error followed by Stopped. Keep the
                // actionable failure page visible instead of replacing it with a normal state.
                return;
            }
            if (_close_requested && event.text != "Stopping" && event.text != "Stopped") {
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
            } else if (_close_requested) {
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
