# Agent-to-Watch Time Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Synchronize Mac time and effective UTC offset to the StopWatch system clock and hardware RTC once after every Agent BLE connection.

**Architecture:** The Mac sends a dedicated time_sync event through the existing encrypted BLE event channel before the first Codex status payload. The firmware validates the event in the existing Codex message parser, then delegates to a HAL method that applies system time, persists a fixed-offset POSIX timezone, and updates the RX8130 RTC for global use.

**Tech Stack:** Python 3.9 asyncio/unittest, Bleak, C++17, cJSON, ESP-IDF 5.5.4, RX8130 RTC.

---

### Task 1: Send Time Once Per BLE Session

**Files:**
- Modify: mac-codex-agent/agent.py
- Modify: mac-codex-agent/runtime.py
- Test: mac-codex-agent/tests/test_agent.py
- Test: mac-codex-agent/tests/test_runtime.py

- [ ] **Step 1: Write the failing payload test**

Add this test with injected clock providers:

~~~python
def test_sync_time_sends_current_epoch_and_timezone_offset(self):
    collector = FakeCollector()
    transport = FakeTransport()
    agent = CodexStatusAgent(
        collector=collector,
        transport=transport,
        time_ms=lambda: 1_783_872_000_123,
        utc_offset_minutes=lambda: 480,
    )

    asyncio.run(agent.sync_time())

    self.assertEqual(
        transport.messages,
        [
            (
                EVENT_MESSAGE,
                {
                    "type": "time_sync",
                    "unix_ms": 1_783_872_000_123,
                    "utc_offset_minutes": 480,
                },
            )
        ],
    )
~~~

- [ ] **Step 2: Run the focused test and verify RED**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent.tests.test_agent.CodexStatusAgentTests.test_sync_time_sends_current_epoch_and_timezone_offset -v
~~~

Expected: error because CodexStatusAgent has no providers or sync_time method.

- [ ] **Step 3: Implement deterministic payload generation**

Add these defaults and extend CodexStatusAgent:

~~~python
import datetime
import time


def _unix_time_ms() -> int:
    return time.time_ns() // 1_000_000


