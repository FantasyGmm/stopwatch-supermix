# Typeless Start Latency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce Typeless startup latency by using a 300 ms voice hold and reusing a safely prepared Core Audio route.

**Architecture:** Keep button timing in `VoiceButtonEdgeGate`. Split `VoiceAudioRoute` into prepare, per-session start/stop, and terminal close phases; `VoiceSessionService` owns preparation and terminal disposal while retaining its existing session cleanup state machine.

**Tech Stack:** C++17 firmware host tests, Python 3.9, PyObjC, Core Audio bridge, `unittest`.

---

### Task 1: Change the voice hold boundary with TDD

**Files:**
- Modify: `tests/voice_input_controller_test.cpp`
- Modify: `main/apps/app_codex_status/voice_input_controller.h`
- Modify: `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`
- Modify: `README.md`
- Modify: `mac-codex-agent/README.md`

- [x] Change boundary assertions to prove release at 299 ms is `ShortClick`
  and a held button at 300 ms is `Press`; run the focused host test and verify
  RED while the constant remains 500.
- [x] Set `kVoiceHoldMs` to 300, update contracts and user-facing text, then run
  the focused host test and verify GREEN.

### Task 2: Introduce a reusable audio-route lifecycle with TDD

**Files:**
- Modify: `mac-codex-agent/tests/test_voice_audio.py`
- Modify: `mac-codex-agent/voice_audio.py`

- [x] Add tests proving `prepare()` opens without start or input switching,
  `start()` uses the prepared queue, `stop()` restores input without closing,
  a second session does not reopen, and `close()` disposes once.
- [x] Run the focused tests and verify RED because `prepare()` and `close()` do
  not yet provide this lifecycle.
- [x] Implement the minimal prepared/opened/started state transitions and
  failure cleanup, then verify GREEN.

### Task 3: Integrate preparation and terminal disposal with TDD

**Files:**
- Modify: `mac-codex-agent/tests/test_voice_session.py`
- Modify: `mac-codex-agent/voice_session.py`

- [x] Extend fakes and tests so `prepare()` prepares the route only when
  readiness is Ready, normal session cleanup calls `stop()`, and disconnect or
  close performs terminal route `close()` after session cleanup.
- [x] Verify focused RED, implement the lifecycle calls without changing the
  existing cleanup-generation ordering, and verify GREEN.

### Task 4: Full verification and delivery

**Files:**
- Verify all modified files.

- [x] Run focused Mac voice tests and firmware voice-controller tests.
- [x] Run the complete Mac Agent test suite and unified firmware host suite.
- [x] Build the production ESP32-S3 firmware and run `git diff --check`.
- [x] Benchmark prepared restart against cold route start and record the
  measured reduction in the handoff.
- [x] Commit the verified source, tests, and design artifacts; install the Mac
  Agent and app-flash the connected watch without backup or NVS erase.

## Self-review

All approved behavior maps to focused RED/GREEN coverage. The plan preserves
short-click, Home chord, security, input restoration, and terminal cleanup;
the optimization removes only repeated queue construction from the active
start path. There are no placeholders or unrelated refactors.
