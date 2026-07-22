# Remote OTA Disabled and Persistent Update Notice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep remote firmware installation structurally disabled while showing `发现新版本 vX.X.X` persistently on the Assistant page until a USB-flashed firmware reaches the announced version.

**Architecture:** A pure StopWatch update-notice model normalizes and compares versions. A narrow build-time overlay passes the XiaoZhi response's version string into a session-scoped, thread-safe bridge; the Assistant runtime consumes it, persists only the target version in NVS, and publishes a dedicated persistent mailbox event. The view renders that event in an independent bottom label, while contract tests enforce the single-factory partition and absence of any product OTA install path.

**Tech Stack:** ESP-IDF 5.5, C++17, Brookesia 0.7 services, ESP-IDF NVS, LVGL/Smooth UI Toolkit, CMake source overlays, host C++ tests, Python `unittest` firmware contracts.

---

## File structure

- Create `main/runtime/assistant/assistant_update_notice_policy.h`: pure semantic-version and pending-notice state API.
- Create `main/runtime/assistant/assistant_update_notice_policy.cpp`: strict normalization, comparison, restore, observe, and display formatting.
- Create `main/runtime/assistant/assistant_update_notice_bridge.h`: session-scoped report type and bridge API.
- Create `main/runtime/assistant/assistant_update_notice_bridge.cpp`: thread-safe cross-thread handoff plus C entry points used by the generated Brookesia source.
- Create `main/runtime/assistant/assistant_update_notice_store.h`: small NVS persistence interface.
- Create `main/runtime/assistant/assistant_update_notice_store.cpp`: `assistant_ui/update_ver` NVS load/save/clear implementation.
- Create `tests/assistant_update_notice_test.cpp`: host tests for parsing, state policy, formatting, and stale-session rejection.
- Create `mac-codex-agent/tests/test_firmware_update_notice_contract.py`: source, UI, overlay, persistence, partition, and OTA-prohibition contracts.
- Modify `main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake`: inject the local version and report only the returned version string through the product bridge.
- Modify `main/runtime/assistant/assistant_event_mailbox.h`: add a dedicated persistent `UpdateAvailable` event slot.
- Modify `main/runtime/assistant/assistant_event_mailbox.cpp`: map `UpdateAvailable` to its own persistent slot.
- Modify `tests/assistant_event_mailbox_test.cpp`: prove update notices survive transient overflow and session cleanup.
- Modify `main/runtime/assistant/assistant_runtime.cpp`: restore/persist notice state, consume session-scoped reports, and publish the event without blocking Assistant startup.
- Modify `main/apps/app_assistant/view/assistant_view.h`: expose update-version rendering and own the extra label.
- Modify `main/apps/app_assistant/view/assistant_view.cpp`: render the exact persistent Chinese notice beneath the key hint.
- Modify `main/apps/app_assistant/app_assistant.cpp`: apply `UpdateAvailable` without replacing lifecycle or conversation text.
- Modify `scripts/run_unified_offline_validation.sh`: compile and run the new host test and renumber the validation stages.

### Task 1: Pure version and pending-notice policy

**Files:**
- Create: `tests/assistant_update_notice_test.cpp`
- Create: `main/runtime/assistant/assistant_update_notice_policy.h`
- Create: `main/runtime/assistant/assistant_update_notice_policy.cpp`

- [ ] **Step 1: Write the failing policy tests**

Create `tests/assistant_update_notice_test.cpp` with assertions for the approved rules:

