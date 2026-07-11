# Codex Alert Outcome Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show only project, concise conversation title, and completed/waiting outcome for each Codex alert while simplifying the model/effort arcs and coloring `ALL HANDLED` green.

**Architecture:** Extend the persistent alert reducer with a pure last-paragraph outcome classifier and live thread-metadata synchronization. Propagate the outcome through the existing Mac snapshot and bounded BLE payload, then let the firmware parser and carousel render a localized result label without task content or time.

**Tech Stack:** Python 3.9, unittest, SQLite/JSONL, PyObjC/AppKit, BLE JSON framing, ESP-IDF 5.5.4, C++17, cJSON, LVGL 9.

---

### Task 1: Classify Alert Outcomes And Refresh Conversation Titles

**Files:**
- Modify: `mac-codex-agent/task_alerts.py`
- Modify: `mac-codex-agent/tests/test_task_alerts.py`

- [ ] **Step 1: Write failing classifier tests**

Add table-driven coverage for completed work, a final question, an explicit confirmation request, a required physical action, an English request, and a question quoted before a final completed paragraph:

```python
from task_alerts import classify_outcome

def test_classifies_only_the_final_paragraph(self):
    cases = (
        ("Build and tests passed.", "completed"),
        ("Build passed.\n\n可以确认这个方案吗？", "waiting"),
        ("Everything is ready.\n\n请回复确认后继续。", "waiting"),
        ("请重新插入手表的数据线。", "waiting"),
        ("Please provide the missing token.", "waiting"),
        ("The log previously asked 'continue?'.\n\nThe repair is complete.", "completed"),
        ("", "completed"),
    )
    for message, expected in cases:
        with self.subTest(message=message):
            self.assertEqual(classify_outcome(message), expected)
```

- [ ] **Step 2: Write failing persistence and metadata tests**

Extend the rollout fixture helper so `task_complete` can include `last_agent_message`. Assert that a waiting completion persists `outcome="waiting"`, old ledger rows without `outcome` load as completed, and changing a thread title from a full prompt to `添加蓝牙开关` updates an existing alert and increments revision exactly once.

- [ ] **Step 3: Run the focused tests and observe failure**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_task_alerts.py -v
```

Expected: failures because `classify_outcome`, `TaskAlert.outcome`, metadata synchronization, and migration do not exist.

- [ ] **Step 4: Implement the pure classifier**

Add a public pure function that inspects only the final non-empty paragraph:

```python
WAITING_PHRASES = (
    "请确认", "请回复", "请提供", "请选择", "需要你", "可以吗",
    "请重新", "告诉我", "等待你的", "无法继续",
    "let me know", "please confirm", "please reply", "please provide",
    "please choose", "need your", "waiting for your", "cannot continue",
)

def classify_outcome(message: Any) -> str:
    if not isinstance(message, str):
        return "completed"
    paragraphs = [part.strip() for part in message.split("\n\n") if part.strip()]
    if not paragraphs:
        return "completed"
    final = paragraphs[-1]
    lowered = final.casefold()
    if final.endswith(("?", "？")) or any(
        phrase in lowered for phrase in WAITING_PHRASES
    ):
        return "waiting"
    return "completed"
```

- [ ] **Step 5: Persist outcome and synchronize thread metadata**

Add `outcome: str` to `TaskAlert`; write `classify_outcome(payload.get("last_agent_message"))` on `task_complete`. During every refresh, map discovered threads by ID and update each matching alert's project/title when the current thread row changes:

```python
def _sync_thread_metadata(self, threads: List[_ThreadInfo]) -> bool:
    current = {thread.id: thread for thread in threads}
    changed = False
    for item in self._state["alerts"]:
        thread = current.get(item.get("thread_id"))
        if thread is None:
            continue
        for key, value in (("project", thread.project), ("title", thread.title)):
            if item.get(key) != value:
                item[key] = value
                changed = True
    if changed:
        self._state["revision"] = int(self._state.get("revision", 0)) + 1
    return changed
