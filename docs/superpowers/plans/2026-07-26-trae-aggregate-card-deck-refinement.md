# TRAE Aggregate Card Deck Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the existing Trae split-panel watch page with the approved C aggregate card deck, including full active-task counts, pending-first rotation, independent Bluetooth/process health, task-scoped model/effort, and the approved animated native robot assets.

**Architecture:** Keep the current one-Mac-client, one-transport, two-page firmware architecture. Compute full active counts and deterministic ordering before the Mac trims detail cards, extend the bounded Trae packet compatibly, parse it into a fixed firmware model, and let a pure carousel controller own automatic versus manual selection while the LVGL page owns only rendering and animation. Preserve the existing page strip, footer, Codex behavior, and BLE service.

**Tech Stack:** Python 3.11, `unittest`, PyObjC process probing, JSON over the existing BLE framing, C++17, cJSON, LVGL 9, Pillow-based reproducible asset conversion, ESP-IDF 5.5.4, native host tests.

---

## File Map

### Mac task and payload contract

- Modify `mac-codex-agent/trae_tasks.py`: expose all normalized ledger tasks, active-only deterministic ordering, and optional task telemetry.
- Modify `mac-codex-agent/trae_adapter.py`: distinguish live/off/unknown process state, compute full pending/running counts, generate opaque stable IDs, and trim only detail cards.
- Modify `mac-codex-agent/agent.py`: serialize the new bounded fields while preserving the old packet keys and heartbeat behavior.
- Modify `mac-codex-agent/tests/test_trae_tasks.py`: prove active ordering and terminal exclusion.
- Modify `mac-codex-agent/tests/test_trae_adapter.py`: prove full counts, process truth states, stable IDs, and per-task telemetry.
- Modify `mac-codex-agent/tests/test_agent.py`: prove wire compatibility, privacy, trimming, and byte limits.

### Firmware model and selection logic

- Modify `main/apps/app_codex_status/trae_status_model.h`: add process state, aggregate counts, truncation, stable task ID, and task telemetry.
- Modify `main/apps/app_codex_status/trae_status_model.cpp`: parse new optional fields with legacy fallbacks and fixed bounds.
- Modify `tests/trae_status_model_test.cpp`: verify new and legacy packets and reject malformed packets without mutating the last snapshot.
- Modify `main/apps/app_codex_status/trae_task_carousel.h`: expose automatic, manual, and manual-timeout operations.
- Modify `main/apps/app_codex_status/trae_task_carousel.cpp`: filter terminal tasks, preserve identity across revisions, and separate pending-first automatic rotation from all-card manual browsing.
- Modify `tests/trae_task_carousel_test.cpp`: verify every controller transition.
- Create `main/apps/app_codex_status/trae_card_tap_logic.h`: bounded pure tap recognizer for the approved deck rectangle.
- Create `main/apps/app_codex_status/trae_card_tap_logic.cpp`: distinguish a short tap from swipe/drag/cancel.
- Create `tests/trae_card_tap_logic_test.cpp`: host-test gesture arbitration.
- Modify `scripts/run_unified_offline_validation.sh`: compile and run the new host test.

### Native assets and LVGL page

- Create `main/apps/app_codex_status/assets/source/trae_robot_head.png`: approved 120 x 120 header source.
- Create `main/apps/app_codex_status/assets/source/trae_robot_idle.png`: approved 107 x 120 idle source.
- Create `main/apps/app_codex_status/assets/source/trae_robot_action.png`: approved 107 x 120 `>`-eye/action source.
- Create `scripts/generate_trae_robot_assets.py`: deterministic PNG-to-`RGB565A8` converter.
- Create `main/apps/app_codex_status/assets/trae_robot_assets.h`: native descriptor declarations.
- Create `main/apps/app_codex_status/assets/trae_robot_assets.cpp`: generated LVGL maps and descriptors.
- Modify `mac-codex-agent/tests/test_trae_status_firmware_contract.py`: verify source hashes, asset descriptors, final labels, and removal of the old signal/split layout.
- Modify `main/apps/app_codex_status/trae_status_ui_layout.h`: lock approved C coordinates and deck bounds.
- Modify `tests/trae_status_ui_layout_test.cpp`: keep every element inside the circular safe region and footer boundary.
- Modify `main/apps/app_codex_status/trae_status_page.h`: add health, deck, animation, and active-page APIs.
- Modify `main/apps/app_codex_status/trae_status_page.cpp`: render the approved C page and manage the two-frame robot timer.
- Modify `main/apps/app_codex_status/app_codex_status.h`: store tap logic and expose view-activity synchronization.
- Modify `main/apps/app_codex_status/app_codex_status.cpp`: pass Bluetooth truth, route taps, update telemetry with the front task, and pause animation off-page/screen-off.

## Task 1: Compute the Complete Active-Task Set on the Mac

**Files:**

- Modify: `mac-codex-agent/trae_tasks.py:34-58,98-153,231-237,299-354`
- Modify: `mac-codex-agent/trae_adapter.py:47-56,78-122,205-226,246-306`
- Test: `mac-codex-agent/tests/test_trae_tasks.py`
- Test: `mac-codex-agent/tests/test_trae_adapter.py`

- [ ] **Step 1: Write failing active-order and full-count tests**

Add these cases to the existing test classes:

