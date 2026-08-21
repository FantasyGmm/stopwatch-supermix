# Formal Firmware Log Convergence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a formal unified firmware configuration that excludes development command surfaces and suppresses historical lifecycle diagnostics while retaining low-frequency field evidence for failures, BLE/security, storage, and reset diagnosis.

**Architecture:** Keep development tooling in source behind `CONFIG_STOPWATCH_DEBUG_COMMANDS`, but disable that option in the unified production defaults. Make Assistant memory/heap/task snapshots compile to no work in production, preserve warnings/errors and low-frequency Codex BLE connection evidence, and enforce the boundary with source/configuration contracts.

**Tech Stack:** ESP-IDF 5.5.4, C++17 firmware, Kconfig/CMake, Python `unittest`, shell validation.

**Spec:** `docs/formal-release-convergence.md`

## Global Constraints

- Do not change BLE, voice, UI, gesture, persistence, or security behavior.
- Keep USB Serial/JTAG output available for field logs; only the command parser/control surface is disabled.
- Preserve warnings and errors plus low-frequency boot/reset, BLE/authentication, NVS/migration, and unrecoverable audio/transport evidence.
- Preserve `.superpowers/` and all unrelated user-owned files.
- Build with ESP-IDF 5.5.4 from a freshly generated configuration before claiming the production image is converged.

---

### Task 1: Lock the production logging boundary with failing contracts

**Files:**
- Modify: `mac-codex-agent/tests/test_debug_command_firmware_contract.py`
- Modify: `mac-codex-agent/tests/test_assistant_trace_firmware_contract.py`

**Interfaces:**
- Consumes: `sdkconfig.unified.defaults`, `main/CMakeLists.txt`, Assistant runtime sources.
- Produces: contracts that reject debug commands, release-time memory snapshot execution, transient `PINMIC` traces, and per-packet NimBLE INFO logging while requiring essential BLE/error evidence.

- [x] **Step 1: Change the debug-command contract to require `# CONFIG_STOPWATCH_DEBUG_COMMANDS is not set` and reject `CONFIG_STOPWATCH_DEBUG_COMMANDS=y` in unified defaults.**
- [x] **Step 2: Add a release logging contract that requires memory snapshot/logging bodies to be guarded by `CONFIG_STOPWATCH_DEBUG_COMMANDS`, keeps NimBLE at Warning, rejects `PINMIC`, and requires Codex BLE connection/error logs.**
- [x] **Step 3: Run the two focused Python modules and verify failures identify the enabled debug default and unguarded lifecycle diagnostics.**

### Task 2: Disable development surfaces and release-time diagnostic work

**Files:**
- Modify: `sdkconfig.unified.defaults`
- Modify: `main/CMakeLists.txt`
- Modify: `main/runtime/assistant/assistant_runtime.cpp`
- Modify: `main/runtime/runtime_supervisor.cpp`
- Modify: `main/runtime/service_runtime.cpp`
- Modify: `docs/formal-release-convergence.md`

**Interfaces:**
- Consumes: `CONFIG_STOPWATCH_DEBUG_COMMANDS` and existing debug-only CMake source selection.
- Produces: a production default with no USB command parser, battery calibration runtime, Assistant trace rendering, or unconditional heap/task snapshot logging.

- [x] **Step 1: Disable `CONFIG_STOPWATCH_DEBUG_COMMANDS` in unified defaults while retaining USB Serial/JTAG output.**
- [x] **Step 2: Exclude the battery calibration service implementation from production image sources.**
- [x] **Step 3: Guard Assistant memory-stage snapshots and success-only setup INFO logs behind the debug option.**
- [x] **Step 4: Guard RuntimeSupervisor snapshot capture, leak-gate logs, and success-only startup INFO behind the debug option without changing session state transitions.**
- [x] **Step 5: Guard the service-runtime success INFO and update the formal convergence document to record the new source state rather than a release candidate.**
- [x] **Step 6: Re-run the focused contracts and verify GREEN.**

### Task 3: Verify the production configuration and relevant behavior

**Files:**
- Verify only: exact diff from baseline to working tree.

**Interfaces:**
- Consumes: the changed defaults and guarded runtime sources.
- Produces: reproducible evidence that production excludes development diagnostics without weakening required failure evidence.

- [x] **Step 1: Run debug-command, Assistant-trace, runtime-diagnostics, BLE firmware-contract, and installer/source contract tests.**
- [x] **Step 2: Run `git diff --check` and compile changed Python modules.**
- [x] **Step 3: Configure a fresh temporary ESP-IDF 5.5.4 build directory and verify its generated `sdkconfig` has debug commands disabled and NimBLE Warning logging.**
- [x] **Step 4: Build the clean firmware candidate and run capacity/package checks appropriate to the repository.**
- [x] **Step 5: Inspect the ELF/strings/source list to confirm debug command strings, `ASTRACE`, and battery calibration runtime symbols are absent while required Codex BLE errors remain.**
- [x] **Step 6: Inspect the exact diff and report that flashing/physical boot acceptance remains a separate gate.**