```cpp
#include "runtime/assistant/assistant_update_notice_policy.h"

#include <cassert>
#include <optional>
#include <string>

int main()
{
    assert(NormalizeAssistantFirmwareVersion("V0.5") == std::optional<std::string>("0.5.0"));
    assert(NormalizeAssistantFirmwareVersion("v1.2.3") == std::optional<std::string>("1.2.3"));
    assert(NormalizeAssistantFirmwareVersion("1") == std::optional<std::string>("1.0.0"));
    assert(!NormalizeAssistantFirmwareVersion("1.2.3.4"));
    assert(!NormalizeAssistantFirmwareVersion("1.beta.3"));
    assert(!NormalizeAssistantFirmwareVersion(""));

    AssistantUpdateNoticeModel model("V0.5");
    assert(model.restore(std::optional<std::string>("0.4.9")) ==
           AssistantUpdateStorageAction::Clear);
    assert(!model.hasPending());
    assert(model.observe("v0.6") == AssistantUpdateStorageAction::Save);
    assert(model.pendingVersion() == "0.6.0");
    assert(model.displayText() == "发现新版本 v0.6.0");
    assert(model.observe("0.5.0") == AssistantUpdateStorageAction::None);
    assert(model.observe("invalid") == AssistantUpdateStorageAction::None);
    assert(model.pendingVersion() == "0.6.0");
    assert(model.restore(std::nullopt) == AssistantUpdateStorageAction::None);
    assert(model.pendingVersion() == "0.6.0");
    assert(model.observe("0.7.1") == AssistantUpdateStorageAction::Save);
    assert(model.pendingVersion() == "0.7.1");

    AssistantUpdateNoticeModel after_usb_upgrade("V0.7.1");
    assert(after_usb_upgrade.restore(std::optional<std::string>("v0.7.1")) ==
           AssistantUpdateStorageAction::Clear);
    assert(!after_usb_upgrade.hasPending());

    AssistantUpdateNoticeModel offline_reopen("V0.5");
    assert(offline_reopen.restore(std::optional<std::string>("v0.6")) ==
           AssistantUpdateStorageAction::None);
    assert(offline_reopen.displayText() == "发现新版本 v0.6.0");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/assistant_update_notice_test.cpp \
  main/runtime/assistant/assistant_update_notice_policy.cpp \
  -o /tmp/assistant_update_notice_test
```

Expected: compilation fails because the policy header and implementation do not exist.

- [ ] **Step 3: Implement the pure model**

Define this public API in `assistant_update_notice_policy.h`:

```cpp
#pragma once

#include <optional>
#include <string>
#include <string_view>

enum class AssistantUpdateStorageAction {
    None,
    Save,
    Clear,
};

std::optional<std::string> NormalizeAssistantFirmwareVersion(std::string_view value);

class AssistantUpdateNoticeModel {
public:
    explicit AssistantUpdateNoticeModel(std::string_view local_version);

    AssistantUpdateStorageAction restore(const std::optional<std::string>& stored_version);
    AssistantUpdateStorageAction observe(std::string_view remote_version);
    bool hasPending() const;
    const std::string& pendingVersion() const;
    std::string displayText() const;

private:
    std::optional<std::string> _local_version;
    std::string _pending_version;
};
```

Implement `assistant_update_notice_policy.cpp` so normalization:

- removes exactly one leading `v` or `V`;
- accepts one to three non-empty decimal components;
- rejects signs, whitespace, suffixes, empty components, overflow, and more than three components;
- pads missing minor or patch values with zero;
- returns canonical `major.minor.patch` without leading zeroes.

Compare parsed numeric triples, never strings. `restore()` returns `Clear` for an invalid stored value or a value not newer than local, retains a valid newer stored value with `None`, and never invents a notice when storage is empty. An empty store must not erase an already pending in-memory target, which preserves the current process's notice after a failed NVS save. `observe()` returns `Save` only when a valid remote value is newer than local and newer than the current pending value. `displayText()` returns an empty string without a pending target and otherwise returns `发现新版本 v` plus the canonical version.

- [ ] **Step 4: Run the host test to verify it passes**

Run the Step 2 compile command, then:

```bash
/tmp/assistant_update_notice_test
```

Expected: exit code `0` with no output.

- [ ] **Step 5: Commit the pure policy**

```bash
git add tests/assistant_update_notice_test.cpp \
  main/runtime/assistant/assistant_update_notice_policy.h \
  main/runtime/assistant/assistant_update_notice_policy.cpp
git commit -m "Add Assistant update notice policy"
```

### Task 2: Session-scoped XiaoZhi version bridge

**Files:**
- Create: `main/runtime/assistant/assistant_update_notice_bridge.h`
- Create: `main/runtime/assistant/assistant_update_notice_bridge.cpp`
- Modify: `tests/assistant_update_notice_test.cpp`
- Modify: `main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake`
- Create: `mac-codex-agent/tests/test_firmware_update_notice_contract.py`