```python
from trae_tasks import TraeTask, TraeTaskSnapshot, ordered_active_tasks


class FakeTracker:
    def __init__(self, tasks):
        self.tasks = tuple(tasks)

    def refresh(self, log_paths, now):
        return TraeTaskSnapshot(
            tasks=self.tasks,
            model=None,
            effort=None,
            revision=9,
        )


def test_ordered_active_tasks_excludes_terminal_and_is_stable(self):
    tasks = (
        TraeTask("s3", "t3", "P3", "Done", "", "completed", 30.0),
        TraeTask("s2", "t2", "P2", "Run", "", "running", 20.0),
        TraeTask("s1", "t1", "P1", "Ask", "", "pending", 10.0),
        TraeTask("s0", "t0", "P0", "Ask newer", "", "pending", 40.0),
    )
    ordered = ordered_active_tasks(tasks)
    self.assertEqual([item.task_id for item in ordered], ["t0", "t1", "t2"])

def test_tracker_reads_appends_from_two_project_logs(self):
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        log_a = root / "project-a.log"
        log_b = root / "project-b.log"
        log_a.write_text("baseline\n", encoding="utf-8")
        log_b.write_text("baseline\n", encoding="utf-8")
        tracker = TraeTaskTracker(root / "cursor.json", root / "ledger.json")
        tracker.refresh((log_a, log_b), now=1.0)
        with log_a.open("a", encoding="utf-8") as stream:
            stream.write(
                "execute_task:start session_id=session-a task_id=task-a\n"
            )
        with log_b.open("a", encoding="utf-8") as stream:
            stream.write(
                "execute_task:start session_id=session-b task_id=task-b\n"
            )
        snapshot = tracker.refresh((log_a, log_b), now=2.0)
        self.assertEqual(
            {(item.session_id, item.task_id) for item in snapshot.tasks},
            {("session-a", "task-a"), ("session-b", "task-b")},
        )

def test_collector_counts_all_active_tasks_before_detail_limit(self):
    collector = self.collector(online=True)
    collector._tracker = FakeTracker(
        tasks=tuple(
            TraeTask(
                "session-{}".format(index),
                "task-{}".format(index),
                "Project {}".format(index),
                "Task {}".format(index),
                "detail",
                "pending" if index < 3 else "running",
                float(100 - index),
            )
            for index in range(7)
        )
    )
    state = collector.get_state()
    self.assertEqual(state["trae_pending_count"], 3)
    self.assertEqual(state["trae_running_count"], 4)
    self.assertEqual(len(state["trae_tasks"]), 4)
    self.assertTrue(state["trae_tasks_truncated"])

def test_process_probe_unknown_is_not_reported_as_off(self):
    state = self.collector(online=None).get_state()
    self.assertEqual(state["trae_process_state"], "unknown")
    self.assertFalse(state["trae_online"])
    self.assertEqual(state["trae_tasks"], [])
```

Keep `FakeTracker` local to `test_trae_adapter.py`; it must not enter production code. The existing `collector(online=value)` helper already injects `lambda: online`, so `None` exercises the unknown state without another test seam. Put the two-log test in `test_trae_tasks.py`. Replace the old `test_priority_is_pending_then_running_then_latest_completed` test because completed tasks are no longer transport candidates.

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_tasks.py \
  mac-codex-agent/tests/test_trae_adapter.py
```

Expected: failures for missing `ordered_active_tasks`, aggregate fields, and the unknown process state.

- [ ] **Step 3: Add active ordering and task telemetry to the normalized model**

Extend `TraeTask` without changing existing positional construction:

```python
@dataclasses.dataclass(frozen=True)
class TraeTask:
    session_id: str
    task_id: str
    project: str
    title: str
    detail: str
    state: Literal["pending", "running", "completed"]
    updated_at: float
    model: str = ""
    effort: str = ""


def ordered_active_tasks(tasks: Iterable[TraeTask]) -> Tuple[TraeTask, ...]:
    state_order = {"pending": 0, "running": 1}
    return tuple(
        sorted(
            (item for item in tasks if item.state in state_order),
            key=lambda item: (
                state_order[item.state],
                -item.updated_at,
                item.session_id,
                item.task_id,
            ),
        )
    )


def select_transport_tasks(
    tasks: Iterable[TraeTask], limit: int = 4
) -> Tuple[TraeTask, ...]:
    return ordered_active_tasks(tasks)[: max(0, limit)]
```

Change `TraeTaskTracker._snapshot()` to return `tasks=self._tasks`, not the already-trimmed selection. Replace the single `_cursor` with `_cursors: Dict[str, TraeLogCursor]`. `refresh()` sorts every existing input log by path, baselines each unseen or replaced log independently, then consumes complete appended lines from every known log before incrementing the revision once and saving. Persist cursor version 2 in the concrete shape `{"version":2,"cursors":{"/tmp/project-a.log":{"path":"/tmp/project-a.log","inode":123,"offset":45,"mtime_ns":678}}}`; `_load_cursors()` must migrate the existing single-cursor JSON object into a one-entry map. Extend ledger load/save through `dataclasses.asdict` with bounded `model` and `effort` defaults so old version-1 ledgers remain readable.

- [ ] **Step 4: Implement three-state process observation and aggregate output**

Make the process probe return `Optional[bool]`: `True` means live, `False` means observed off, and `None` means AppKit/probe unavailable. Replace the start of `get_state()` with:

```python
process_result = self._process_is_running()
process_state = "live" if process_result is True else (
    "off" if process_result is False else "unknown"
)
if process_result is not True:
    return {
        "trae_online": False,
        "trae_process_state": process_state,
        "trae_model": "--",
        "trae_effort": "--",
        "trae_revision": 0,
        "trae_pending_count": 0,
        "trae_running_count": 0,
        "trae_tasks_truncated": False,
        "trae_tasks": [],
    }
