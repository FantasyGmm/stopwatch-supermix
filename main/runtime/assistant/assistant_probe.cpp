/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "assistant_probe.h"
#include "assistant_runtime.h"

bool StartAssistantCompatibilityProbe()
{
    return GetAssistantRuntime().start() == AssistantStartResult::Submitted;
}

bool StopAssistantCompatibilityProbe()
{
    return GetAssistantRuntime().requestStop();
}

void UpdateAssistantCompatibilityProbe(uint32_t now_ms)
{
    GetAssistantRuntime().update(now_ms);
}

bool AssistantCompatibilityProbeStopped()
{
    return GetAssistantRuntime().state() == AssistantRuntimeState::Stopped;
}