- [ ] **Step 1: Add failing bridge and overlay tests**

Extend the host test with this session behavior:

```cpp
#include "runtime/assistant/assistant_update_notice_bridge.h"
#include <cstdlib>

auto& bridge = GetAssistantUpdateNoticeBridge();
bridge.beginSession(7);
stopwatch_report_remote_firmware_version("v0.6.0");
auto report = bridge.consume();
assert(report.has_value());
assert(report->session_id == 7);
assert(report->version == "v0.6.0");

bridge.endSession(7);
stopwatch_report_remote_firmware_version("v9.9.9");
assert(!bridge.consume());

char* local = stopwatch_duplicate_firmware_version();
assert(local != nullptr);
assert(std::string(local) == "V0.5");
std::free(local);
```

Create `test_firmware_update_notice_contract.py` and initially require the bridge hooks in the overlay:

```python
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FirmwareUpdateNoticeContractTests(unittest.TestCase):
    def test_xiaozhi_overlay_reports_version_without_install_inputs(self):
        overlay = (ROOT / "main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake").read_text(encoding="utf-8")
        self.assertIn("stopwatch_duplicate_firmware_version", overlay)
        self.assertIn("stopwatch_report_remote_firmware_version", overlay)
        self.assertIn("info.current_version", overlay)
        self.assertIn("info.firmware_version", overlay)
        self.assertNotIn("info.firmware_url", overlay)
        self.assertNotIn("esp_https_ota", overlay)
        self.assertNotIn("esp_ota_", overlay)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run both tests to verify they fail**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
  tests/assistant_update_notice_test.cpp \
  main/runtime/assistant/assistant_update_notice_policy.cpp \
  main/runtime/assistant/assistant_update_notice_bridge.cpp \
  -o /tmp/assistant_update_notice_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_firmware_update_notice_contract -v
```

Expected: the C++ compile fails because the bridge does not exist, and the Python contract fails because the overlay lacks the hooks.

- [ ] **Step 3: Implement the bridge**

Expose the following API in `assistant_update_notice_bridge.h`:

```cpp
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct AssistantUpdateReport {
    uint64_t session_id;
    std::string version;
};

class AssistantUpdateNoticeBridge {
public:
    void beginSession(uint64_t session_id);
    void endSession(uint64_t session_id);
    void report(const char* version);
    std::optional<AssistantUpdateReport> consume();

private:
    std::mutex _mutex;
    uint64_t _active_session = 0;
    std::optional<AssistantUpdateReport> _latest;
};

AssistantUpdateNoticeBridge& GetAssistantUpdateNoticeBridge();

extern "C" char* stopwatch_duplicate_firmware_version();
extern "C" void stopwatch_report_remote_firmware_version(const char* version);
```

In the implementation, protect all bridge state with `_mutex`, keep only the latest report for the active session, reject null/empty versions and reports received with no active session, and discard the pending report when that session ends. Implement `stopwatch_duplicate_firmware_version()` with `std::malloc` and `std::memcpy` from `common::FirmwareVersion` because `esp_xiaozhi_chat_free_info()` later frees `info.current_version` with `free()`.

- [ ] **Step 4: Extend the existing generated-source overlay**

Add guarded CMake replacements to `brookesia_xiaozhi_mcp_ownership_overlay.cmake` that transform the generated Brookesia source from:

```cpp
esp_xiaozhi_chat_info_t info{};
auto ret = esp_xiaozhi_chat_get_info(&info);
```

to:

```cpp
esp_xiaozhi_chat_info_t info{};
info.current_version = stopwatch_duplicate_firmware_version();
auto ret = esp_xiaozhi_chat_get_info(&info);
if (ret == ESP_OK && info.firmware_version != nullptr) {
    stopwatch_report_remote_firmware_version(info.firmware_version);
}
```

Inject these declarations near the generated source's includes:

```cpp
extern "C" char* stopwatch_duplicate_firmware_version();
extern "C" void stopwatch_report_remote_firmware_version(const char* version);
```

