# Codex and Trae Unified Status Pages Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Trae project/task status page to the existing Codex watch app, opened by a right-to-left swipe, while preserving the current Codex behavior and one Mac client/one BLE connection.

**Architecture:** Keep Codex and Trae collection, caching, freshness, serialization, and firmware models independent, then compose them only at the existing Mac runtime and BLE transport boundaries. Send separate source-tagged status messages over the current framed BLE service. On the watch, route messages into two persistent page models and slide a two-page LVGL strip behind shared page dots and battery UI. Trae log parsing is incremental, cursor-backed, sanitized, and fail-closed because its local formats are private and version-dependent.

**Tech Stack:** Python 3, PyObjC, `bleak`, SQLite read-only access, `unittest`, C++17, LVGL 9, cJSON, NimBLE, native host tests, ESP-IDF 5.5.4.

---

### Task 1: Lock the Trae log event and task-state contract

**Files:**
- Create: `mac-codex-agent/trae_tasks.py`
- Create: `mac-codex-agent/tests/fixtures/trae/agent.log`
- Create: `mac-codex-agent/tests/test_trae_tasks.py`

- [ ] **Step 1: Create a sanitized representative log fixture**

Create `mac-codex-agent/tests/fixtures/trae/agent.log` containing invented identifiers only:

```text
2026-07-24 10:00:00 model_info: CustomModel { config_name: "model-a", display_model_name: Some("Model A"), reasoning_effort: Some("high") }
2026-07-24 10:00:01 execute_task:start session_id=session-a task_id=task-a
2026-07-24 10:00:02 [append_resume_listener] started session_id=session-a task_id=task-a
2026-07-24 10:00:03 progress_notice session_id=session-a task_id=task-a
2026-07-24 10:00:04 ToolCall arrived name=AskUserQuestion session_id=session-a task_id=task-a
2026-07-24 10:00:05 [append_resume_listener] exiting session_id=session-a task_id=task-a
```

The fixture must not contain copied user prompts, responses, tool arguments, endpoints, device data, or production session IDs.

- [ ] **Step 2: Write failing parser and priority tests**

Create `mac-codex-agent/tests/test_trae_tasks.py` with tests for:

```python
class TraeTaskTests(unittest.TestCase):
    def test_parser_maps_start_progress_pending_and_exit(self): ...
    def test_generic_listener_exit_does_not_clear_an_existing_pending_state(self): ...
    def test_new_execution_after_pending_returns_the_task_to_running(self): ...
    def test_unknown_or_malformed_lines_produce_no_event(self): ...
    def test_parser_result_never_contains_raw_line(self): ...
    def test_priority_is_pending_then_running_then_latest_completed(self): ...
    def test_transport_selection_is_stable_and_limited_to_four(self): ...
```

Assert the public task state strings are exactly `pending`, `running`, and `completed`.

- [ ] **Step 3: Run the focused test and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_tasks.py
```

Expected: import failure because `trae_tasks.py` does not exist.

- [ ] **Step 4: Implement the smallest event parser and selector**

Create these immutable public types in `mac-codex-agent/trae_tasks.py`:

```python
@dataclass(frozen=True)
class TraeLogEvent:
    kind: Literal["started", "progress", "pending", "completed", "model"]
    session_id: str | None = None
    task_id: str | None = None
    model: str | None = None
    effort: str | None = None

@dataclass(frozen=True)
class TraeTask:
    session_id: str
    task_id: str
    project: str
    title: str
    detail: str
    state: Literal["pending", "running", "completed"]
    updated_at: float
```

Add:

```python
def parse_trae_log_event(line: str) -> TraeLogEvent | None: ...
def select_transport_tasks(
    tasks: Iterable[TraeTask], limit: int = 4
) -> tuple[TraeTask, ...]: ...
```

Recognize only the approved lifecycle markers. Extract identifiers and selected `model_info: CustomModel` fields with bounded regular expressions. Do not retain the input line on any returned object or log it on parse failure. Keep transport ordering deterministic; the watch owns the three-second carousel timer.

- [ ] **Step 5: Run the focused test and verify GREEN**

Run the command from Step 3.

Expected: all tests pass.

- [ ] **Step 6: Commit the task-state contract**

```bash
git add mac-codex-agent/trae_tasks.py \
  mac-codex-agent/tests/fixtures/trae/agent.log \
  mac-codex-agent/tests/test_trae_tasks.py