```

After enrichment, compute and return the complete counts before selecting four details:

```python
active_tasks = ordered_active_tasks(enriched_tasks)
transport_tasks = active_tasks[:4]
pending_count = sum(item.state == "pending" for item in active_tasks)
running_count = sum(item.state == "running" for item in active_tasks)
return {
    "trae_online": True,
    "trae_process_state": "live",
    "trae_model": model or "--",
    "trae_effort": effort or "--",
    "trae_revision": snapshot.revision,
    "trae_pending_count": pending_count,
    "trae_running_count": running_count,
    "trae_tasks_truncated": len(active_tasks) > len(transport_tasks),
    "trae_tasks": [
        self._task_payload(task, model or "", effort or "")
        for task in transport_tasks
    ],
}
```

Use `hashlib.sha256((session_id + "\0" + task_id).encode("utf-8")).hexdigest()[:16]` as the transmitted opaque ID. `_task_payload` must include `id`, `model`, and `effort`; task values win, then the snapshot values are used. Do not transmit `session_id` or `task_id`.

- [ ] **Step 5: Run focused tests and verify GREEN**

Run the command from Step 2.

Expected: all Trae tracker and adapter tests pass.

- [ ] **Step 6: Commit the Mac aggregation contract**

```bash
git add mac-codex-agent/trae_tasks.py \
  mac-codex-agent/trae_adapter.py \
  mac-codex-agent/tests/test_trae_tasks.py \
  mac-codex-agent/tests/test_trae_adapter.py
git commit -m "feat: aggregate active Trae tasks"
```

## Task 2: Serialize the Backward-Compatible Bounded Packet

**Files:**

- Modify: `mac-codex-agent/agent.py:21-27,157-199`
- Test: `mac-codex-agent/tests/test_agent.py:57-75,223-290`

- [ ] **Step 1: Write failing payload compatibility and trimming tests**

Add these assertions:

```python
def test_trae_payload_keeps_full_counts_when_details_are_trimmed(self):
    status = trae_status()
    status.update({
        "trae_process_state": "live",
        "trae_pending_count": 3,
        "trae_running_count": 4,
        "trae_tasks_truncated": True,
    })
    status["trae_tasks"] = [
        {
            "id": "id-{}".format(index),
            "project": "P",
            "title": "T{}".format(index),
            "detail": "状态" * 72,
            "time": "10:00",
            "state": "pending" if index < 3 else "running",
            "model": "Doubao",
            "effort": "High",
        }
        for index in range(7)
    ]
    payload = _watch_trae_status(status)
    self.assertEqual(payload["trae_pending_count"], 3)
    self.assertEqual(payload["trae_running_count"], 4)
    self.assertTrue(payload["trae_tasks_truncated"])
    self.assertLessEqual(len(payload["trae_tasks"]), 4)
    self.assertLessEqual(len(_compact_json_bytes(payload)), 1024)

def test_legacy_status_derives_counts_and_process_state(self):
    payload = _watch_trae_status(trae_status())
    self.assertEqual(payload["trae_process_state"], "live")
    self.assertEqual(payload["trae_pending_count"], 0)
    self.assertEqual(payload["trae_running_count"], 1)

def test_payload_never_exposes_private_task_keys(self):
    payload = _watch_trae_status(trae_status())
    encoded = json.dumps(payload, ensure_ascii=False)
    self.assertNotIn("private-session", encoded)
    self.assertNotIn("private-task", encoded)
```

- [ ] **Step 2: Run the agent test and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_agent.py
```

Expected: the new fields are missing and legacy counts are not derived.

- [ ] **Step 3: Extend `_watch_trae_status` without changing framing or heartbeat**

Normalize the state before building the packet:

```python
raw_tasks = status.get("trae_tasks")
source_tasks = raw_tasks if isinstance(raw_tasks, list) else []
active_tasks = [
    item
    for item in source_tasks
    if isinstance(item, Mapping) and item.get("state") in ("pending", "running")
]
process_state = status.get("trae_process_state")
if process_state not in ("live", "off", "unknown"):
    process_state = "live" if status.get("trae_online") is True else "off"
pending_count = _nonnegative_integer(status.get("trae_pending_count"))
running_count = _nonnegative_integer(status.get("trae_running_count"))
if "trae_pending_count" not in status:
    pending_count = sum(item.get("state") == "pending" for item in active_tasks)
if "trae_running_count" not in status:
    running_count = sum(item.get("state") == "running" for item in active_tasks)
```

Add these packet fields:

```python
"trae_process_state": process_state,
"trae_pending_count": pending_count,
"trae_running_count": running_count,
"trae_tasks_truncated": (
    status.get("trae_tasks_truncated") is True or len(active_tasks) > MAX_TRAE_TASKS
),
```

Each detail object includes bounded `id` (32), `model` (64), and `effort` (32). If byte-limit trimming pops a card or clears details, set `trae_tasks_truncated` to `True`. Keep `MAX_TRAE_MESSAGE_SIZE = 1024`, `MAX_TRAE_TASKS = 4`, the status message type, fingerprinting, and six-second heartbeat unchanged.

