# Lightweight Architecture Convergence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Simplify stale repository contracts, unify route ownership per BLE session, and centralize the smallest useful firmware runtime-resource boundary.

**Architecture:** Preserve all existing protocol and UI behavior. First remove duplicate and historical-text coupling, then reset and inject one logical session registry into both status publication and task voice submission, and finally move only repeated transport/voice/input cleanup behind `CodexRuntimeSession`.

**Tech Stack:** Python 3.9 `unittest`, C++17, ESP-IDF 5.5.4, LVGL, NimBLE, Git.

**Spec:** `docs/superpowers/designs/2026-08-22-lightweight-architecture-convergence.md`

## Global Constraints

- Do not modify UUIDs, wire formats, crypto, nonce/replay behavior, queue sizes, credentials, NVS, UI geometry, gesture semantics, or normal no-backup flashing.
- Preserve the user-owned untracked `.superpowers/` directory.
- Use focused TDD for behavioral work and stage only files in this plan.
- Keep each phase independently reviewable and avoid opportunistic refactors.

---

### Task 1: Simplify live contracts and duplicate manifests

**Files:**
- Modify: `README.md`
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`
- Modify: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`

**Interfaces:**
- Consumes: current source-only installer and candidate build/flash scripts.
- Produces: one canonical module entry per installer list and tests that assert executable release behavior instead of dated report prose.

- [ ] Remove report-text assertions and add behavior-level assertions for the current build/flash scripts; run the focused test first and confirm the obsolete expectation fails.
- [ ] Update README current workflow wording and remove duplicate installer/test manifest entries with the smallest diff.
- [ ] Run `test_install_script.py`, `test_unified_runtime_firmware_contract.py`, and `test_unified_flash_tools.py`; run `git diff --check` and commit the phase.

### Task 2: Share one route registry per BLE session

**Files:**
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_runtime.py`

**Interfaces:**
- Consumes: `TaskRouteRegistry`, `TaskVoiceSubmitter`, and `CodexStatusAgent`.
- Produces: `CodexStatusAgent(..., task_route_registry=registry)` and one registry reset at each serialized BLE-session boundary.

- [ ] Add a focused failing test proving the status publisher uses an injected registry and does not create a second registry.
- [ ] Add a focused failing runtime test proving a collision blocks one session but a new session starts with a clean registry while publisher and submitter share it.
- [ ] Inject the registry into `CodexStatusAgent`; reset the runtime registry at the serialized connection boundary so the existing submitter and publisher share one session-scoped mapping. Preserve explicit test injections.
- [ ] Run route, status-agent, voice-submitter, and focused runtime tests; run Python compile and `git diff --check`, then commit the phase.

### Task 3: Extract the minimal firmware runtime session

**Files:**
- Create: `main/apps/app_codex_status/codex_runtime_session.h`
- Create: `main/apps/app_codex_status/codex_runtime_session.cpp`
- Create: `tests/codex_runtime_session_test.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_unified_voice_validation.py`

**Interfaces:**
- Consumes: existing transport, input, voice, probe, and pinned-microphone classes.
- Produces: a minimal `CodexRuntimeSession` cleanup facade and narrow accessors used by `AppCodexStatus`.

- [ ] Add a focused test for the lifecycle seam that proves disconnect and shutdown cleanup are idempotent; run it and confirm it fails because the session does not exist.
- [ ] Implement `CodexRuntimeSession` with the minimum existing-resource references and lifecycle methods needed by current call sites; do not add a parallel state machine.
- [ ] Replace `AppCodexStatus` resource members and duplicated disconnect/close cleanup with the facade without moving UI, pairing policy, registry persistence, or protocol logic.
- [ ] Add the host test to the existing validation stage, update the exact stage/source contract, and run the new host test plus existing voice/input/pinned-microphone controller tests.
- [ ] Run relevant Python contracts, C++ host validation, Python compile, `git diff --check`, and commit the phase.

### Task 4: Final verification and integration review

**Files:**
- Verify only; no planned production changes.

**Interfaces:**
- Consumes: the three independently committed phases.
- Produces: fresh evidence that the branch is safe to integrate.

- [ ] Review `git diff e0ceabc...HEAD`, confirm no user-owned/unplanned files are present, and check design scope line by line.
- [ ] Run focused Python suites, route/runtime tests that are stable in the project environment, all relevant C++ host tests, `py_compile`, and `git diff --check`.
- [ ] If a clean ESP-IDF environment is available, run the candidate compile/capacity gate; otherwise report that build gate separately without inferring it from host tests.
- [ ] Use the finishing workflow to integrate the branch without touching `.superpowers/`.