git commit -m "feat: define sanitized Trae task events"
```

### Task 2: Add incremental, persistent Trae log tracking

**Files:**
- Modify: `mac-codex-agent/trae_tasks.py`
- Modify: `mac-codex-agent/tests/test_trae_tasks.py`

- [ ] **Step 1: Write failing cursor lifecycle tests**

Add tests that create temporary log and ledger files:

```python
def test_first_observation_baselines_at_eof_without_replaying_history(self): ...
def test_first_observation_scans_only_a_bounded_tail_for_latest_model_metadata(self): ...
def test_refresh_reads_only_bytes_appended_after_cursor(self): ...
def test_cursor_survives_tracker_recreation(self): ...
def test_truncation_or_inode_change_rebaselines_safely(self): ...
def test_ledger_contains_normalized_metadata_not_raw_prompt_or_log(self): ...
```

Assert the persisted cursor records path, inode, byte offset, and modification time. Assert the normalized ledger contains only IDs, sanitized labels, state, and timestamps.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_tasks.py
```

Expected: failures because `TraeTaskTracker` and cursor persistence do not exist.

- [ ] **Step 3: Implement cursor-backed tracking**

Add:

```python
@dataclass(frozen=True)
class TraeLogCursor:
    path: str
    inode: int
    offset: int
    mtime_ns: int

@dataclass(frozen=True)
class TraeTaskSnapshot:
    tasks: tuple[TraeTask, ...]
    model: str | None
    effort: str | None
    revision: int

class TraeTaskTracker:
    def refresh(self, log_paths: Sequence[Path], now: float) -> TraeTaskSnapshot: ...
```

Implementation rules:

- First observation of an existing log stores EOF and emits no historical tasks.
- On that first observation only, inspect at most the final 256 KiB for the newest valid `model_info: CustomModel` event; do not interpret lifecycle markers from this metadata tail.
- Later refreshes open the file in binary mode, seek to the stored byte offset, and decode only complete appended lines with replacement for invalid UTF-8.
- File replacement, inode change, or size smaller than offset causes a safe EOF rebaseline.
- `AskUserQuestion` keeps a task Pending across a later generic listener-exit marker; a new execution for the same task clears Pending back to Running.
- A listener-exit marker completes only a non-Pending task.
- Cursor and normalized ledger writes use a temporary sibling plus `os.replace`.
- Ledger entries never store raw log lines, prompts, responses, tool arguments, or tool outputs.
- Keep at most 32 normalized task records and expose at most four visible tasks.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run the command from Step 2.

Expected: all tests pass.

- [ ] **Step 5: Commit incremental tracking**

```bash
git add mac-codex-agent/trae_tasks.py \
  mac-codex-agent/tests/test_trae_tasks.py
git commit -m "feat: track Trae logs incrementally"
```

### Task 3: Collect Trae liveness, model, effort, project, and sanitized task text

**Files:**
- Create: `mac-codex-agent/trae_adapter.py`
- Create: `mac-codex-agent/tests/test_trae_adapter.py`

- [ ] **Step 1: Write failing adapter tests with injected filesystem and process probes**

Cover:

```python
class TraeStateCollectorTests(unittest.TestCase):
    def test_offline_state_does_not_touch_logs(self): ...
    def test_online_state_reports_actual_selected_model_and_effort(self): ...
    def test_first_run_can_use_session_correlated_current_agent_metadata(self): ...
    def test_none_effort_becomes_double_dash(self): ...
    def test_project_prefers_session_correlated_sandbox_mapping(self): ...
    def test_project_falls_back_to_workspace_json_folder_name(self): ...
    def test_task_text_is_single_line_bounded_and_control_character_free(self): ...
    def test_uncorrelated_input_history_is_ignored(self): ...
    def test_missing_or_invalid_private_state_fails_closed(self): ...
    def test_collector_never_returns_raw_prompt_response_or_log_content(self): ...
```