- [ ] **Step 4: Run the agent and protocol tests**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_protocol.py
```

Expected: all tests pass; every Trae packet is at most 1024 UTF-8 bytes and decodes through existing framing.

- [ ] **Step 5: Commit the wire contract**

```bash
git add mac-codex-agent/agent.py mac-codex-agent/tests/test_agent.py
git commit -m "feat: send Trae aggregate task status"
```

## Task 3: Parse Aggregate and Task-Scoped Fields on the Watch

**Files:**

- Modify: `main/apps/app_codex_status/trae_status_model.h`
- Modify: `main/apps/app_codex_status/trae_status_model.cpp`
- Modify: `tests/trae_status_model_test.cpp`

- [ ] **Step 1: Write failing new-packet and legacy-fallback host tests**

Add a new-packet case:

```cpp
assert(parse(
    R"({"type":"status","source":"trae","trae_online":true,"trae_process_state":"live","trae_model":"Fallback","trae_effort":"medium","trae_revision":20,"trae_pending_count":6,"trae_running_count":3,"trae_tasks_truncated":true,"trae_tasks":[{"id":"abc123","project":"StopWatch","title":"Confirm","detail":"Waiting","time":"10:00","state":"pending","model":"Doubao","effort":"High"}]})",
    8000,
    snapshot
));
assert(snapshot.process_state == TraeProcessState::Live);
assert(snapshot.pending_count == 6);
assert(snapshot.running_count == 3);
assert(snapshot.tasks_truncated);
assert(std::string(snapshot.tasks[0].id.data()) == "abc123");
assert(std::string(snapshot.tasks[0].model.data()) == "Doubao");
assert(std::string(snapshot.tasks[0].effort.data()) == "High");
```

Update the existing legacy packet assertions so a missing `trae_process_state` maps from `trae_online`, missing counts are derived only from pending/running tasks, and missing IDs receive a bounded `legacy-<index>` value.

- [ ] **Step 2: Run the host model test and verify RED**

Run:

```bash
source "${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}/export.sh" >/dev/null
mkdir -p build-host-tests
cc -Wall -Wextra -Werror -I"$IDF_PATH/components/json/cJSON" \
  -c "$IDF_PATH/components/json/cJSON/cJSON.c" -o build-host-tests/trae-cJSON.o
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  -I"$IDF_PATH/components/json/cJSON" \
  tests/trae_status_model_test.cpp \
  main/apps/app_codex_status/trae_status_model.cpp \
  build-host-tests/trae-cJSON.o \
  -o build-host-tests/trae_status_model_test
build-host-tests/trae_status_model_test
```

Expected: compile failures for the new enum and fields.

- [ ] **Step 3: Extend the bounded C++ model**

Use these exact public fields:

```cpp
enum class TraeProcessState : uint8_t {
    Unknown,
    Live,
    Off,
};

struct TraeTask {
    std::array<char, 33> id{};
    std::array<char, 33> project{};
    std::array<char, 49> title{};
    std::array<char, 73> detail{};
    std::array<char, 9> time{};
    std::array<char, 65> model{};
    std::array<char, 33> effort{};
    TraeTaskState state = TraeTaskState::Completed;
};

struct TraeStatusSnapshot {
    bool online = false;
    TraeProcessState process_state = TraeProcessState::Unknown;
    std::array<char, 65> model{};
    std::array<char, 33> effort{};
    int revision = 0;
    uint32_t received_at_ms = 0;
    uint16_t pending_count = 0;
    uint16_t running_count = 0;
    bool tasks_truncated = false;
    std::array<TraeTask, 4> tasks{};
    size_t task_count = 0;
};
```

Parse new fields when present. Accept only `live`, `off`, or `unknown`; fall back to the legacy boolean when absent. Use optional-string helpers for `id`, `model`, and `effort`; synthesize `legacy-0` through `legacy-3` only when `id` is absent. Counts must be nonnegative integers capped at `UINT16_MAX`; when absent, derive them from the parsed pending/running detail cards. Completed legacy cards may parse but never increment counts. Continue parsing into a temporary snapshot and assign only after the complete packet validates.

- [ ] **Step 4: Run the host model test and verify GREEN**

Run the final executable command from Step 2.

Expected: exit status 0.

- [ ] **Step 5: Commit the firmware data contract**

```bash
git add main/apps/app_codex_status/trae_status_model.h \
  main/apps/app_codex_status/trae_status_model.cpp \
  tests/trae_status_model_test.cpp
git commit -m "feat: parse Trae aggregate task payloads"
```

## Task 4: Upgrade the Carousel into the Card-Deck Controller

**Files:**

- Modify: `main/apps/app_codex_status/trae_task_carousel.h`
- Modify: `main/apps/app_codex_status/trae_task_carousel.cpp`
- Modify: `tests/trae_task_carousel_test.cpp`

- [ ] **Step 1: Replace completed-task expectations with the approved controller tests**

The host test must assert this sequence:

```cpp
TraeTaskCarousel deck;
assert(deck.apply(snapshot_with_pending_and_running(), 0));
assert(std::string(deck.current()->id.data()) == "pending-a");
assert(deck.update(3000));
assert(std::string(deck.current()->id.data()) == "pending-b");
assert(deck.manual_next(3200));
assert(std::string(deck.current()->id.data()) == "running-a");
assert(!deck.update(7000));
assert(deck.update(8200));
assert(std::string(deck.current()->id.data()) == "pending-a");

