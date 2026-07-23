# Codex Running Color Separation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the lower `RUNNING` alert heading in clear cyan `#49C9EE` while keeping project names in Beacon Violet and leaving every other Codex status-page behavior unchanged.

**Architecture:** Add one firmware palette token to `codex_status_ui_layout.h` and use it only in the `CodexTaskState::Running` branch of `AppCodexStatus::render_alert()`. Protect the semantic split with the native palette test and the existing source-contract test, then rebuild and validate the unified firmware before any separately authorized device write.

**Tech Stack:** C++17, LVGL, Python `unittest`, ESP-IDF 5.5.4, esptool, StopWatch unified build/flash scripts.

---

## File map

| File | Responsibility |
|---|---|
| `main/apps/app_codex_status/codex_status_ui_layout.h` | Own the approved Codex status-page palette, including the new Running cyan token. |
| `main/apps/app_codex_status/app_codex_status.cpp` | Map `CodexTaskState::Running` to the new token while preserving all other state mappings. |
| `tests/codex_status_ui_layout_test.cpp` | Lock exact palette values at native-test time. |
| `mac-codex-agent/tests/test_firmware_font_contract.py` | Lock the Running/project semantic color split in the firmware source contract. |

No new production file or abstraction is required.

### Task 1: Add the Running color contract and minimal firmware mapping

**Files:**
- Modify: `tests/codex_status_ui_layout_test.cpp:71-78`
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py:59-72`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h:16-24`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp:736-743`

- [ ] **Step 1: Write the failing native palette assertion**

Add the Running token assertion beside the existing palette assertions in
`tests/codex_status_ui_layout_test.cpp`:

```cpp
assert(kBeaconViolet == 0x7C9DFF);
assert(kRunningCyan == 0x49C9EE);
assert(kQuotaMint == 0x53D7A4);
```

- [ ] **Step 2: Strengthen the firmware source contract**

Replace `test_codex_alert_heading_uses_distinct_summary_colors()` with:

```python
def test_codex_alert_heading_uses_distinct_summary_colors(self):
    app = (self.codex_root / "app_codex_status.cpp").read_text(
        encoding="utf-8"
    )
    layout = (self.codex_root / "codex_status_ui_layout.h").read_text(
        encoding="utf-8"
    )
    carousel = (self.codex_root / "codex_alert_carousel.cpp").read_text(
        encoding="utf-8"
    )

    self.assertIn(
        "constexpr uint32_t kRunningCyan = 0x49C9EE", layout
    )
    self.assertIn("CodexTaskState::Running", app)
    self.assertIn("state_color = kRunningCyan;", app)
    self.assertIn(
        '_panel, "--", kAlertProjectY, &lv_font_montserrat_22, '
        "kBeaconViolet",
        app,
    )
    self.assertIn("kCompletedGreen", app)
    self.assertIn('return "COMPLETED";', carousel)
    self.assertIn('? "PENDING" : "RUNNING"', carousel)
    self.assertNotIn("ALL HANDLED", carousel)
    self.assertNotIn("all_handled", carousel)
```

This checks both halves of the approved distinction: Running must use cyan and
the project label must remain violet.

- [ ] **Step 3: Run both tests and verify the new contract fails**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test &&
  /tmp/codex_status_ui_layout_test

mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py
```

Expected: compilation fails because `kRunningCyan` is not declared, and the
Python suite fails because neither the token nor `state_color =
kRunningCyan;` exists yet.

- [ ] **Step 4: Add the approved palette token**

In `main/apps/app_codex_status/codex_status_ui_layout.h`, keep the existing
palette ordering and insert:

```cpp
constexpr uint32_t kBeaconViolet    = 0x7C9DFF;
constexpr uint32_t kRunningCyan     = 0x49C9EE;
constexpr uint32_t kQuotaMint       = 0x53D7A4;
```

- [ ] **Step 5: Map only Running alerts to the new token**

In `AppCodexStatus::render_alert()`, change only the Running branch:

```cpp
} else if (alert->state == CodexTaskState::Running) {
    state_label = "正在处理";
    state_color = kRunningCyan;
}
```

Do not change `kCodexBlue`, because the upper connection-status label continues
to use Beacon Violet.

