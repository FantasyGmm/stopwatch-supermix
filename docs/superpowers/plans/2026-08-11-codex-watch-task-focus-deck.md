# Codex Watch Task Focus Deck Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the approved original-E watch task focus deck, verbatim last-reply reader, persistent task selection, and direct pending/start or running/steer voice routing.

**Architecture:** The Mac Agent owns Codex thread and turn IDs and gives the watch only a deterministic 64-bit route token. Compact tasks stay in the existing status snapshot; replies use separate UTF-8-safe chunks. The watch owns a pure deck model and LVGL view; PTT begin captures the selected route, and the Mac reads one new Typeless history row before a fail-closed Codex App Server submission.

**Tech Stack:** ESP-IDF C++17, LVGL, cJSON, AES-CCM production voice framing, Python 3 asyncio/sqlite3, Codex App Server JSON-RPC, unittest, C++ host tests.

---

## File structure

- `mac-codex-agent/task_routes.py`: opaque route derivation, route registry, reply bounds/chunks.
- `mac-codex-agent/task_alerts.py`: retain exact reply and active turn ID.
- `mac-codex-agent/codex_adapter.py`, `agent.py`: produce compact lists and detail messages.
- `mac-codex-agent/typeless_transcripts.py`: read-only Typeless high-water mark and bounded poll.
- `mac-codex-agent/codex_app_server.py`, `task_voice_submitter.py`: JSON-RPC and start/steer policy.
- `mac-codex-agent/voice_input_protocol.py`, `voice_session.py`, `runtime.py`: route-bound recording lifecycle.
- `main/apps/app_codex_status/codex_status_model.{h,cpp}`: parse route tasks and detail chunks.
- `main/apps/app_codex_status/codex_task_detail_store.{h,cpp}`: atomic revisioned reassembly.
- `main/apps/app_codex_status/codex_task_focus_deck.{h,cpp}`: pure order, browse, selection, fallback.
- `main/apps/app_codex_status/codex_task_focus_layout.h`: fixed 466 × 466 original-E tokens.
- `main/apps/app_codex_status/codex_task_focus_view.{h,cpp}`: reusable LVGL deck/detail rendering.
- Existing swipe, voice protocol/controller, app coordinator, CMake, and validation files integrate the feature.
- Focused Python and C++ tests mirror each module; `docs/superpowers/verification/` holds final evidence.

### Task 1: Retain routable task metadata

**Files:** Create `mac-codex-agent/task_routes.py`, `mac-codex-agent/tests/test_task_routes.py`; modify `mac-codex-agent/task_alerts.py`, `mac-codex-agent/tests/test_task_alerts.py`.

- [ ] Write tests proving deterministic distinct `^[0-9a-f]{16}$` route IDs, collision rejection, running `turn_id`, exact completed `last_agent_message`, and inheritance of the last stable reply into the next running alert.
- [ ] Run `python3 -m unittest mac-codex-agent/tests/test_task_routes.py mac-codex-agent/tests/test_task_alerts.py`; verify RED from missing module/fields.
- [ ] Implement `route_id_for_thread()` with BLAKE2b `digest_size=8, person=b"SWRoute1"`; add immutable `turn_id: Optional[str]` and `last_reply: str` fields; persist optional ledger keys compatibly.
- [ ] Re-run the focused tests and verify GREEN.
- [ ] Commit with `git commit -m "feat: retain routable Codex task replies"`.

### Task 2: Send compact tasks and exact detail chunks

**Files:** Modify `mac-codex-agent/codex_adapter.py`, `mac-codex-agent/agent.py`, `mac-codex-agent/tests/test_codex_adapter.py`, `mac-codex-agent/tests/test_agent.py`.

- [ ] Test that internal alerts include route/turn/reply, the watch list includes only route/project/title/state for active tasks, main JSON stays at most 2048 bytes, and concatenated UTF-8 chunks equal the bounded original reply.
- [ ] Run `python3 -m unittest mac-codex-agent/tests/test_codex_adapter.py mac-codex-agent/tests/test_agent.py`; verify RED.
- [ ] Implement `task_detail` messages with `{type,route,revision,index,count,text}`, 12 KiB Unicode-boundary truncation plus explicit marker, and per-session digest caching in `CodexStatusAgent.tick()`.
- [ ] Re-run focused tests and verify every encoded message is at most 2048 bytes.
- [ ] Commit with `git commit -m "feat: sync Codex task details to watch"`.

### Task 3: Parse and reassemble details on watch

**Files:** Modify `codex_status_model.{h,cpp}` and its test; create `codex_task_detail_store.{h,cpp}` and `tests/codex_task_detail_store_test.cpp`; modify unified validation.