Build all state in a temporary directory. If SQLite is used in a test, create a minimal `state.vscdb` with test-owned invented values.

- [ ] **Step 2: Run the adapter test and verify RED**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_adapter.py
```

Expected: import failure because `trae_adapter.py` does not exist.

- [ ] **Step 3: Implement explicit paths and injectable probes**

Create:

```python
@dataclass(frozen=True)
class TraePaths:
    logs: Path
    workspace_storage: Path
    sandbox: Path
    cursor: Path
    ledger: Path

class TraeStateCollector:
    def __init__(
        self,
        paths: TraePaths,
        process_is_running: Callable[[], bool],
        now: Callable[[], float] = time.time,
    ): ...

    def get_state(self) -> dict[str, object]: ...
```

The production process probe checks bundle identifier `cn.trae.app`. Default paths live below `~/Library/Application Support/Trae CN` for source data and below the existing StopWatch agent cache directory for cursor/ledger data.

- [ ] **Step 4: Implement bounded metadata resolution**

The collector must:

- Read model and effort only from parsed selected `model_info: CustomModel` events or explicitly selected fields in session-correlated current-agent state; never infer a selection from the list of supported models or efforts.
- Resolve project from the task/session-correlated sandbox JSON first, then the newest valid `workspace.json`.
- Read only allowlisted `state.vscdb` keys through a read-only SQLite URI when task text is explicitly correlated to the current session; ignore global or uncorrelated input history.
- Collapse whitespace, remove control characters, and bound project/title/detail before returning them.
- Never open or depend on `ModularData/ai-agent/database.db`.
- Return `trae_online`, `trae_model`, `trae_effort`, `trae_revision`, and `trae_tasks`.
- Use `--` for unavailable model/effort and an empty task list on an unrecognized format.

- [ ] **Step 5: Run adapter and tracker tests and verify GREEN**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_tasks.py \
  mac-codex-agent/tests/test_trae_adapter.py
```

Expected: all tests pass.

- [ ] **Step 6: Commit the Trae collector**

```bash
git add mac-codex-agent/trae_adapter.py \
  mac-codex-agent/tests/test_trae_adapter.py
git commit -m "feat: collect bounded Trae status"
```

### Task 4: Compose Codex and Trae without sharing failures

**Files:**
- Create: `mac-codex-agent/combined_collector.py`
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/app_state.py`
- Modify: `mac-codex-agent/menu_app.py`
- Create: `mac-codex-agent/tests/test_combined_collector.py`
- Modify: `mac-codex-agent/tests/test_runtime.py`
- Modify: `mac-codex-agent/tests/test_app_state.py`
- Modify: `mac-codex-agent/tests/test_menu_app.py`

- [ ] **Step 1: Write failing isolation and snapshot tests**

Tests must prove:

- A Trae exception preserves the newest Codex values.
- A Codex exception preserves the newest Trae values.
- Each source keeps its own last-known cache and collection timestamp.
- Codex pending-notification methods still delegate only to `CodexStateCollector`.
- `AgentSnapshot` contains a nested `TraeSnapshot`.
- The menu adds a Trae section with online/model/effort/project/task state, without changing the application title or bundle identity.

- [ ] **Step 2: Run the focused tests and verify RED**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_combined_collector.py \
  mac-codex-agent/tests/test_runtime.py \
  mac-codex-agent/tests/test_app_state.py \
  mac-codex-agent/tests/test_menu_app.py
```

Expected: failures for the missing composite collector and Trae snapshot.

- [ ] **Step 3: Implement independent source caches**

Create:

```python
class CompositeStateCollector:
    def get_state(self) -> dict[str, object]: ...
    def consume_pending_notification(self): ...
    def claim_pending_notification(self, notification): ...
```

Call each collector separately inside `try/except`. On a source failure, keep that source's last normalized state and mark only its freshness stale. Log the source name and exception class only; never log exception text, source payloads, or input lines.

- [ ] **Step 4: Extend runtime and menu snapshots**

Add a frozen `TraeSnapshot` containing:

```python
online: bool
model: str
effort: str
project: str
task_state: str
task_title: str
task_detail: str
fresh: bool
```

Update the runtime wiring to construct `CompositeStateCollector(CodexStateCollector(...), TraeStateCollector(...))`. Preserve the current `StopWatch Codex Agent` name and `com.stopwatch.codex-agent` identity.

- [ ] **Step 5: Run focused and full Mac tests**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_combined_collector.py \
  mac-codex-agent/tests/test_runtime.py \
  mac-codex-agent/tests/test_app_state.py \
  mac-codex-agent/tests/test_menu_app.py

mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all tests pass.

- [ ] **Step 6: Commit composition and menu state**

```bash
git add mac-codex-agent/combined_collector.py \
  mac-codex-agent/runtime.py \
  mac-codex-agent/app_state.py \
  mac-codex-agent/menu_app.py \
  mac-codex-agent/tests/test_combined_collector.py \
  mac-codex-agent/tests/test_runtime.py \
  mac-codex-agent/tests/test_app_state.py \
  mac-codex-agent/tests/test_menu_app.py
git commit -m "feat: compose Codex and Trae status"
```

### Task 5: Send separate source-tagged BLE status messages

**Files:**
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_protocol.py`

- [ ] **Step 1: Write failing serialization and cadence tests**

Add tests for:

```python
def test_codex_payload_excludes_all_trae_fields(self): ...
def test_codex_payload_keeps_legacy_shape_without_source_tag(self): ...
def test_trae_payload_is_source_tagged_and_bounded(self): ...
def test_trae_payload_keeps_at_most_four_tasks(self): ...
def test_unicode_truncation_produces_valid_utf8_json(self): ...
def test_unchanged_trae_state_uses_six_second_heartbeat(self): ...
def test_changed_trae_state_sends_immediately(self): ...
def test_reconnect_order_is_time_then_codex_then_trae(self): ...
def test_trae_state_never_emits_a_chime_event(self): ...
```

Assert the encoded Trae JSON is at most 1024 bytes and the framed message remains below `MAX_MESSAGE_SIZE`.

- [ ] **Step 2: Run the focused tests and verify RED**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_protocol.py
```

Expected: failures because the agent currently emits one combined status payload.

- [ ] **Step 3: Split serialization at the existing sender**

Add private helpers:

```python
def _watch_codex_status(status: Mapping[str, object]) -> dict[str, object]: ...
def _watch_trae_status(status: Mapping[str, object]) -> dict[str, object]: ...
def _trae_fingerprint(payload: Mapping[str, object]) -> str: ...
```

Keep the Codex payload in its current legacy shape so installed old firmware continues to consume it. The Trae shape is:

```json
{
  "type": "status",
  "source": "trae",
  "trae_online": true,
  "trae_model": "Model A",
  "trae_effort": "high",
  "trae_revision": 12,
  "trae_tasks": [
    {
      "project": "StopWatch",
      "title": "Add Trae status",
      "detail": "Implementing the task parser",
      "time": "10:00",
      "state": "running"
    }
  ]
}
```

Limits before JSON encoding: four tasks, 32 characters for project, 48 for title, and 72 for detail. If the byte cap is still exceeded, drop lowest-priority tasks first, then shorten detail by Unicode code points. Never slice encoded UTF-8 bytes.

- [ ] **Step 4: Add change-driven sending plus heartbeat**

Keep the current two-second collector/tick cadence for Codex. Send Trae only when its canonical fingerprint changes or six seconds have elapsed since its last send. Reset both source-send markers on reconnect, then send time sync, Codex, and Trae in that order over the existing status message type and BLE service.