As with the existing ownership and wake-gate transforms, make CMake fail with a clear message if either expected upstream block is absent. Do not pass `firmware_url`, `force`, or any install callback into product code.

- [ ] **Step 5: Run the focused tests**

Run both Step 2 commands and then `/tmp/assistant_update_notice_test`.

Expected: both tests pass and the host binary exits `0`.

- [ ] **Step 6: Commit the bridge and extraction seam**

```bash
git add main/runtime/assistant/assistant_update_notice_bridge.h \
  main/runtime/assistant/assistant_update_notice_bridge.cpp \
  main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake \
  tests/assistant_update_notice_test.cpp \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py
git commit -m "Bridge XiaoZhi update metadata to Assistant"
```

### Task 3: Persist and publish update notice state

**Files:**
- Create: `main/runtime/assistant/assistant_update_notice_store.h`
- Create: `main/runtime/assistant/assistant_update_notice_store.cpp`
- Modify: `main/runtime/assistant/assistant_event_mailbox.h`
- Modify: `main/runtime/assistant/assistant_event_mailbox.cpp`
- Modify: `tests/assistant_event_mailbox_test.cpp`
- Modify: `main/runtime/assistant/assistant_runtime.cpp`
- Modify: `mac-codex-agent/tests/test_firmware_update_notice_contract.py`

- [ ] **Step 1: Write failing mailbox and persistence contracts**

Add this event to the existing mailbox host test before filling its transient queue:

```cpp
mailbox.publish({1, AssistantEventKind::UpdateAvailable, "0.6.0"});
```

Update the expected drain sequence to include `UpdateAvailable`. Replace the existing zero-capacity block with:

```cpp
persistent_only.publish({7, AssistantEventKind::UserText, "dropped"});
persistent_only.publish({7, AssistantEventKind::State, "Running"});
persistent_only.publish({7, AssistantEventKind::UpdateAvailable, "0.6.0"});
assert(persistent_only.droppedTransientCount() == 1);
events = drain(persistent_only);
assert(events.size() == 2);
assert(events[0].kind == AssistantEventKind::State);
assert(events[1].kind == AssistantEventKind::UpdateAvailable);
assert(events[1].text == "0.6.0");
```

Extend the Python contract:

```python
def test_notice_uses_product_nvs_and_a_persistent_mailbox_slot(self):
    store = (ROOT / "main/runtime/assistant/assistant_update_notice_store.cpp").read_text(encoding="utf-8")
    mailbox_h = (ROOT / "main/runtime/assistant/assistant_event_mailbox.h").read_text(encoding="utf-8")
    mailbox_cpp = (ROOT / "main/runtime/assistant/assistant_event_mailbox.cpp").read_text(encoding="utf-8")
    runtime = (ROOT / "main/runtime/assistant/assistant_runtime.cpp").read_text(encoding="utf-8")
    self.assertIn('"assistant_ui"', store)
    self.assertIn('"update_ver"', store)
    self.assertIn("nvs_commit", store)
    self.assertIn("UpdateAvailable", mailbox_h)
    self.assertIn("AssistantEventKind::UpdateAvailable", mailbox_cpp)
    self.assertIn("AssistantUpdateNoticeStore", runtime)
    self.assertIn("GetAssistantUpdateNoticeBridge", runtime)
```

- [ ] **Step 2: Run the focused tests to verify they fail**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
  tests/assistant_event_mailbox_test.cpp \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  -o /tmp/assistant_event_mailbox_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_firmware_update_notice_contract -v
```

Expected: compilation fails on `UpdateAvailable`, and the Python contract reports the missing store/runtime integration.

- [ ] **Step 3: Add the NVS store**

Define this interface in `assistant_update_notice_store.h`:

```cpp
#pragma once

#include <optional>
#include <string>
#include <string_view>