- [ ] Test strict lowercase 16-hex routes, invalid-input no-mutation, pending/running tasks, out-of-order chunks, identical/conflicting duplicates, revision replacement, incomplete non-publication, UTF-8 preservation, route eviction, and bounds.
- [ ] Compile the parser test and the missing detail-store binary; verify RED.
- [ ] Add `uint64_t route` to `CodexTaskAlert`, `CodexTaskDetailChunk`, `CodexMessageKind::TaskDetail`, strict string-to-uint64 parsing, and `CodexTaskDetailStore::{apply,find,retain_routes,reset}` with all-or-nothing publication.
- [ ] Add the host stage, run both focused binaries and `IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --contract`; verify GREEN and exact stage count.
- [ ] Commit with `git commit -m "feat: reassemble Codex task replies on watch"`.

### Task 4: Implement pure deck state and gesture ownership

**Files:** Create `codex_task_focus_deck.{h,cpp}`, `tests/codex_task_focus_deck_test.cpp`; modify swipe/touch logic tests and unified validation.

- [ ] Test pending-first stable order, completed filtering, no wrap, one boundary event, browse-not-select, tap-select, route persistence, pending-then-running fallback, empty reset, bottom-center upward entry, axis locking, and unchanged horizontal Codex/Trae paging.
- [ ] Compile the missing deck test and verify RED.
- [ ] Implement `CodexTaskFocusDeck::{apply,browse,select_focused,focused,selected_route}` returning explicit focus/boundary/selection events; add `OpenTaskDeck` to swipe results and classify only the bottom-center safe zone.
- [ ] Run focused deck/swipe/touch tests and contract validation; verify GREEN.
- [ ] Commit with `git commit -m "feat: add Codex task focus deck state"`.

### Task 5: Render the original-E deck and detail view

**Files:** Create `codex_task_focus_layout.h`, `codex_task_focus_view.{h,cpp}`, `tests/codex_task_focus_layout_test.cpp`; modify `main/CMakeLists.txt` and unified validation.

- [ ] Test viewport, right-track reservation, centered three-line title region, detail viewport, 44 px tap target, exact palette, node shapes, and absence of gradient/shadow/card tokens.
- [ ] Compile the missing layout test and verify RED.
- [ ] Implement one reusable LVGL object tree exposing `init`, `shutdown`, `show_deck`, `show_detail`, `set_recording`, `set_visible`, hit testing, and detail scrolling. Use Montserrat/PuHui, black background, centered project/title/status, clipped right arc, diamond pending nodes, circular running nodes, and capsule focus.
- [ ] Run layout test and compile new sources with the ESP32-S3 compile command; verify GREEN.
- [ ] Commit with `git commit -m "feat: render original E task focus deck"`.

### Task 6: Bind route into authenticated voice PTT begin

**Files:** Modify watch and Mac voice protocol, watch controller, and their existing tests.

- [ ] Add cross-language tests for protocol 5 begin `<BHHQ>` with route `0x0123456789abcdef`, zero-route rejection, authenticated tamper rejection, controller selection snapshot, and mid-recording selection immutability.
- [ ] Run focused C++ protocol/controller tests and `python3 -m unittest mac-codex-agent/tests/test_voice_input_protocol.py`; verify RED.
- [ ] Set version 5, add explicit little-endian 64-bit helpers and `BeginBody.route`, then add `VoiceInputController::set_task_route(uint64_t)` and copy persistent route into the session before encoding begin.
- [ ] Run both language suites and verify GREEN.
- [ ] Commit with `git commit -m "feat: bind voice sessions to Codex task routes"`.

### Task 7: Capture exactly one new Typeless transcript

**Files:** Create `mac-codex-agent/typeless_transcripts.py`, `mac-codex-agent/tests/test_typeless_transcripts.py`.

- [ ] Build temporary `history_v2` tests for high-water marks, pre-existing row exclusion, processing-to-complete polling, Unicode, empty/failed rows, timeout, lock/missing-table failure, and single-result semantics.
- [ ] Run the focused test and verify RED from the missing module.
- [ ] Implement `TranscriptCursor` and `TypelessTranscriptSource::{mark,wait_after}` using read-only SQLite URI, `(created_at,id)` order, injected sleep, terminal-success status, nonempty `refined_text`, bounded timeout, and transcript-free errors/logs.
- [ ] Re-run and verify GREEN.
- [ ] Commit with `git commit -m "feat: capture completed Typeless transcripts"`.

### Task 8: Route through Codex App Server without fallback

**Files:** Create `mac-codex-agent/codex_app_server.py`, `task_voice_submitter.py`, and their focused tests.

