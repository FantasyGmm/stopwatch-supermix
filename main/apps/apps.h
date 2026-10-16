/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <sdkconfig.h>
#include "app_launcher/app_launcher.h"
#include "app_codex_status/app_codex_status.h"
#if CONFIG_STOPWATCH_STANDALONE_ASSISTANT
#include "app_assistant/app_assistant.h"
#endif
#include "app_setup/app_setup.h"
#include "app_imu/app_imu.h"
#include "app_fft/app_fft.h"
#include "app_badge/app_badge.h"
#include "app_env_iii/app_env_iii.h"