def _utc_offset_minutes() -> int:
    offset = datetime.datetime.now().astimezone().utcoffset()
    return int(offset.total_seconds() // 60) if offset is not None else 0


class CodexStatusAgent:
    def __init__(
        self,
        collector: CodexStateCollector,
        transport: Any,
        time_ms: Callable[[], int] = _unix_time_ms,
        utc_offset_minutes: Callable[[], int] = _utc_offset_minutes,
    ):
        self.collector = collector
        self.transport = transport
        self._time_ms = time_ms
        self._utc_offset_minutes = utc_offset_minutes

    async def sync_time(self) -> None:
        payload = {
            "type": "time_sync",
            "unix_ms": self._time_ms(),
            "utc_offset_minutes": self._utc_offset_minutes(),
        }
        await self.transport.send_json(
            json.dumps(payload, separators=(",", ":")), EVENT_MESSAGE
        )
~~~

- [ ] **Step 4: Run the focused test and verify GREEN**

Run the Step 2 command. Expected: one test passes.

- [ ] **Step 5: Add failing runtime ordering and reconnect assertions**

Import EVENT_MESSAGE and STATUS_MESSAGE from protocol. Update the existing status reuse test:

~~~python
await wait_for_messages(transport, 2)
self.assertEqual(transport.messages[0][0], EVENT_MESSAGE)
self.assertEqual(json.loads(transport.messages[0][1])["type"], "time_sync")
self.assertEqual(transport.messages[1][0], STATUS_MESSAGE)
self.assertEqual(json.loads(transport.messages[1][1]), status)
~~~

Extend the reconnect test so the first message on both transport instances is a time_sync event.

Add a `send_error` option to `FakeTransport`, raise it from `send_json()`, and
add this failure-path test:

~~~python
def __init__(
    self,
    connect_result=True,
    connect_error=None,
    block_connect=False,
    block_disconnect=False,
    send_error=None,
):
    # Keep the existing assignments.
    self.send_error = send_error

async def send_json(self, payload, message_type):
    if self.send_error is not None:
        raise self.send_error
    self.messages.append((message_type, payload))
~~~

~~~python
async def test_failed_time_sync_write_disconnects_and_retries(self):
    factory = FakeTransportFactory(
        [{"send_error": ConnectionError("time sync failed")}, {}]
    )
    runtime = AgentRuntime(
        collector=FakeCollector(),
        transport_factory=factory,
        status_interval=0.01,
        retry_delays=(0.01,),
    )
    task = asyncio.create_task(runtime.run())
    await factory.wait_for_instances(2)
    await wait_for_messages(factory.instances[1], 1)
    self.assertEqual(factory.instances[0].disconnect_count, 1)
    self.assertEqual(
        json.loads(factory.instances[1].messages[0][1])["type"], "time_sync"
    )
    runtime.submit(AgentCommand.STOP)
    await asyncio.wait_for(task, 1)
~~~

- [ ] **Step 6: Run runtime tests and verify RED**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_runtime.py -v
~~~

Expected: first-message and reconnect assertions fail.

- [ ] **Step 7: Send time immediately after connection**

In AgentRuntime._run_session(), insert:

~~~python
self._transition(ConnectionState.CONNECTED)
status_agent = CodexStatusAgent(self._collector, transport)
await status_agent.sync_time()
if self._status_ready is not None:
    await self._status_ready.wait()
~~~

Do not catch the write exception locally; the existing session path must disconnect and retry.

- [ ] **Step 8: Run all Mac tests and commit**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest discover   -s mac-codex-agent/tests -p 'test_*.py'
git add mac-codex-agent/agent.py mac-codex-agent/runtime.py   mac-codex-agent/tests/test_agent.py mac-codex-agent/tests/test_runtime.py
git commit -m "Sync Mac time on watch connection"
~~~

Expected: all Python tests pass.

### Task 2: Parse and Validate Time-Sync Events

**Files:**
- Create: tests/codex_status_model_test.cpp
- Modify: main/apps/app_codex_status/codex_status_model.h
- Modify: main/apps/app_codex_status/codex_status_model.cpp

- [ ] **Step 1: Write a failing host parser test**

~~~cpp
#include "codex_status_model.h"

#include <cassert>
#include <cstring>

static CodexMessageKind parse(const char* json, CodexStatusSnapshot& snapshot)
{
    return parse_codex_message(json, std::strlen(json), snapshot);
}

int main()
{
    CodexStatusSnapshot snapshot;
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::TimeSync);
    assert(snapshot.unix_time_ms == 1783872000123LL);
    assert(snapshot.utc_offset_minutes == 480);

    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123.5,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1577836799999,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":841})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":4102444800001,"utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":-841})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":480.5})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"time_sync","utc_offset_minutes":480})",
        snapshot
    ) == CodexMessageKind::Invalid);
    assert(parse(
        R"({"type":"chime","agent":"codex"})", snapshot
    ) == CodexMessageKind::Chime);
}
~~~

- [ ] **Step 2: Compile and verify RED**

~~~bash
clang -c /Users/bytedance/esp/esp-idf-v5.5.4/components/json/cJSON/cJSON.c   -I/Users/bytedance/esp/esp-idf-v5.5.4/components/json/cJSON   -o /tmp/cJSON.o
clang++ -std=c++17   -Imain/apps/app_codex_status   -I/Users/bytedance/esp/esp-idf-v5.5.4/components/json/cJSON   tests/codex_status_model_test.cpp   main/apps/app_codex_status/codex_status_model.cpp /tmp/cJSON.o   -o /tmp/codex_status_model_test
~~~

Expected: compile failure because TimeSync and its fields do not exist.

- [ ] **Step 3: Extend the parser model**

Add int64_t unix_time_ms and int utc_offset_minutes to CodexStatusSnapshot, and TimeSync to CodexMessageKind. Add exact integer parsing and these limits:

~~~cpp
constexpr int64_t kMinimumUnixMs = 1577836800000LL;
constexpr int64_t kMaximumUnixMs = 4102444800000LL;
constexpr int kMaximumUtcOffsetMinutes = 14 * 60;

bool json_exact_int64(cJSON* root, const char* key, int64_t& value)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        std::abs(item->valuedouble) > 9007199254740991.0) {
        return false;
    }
    value = static_cast<int64_t>(item->valuedouble);
    return true;
}
~~~

For type time_sync, require both fields, enforce inclusive timestamp and offset bounds, populate the snapshot, delete the JSON root, and return TimeSync. Keep status and chime behavior unchanged.

Use this branch before normal status parsing:

~~~cpp
if (type == "time_sync") {
    int64_t unix_time_ms = 0;
    int64_t offset_minutes = 0;
    const bool valid =
        json_exact_int64(root, "unix_ms", unix_time_ms) &&
        json_exact_int64(root, "utc_offset_minutes", offset_minutes) &&
        unix_time_ms >= kMinimumUnixMs && unix_time_ms <= kMaximumUnixMs &&
        offset_minutes >= -kMaximumUtcOffsetMinutes &&
        offset_minutes <= kMaximumUtcOffsetMinutes;
    if (!valid) {
        cJSON_Delete(root);
        return CodexMessageKind::Invalid;
    }
    snapshot = CodexStatusSnapshot{};
    snapshot.unix_time_ms = unix_time_ms;
    snapshot.utc_offset_minutes = static_cast<int>(offset_minutes);
    cJSON_Delete(root);
    return CodexMessageKind::TimeSync;
}
~~~

- [ ] **Step 4: Compile and run the parser test**

Run the Step 2 commands, then:

~~~bash
/tmp/codex_status_model_test
~~~

Expected: exit 0 with no output.

- [ ] **Step 5: Commit the parser slice**

~~~bash
git add tests/codex_status_model_test.cpp   main/apps/app_codex_status/codex_status_model.h   main/apps/app_codex_status/codex_status_model.cpp
git commit -m "Parse watch time sync events"
~~~

### Task 3: Apply Time and Fixed-Offset Timezone Globally

**Files:**
- Create: main/hal/timezone_offset.h
- Create: main/hal/timezone_offset.cpp
- Create: tests/timezone_offset_test.cpp
- Modify: main/hal/hal.h
- Modify: main/hal/hal_rtc.cpp

- [ ] **Step 1: Write a failing pure formatter test**

~~~cpp
#include "main/hal/timezone_offset.h"

#include <cassert>

int main()
{
    assert(posix_timezone_for_utc_offset(480).value() == "<UTC+0800>-8");
    assert(posix_timezone_for_utc_offset(-300).value() == "<UTC-0500>5");
    assert(posix_timezone_for_utc_offset(330).value() == "<UTC+0530>-5:30");
    assert(posix_timezone_for_utc_offset(345).value() == "<UTC+0545>-5:45");
    assert(posix_timezone_for_utc_offset(0).value() == "<UTC+0000>0");
    assert(!posix_timezone_for_utc_offset(841).has_value());
    assert(!posix_timezone_for_utc_offset(-841).has_value());
}
~~~

- [ ] **Step 2: Compile and verify RED**

~~~bash
clang++ -std=c++17 -I. tests/timezone_offset_test.cpp   main/hal/timezone_offset.cpp -o /tmp/timezone_offset_test
~~~

Expected: failure because the helper files do not exist.

- [ ] **Step 3: Implement the pure formatter**

Declare:

~~~cpp
#pragma once

#include <optional>
#include <string>

std::optional<std::string> posix_timezone_for_utc_offset(int offset_minutes);
~~~

Implement the complete helper:

~~~cpp
#include "timezone_offset.h"

#include <cstdio>
#include <cstdlib>

std::optional<std::string> posix_timezone_for_utc_offset(int offset_minutes)
{
    if (offset_minutes < -14 * 60 || offset_minutes > 14 * 60) {
        return std::nullopt;
    }

    const int absolute = std::abs(offset_minutes);
    const int hours = absolute / 60;
    const int minutes = absolute % 60;
    const char label_sign = offset_minutes < 0 ? '-' : '+';
    char buffer[40] = {};

    if (offset_minutes == 0) {
        std::snprintf(buffer, sizeof(buffer), "<UTC+0000>0");
    } else if (minutes == 0) {
        const char* posix_sign = offset_minutes > 0 ? "-" : "";
        std::snprintf(
            buffer,
            sizeof(buffer),
            "<UTC%c%02d%02d>%s%d",
            label_sign,
            hours,
            minutes,
            posix_sign,
            hours
        );
    } else {
        const char* posix_sign = offset_minutes > 0 ? "-" : "";
        std::snprintf(
            buffer,
            sizeof(buffer),
            "<UTC%c%02d%02d>%s%d:%02d",
            label_sign,
            hours,
            minutes,
            posix_sign,
            hours,
            minutes
        );
    }
    return std::string(buffer);
}
~~~

- [ ] **Step 4: Run the formatter test and verify GREEN**

Run the Step 2 compile command and:

~~~bash
/tmp/timezone_offset_test
~~~

Expected: exit 0 with no output.

- [ ] **Step 5: Add the global HAL operation**

Add this declaration to Hal:

~~~cpp
bool syncUnixTime(int64_t unix_ms, int utc_offset_minutes);
~~~

Implement in hal_rtc.cpp:

~~~cpp
bool Hal::syncUnixTime(int64_t unix_ms, int utc_offset_minutes)
{
    const auto timezone = posix_timezone_for_utc_offset(utc_offset_minutes);
    if (!timezone || unix_ms < 0) {
        return false;
    }
    struct timeval tv = {
        .tv_sec = static_cast<time_t>(unix_ms / 1000),
        .tv_usec = static_cast<suseconds_t>((unix_ms % 1000) * 1000),
    };
    if (settimeofday(&tv, nullptr) != 0) {
        mclog::tagError(_tag, "failed to apply synchronized system time");
        return false;
    }
    setTimezone(*timezone);
    syncSystemTimeToRtc();
    return true;
}
~~~

Include timezone_offset.h and cstdint. Do not convert the epoch to local time before storing it; the RTC already stores UTC.

- [ ] **Step 6: Commit the HAL slice**

~~~bash
git add main/hal/timezone_offset.h main/hal/timezone_offset.cpp   tests/timezone_offset_test.cpp main/hal/hal.h main/hal/hal_rtc.cpp
git commit -m "Apply synchronized time to watch RTC"
~~~

### Task 4: Dispatch, Build, Install, and Validate

**Files:**
- Modify: main/apps/app_codex_status/app_codex_status.cpp
- Modify: mac-codex-agent/README.md
- Modify: README.md

- [ ] **Step 1: Dispatch time sync before UI rendering**

In AppCodexStatus::handle_status_json(), after invalid/chime handling and before LVGL work:

~~~cpp
if (kind == CodexMessageKind::TimeSync) {
    const bool applied = GetHAL().syncUnixTime(
        snapshot.unix_time_ms,
        snapshot.utc_offset_minutes
    );
    if (!applied) {
        mclog::tagError(getAppInfo().name, "time sync event rejected by HAL");
    }
    return applied;
}
~~~

This path must not reset model, quota, or alert UI state.

- [ ] **Step 2: Document the behavior**

Add to both README files:

~~~text
After each successful BLE connection, the Mac agent sends the current Unix time
and effective UTC offset before the first Codex status update. The watch applies
them to its global system clock, persists the timezone, and writes the RX8130
RTC. This version uses a fixed current offset and refreshes it on reconnect.
~~~

- [ ] **Step 3: Run all host verification**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest discover   -s mac-codex-agent/tests -p 'test_*.py'
/tmp/codex_status_model_test
/tmp/timezone_offset_test
git diff --check
~~~

Expected: all Python tests pass, both C++ tests exit 0, and diff check is clean.

- [ ] **Step 4: Build the firmware**

~~~bash
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
~~~

Expected: the build succeeds and fits ota_0.

- [ ] **Step 5: Reinstall and validate the Mac bundle**

Stop the existing menu process, then:

~~~bash
cd mac-codex-agent
./install_launch_agent.sh
open "$HOME/Applications/StopWatch Codex Agent.app"
~~~

Expected: the process runs manually, no LaunchAgent plist exists, and installed agent.py/runtime.py match the worktree.

- [ ] **Step 6: Flash only ota_0**

Detect the current /dev/cu.usbmodem* port. Use the repository's verified write and readback workflow for ota_0 at 0x20000. Do not erase NVS, resources, or assistant ota_1.

Expected: esptool verification succeeds and readback SHA-256 matches build/StopWatch-UserDemo.bin.

- [ ] **Step 7: Validate the real clock lifecycle**

1. Record the watch's old time or set it visibly wrong.
2. Open Codex and wait for Agent BLE connection.
3. Confirm date/time matches the Mac, including UTC+08:00.
4. Return to Launcher and open a watch face; confirm the correction remains.
5. Reboot; confirm RX8130 restores the corrected time.
6. Reopen Codex; confirm reconnect refreshes time without repeated syncs in one session.

- [ ] **Step 8: Commit integration and documentation**

~~~bash
git add main/apps/app_codex_status/app_codex_status.cpp   mac-codex-agent/README.md README.md
git commit -m "Apply Agent time sync across watch apps"
~~~