class AssistantUpdateNoticeStore {
public:
    std::optional<std::string> load() const;
    bool save(std::string_view version) const;
    bool clear() const;
};
```

Implement it with ESP-IDF `nvs_open`, `nvs_get_str`, `nvs_set_str`, `nvs_erase_key`, and `nvs_commit`. Use namespace `assistant_ui` and key `update_ver`. Treat `ESP_ERR_NVS_NOT_FOUND` as an empty store, log all other failures, commit every save/erase, and never erase the whole namespace.

- [ ] **Step 4: Make update events persistent**

Add `UpdateAvailable` to `AssistantEventKind`, increase the mailbox persistent array from three to four slots, and return slot `3` from `persistentIndex()` for the new kind. Keep conversation events transient.

- [ ] **Step 5: Integrate the model, bridge, and store into the runtime**

Add these `AssistantRuntime::Impl` members:

```cpp
AssistantUpdateNoticeModel update_notice{common::FirmwareVersion};
AssistantUpdateNoticeStore update_notice_store;
```

Add focused helpers with these responsibilities:

```cpp
void restoreUpdateNotice(uint64_t session_id);
void consumeUpdateNoticeReport(uint64_t session_id);
void applyUpdateStorageAction(AssistantUpdateStorageAction action);
void publishUpdateNotice(uint64_t session_id);
```

At accepted `start()`:

1. call `GetAssistantUpdateNoticeBridge().beginSession(session)`;
2. load and restore NVS before attempting Wi-Fi;
3. clear stale persisted state when local firmware is equal/newer;
4. immediately publish a canonical `UpdateAvailable` event when a pending target remains.

At the start of each active `update()` call, consume at most one bridge report. Reject a report whose `session_id` differs from `state.sessionId()`. Pass the version to `update_notice.observe()`, save only when it returns `Save`, and publish the in-memory pending version even when the NVS save fails.

In every `releaseSession()` path, call `GetAssistantUpdateNoticeBridge().endSession(session_id)` before unbinding resources. NVS failures must only log; they must not invoke `failStart()`, modify Assistant lifecycle state, or block resource release.

- [ ] **Step 6: Run the focused tests**

Compile and run the mailbox test, then run the Python contract from Step 2.

Expected: both pass.

- [ ] **Step 7: Commit persistence and runtime publication**

```bash
git add main/runtime/assistant/assistant_update_notice_store.h \
  main/runtime/assistant/assistant_update_notice_store.cpp \
  main/runtime/assistant/assistant_event_mailbox.h \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  main/runtime/assistant/assistant_runtime.cpp \
  tests/assistant_event_mailbox_test.cpp \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py
git commit -m "Persist Assistant firmware update notices"
```

### Task 4: Render the persistent Assistant-page notice

**Files:**
- Modify: `main/apps/app_assistant/view/assistant_view.h`
- Modify: `main/apps/app_assistant/view/assistant_view.cpp`
- Modify: `main/apps/app_assistant/app_assistant.cpp`
- Modify: `mac-codex-agent/tests/test_firmware_update_notice_contract.py`

- [ ] **Step 1: Add a failing UI contract**

Extend the Python test:

```python
def test_assistant_page_renders_exact_noninteractive_notice(self):
    view_h = (ROOT / "main/apps/app_assistant/view/assistant_view.h").read_text(encoding="utf-8")
    view_cpp = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text(encoding="utf-8")
    app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text(encoding="utf-8")
    self.assertIn("setUpdateVersion", view_h)
    self.assertIn("_update_label", view_h)
    self.assertIn("发现新版本 v", view_cpp)
    self.assertIn("LV_ALIGN_BOTTOM_MID", view_cpp)
    self.assertIn("AssistantEventKind::UpdateAvailable", app)
    self.assertIn("setUpdateVersion(event.text.c_str())", app)
    for forbidden in ("Update now", "立即升级", "esp_restart", "esp_ota_"):
        self.assertNotIn(forbidden, view_cpp + app)
```

- [ ] **Step 2: Run the UI contract to verify it fails**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_firmware_update_notice_contract.FirmwareUpdateNoticeContractTests.test_assistant_page_renders_exact_noninteractive_notice -v
```

Expected: failure because the label and event case do not exist.

- [ ] **Step 3: Add the independent bottom label**

Add this method and member to `AssistantView`:

```cpp
void setUpdateVersion(const char* canonical_version);
std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _update_label;
```