TraeStatusSnapshot reordered = snapshot_with_same_ids_reordered();
reordered.revision = 2;
assert(deck.apply(reordered, 8300));
assert(std::string(deck.current()->id.data()) == "pending-a");

TraeStatusSnapshot terminal_only;
terminal_only.revision = 3;
terminal_only.task_count = 1;
terminal_only.tasks[0] = task("done", "Done", TraeTaskState::Completed);
assert(deck.apply(terminal_only, 9000));
assert(deck.current() == nullptr);
```

Use helpers that always populate `id`, so identity behavior is explicit.

- [ ] **Step 2: Run the carousel host test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/trae_task_carousel_test.cpp \
  main/apps/app_codex_status/trae_task_carousel.cpp \
  -o build-host-tests/trae_task_carousel_test
build-host-tests/trae_task_carousel_test
```

Expected: compile failure for `manual_next` and behavioral failures for completed filtering/identity retention.

- [ ] **Step 3: Implement automatic and manual modes**

Expose this interface:

```cpp
class TraeTaskCarousel {
public:
    bool apply(const TraeStatusSnapshot& snapshot, uint32_t now_ms);
    bool update(uint32_t now_ms);
    bool manual_next(uint32_t now_ms);
    void reset();

    const TraeTask* current() const;
    TraeTaskState display_state() const;
    size_t task_count() const;
    bool manual_mode() const;

private:
    size_t first_priority_index() const;
    size_t next_priority_index(size_t from) const;
    int _revision = -1;
    std::array<TraeTask, 4> _tasks{};
    size_t _task_count = 0;
    size_t _index = 0;
    uint32_t _last_rotation_ms = 0;
    uint32_t _last_manual_ms = 0;
    bool _manual_mode = false;
};
```

Keep `kRotationIntervalMs = 3000` and add `kManualTimeoutMs = 5000`. `apply()` copies only pending/running tasks and retains the current ID if present. `update()` returns to `first_priority_index()` when manual mode times out; otherwise it rotates only the highest-priority state. `manual_next()` cycles through all active detail cards in their packet order. With zero or one applicable card, return `false` instead of animating a fake change.

- [ ] **Step 4: Run the carousel host test and verify GREEN**

Run the command from Step 2.

Expected: exit status 0.

- [ ] **Step 5: Commit the deck controller**

```bash
git add main/apps/app_codex_status/trae_task_carousel.h \
  main/apps/app_codex_status/trae_task_carousel.cpp \
  tests/trae_task_carousel_test.cpp
git commit -m "feat: add Trae card deck selection logic"
```

## Task 5: Add Tap-versus-Swipe Arbitration

**Files:**

- Create: `main/apps/app_codex_status/trae_card_tap_logic.h`
- Create: `main/apps/app_codex_status/trae_card_tap_logic.cpp`
- Create: `tests/trae_card_tap_logic_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh:193-203`

- [ ] **Step 1: Write the failing pure gesture test**

Create `tests/trae_card_tap_logic_test.cpp`:

```cpp
#include "apps/app_codex_status/trae_card_tap_logic.h"
#include <cassert>

int main()
{
    TraeCardTapLogic tap;
    tap.press(200, 300, 1000, true);
    tap.move(205, 304);
    assert(tap.release(1250));

    tap.press(200, 300, 2000, true);
    tap.move(245, 302);
    assert(!tap.release(2200));

    tap.press(80, 120, 3000, false);
    assert(!tap.release(3100));

    tap.press(200, 300, 4000, true);
    tap.cancel();
    assert(!tap.release(4100));
}
```

- [ ] **Step 2: Run the gesture test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/trae_card_tap_logic_test.cpp \
  main/apps/app_codex_status/trae_card_tap_logic.cpp \
  -o build-host-tests/trae_card_tap_logic_test
```

Expected: missing source/header failure.

- [ ] **Step 3: Implement the bounded recognizer**

Create the public class with `press`, `move`, `release`, and `cancel`. A valid tap requires: the initial press was inside the deck, total duration is at most 500 ms, and both absolute x/y displacement are at most 12 px. `cancel()` clears the candidate. Store only integer coordinates, start time, and two booleans; the recognizer does not depend on LVGL.

- [ ] **Step 4: Add the host test to offline validation and run it**

Add this compile/run block immediately after the existing status-page swipe test:

```bash
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/trae_card_tap_logic_test.cpp \
    main/apps/app_codex_status/trae_card_tap_logic.cpp \
    -o "$HOST_TEST_DIR/trae_card_tap_logic_test"
"$HOST_TEST_DIR/trae_card_tap_logic_test"
```

Run the command from Step 2 followed by `build-host-tests/trae_card_tap_logic_test`.

Expected: exit status 0.

- [ ] **Step 5: Commit gesture arbitration**

```bash
git add main/apps/app_codex_status/trae_card_tap_logic.h \
  main/apps/app_codex_status/trae_card_tap_logic.cpp \
  tests/trae_card_tap_logic_test.cpp \
  scripts/run_unified_offline_validation.sh
