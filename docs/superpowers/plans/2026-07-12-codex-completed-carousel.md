# Codex Completed Carousel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display a static green `COMPLETED` state when no Codex alerts are pending and rotate only actionable pending alerts.

**Architecture:** Keep the Mac alert ledger and BLE payload unchanged. Make `CodexAlertCarousel` select and rotate only pending entries while preserving the latest handled entry as the static all-clear detail.

**Tech Stack:** C++17, ESP-IDF 5.5.4, LVGL 9, Python 3.9 `unittest`, host `clang++` tests.

---

### Task 1: Specify Pending-Only Carousel Behavior

**Files:**
- Create: `tests/codex_alert_carousel_test.cpp`
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py`

- [ ] **Step 1: Add a host test for a mixed alert list**

Create a snapshot containing pending A, handled B, and pending C. Assert that
`apply()` selects A and successive three-second updates alternate A and C
without ever selecting B:

```cpp
CodexStatusSnapshot mixed;
mixed.alerts_valid = true;
mixed.alert_revision = 1;
mixed.pending_count = 2;
mixed.handled_count = 1;
mixed.alerts = {
    {"Project A", "Pending A", "10:00", true, false},
    {"Project B", "Handled B", "09:59", false, false},
    {"Project C", "Pending C", "09:58", true, true},
};
CodexAlertCarousel carousel;
assert(carousel.apply(mixed, 0));
assert(carousel.current()->title == "Pending A");
assert(carousel.update(3000));
assert(carousel.current()->title == "Pending C");
assert(carousel.update(6000));
assert(carousel.current()->title == "Pending A");
```

- [ ] **Step 2: Add a host test for the completed state**

Apply two handled records and assert that the heading is `COMPLETED`, the first
record remains selected, and `update()` returns false after the rotation period:

```cpp
CodexStatusSnapshot completed;
completed.alerts_valid = true;
completed.alert_revision = 2;
completed.pending_count = 0;
completed.handled_count = 2;
completed.alerts = {
    {"StopWatch", "codex监控器", "10:00", false, false},
    {"webshot", "优化 UI 视觉", "09:58", false, false},
};
assert(carousel.apply(completed, 7000));
assert(carousel.heading() == "COMPLETED");
const std::string fixed_title = carousel.current()->title;
assert(!carousel.update(10000));
assert(carousel.current()->title == fixed_title);
```

- [ ] **Step 3: Tighten the firmware source contract**

Assert that `codex_alert_carousel.cpp` contains `return "COMPLETED"`, no longer
contains `ALL HANDLED` or a `HANDLED` heading format, and the activity color
still uses `_alert_carousel.all_handled() ? kHandledGreen : kCodexRed`.

- [ ] **Step 4: Run tests and verify the new tests fail**

```bash
clang++ -std=c++17 -Imain/apps/app_codex_status \
  tests/codex_alert_carousel_test.cpp \
  main/apps/app_codex_status/codex_alert_carousel.cpp \
  -o /tmp/codex_alert_carousel_test && /tmp/codex_alert_carousel_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py -v
```

Expected: the host assertion sees a handled record rotate into view or the old
heading, and the source contract still finds `ALL HANDLED`.

### Task 2: Implement The Completed State Machine

**Files:**
- Modify: `main/apps/app_codex_status/codex_alert_carousel.cpp`

- [ ] **Step 1: Select the first actionable record on apply**

After copying alerts, initialize `_index` to the first pending record when
`pending_count > 0`; otherwise leave it at the first, most-recent handled row:

```cpp
_index = 0;
if (_pending_count > 0) {
    const auto pending = std::find_if(
        _alerts.begin(), _alerts.end(),
        [](const CodexTaskAlert& alert) { return alert.pending; }
    );
    if (pending != _alerts.end()) {
        _index = static_cast<size_t>(std::distance(_alerts.begin(), pending));
    }
}
```

- [ ] **Step 2: Rotate through pending records only**

Count pending records present in the bounded payload. Return false when fewer
than two are available. For elapsed rotations, advance to the next record whose
`pending` flag is true and never select a handled record:

```cpp
const size_t available_pending = static_cast<size_t>(std::count_if(
    _alerts.begin(), _alerts.end(),
    [](const CodexTaskAlert& alert) { return alert.pending; }
));
if (available_pending < 2 || now_ms - _last_rotation_ms < kRotationIntervalMs) {
    return false;
}
const uint32_t elapsed = now_ms - _last_rotation_ms;
const size_t steps = elapsed / kRotationIntervalMs;
const size_t rotations = steps % available_pending;
for (size_t step = 0; step < rotations; ++step) {
    do {
        _index = (_index + 1) % _alerts.size();
    } while (!_alerts[_index].pending);
}
_last_rotation_ms += static_cast<uint32_t>(steps) * kRotationIntervalMs;
return rotations != 0;
```

- [ ] **Step 3: Replace handled headings with the completed heading**

Keep `NO ALERTS` for an empty list. Return `COMPLETED` whenever
`pending_count == 0`; otherwise format only `PENDING X/N`:

```cpp
if (_alerts.empty()) return "NO ALERTS";
if (_pending_count == 0) return "COMPLETED";
std::snprintf(text, sizeof(text), "PENDING %d/%d", position,
              std::max(_pending_count, 1));
```

- [ ] **Step 4: Run focused tests and commit**

Run the host test and source-contract suite from Task 1. Both must pass. Commit
only the carousel behavior and its tests:

```bash
git add main/apps/app_codex_status/codex_alert_carousel.cpp \
  tests/codex_alert_carousel_test.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py
git commit -m "Show static completed Codex alerts"
```

### Task 3: Build And Flash Launcher Firmware

**Files:**
- Verify: `build/StopWatch-UserDemo.bin`

- [ ] **Step 1: Run the complete automated checks**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
git diff --check
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
```

Expected: all Python and host contract tests pass, there are no whitespace
errors, and the firmware fits the configured `ota_0` application partition.

- [ ] **Step 2: Flash only the launcher slot**

With `/dev/cu.usbmodem1101` connected, preserve NVS, assets, and assistant
`ota_1` by writing only the launcher binary:

```bash
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem1101 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x20000 build/StopWatch-UserDemo.bin
```

- [ ] **Step 3: Verify the real device**

Confirm the launcher boots without a reset loop, the Mac agent reconnects over
BLE, all-handled state reads green `COMPLETED`, the displayed handled detail
does not rotate, and two pending records still rotate as `PENDING 1/2` and
`PENDING 2/2`.