Create `_update_label` after `_hint_label`, use `font_puhui_14_1`, center it at `LV_ALIGN_BOTTOM_MID`, keep the existing hint at its current position, and place the notice below it. Initialize the label to empty. Implement:

```cpp
void AssistantView::setUpdateVersion(const char* canonical_version)
{
    if (!_update_label) {
        return;
    }
    if (canonical_version == nullptr || canonical_version[0] == '\0') {
        _update_label->setText("");
        return;
    }
    const std::string text = std::string("发现新版本 v") + canonical_version;
    _update_label->setText(text);
}
```

Reset `_update_label` first in `destroy()`. Do not modify it from `setState()`, so lifecycle, errors, activation, and conversation changes cannot erase it.

- [ ] **Step 4: Apply the new event without changing existing state text**

Add this isolated switch case to `AppAssistant::applyRuntimeEvent()`:

```cpp
case AssistantEventKind::UpdateAvailable:
    _view->setUpdateVersion(event.text.c_str());
    break;
```

Do not add `UpdateAvailable` to `isConversationEvent()` and do not make it conditional on `_close_controller`; it is independent display state for the current page.

- [ ] **Step 5: Run the complete update-notice Python contract**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_firmware_update_notice_contract -v
```

Expected: all update-notice contract tests pass.

- [ ] **Step 6: Commit the UI**

```bash
git add main/apps/app_assistant/view/assistant_view.h \
  main/apps/app_assistant/view/assistant_view.cpp \
  main/apps/app_assistant/app_assistant.cpp \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py
git commit -m "Show persistent firmware update notice"
```

### Task 5: Enforce the no-OTA product contract

**Files:**
- Modify: `mac-codex-agent/tests/test_firmware_update_notice_contract.py`
- Verify: `partitions.csv`
- Verify: `sdkconfig.defaults`

- [ ] **Step 1: Add failing-or-protective OTA prohibition coverage**

Add these tests:

```python
import csv
import re

def test_remote_install_is_structurally_disabled(self):
    rows = []
    with (ROOT / "partitions.csv").open(newline="", encoding="utf-8") as handle:
        for row in csv.reader(handle):
            values = [value.strip() for value in row]
            if values and values[0] and not values[0].startswith("#"):
                rows.append(values)
    apps = [row for row in rows if len(row) >= 3 and row[1] == "app"]
    self.assertEqual([(row[0], row[2]) for row in apps], [("app", "factory")])
    self.assertFalse(any(row[0] == "otadata" for row in rows))
    self.assertFalse(any(row[2] in ("ota_0", "ota_1") for row in apps))

def test_product_sources_have_no_remote_install_path(self):
    forbidden = re.compile(
        r"esp_https_ota|esp_ota_begin|esp_ota_write|"
        r"esp_ota_end|esp_ota_set_boot_partition|esp_restart"
    )
    offenders = []
    roots = (
        ROOT / "main/runtime/assistant",
        ROOT / "main/apps/app_assistant",
        ROOT / "main/cmake",
    )
    for root in roots:
        for path in root.rglob("*"):
            if path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp", ".cmake"}:
                if forbidden.search(path.read_text(encoding="utf-8")):
                    offenders.append(path.relative_to(ROOT).as_posix())
    self.assertEqual(offenders, [])

def test_bootstrap_endpoint_remains_metadata_only(self):
    defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
    self.assertIn(
        'CONFIG_XIAOZHI_OTA_URL="https://api.tenclass.net/xiaozhi/ota/"',
        defaults,
    )
    overlay = (ROOT / "main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake").read_text(encoding="utf-8")
    self.assertIn("info.firmware_version", overlay)
    self.assertNotIn("info.firmware_url", overlay)
```

- [ ] **Step 2: Run the prohibition contract**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_firmware_update_notice_contract -v
```

Expected: pass. If it fails, remove only the newly discovered product install path; do not remove the XiaoZhi bootstrap endpoint needed for activation and transport configuration.

- [ ] **Step 3: Confirm configuration and partition output**

Run:

```bash
rg -n "otadata|ota_0|ota_1|ROLLBACK_ENABLE" partitions.csv sdkconfig.defaults sdkconfig.unified.defaults
```

