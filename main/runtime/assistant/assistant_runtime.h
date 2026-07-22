/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "assistant_event_mailbox.h"
#include "assistant_state.h"

#include <cstddef>
#include <cstdint>
#include <memory>

enum class AssistantStartResult : uint8_t {
    Submitted,
    AlreadyActive,
    ServiceUnavailable,
    ResourceBusy,
    ConfigurationFailed,
    SubmitFailed,
};

class AssistantRuntime {
public:
    AssistantRuntime();
    ~AssistantRuntime();

    AssistantRuntime(const AssistantRuntime&) = delete;
    AssistantRuntime& operator=(const AssistantRuntime&) = delete;

    AssistantStartResult start();
    bool requestStop();
    bool requestOutputVolumeSync(int volume);
    void update(uint32_t now_ms);
    AssistantRuntimeState state() const;
    uint64_t sessionId() const;
    bool pollEvent(AssistantEvent& event);
    std::size_t bindingCount() const;
    std::size_t subscriptionCount() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

AssistantRuntime& GetAssistantRuntime();
