/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "assistant_close_controller.h"
#include "view/assistant_view.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <runtime/assistant/assistant_event_mailbox.h>
#include <runtime/assistant/assistant_state.h>
#include <sdkconfig.h>
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
#include <debug/debug_assistant_control.h>
#endif
#include <memory>

class AppAssistant : public mooncake::AppAbility
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
                   , public DebugAssistantTarget
#endif
{
public:
    AppAssistant();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
    bool requestDebugGracefulHome() override;
    void logDebugAssistantStatus() override;
#endif

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<AssistantView> _view;
    AssistantCloseController _close_controller;
    bool _retry_available = false;
#if CONFIG_STOPWATCH_DEBUG_COMMANDS
    bool _debug_close_pending = false;
#endif

    bool requestGracefulClose(AssistantCloseTrigger trigger);
    void drainRuntimeEvents();
    void applyRuntimeEvent(const AssistantEvent& event, AssistantRuntimeState runtime_state);
};