```

Normalize missing or invalid ledger outcomes to `completed` after load and persist that migration on the next refresh without changing acknowledgement or notification flags.

- [ ] **Step 6: Run focused tests and commit**

Run the focused suite again and expect all tracker tests to pass. Commit only tracker files:

```bash
git add mac-codex-agent/task_alerts.py mac-codex-agent/tests/test_task_alerts.py
git commit -m "Track concise Codex alert outcomes"
```

### Task 2: Propagate Outcomes Through Mac Menu And BLE

**Files:**
- Modify: `mac-codex-agent/codex_adapter.py`
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/app_state.py`
- Modify: `mac-codex-agent/menu_app.py`
- Modify: `mac-codex-agent/tests/test_codex_adapter.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_app_state.py`
- Modify: `mac-codex-agent/tests/test_menu_app.py`

- [ ] **Step 1: Write failing propagation tests**

Add `outcome="completed"` and `outcome="waiting"` to fixture `TaskAlert` values. Assert that collector JSON contains the outcome, `TaskAlertView` rejects invalid outcomes, the watch-bounded payload retains valid outcomes, and menu rows render:

```text
StopWatch · Completed · 添加蓝牙开关 · 09:41
website · Waiting for Instruction · 修复首页导航 · 09:38
```

- [ ] **Step 2: Run focused tests and observe failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_codex_adapter.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_app_state.py \
  mac-codex-agent/tests/test_menu_app.py -v
```

Expected: failures because outcome is not part of collector JSON, bounded watch records, or menu state.

- [ ] **Step 3: Add outcome to the common data model**

Emit `"outcome": alert.outcome` in `CodexStateCollector.get_state()`. Add `outcome` to `TaskAlertView`, accepting only `completed` and `waiting`. Include outcome in `_watch_status()` while keeping the eight-record and 2048-byte limits:

```python
"outcome": (
    str(item.get("outcome") or "completed")
    if item.get("outcome") in ("completed", "waiting")
    else "completed"
),
```

- [ ] **Step 4: Add outcome labels to the Mac ledger**

Map outcomes to `Completed` and `Waiting for Instruction` in `_rebuild_alert_submenu()` and insert the label between conversation title and time. Keep pending rows orange, handled rows secondary, and retain the complete current-day ledger.

- [ ] **Step 5: Run focused tests and commit**

Run the four focused test modules and expect all to pass. Commit the propagation files as one contract change:

```bash
git add mac-codex-agent/codex_adapter.py mac-codex-agent/agent.py \
  mac-codex-agent/app_state.py mac-codex-agent/menu_app.py \
  mac-codex-agent/tests/test_codex_adapter.py mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_app_state.py mac-codex-agent/tests/test_menu_app.py
git commit -m "Show Codex alert outcomes in the Mac agent"
```

### Task 3: Parse And Render Completed Or Waiting On StopWatch

**Files:**
- Modify: `main/apps/app_codex_status/codex_status_model.h`
- Modify: `main/apps/app_codex_status/codex_status_model.cpp`
- Modify: `main/apps/app_codex_status/codex_alert_carousel.h`
- Modify: `main/apps/app_codex_status/codex_alert_carousel.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py`

- [ ] **Step 1: Extend the firmware source contract test and observe failure**

Assert that the firmware model contains a waiting flag, the parser accepts `completed`/`waiting`, Activity detail uses `已完成`/`等待指令`, no longer concatenates `alert->time`, and the carousel exposes `all_handled()`.

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py -v
```

Expected: failure because the outcome contract and rendering do not exist.

- [ ] **Step 2: Extend the bounded firmware alert model**

Add `bool waiting = false` to `CodexTaskAlert`. In `parse_alerts`, treat a missing outcome as completed, set waiting for `"waiting"`, and reject the array when a present outcome is neither `completed` nor `waiting`:

```cpp
cJSON* outcome = cJSON_GetObjectItemCaseSensitive(item, "outcome");
bool waiting = false;
if (outcome != nullptr) {
    if (!cJSON_IsString(outcome) || outcome->valuestring == nullptr) return false;
    const std::string value = outcome->valuestring;
    if (value != "completed" && value != "waiting") return false;
    waiting = value == "waiting";
}
```

- [ ] **Step 3: Expose acknowledgement summary from the carousel**