git commit -m "feat: recognize Trae deck taps"
```

## Task 6: Promote and Convert the Approved Robot Assets

**Files:**

- Create: `main/apps/app_codex_status/assets/source/trae_robot_head.png`
- Create: `main/apps/app_codex_status/assets/source/trae_robot_idle.png`
- Create: `main/apps/app_codex_status/assets/source/trae_robot_action.png`
- Create: `scripts/generate_trae_robot_assets.py`
- Create: `main/apps/app_codex_status/assets/trae_robot_assets.h`
- Create: `main/apps/app_codex_status/assets/trae_robot_assets.cpp`
- Modify: `mac-codex-agent/tests/test_trae_status_firmware_contract.py`

- [ ] **Step 1: Add failing asset provenance and descriptor tests**

Add a test that hashes the three tracked sources and checks these exact values:

```python
expected = {
    "trae_robot_head.png": "063ef188f4d0df29a2708616bda5f84f5a0c38c47f6f60df590bc2c53d7422d9",
    "trae_robot_idle.png": "2927a2aab485b7b3b9f60f7189a6da132c2be28625a88c517204e345a0cf7c2e",
    "trae_robot_action.png": "5a6fbb08418469414c615b69389bd92d1817f3a2a5af0b35a1e910e927f7defa",
}
for name, digest in expected.items():
    data = (self.app_root / "assets" / "source" / name).read_bytes()
    self.assertEqual(hashlib.sha256(data).hexdigest(), digest)
```

Also assert `trae_robot_assets.cpp` contains exactly three `lv_image_dsc_t` declarations using `LV_COLOR_FORMAT_RGB565A8`, and the header declares `trae_robot_head`, `trae_robot_idle`, and `trae_robot_action`.

- [ ] **Step 2: Promote the approved prototype PNGs without resampling**

Promote the already approved files with these exact commands:

```bash
mkdir -p main/apps/app_codex_status/assets/source
cp .superpowers/brainstorm/22610-1785043243/content/trae-pixel-head.png \
  main/apps/app_codex_status/assets/source/trae_robot_head.png
cp .superpowers/brainstorm/22610-1785043243/content/trae-pixel-idle.png \
  main/apps/app_codex_status/assets/source/trae_robot_idle.png
cp .superpowers/brainstorm/22610-1785043243/content/trae-pixel-action.png \
  main/apps/app_codex_status/assets/source/trae_robot_action.png
```

to the three tracked `assets/source/trae_robot_*.png` paths. Verify dimensions are 120 x 120, 107 x 120, and 107 x 120 respectively, and verify the hashes from Step 1 before conversion.

- [ ] **Step 3: Add a deterministic RGB565A8 generator**

Create `scripts/generate_trae_robot_assets.py` with an explicit input manifest and this pixel encoding rule:

```python
def rgb565a8_bytes(path: pathlib.Path) -> tuple[int, int, bytes]:
    image = Image.open(path).convert("RGBA")
    color = bytearray()
    alpha = bytearray()
    for red, green, blue, opacity in image.getdata():
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        color.extend((value & 0xFF, value >> 8))
        alpha.append(opacity)
    return image.width, image.height, bytes(color + alpha)
```

The script writes `trae_robot_assets.h` and `.cpp` in stable manifest order, wraps byte rows at 24 values, uses `alignas(4)`, sets stride to `width * 2`, and emits LVGL 9 descriptors matching `codex_pet_assets.cpp`. It must accept `--check`; in check mode it renders to memory and exits nonzero if tracked outputs differ.

- [ ] **Step 4: Generate and verify the native resources**

Run:

```bash
python3 scripts/generate_trae_robot_assets.py
python3 scripts/generate_trae_robot_assets.py --check
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py
```

Expected: generation check and contract test pass; no scaling occurs in the generator.

- [ ] **Step 5: Commit the approved native resources**

```bash
git add main/apps/app_codex_status/assets/source/trae_robot_head.png \
  main/apps/app_codex_status/assets/source/trae_robot_idle.png \
  main/apps/app_codex_status/assets/source/trae_robot_action.png \
  main/apps/app_codex_status/assets/trae_robot_assets.h \
  main/apps/app_codex_status/assets/trae_robot_assets.cpp \
  scripts/generate_trae_robot_assets.py \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py
git commit -m "feat: add native Trae robot assets"
```

## Task 7: Build the Approved C Layout and Render States

**Files:**

- Modify: `main/apps/app_codex_status/trae_status_ui_layout.h`
- Modify: `tests/trae_status_ui_layout_test.cpp`
- Modify: `main/apps/app_codex_status/trae_status_page.h`
- Modify: `main/apps/app_codex_status/trae_status_page.cpp`
- Modify: `mac-codex-agent/tests/test_trae_status_firmware_contract.py`

- [ ] **Step 1: Replace old split-panel contract assertions with C-layout assertions**

The C++ layout test must assert these approved constants:

```cpp
assert(kBrandIconX == 168 && kBrandIconY == 22);
assert(kBrandX == 218 && kBrandY == 32);
assert(kHealthX == 118 && kHealthY == 67 && kHealthWidth == 230);
assert(kRobotX == 96 && kRobotY == 105);
assert(kRobotWidth == 77 && kRobotHeight == 86);
assert(kTelemetryX == 214);
assert(kDividerX == 82 && kDividerY == 223 && kDividerWidth == 302);
assert(kSummaryY == 240);
assert(kDeckX == 108 && kDeckY == 268);
assert(kDeckWidth == 250 && kDeckHeight == 120);
assert(kCardWidth == 250 && kCardHeight == 98);
assert(kDeckY + kDeckHeight < kPageIndicatorY);
assert(kPageIndicatorY == 416 && kBatteryY == 438);
```

Update the Python contract test to require `"TRAE"`, `"BT LIVE"`, `"TRAE LIVE"`, `"待确认"`, `"运行中"`, `"暂无进行中的任务"`, `"等待连接"`, `"TRAE 未运行"`, and `"状态不可用"`. Forbid `make_signal_mark`, `kColumnDividerX`, `"已完成"`, and quota widgets in `trae_status_page.cpp`.

- [ ] **Step 2: Run layout and contract tests and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/trae_status_ui_layout_test.cpp \
  -o build-host-tests/trae_status_ui_layout_test
build-host-tests/trae_status_ui_layout_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py
```

