# Core Audio Queue Restart Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep prepared Core Audio queues writable across consecutive voice sessions.

**Architecture:** Add a tiny native buffer-schedule state object and make the Core Audio bridge reschedule its existing buffers after every synchronous stop. Preserve the Python lifecycle and fail closed by disposing a partially rescheduled queue.

**Tech Stack:** C++17, Objective-C++, AudioToolbox Audio Queue Services, Python `unittest`.

---

### Task 1: Prove the restart lifecycle with TDD

**Files:**
- Create: `mac-codex-agent/native/voice_audio_queue_lifecycle.h`
- Modify: `mac-codex-agent/native/voice_audio_ring_test.cpp`

- [x] Add assertions that a new queue needs scheduling, a scheduled queue does
  not, and a stopped queue needs scheduling again.
- [x] Compile the focused native test and verify RED because the lifecycle
  helper does not exist.
- [x] Implement the minimal schedule state helper and verify GREEN.

### Task 2: Reschedule native buffers before restart

**Files:**
- Modify: `mac-codex-agent/native/voice_audio_bridge.mm`

- [x] Separate buffer allocation from buffer clearing and enqueueing.
- [x] Mark buffers scheduled only after every enqueue succeeds.
- [x] On `swva_start`, reschedule when required; dispose on partial failure.
- [x] On synchronous stop and close, mark the schedule unscheduled.

### Task 3: Verify and deploy

**Files:**
- Verify all modified files.

- [x] Run the native focused test, voice-focused Agent tests, complete Agent
  suite, unified firmware host suite, `py_compile`, and `git diff --check`.
- [x] Build the native library and run at least three real BlackHole
  start/write/stop cycles with full-frame acceptance.
- [x] Commit the verified fix, reinstall the Mac Agent, and relaunch it without
  flashing the watch.

## Self-review

Every observed failure maps to the native schedule transition. The plan keeps
the optimization, adds no new public protocol, and verifies the exact repeated
session sequence that escaped the earlier FakeBridge tests.