Expected: no OTA app/data partition and no enabled rollback setting. The bootstrap URL is allowed because the previous contract proves that only the version string crosses into product code.

- [ ] **Step 4: Commit the prohibition contract**

```bash
git add mac-codex-agent/tests/test_firmware_update_notice_contract.py
git commit -m "Guard against unsupported remote OTA"
```

### Task 6: Integrate with the complete offline validation gate

**Files:**
- Modify: `scripts/run_unified_offline_validation.sh`
- Verify: all files changed in Tasks 1-5

- [ ] **Step 1: Add the host test to the validation runner**

Insert a new stage after the Assistant event mailbox test:

```bash
echo "[5/16] Assistant update notice host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_update_notice_test.cpp \
    main/runtime/assistant/assistant_update_notice_policy.cpp \
    main/runtime/assistant/assistant_update_notice_bridge.cpp \
    -o "$HOST_TEST_DIR/assistant_update_notice_test"
"$HOST_TEST_DIR/assistant_update_notice_test"
```

Renumber every later stage and the final whitespace check so the runner reports `[1/16]` through `[16/16]` exactly once each.

- [ ] **Step 2: Run targeted tests before the expensive build**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_firmware_update_notice_contract -v
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
  tests/assistant_update_notice_test.cpp \
  main/runtime/assistant/assistant_update_notice_policy.cpp \
  main/runtime/assistant/assistant_update_notice_bridge.cpp \
  -o /tmp/assistant_update_notice_test
/tmp/assistant_update_notice_test
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
  tests/assistant_event_mailbox_test.cpp \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  -o /tmp/assistant_event_mailbox_test
/tmp/assistant_event_mailbox_test
```

Expected: Python reports all tests `OK`; both host binaries exit `0`.

- [ ] **Step 3: Run the full offline validation**

Run:

```bash
./scripts/run_unified_offline_validation.sh
```

Expected: all 16 stages pass, including Python contracts, host tests, a clean unified firmware build, size gates, and `git diff --check`.

- [ ] **Step 4: Inspect the final binary and source boundaries**

Run:

```bash
rg -n "esp_https_ota|esp_ota_begin|esp_ota_write|esp_ota_set_boot_partition" \
  main/runtime/assistant main/apps/app_assistant main/cmake
rg -n "otadata|ota_0|ota_1" partitions.csv
git diff --check
```

Expected: both OTA searches have no matches and `git diff --check` has no output.

- [ ] **Step 5: Commit runner integration**

```bash
git add scripts/run_unified_offline_validation.sh
git commit -m "Validate persistent update notices offline"
```

### Task 7: Hardware validation when the watch is available

**Files:**
- Verify only; do not modify production files unless a reproducible defect is found.

- [ ] **Step 1: Flash only after preserving the existing recovery boundary**

Use the repository's verified full-Flash backup and normal unified flashing workflow. Do not erase NVS before the persistence test.

Expected: the watch boots to Launcher and the current applications remain available.

- [ ] **Step 2: Verify a server-announced update**

Open Assistant while the XiaoZhi service returns a valid version newer than `common::FirmwareVersion`.

Expected: the page shows exactly `发现新版本 vX.X.X` beneath the existing key hint. Connecting, activation, listening, speech, error, and close text continue to update normally.

- [ ] **Step 3: Verify persistence without a successful refresh**

Close Assistant, disable Wi-Fi or make the bootstrap endpoint unavailable, reopen Assistant, and then reboot and reopen once more.

Expected: the same notice remains visible after close/open and reboot even though no new server response succeeds.

- [ ] **Step 4: Verify USB-upgrade clearing**

Flash a firmware whose `common::FirmwareVersion` is equal to or newer than the saved target without erasing NVS, boot, and open Assistant.

Expected: the stale notice is cleared and does not return unless the server later reports a still-newer valid version.

- [ ] **Step 5: Record evidence separately**

Record automated, build, and hardware results as separate evidence. If hardware is unavailable or the service cannot return a newer target, leave only the corresponding hardware row pending; do not downgrade the completed offline results.