- [ ] **Step 6: Run the focused tests and verify they pass**

Run the same two commands from Step 3.

Expected:

- Native layout executable exits `0`.
- Python output ends with `Ran 5 tests` and `OK`.

- [ ] **Step 7: Inspect the exact implementation diff**

Run:

```bash
git diff --check -- \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  tests/codex_status_ui_layout_test.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py

git diff --stat -- \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  tests/codex_status_ui_layout_test.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py
```

Expected: no whitespace errors and changes are limited to the four approved
files.

- [ ] **Step 8: Commit the tested color separation**

```bash
git add \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  tests/codex_status_ui_layout_test.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py
git commit -m "Differentiate Codex running status color"
```

### Task 2: Run the complete regression and unified-build gates

**Files:**
- Verify: `tests/codex_status_ui_layout_test.cpp`
- Verify: `tests/codex_alert_carousel_test.cpp`
- Verify: `tests/codex_pending_glow_logic_test.cpp`
- Verify: `tests/codex_status_model_test.cpp`
- Verify: `mac-codex-agent/tests/test_firmware_font_contract.py`
- Generate: `build-unified/StopWatch-UserDemo.bin`
- Generate: `build-unified/unified-runtime-metrics.json`

- [ ] **Step 1: Capture the exact verification HEAD**

```bash
VERIFY_HEAD="$(git rev-parse HEAD)"
printf 'VERIFY_HEAD=%s\n' "$VERIFY_HEAD"
```

Expected: the full commit ID of Task 1.

- [ ] **Step 2: Run all focused Codex regressions**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test &&
  /tmp/codex_status_ui_layout_test

mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py

clang++ -std=c++17 -Imain/apps/app_codex_status \
  tests/codex_alert_carousel_test.cpp \
  main/apps/app_codex_status/codex_alert_carousel.cpp \
  -o /tmp/codex_alert_carousel_test &&
  /tmp/codex_alert_carousel_test

c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/codex_pending_glow_logic_test &&
  /tmp/codex_pending_glow_logic_test

clang -c "$HOME/esp/esp-idf-v5.5.4/components/json/cJSON/cJSON.c" \
  -I"$HOME/esp/esp-idf-v5.5.4/components/json/cJSON" \
  -o /tmp/cJSON.o

clang++ -std=c++17 \
  -Imain/apps/app_codex_status \
  -I"$HOME/esp/esp-idf-v5.5.4/components/json/cJSON" \
  tests/codex_status_model_test.cpp \
  main/apps/app_codex_status/codex_status_model.cpp \
  /tmp/cJSON.o \
  -o /tmp/codex_status_model_test &&
  /tmp/codex_status_model_test
```

Expected: every command exits `0`; the Python contract reports five passing
tests.

- [ ] **Step 3: Run the complete Mac Agent suite**

```bash
PYTHONPATH=mac-codex-agent \
  mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: the suite exits `0` and ends with `OK`. Exception-path logging from
collector or BLE failure tests is expected when the final result is `OK`.

- [ ] **Step 4: Build the unified firmware**

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh" >/dev/null
./scripts/build_unified_candidate.sh
```

Expected: exit `0`, `build-unified/StopWatch-UserDemo.bin` exists, and
`build-unified/unified-runtime-metrics.json` reports
`"capacity_gate_passed": true`.

- [ ] **Step 5: Verify build identity and preserve artifact evidence**

```bash
python3 - <<'PY'
import hashlib
import json
import pathlib
import subprocess

root = pathlib.Path(".")
head = subprocess.check_output(
    ["git", "rev-parse", "HEAD"], text=True
).strip()
metrics = json.loads(
    (root / "build-unified/unified-runtime-metrics.json").read_text()
)
app = root / metrics["app_bin"]
assets = root / metrics["assets_bin"]

assert metrics["source_git_commit"] == head
assert metrics["build_project_version"] == head[:7]
assert metrics["capacity_gate_passed"] is True
assert hashlib.sha256(app.read_bytes()).hexdigest() == metrics["app_sha256"]
assert (
    hashlib.sha256(assets.read_bytes()).hexdigest()
    == metrics["assets_sha256"]
)
print(json.dumps({
    "head": head,
    "app_bytes": metrics["app_bytes"],
    "app_sha256": metrics["app_sha256"],
    "assets_bytes": metrics["assets_bytes"],
    "assets_sha256": metrics["assets_sha256"],
    "headroom_percent": metrics["app_headroom_percent"],
}, indent=2))
PY

