/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

bool StartAssistantCompatibilityProbe();
bool StopAssistantCompatibilityProbe();
void UpdateAssistantCompatibilityProbe(uint32_t now_ms);
bool AssistantCompatibilityProbeStopped();
