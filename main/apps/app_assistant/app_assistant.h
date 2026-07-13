/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/assistant_view.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <runtime/assistant/assistant_event_mailbox.h>
#include <runtime/assistant/assistant_state.h>
#include <memory>

class AppAssistant : public mooncake::AppAbility {
public:
    AppAssistant();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<AssistantView> _view;
    bool _close_requested = false;
    bool _retry_available = false;

    void drainRuntimeEvents();
    void applyRuntimeEvent(const AssistantEvent& event, AssistantRuntimeState runtime_state);
};
