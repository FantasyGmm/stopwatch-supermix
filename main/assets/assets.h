/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <sdkconfig.h>
#include <lvgl.h>

LV_FONT_DECLARE(MontserratSemiBold26);
LV_FONT_DECLARE(MontserratBold12Trae);
LV_FONT_DECLARE(MontserratBold13Trae);
LV_FONT_DECLARE(MontserratBold14Trae);
LV_FONT_DECLARE(MontserratBold15Trae);
LV_FONT_DECLARE(MontserratBold16);
LV_FONT_DECLARE(MontserratBold16Trae);
LV_FONT_DECLARE(MontserratBold17Trae);
LV_FONT_DECLARE(MontserratBold18Trae);
LV_FONT_DECLARE(MontserratBold19Trae);
LV_FONT_DECLARE(MontserratBold22);
LV_FONT_DECLARE(MontserratBold24Digits);
LV_FONT_DECLARE(MontserratBold24Trae);
LV_FONT_DECLARE(MontserratBold25Digits);
LV_FONT_DECLARE(MontserratBold26Digits);
LV_FONT_DECLARE(MontserratBold28Trae);
LV_FONT_DECLARE(MontserratBold29Trae);
LV_FONT_DECLARE(MontserratMedium13Trae);
LV_FONT_DECLARE(MontserratMedium14Trae);
LV_FONT_DECLARE(MontserratMedium15Trae);
LV_FONT_DECLARE(CommissionerMedium64);
LV_FONT_DECLARE(CommissionerMedium108);
LV_FONT_DECLARE(lv_font_maple_mono_medium_24);
LV_FONT_DECLARE(lv_font_maple_mono_medium_28);
LV_FONT_DECLARE(lv_font_maple_mono_medium_48);
LV_FONT_DECLARE(EnvSensorText16);
LV_FONT_DECLARE(EnvSensorTitle22);
LV_FONT_DECLARE(EnvSensorLauncher26);

LV_IMG_DECLARE(icon_indicator_left);
LV_IMG_DECLARE(icon_indicator_right);
LV_IMG_DECLARE(icon_clock);
#if CONFIG_STOPWATCH_STANDALONE_ASSISTANT
LV_IMG_DECLARE(icon_assistant);
#endif
LV_IMG_DECLARE(icon_setup);
LV_IMG_DECLARE(icon_imu);
LV_IMG_DECLARE(icon_fft);
LV_IMG_DECLARE(icon_codex);
LV_IMG_DECLARE(icon_badge);

LV_IMG_DECLARE(classic_hour_hand);
LV_IMG_DECLARE(classic_minute_hand);
LV_IMG_DECLARE(classic_second_hand);
LV_IMG_DECLARE(alarm_icon);
LV_IMG_DECLARE(go_home_guide);
LV_IMG_DECLARE(icon_bat_lightning);

LV_IMG_DECLARE(big_number_0);
LV_IMG_DECLARE(big_number_1);
LV_IMG_DECLARE(big_number_2);
LV_IMG_DECLARE(big_number_3);
LV_IMG_DECLARE(big_number_4);
LV_IMG_DECLARE(big_number_5);
LV_IMG_DECLARE(big_number_6);
LV_IMG_DECLARE(big_number_7);
LV_IMG_DECLARE(big_number_8);
LV_IMG_DECLARE(big_number_9);