- [ ] **Step 5: Run focused and full Mac tests**

Run the command from Step 2, then:

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all tests pass.

- [ ] **Step 6: Commit source-specific transport behavior**

```bash
git add mac-codex-agent/agent.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_protocol.py
git commit -m "feat: send Trae status over shared BLE"
```

### Task 6: Route and store Trae messages independently on the watch

**Files:**
- Create: `main/apps/app_codex_status/status_message_router.h`
- Create: `main/apps/app_codex_status/status_message_router.cpp`
- Create: `main/apps/app_codex_status/trae_status_model.h`
- Create: `main/apps/app_codex_status/trae_status_model.cpp`
- Create: `main/apps/app_codex_status/trae_task_carousel.h`
- Create: `main/apps/app_codex_status/trae_task_carousel.cpp`
- Create: `tests/status_message_router_test.cpp`
- Create: `tests/trae_status_model_test.cpp`
- Create: `tests/trae_task_carousel_test.cpp`
- Modify: `tests/codex_status_model_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write failing native router/model/carousel tests**

Lock these rules:

- Missing `source` and `source == "codex"` route to the existing Codex parser.
- `source == "trae"` routes to the Trae parser.
- Unknown sources are rejected without mutating either model.
- The existing Codex parser returns Invalid for a source-tagged Trae payload, so the legacy handler does not apply it, proving old-firmware compatibility.
- Trae requires `type == "status"` and accepts at most four valid tasks.
- Invalid state strings or malformed task arrays reject the entire Trae update.
- Model, effort, project, title, and detail are copied into fixed-size owned buffers.
- Revision and received timestamp belong only to Trae.
- Carousel priority is Pending, Running, then newest Completed; equal-state entries rotate every three seconds.

- [ ] **Step 2: Compile tests and verify RED**

```bash
TRAE_CJSON_DIR="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}/components/json/cJSON"
cc -Wall -Wextra -Werror -I"$TRAE_CJSON_DIR" \
  -c "$TRAE_CJSON_DIR/cJSON.c" -o /tmp/trae-cJSON.o

c++ -std=c++17 -Wall -Wextra -Werror -Imain -I"$TRAE_CJSON_DIR" \
  tests/status_message_router_test.cpp \
  main/apps/app_codex_status/status_message_router.cpp \
  /tmp/trae-cJSON.o \
  -o /tmp/status_message_router_test

c++ -std=c++17 -Wall -Wextra -Werror -Imain -I"$TRAE_CJSON_DIR" \
  tests/trae_status_model_test.cpp \
  main/apps/app_codex_status/trae_status_model.cpp \
  /tmp/trae-cJSON.o \
  -o /tmp/trae_status_model_test

c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/trae_task_carousel_test.cpp \
  main/apps/app_codex_status/trae_task_carousel.cpp \
  -o /tmp/trae_task_carousel_test
```

Expected: compilation fails because the new files do not exist.

- [ ] **Step 3: Implement the pure router and fixed-size model**

Use:

```cpp
enum class StatusMessageSource {
    LegacyCodex,
    Codex,
    Trae,
    Unknown,
};