- [ ] Test one-time initialize, ID-matched responses among notifications, bounded errors, `turn/start`, `turn/steer` with exact `expectedTurnId`, stale/missing/colliding route rejection, missing running turn rejection, and no start fallback after steer rejection.
- [ ] Run the two focused tests and verify RED.
- [ ] Implement a serialized persistent line-JSON client. Submit pending with `turn/start` and `input:[{type:"text",text}]`; submit running with `turn/steer`, thread ID, expected turn ID, and the same input. Redact transcript parameters from all diagnostics.
- [ ] Re-run and verify GREEN.
- [ ] Commit with `git commit -m "feat: route watch voice through Codex App Server"`.

### Task 9: Integrate transcript submission into voice lifecycle

**Files:** Modify `voice_session.py`, `runtime.py`, `test_voice_session.py`, `test_runtime.py`.

- [ ] Test cursor mark before shortcut start, route snapshot at begin, Typeless stop before poll, one submit after validated end, no submit on disconnect/abort/cancel, timeout/empty/stale route to Error, no double-submit, and no transcript in logs/errors.
- [ ] Run both focused test modules and verify RED.
- [ ] Inject transcript source/submitter/timeout into `VoiceSessionService`; after existing drain and resource stop, await the new transcript and submit captured route before Ready. Share route registry/transcript source/App Server client for runtime lifetime and close the client in finalization.
- [ ] Re-run and verify GREEN without weakening existing bounded cleanup tests.
- [ ] Commit with `git commit -m "feat: submit Typeless voice to selected Codex task"`.

### Task 10: Coordinate watch view, touch, detail, and voice

**Files:** Modify `app_codex_status.{h,cpp}`, `main/CMakeLists.txt`; create `tests/test_codex_task_focus_firmware_contract.py`.

- [ ] Contract-test ownership of deck/view/store, list-before-render ordering, detail-only message handling, route eviction, bottom-center entry, vertical cancellation of paging, boundary haptic, select-before-detail, `set_task_route`, no-selection/disconnect voice disablement, Agent-panel preservation, and no release diagnostics.
- [ ] Run the new contract and verify RED.
- [ ] Add Status/TaskDeck/TaskDetail modes and touch state. Apply list snapshots to deck then store retention; apply detail chunks without resetting telemetry; update the controller route on selection/fallback; render cached detail; gate long-hold voice by connected detail selection; call the existing HAL haptic once at the state boundary.
- [ ] Run contract, focused host tests, and ESP32-S3 compilation for app/view/controller; verify GREEN.
- [ ] Commit with `git commit -m "feat: connect Codex task focus deck to voice"`.

### Task 11: Visual fidelity and complete verification

**Files:** Create `docs/superpowers/verification/codex-watch-task-focus-deck.md` and capture artifacts when supported; modify only tested defects found by comparison.

- [ ] Capture the authoritative E watch crop from `/Users/luke/.codex/visualizations/2026/07/29/019fae12-604c-7c80-a90a-3af247283570/watch-task-voice-flow.html` at 466 × 466.
- [ ] Capture native LVGL pending, running, mixed-boundary, long-CJK detail, empty, and recording states without hardware side effects. If off-device LVGL capture is unavailable, document the exact blocker and six-state on-device screenshot packet still required; do not claim visual completion.
- [ ] Compare geometry, type, color, rotary nodes, interactions, then long/empty states. For every defect, tighten a test first, observe RED, patch, and re-run GREEN. Use `scripts/compare-images.sh ... --threshold 0.03` when both images exist.
- [ ] Run `python3 -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'`, unified `--host-tests-only`, unified `--contract`, changed ESP32-S3 translation-unit compile or isolated firmware build, `git diff --check`, and `git status --short`.
- [ ] Self-review every requirement in the approved design spec, especially one task per screen, right rotary identity, verbatim stable reply, browse-not-select, fallback, authenticated captured route, exact start/steer, gesture non-conflict, and no release diagnostics.
- [ ] Commit evidence/fixes with `git commit -m "test: verify Codex watch task focus flow"`.

## Plan self-review

- **Spec coverage:** Tasks 1–3 cover authoritative metadata and original reply transport; 4–5 cover original-E interaction/visuals; 6 binds routing cryptographically; 7–9 cover Typeless/App Server; 10 integrates the watch; 11 supplies visual and full-system evidence.
- **Placeholder scan:** No TBD/TODO/similar-to step remains. Every task names tests, RED evidence, implementation interface, GREEN evidence, and a commit.
- **Type consistency:** Route is 16 lowercase hex characters in Mac/status JSON and a nonzero `uint64_t` on watch/wire. `TaskAlert.last_reply` feeds `task_detail`; `CodexTaskDetailChunk` feeds the store; `selected_route()` feeds controller `set_task_route()` and version-5 `BeginBody.route` decoded by Mac.
- **Scope control:** No live stream, transcript editing, completed-task deck, composer automation, Agent-panel replacement, Trae redesign, or release diagnostic work is included.