Expected: old split-panel constants and signal mark fail the new contract.

- [ ] **Step 3: Define the page API and widget ownership**

Use this public API:

```cpp
class TraeStatusPage {
public:
    void init(lv_obj_t* parent);
    void render(
        const TraeStatusSnapshot& snapshot,
        const TraeTask* task,
        bool stale,
        bool bluetooth_connected,
        size_t visible_task_count
    );
    void set_active(bool active);
    bool contains_deck_point(int x, int y) const;
    void shutdown();

private:
    static void robot_timer_cb(lv_timer_t* timer);
    void set_robot_action(bool action);
    lv_obj_t* _root = nullptr;
    lv_obj_t* _brand_icon = nullptr;
    lv_obj_t* _bt_status = nullptr;
    lv_obj_t* _process_status = nullptr;
    lv_obj_t* _robot = nullptr;
    lv_obj_t* _model = nullptr;
    lv_obj_t* _effort = nullptr;
    lv_obj_t* _pending_count = nullptr;
    lv_obj_t* _running_count = nullptr;
    std::array<lv_obj_t*, 3> _cards{};
    lv_timer_t* _robot_timer = nullptr;
    bool _robot_action = false;
};
```

The implementation may add private labels for each card, but all LVGL ownership stays inside this class.

- [ ] **Step 4: Implement header, health, telemetry, summary, and deck rendering**

Create the static header image from `trae_robot_head` with antialiasing off. Use pre-scaled LVGL scale values that make its visible area approximately 32 x 32; adjust object y so visible-pixel center aligns to `TRAE` within 1 px. Render health according to this exact truth table:

```text
bluetooth=false                         -> BT OFF   · TRAE --
bluetooth=true, stale=true              -> BT LIVE  · TRAE --
bluetooth=true, process_state=Live      -> BT LIVE  · TRAE LIVE
bluetooth=true, process_state=Off       -> BT LIVE  · TRAE OFF
bluetooth=true, process_state=Unknown   -> BT LIVE  · TRAE --
```

Render front-task model/effort when nonempty; otherwise use snapshot values; otherwise `--`. Render counts from snapshot fields. Draw up to two restrained back cards at y offsets 10 and 18 with reduced opacity/scale, then the front card. Card text is state, ordinal/overflow, project, title, and a single ellipsized detail line. Never render completed state.

Use these empty-state decisions in priority order:

```text
!bluetooth_connected            -> 等待连接
stale                           -> 状态不可用
process Off                     -> TRAE 未运行
process Unknown                 -> 状态不可用
active counts both zero         -> 暂无进行中的任务
```

- [ ] **Step 5: Implement the two-frame robot timer**

Create the robot from `trae_robot_idle`, disable antialiasing, and scale the 107 x 120 source into the 77 x 86 design box without changing aspect ratio. The timer stays idle for 2200 ms, shows `trae_robot_action` for 180 ms, then returns to idle. `set_active(false)` pauses the timer and forces idle; `set_active(true)` resumes it. A changed front task may immediately show the action frame once, but repeated `render()` calls for the same stable ID must not restart it continuously.

- [ ] **Step 6: Run layout and firmware contract tests and verify GREEN**

Run the commands from Step 2 plus:

```bash
python3 scripts/generate_trae_robot_assets.py --check
```

Expected: all three checks pass.

- [ ] **Step 7: Commit the C page implementation**

```bash
git add main/apps/app_codex_status/trae_status_ui_layout.h \
  tests/trae_status_ui_layout_test.cpp \
  main/apps/app_codex_status/trae_status_page.h \
  main/apps/app_codex_status/trae_status_page.cpp \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py
git commit -m "feat: render Trae aggregate card deck"
```

## Task 8: Integrate Live Bluetooth Truth, Taps, and Animation Lifecycle

**Files:**

- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp:250-405,552-611,756-887,890-1058`
- Modify: `mac-codex-agent/tests/test_trae_status_firmware_contract.py`

- [ ] **Step 1: Add failing integration contract assertions**

Require these tokens in `app_codex_status.h/.cpp`:

```python
for token in (
    "TraeCardTapLogic _trae_card_tap",
    "_trae_carousel.manual_next(now_ms)",
    "_trae_status_page->contains_deck_point",
    "_trae_status_page->set_active(_current_page == 1 && !_screen_off)",
    "_ble_transport && _ble_transport->connected()",
):
    self.assertIn(token, header + source)
```

Also assert the existing `lv_obj_set_pos(_trae_page_root, 466, 0)`, 140 ms page animation, page-dot positions, and battery y position remain unchanged.

- [ ] **Step 2: Run the firmware contract test and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py
```

Expected: missing tap/lifecycle integration tokens.

- [ ] **Step 3: Feed both recognizers from the existing touch stream**