enum class TraeTaskState {
    Pending,
    Running,
    Completed,
};
```

`TraeStatusSnapshot` owns `online`, `model`, `effort`, `revision`, `received_at_ms`, and an array of four `TraeTask` values. Parsing happens into a temporary snapshot and commits only after full validation.

- [ ] **Step 4: Implement deterministic task selection**

Keep selection and rotation pure and independent of LVGL. Use the injected `now_ms` and stable task update order so host tests do not depend on wall-clock time.

- [ ] **Step 5: Add all three host tests to offline validation**

Add explicit compile-and-run stages for the router, Trae model, Trae carousel, and existing Codex model tests to `scripts/run_unified_offline_validation.sh`. Add a Trae-payload Invalid assertion to `tests/codex_status_model_test.cpp`. Use the commands from Step 2 and the same cJSON object for both model parsers. Renumber the displayed stages without removing any current gate.

- [ ] **Step 6: Run native tests and the offline validation**

```bash
scripts/run_unified_offline_validation.sh
```

Expected: every stage passes, including the three new host tests and the existing clean firmware build.

- [ ] **Step 7: Commit firmware message models**

```bash
git add main/apps/app_codex_status/status_message_router.h \
  main/apps/app_codex_status/status_message_router.cpp \
  main/apps/app_codex_status/trae_status_model.h \
  main/apps/app_codex_status/trae_status_model.cpp \
  main/apps/app_codex_status/trae_task_carousel.h \
  main/apps/app_codex_status/trae_task_carousel.cpp \
  tests/status_message_router_test.cpp \
  tests/trae_status_model_test.cpp \
  tests/trae_task_carousel_test.cpp \
  tests/codex_status_model_test.cpp \
  scripts/run_unified_offline_validation.sh
git commit -m "feat: model Trae status on watch"
```

### Task 7: Lock page-swipe recognition and global-edge arbitration

**Files:**
- Create: `main/apps/app_codex_status/status_page_swipe_logic.h`
- Create: `main/apps/app_codex_status/status_page_swipe_logic.cpp`
- Create: `tests/status_page_swipe_logic_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write the failing pure gesture test**

Cover:

```cpp
assert(right_to_left_fast_swipe_moves_codex_to_trae());
assert(left_to_right_fast_swipe_moves_trae_to_codex());
assert(long_horizontal_drag_moves_page());
assert(vertical_drag_does_not_move_page());
assert(short_slow_drag_snaps_back());
assert_codex_page_does_not_wrap_left();
assert_trae_page_does_not_wrap_right();
assert_cancel_clears_active_gesture();
```

Use the approved thresholds: fast distance at least 36 px within 260 ms, or total horizontal drag at least 100 px; horizontal distance must be greater than vertical distance.

- [ ] **Step 2: Compile and verify RED**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/status_page_swipe_logic_test.cpp \
  main/apps/app_codex_status/status_page_swipe_logic.cpp \
  -o /tmp/status_page_swipe_logic_test
```

Expected: compilation fails because the swipe logic does not exist.

- [ ] **Step 3: Implement a pure press/move/release state machine**

Expose:

```cpp
class StatusPageSwipeLogic {
public:
    void press(int x, int y, uint32_t now_ms);
    void move(int x, int y);
    StatusPageSwipeResult release(uint32_t now_ms, int current_page);
    void cancel();
};
```

The logic returns only `Stay`, `Codex`, or `Trae`; LVGL animation remains outside this class.

- [ ] **Step 4: Add the native test to offline validation and run it**

Update `scripts/run_unified_offline_validation.sh`, then run:

```bash
scripts/run_unified_offline_validation.sh
```

Expected: all stages pass.

- [ ] **Step 5: Commit swipe behavior**

```bash
git add main/apps/app_codex_status/status_page_swipe_logic.h \
  main/apps/app_codex_status/status_page_swipe_logic.cpp \
  tests/status_page_swipe_logic_test.cpp \
  scripts/run_unified_offline_validation.sh
git commit -m "feat: add status page swipe logic"
```

### Task 8: Build the approved C-layout Trae page and two-page strip

**Files:**
- Create: `main/apps/app_codex_status/trae_status_page.h`
- Create: `main/apps/app_codex_status/trae_status_page.cpp`
- Create: `main/apps/app_codex_status/trae_status_ui_layout.h`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h`
- Create: `tests/trae_status_ui_layout_test.cpp`
- Modify: `tests/codex_status_ui_layout_test.cpp`
- Create: `mac-codex-agent/tests/test_trae_status_firmware_contract.py`
- Modify: `mac-codex-agent/tests/test_edge_control_firmware_contract.py`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write failing geometry and integration contracts**

Lock the approved geometry:

```cpp
static_assert(trae_status_ui::kDividerX == 82);
static_assert(trae_status_ui::kDividerY == 223);
static_assert(trae_status_ui::kDividerWidth == 302);
static_assert(trae_status_ui::kLowerPanelX == 69);
static_assert(trae_status_ui::kLowerPanelY == 246);
static_assert(trae_status_ui::kLowerPanelWidth == 328);
static_assert(trae_status_ui::kLowerPanelHeight == 118);
static_assert(trae_status_ui::kColumnDividerX == 210);
```

The Python source contract must assert:

- A clipped 466 x 466 viewport owns a 932 x 466 page strip.
- Codex root is at x=0 and Trae root at x=466.
- The strip animates only between x=0 and x=-466 with 140 ms ease-out.
- A shared two-circle page indicator is y=416 and the shared battery is y=438, both outside the moving strip.
- The indicator uses LVGL circle objects rather than font glyphs: Codex fills the first circle and outlines the second; Trae outlines the first and fills the second.
- Trae retains MODEL and EFFORT and contains no quota bar.
- Left lower column shows state/project; right lower column shows title/detail.
- The Trae signal mark is drawn from LVGL primitives and does not import a proprietary asset.
- The app checks `GetGlobalEdgeControlService().ownsTouch()` and `lastReleaseConsumed()` before committing a page swipe.
- Screen-off and view destruction cancel an active swipe.

- [ ] **Step 2: Run the tests and verify RED**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/trae_status_ui_layout_test.cpp \
  -o /tmp/trae_status_ui_layout_test

mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py \
  mac-codex-agent/tests/test_edge_control_firmware_contract.py
```

Expected: failures because the Trae page and page strip do not exist.

- [ ] **Step 3: Create the Trae view component**

`TraeStatusPage` owns only Trae labels, the outlined signal mark, and rendering of a `TraeStatusSnapshot`. Visible states are:

- Offline source: `TRAE OFFLINE`, model/effort `--`.
- Online but no recognized current task: `TRAE WAIT`, state `空闲`.
- Pending: `等待指令`.
- Running: `正在处理`.
- Completed: `已完成`.

All copied text must use fixed LVGL long modes and remain inside its column. Detail is one line and may ellipsize; it must not scroll.

- [ ] **Step 4: Refactor the app into a persistent page strip**

In `AppCodexStatus`:

- Keep the existing 466 x 466 `_panel` as the clipped viewport.
- Add `_page_strip`, `_codex_page`, and `_trae_page`; create each once during `create_view`.
- Reparent current Codex-only content to `_codex_page` without changing its carousel, pet, alert, or Pending glow behavior.
- Keep one shared two-circle page indicator and battery label on the viewport so they do not slide.
- Route incoming status JSON by source, refresh only the corresponding model, and maintain independent last-received timestamps.
- Poll raw touch while the app is visible, but cancel immediately when the global edge service owns the touch or consumed the release.
- Decide the target only on release and animate the strip for 140 ms with ease-out.
- Reset to the Codex page when the app is newly opened; do not use buttons for page navigation.

- [ ] **Step 5: Preserve independent freshness**

Codex freshness continues to use its existing timeout and rendering. Trae heartbeat freshness is evaluated separately; stale Trae displays `TRAE WAIT` or `TRAE OFFLINE` according to its last liveness field without altering Codex.

- [ ] **Step 6: Run layout, contract, and full offline validation**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test && \
  /tmp/codex_status_ui_layout_test

c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/trae_status_ui_layout_test.cpp \
  -o /tmp/trae_status_ui_layout_test && \
  /tmp/trae_status_ui_layout_test

mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py \
  mac-codex-agent/tests/test_edge_control_firmware_contract.py

scripts/run_unified_offline_validation.sh
```

Expected: all tests and the clean firmware build pass.

- [ ] **Step 7: Commit the watch UI**