Add pure methods:

```cpp
bool empty() const { return _alerts.empty(); }
bool all_handled() const { return !_alerts.empty() && _pending_count == 0; }
```

Keep revision reset and three-second rotation unchanged.

- [ ] **Step 4: Render only project, conversation, and outcome**

Replace the time detail with:

```cpp
const std::string detail =
    std::string(alert->waiting ? "等待指令" : "已完成") + " \xC2\xB7 " + alert->title;
```

Continue using `font_puhui_14_1` for project and detail so Chinese remains readable.

- [ ] **Step 5: Color summary headings by state**

Define `kHandledGreen = 0x3CB371`. In `render_alert()`, set heading color to dim gray for empty, green when `all_handled()`, red when there are pending alerts, and never derive the global heading color from the currently rotated handled row.

- [ ] **Step 6: Run the contract test and build firmware**

Run the focused Python contract, then:

```bash
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
```

Expected: build succeeds and `StopWatch-UserDemo.bin` remains below the `0x4f0000` application partition.

- [ ] **Step 7: Commit firmware outcome support**

```bash
git add main/apps/app_codex_status/codex_status_model.h \
  main/apps/app_codex_status/codex_status_model.cpp \
  main/apps/app_codex_status/codex_alert_carousel.h \
  main/apps/app_codex_status/codex_alert_carousel.cpp \
  main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py
git commit -m "Show completed and waiting Codex alerts on StopWatch"
```

### Task 4: Simplify Model And Effort Arcs

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py`

- [ ] **Step 1: Add a failing direct-value layout contract**

Assert the Codex app no longer creates static `MODEL`, `EFFORT`, `auto`, or `reason` labels, while `_model_value` and `_effort_value` remain present with fixed widths and dot truncation.

- [ ] **Step 2: Run the contract and observe failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py -v
```

Expected: failure on the four legacy captions.

- [ ] **Step 3: Center direct values inside each arc**

Remove the four static captions. Create `_model_value` at the arc center using Montserrat 14 and `_effort_value` using Montserrat 18. Set each to width 92, centered alignment, and `LV_LABEL_LONG_MODE_DOTS`; retain uppercase effort conversion and current model truncation safety.

- [ ] **Step 4: Re-run the contract and firmware build**

Run the focused contract and `idf.py build`. Confirm the binary remains below `0x4f0000` and no labels overlap the pet image.

- [ ] **Step 5: Commit the layout simplification**

```bash
git add main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py
git commit -m "Simplify StopWatch model and effort display"
```

### Task 5: Install, Flash, And Verify The Complete Flow

**Files:**
- Modify: `README.md`
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Update documentation**

Document concise conversation-title synchronization, completed/waiting classification, acknowledgement semantics, green `ALL HANDLED`, and direct model/effort values. State that classification is local and heuristic.

- [ ] **Step 2: Run final automated checks**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover -s mac-codex-agent/tests -v
bash -n mac-codex-agent/install_launch_agent.sh
git diff --check
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
```

Expected: every test passes, shell syntax and whitespace checks are clean, and firmware fits the application partition.

- [ ] **Step 3: Reinstall and manually start the Mac agent**

Quit `com.stopwatch.codex-agent`, run `mac-codex-agent/install_launch_agent.sh`, open `~/Applications/StopWatch Codex Agent.app`, verify code signing, and confirm no LaunchAgent plist/service exists.

- [ ] **Step 4: Flash launcher only**

Detect `/dev/cu.usbmodem*` and write only:

```bash
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x20000 build/StopWatch-UserDemo.bin
```

Do not erase or write NVS, `ota_1`, storage, or assets.

- [ ] **Step 5: Verify real device behavior**

Confirm boot logs load `ota_0`, Launcher opens without a reset loop, Mac BLE reconnects after opening Codex, a completed turn displays `已完成 · <short title>`, an interaction request displays `等待指令 · <short title>`, replying handles only that conversation, and `ALL HANDLED` turns green.

- [ ] **Step 6: Commit documentation**

```bash
git add README.md mac-codex-agent/README.md
git commit -m "Document concise Codex alert outcomes"
```
