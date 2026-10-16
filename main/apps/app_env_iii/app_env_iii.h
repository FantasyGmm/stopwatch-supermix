/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "view/env_iii_view.h"
#include <apps/common/key_manager/key_manager.h>
#include <memory>
#include <mooncake.h>

class AppEnvIII : public mooncake::AppAbility {
public:
    AppEnvIII();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::EnvIIIView> _view;
};