Add `TraeCardTapLogic _trae_card_tap` to `AppCodexStatus`. On press, call `_page_swipe.press(point.x, point.y, now_ms)` and `_trae_card_tap.press(point.x, point.y, now_ms, deck_candidate)`; `deck_candidate` is true only when `_current_page == 1` and `contains_deck_point(point.x, point.y)` is true. On move, call `_page_swipe.move(point.x, point.y)` and `_trae_card_tap.move(point.x, point.y)`. On release, handle the page-swipe result first. If the page stays put and `_trae_card_tap.release(now_ms)` succeeds, call `_trae_carousel.manual_next(now_ms)` and render only when it returns true. Any global edge-control ownership, screen-off transition, page transition, or view destruction cancels both recognizers.

- [ ] **Step 4: Pass health truth and selected telemetry to the page**

Update `render_trae_status()` to calculate:

```cpp
const bool bluetooth_connected =
    _ble_transport && _ble_transport->connected();
_trae_status_page->render(
    _trae_snapshot,
    _trae_carousel.current(),
    _trae_stale,
    bluetooth_connected,
    _trae_carousel.task_count()
);
```

Re-render when BLE connects/disconnects so the health line changes immediately, even before a new Trae payload arrives. Do not reuse the Codex `set_status()` label as the Trae process state.

- [ ] **Step 5: Synchronize page and screen lifecycle**

After every `set_page`, `enter_screen_off`, `wake_screen`, and initial view creation, call:

```cpp
if (_trae_status_page != nullptr) {
    _trae_status_page->set_active(_current_page == 1 && !_screen_off);
}
```

Keep Codex as startup page zero, preserve page animation/footer code, and do not change global edge-control arbitration.

- [ ] **Step 6: Run focused host, contract, and Mac suites**

Run:

```bash
build-host-tests/trae_status_model_test
build-host-tests/trae_task_carousel_test
build-host-tests/trae_card_tap_logic_test
build-host-tests/trae_status_ui_layout_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_trae_tasks.py \
  mac-codex-agent/tests/test_trae_adapter.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py
```

Expected: all commands exit 0.

- [ ] **Step 7: Commit application integration**

```bash
git add main/apps/app_codex_status/app_codex_status.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/tests/test_trae_status_firmware_contract.py
git commit -m "feat: integrate Trae deck interaction"
```

## Task 9: Run Release-Grade Offline and Physical Verification

**Files:**

- Modify only if a discovered regression requires a scoped fix.
- Preserve untracked `.superpowers/` and `hardware-evidence/` unless the user explicitly asks to commit evidence.

- [ ] **Step 1: Run the complete Mac suite**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all discovered tests pass. Report the exact test count.

- [ ] **Step 2: Run the unified offline validation**

Run:

```bash
./scripts/run_unified_offline_validation.sh
```

Expected: every host-test stage, clean ESP32-S3 build, capacity gate, asset check, and `git diff --check` pass. Record the firmware path, byte size, SHA-256, and app/assets headroom. Do not describe this as physical validation.

- [ ] **Step 3: Inspect the generated artifact and repository scope**

Run:

```bash
shasum -a 256 build-unified/StopWatch-UserDemo.bin
git status --short
git diff --check
git log --oneline --decorate -12
```

Expected: only intentional source changes/commits are present; `.superpowers/` and `hardware-evidence/` remain untracked and uncommitted.

- [ ] **Step 4: Flash the connected watch without creating a new backup**

Resolve the exact serial port first:

```bash
ls -1 /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null
```

Use the detected explicit port. Per the user's standing instruction, do not make a new flash backup. If `flash_unified_candidate.sh` still requires `--backup`, pass the already verified existing backup only as its safety precondition; do not run `backup_watch_flash.sh`. Then run:

```bash
./scripts/flash_unified_candidate.sh \
  --port /dev/cu.usbmodem101 \
  --backup /Users/bytedance/Projects.localized/Codex/StopWatch/backups/watch-full-flash-20260722-213116.bin
```

Replace `/dev/cu.usbmodem101` only with the port actually detected in this step. Expected: esptool writes bootloader, partition table, application, and assets, then hard-resets successfully.

- [ ] **Step 5: Perform the physical acceptance matrix**

Verify and record each item separately:

```text
1. Codex opens first; right-to-left swipe opens TRAE; reverse swipe returns.
2. Header icon and TRAE visible centers align within 1 px by screenshot inspection.
3. BT LIVE/OFF follows actual pairing; BT OFF always makes TRAE --.
4. Closing/opening TRAE produces OFF/LIVE after the next accepted packet.
5. Zero active tasks shows both zero counts and 暂无进行中的任务.
6. More than four active tasks keeps full counts while showing at most four cards.
7. Pending cards auto-rotate every three seconds; running never auto-replaces them.
8. Tapping reaches running cards; after five seconds the front returns to pending.
9. A horizontal swipe never advances the card deck.
10. Model/effort changes with a task-scoped card and falls back safely.
11. Robot is proportionate, crisp, animates briefly, pauses off-page, and pauses screen-off.
12. Page dots stay above the battery; Home and screen-off controls still work.
```

Build, flash, BLE delivery, and physical visual/gesture behavior are four distinct evidence gates.

- [ ] **Step 6: Commit only regression fixes, then report evidence**

If physical verification required code changes, rerun Steps 1-5 and commit each scoped fix with its tests. If no fix was needed, create no empty commit. Final reporting must state exact automated results, artifact hash, flash result, and every physical item that was or was not observed.
