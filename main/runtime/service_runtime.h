/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

class ServiceRuntime {
public:
    bool start();
    bool isStarted() const
    {
        return _started;
    }

private:
    bool _started = false;
};

ServiceRuntime& GetServiceRuntime();