test "$(git rev-parse HEAD)" = \
  "$(python3 -c 'import json; print(json.load(open("build-unified/unified-runtime-metrics.json"))["source_git_commit"])')"
```

Expected: all assertions pass, hashes are printed, and HEAD did not change
during verification.

### Task 3: Back up, flash, boot-check, and physically accept the watch

**Files:**
- Create: `hardware-evidence/firmware-backups/2026-07-24-before-running-cyan.bin`
- Create: `hardware-evidence/firmware-backups/2026-07-24-before-running-cyan.bin.sha256`
- Use: `build-unified/StopWatch-UserDemo.bin`
- Use: `build-unified/generated_assets.bin`

- [ ] **Step 1: Stop for explicit device-write authorization**

Report the verified HEAD, app hash, capacity result, detected serial port, and
the planned backup path. Ask the user to confirm the device write. Do not run a
backup reset or flash command before that confirmation.

- [ ] **Step 2: Re-probe the target immediately after authorization**

```bash
PORTS="$(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print | sort)"
printf '%s\n' "$PORTS"
test "$(printf '%s\n' "$PORTS" | sed '/^$/d' | wc -l | tr -d ' ')" = "1"
PORT="$PORTS"
```

Expected: exactly one usable StopWatch port. If no port or multiple ports are
present, stop without writing.

- [ ] **Step 3: Create and verify a fresh full-flash recovery image**

```bash
source scripts/resolve_esptool_python.sh
export PYTHON="$(resolve_esptool_python build-unified)"
BACKUP="hardware-evidence/firmware-backups/2026-07-24-before-running-cyan.bin"

./scripts/backup_watch_flash.sh --port "$PORT" --output "$BACKUP"
test "$(stat -f '%z' "$BACKUP")" = "16777216"
shasum -a 256 -c "$BACKUP.sha256"
```

Expected: a 16 MiB backup exists and its checksum reports `OK`.

- [ ] **Step 4: Reconfirm artifact identity immediately before writing**

```bash
test "$(git rev-parse HEAD)" = \
  "$(python3 -c 'import json; print(json.load(open("build-unified/unified-runtime-metrics.json"))["source_git_commit"])')"
python3 - <<'PY'
import hashlib
import json
import pathlib
import subprocess

metrics = json.loads(
    pathlib.Path("build-unified/unified-runtime-metrics.json").read_text()
)
head = subprocess.check_output(
    ["git", "rev-parse", "HEAD"], text=True
).strip()
app = pathlib.Path(metrics["app_bin"])
assert metrics["source_git_commit"] == head
assert hashlib.sha256(app.read_bytes()).hexdigest() == metrics["app_sha256"]
assert metrics["capacity_gate_passed"] is True
print(metrics["app_sha256"])
PY
```

Expected: all assertions pass and the exact app hash is printed.

- [ ] **Step 5: Flash the unified candidate once**

```bash
./scripts/flash_unified_candidate.sh \
  --port "$PORT" \
  --backup "$BACKUP"
```

Expected: bootloader, partition table, unified app, and assets each report
successful write/hash verification, followed by a hard reset. Do not repeat the
flash if the wrapper output is interrupted after esptool has already verified
all four regions.

- [ ] **Step 6: Validate boot without another reset or write**

```bash
python3 scripts/validate_watch_boot.py \
  --port "$PORT" \
  --no-reset \
  --timeout 45 \
  --log /tmp/stopwatch-running-cyan-boot.log \
  --summary /tmp/stopwatch-running-cyan-boot-summary.json
```

Expected: exit `0`, summary result `passed`, and Launcher reports `open`.

- [ ] **Step 7: Ask for physical screen acceptance**

Ask the user to verify on the watch:

- `RUNNING` is clear cyan.
- The project name remains Beacon Violet.
- The two labels are immediately distinguishable.
- Pending remains red and Completed remains green when those states appear.
- No layout, truncation, pet, quota, divider, or battery regression is visible.

Record physical acceptance separately from build, flash, hash, and boot
evidence.