```bash
git add main/apps/app_codex_status/trae_status_page.h \
  main/apps/app_codex_status/trae_status_page.cpp \
  main/apps/app_codex_status/trae_status_ui_layout.h \
  main/apps/app_codex_status/app_codex_status.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  tests/trae_status_ui_layout_test.cpp \
  tests/codex_status_ui_layout_test.cpp \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py \
  mac-codex-agent/tests/test_edge_control_firmware_contract.py \
  scripts/run_unified_offline_validation.sh
git commit -m "feat: add swipeable Trae status page"
```

### Task 9: Package the new Mac modules without changing product identity

**Files:**
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Write failing installation contract tests**

Extend `test_install_script.py` to require installation of:

```text
trae_tasks.py
trae_adapter.py
combined_collector.py
```

Also assert the launch agent label, application name, BLE service UUID, and bundle identity remain unchanged.

- [ ] **Step 2: Run the installer test and verify RED**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_install_script.py
```

Expected: failure because the new modules are not in the explicit copy list.

- [ ] **Step 3: Update installation and operator documentation**

Add the three modules to the existing explicit module list. Document:

- Trae CN is optional; Codex continues working when it is absent.
- The collector reads local Trae status only and does not make network requests.
- Private-format parse failure displays WAIT/`--`.
- Cursor and normalized ledger cache locations.
- No raw prompt, response, code, tool output, header, or log line is persisted or transmitted.

- [ ] **Step 4: Run installer and full Mac tests**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_install_script.py

mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all tests pass.

- [ ] **Step 5: Commit packaging changes**

```bash
git add mac-codex-agent/install_launch_agent.sh \
  mac-codex-agent/tests/test_install_script.py \
  mac-codex-agent/README.md
git commit -m "docs: package unified Trae monitoring"
```

### Task 10: Run release gates and validate real interaction

**Files:**
- Modify only if a gate exposes a defect in the files already listed above.

- [ ] **Step 1: Run the complete automated Mac suite**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all tests pass with no skipped Trae contract tests.

- [ ] **Step 2: Run the complete offline firmware validation**

```bash
scripts/run_unified_offline_validation.sh
```

Expected: every host-test, source-contract, dependency, size, and clean firmware-build stage passes.

- [ ] **Step 3: Check patch integrity and product identity**

```bash
git diff --check
git status --short
rg -n "StopWatch Codex Agent|com\\.stopwatch\\.codex-agent" \
  mac-codex-agent
```

Expected: no whitespace errors; only intended files are modified; existing product identity remains present.

- [ ] **Step 4: Install locally and verify Mac runtime behavior**

Use the repository's documented installer, relaunch the agent, and verify:

- Existing Codex menu status and watch updates continue every two seconds.
- Trae absent/offline does not interrupt Codex.
- Opening Trae CN changes only the Trae menu section.
- Starting, progressing, awaiting input, and completing an invented disposable task produce the expected normalized states.
- Agent logs contain no raw Trae log lines, prompt text, response text, code, tool arguments, or tool output.
- Restarting the agent does not replay the historical Trae log.

- [ ] **Step 5: Flash a test watch and verify the approved interaction**

After following the repository's backup-first flashing workflow, verify on hardware:

- Codex opens by default with `●  ○`.
- A right-to-left swipe opens Trae with `○  ●`; the reverse swipe returns.
- Top/left/right global edge controls still win over an in-app swipe.
- Vertical and short slow drags do not switch pages.
- Battery remains below the dots at the bottom on both pages.
- Trae shows MODEL and EFFORT and no quota bars.
- The selected C layout shows state/project on the left and title/detail on the right.
- Pending Codex glow, pet, alert carousel, screen-off behavior, and buttons are unchanged.
- Disconnecting or stopping Trae stales only the Trae page.

- [ ] **Step 6: Record evidence and create a final verification commit only if needed**

If validation requires fixes, add focused regression tests first, implement the smallest fix, rerun Steps 1–5, then commit:

```bash
git diff --name-only
git add -p
git commit -m "fix: harden unified status page validation"
```

Do not commit generated build trees, local Trae data, cursor/ledger contents, `.superpowers/`, or `hardware-evidence/`.
