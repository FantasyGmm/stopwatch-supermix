# Assistant Internal Memory Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use executing-plans to implement this plan task-by-task.

**Goal:** Prevent Assistant startup from rebooting the watch when Brookesia starts its NVS/SNTP dependencies, while preserving UI, DMA, and flash-operation behavior.

**Architecture:** Keep the 32 KiB internal-only reserve, route ordinary allocations directly to PSRAM when possible, and move the ESP-IDF-supported Wi-Fi, lwIP, Bluetooth, and radio static BSS to PSRAM. Move the 24 KiB audio worker stack to PSRAM, use a light Brookesia audio compatibility layer, add contiguous-internal-heap diagnostics, and make Assistant service setup/action submission exception-safe so allocation pressure rolls back to an error state instead of aborting the firmware.

**Tech Stack:** ESP-IDF 5.5.4, ESP32-S3 octal PSRAM, C++17, Brookesia services, Python contract tests, native host tests, isolated unified firmware build.

---

## Assessment and decision

- Reproduced failure: `SNTP::try_load_data()` starts an NVS operation, Brookesia creates a promise/mutex, the underlying FreeRTOS semaphore allocation fails, `boost::thread_resource_error` escapes, and the process terminates.
- Measured pre-fix candidate: static DIRAM `200,947 B`; immediately before the failing Assistant start, internal free/minimum heap was approximately `71/26 KiB`, while PSRAM had approximately `7.4 MiB` free.
- Isolated ESP-IDF external-BSS build: static DIRAM `186,795 B`, a reduction of `14,152 B` (7.0%); `.ext_ram.bss` is `14,224 B`; application binary growth is `208 B`.
- The chosen option is narrower than lowering `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`: ESP-IDF automatically relocates only the BSS of `lwip`, `net80211`, `pp`, and `bt` plus explicitly attributed sections.
- Initial implementation kept `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` and `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`. Hardware follow-up then measured only `19,591 B` internal free with an `11,264 B` largest block after service setup, immediately followed by `wifi:mem fail` during Xiaozhi TLS activation. The final validated policy sets the ordinary-allocation preference threshold to `0` while retaining the 32 KiB internal/DMA reserve. It also creates the 24 KiB audio worker with `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` and uses a light feeder/recorder compatibility overlay, preserving internal contiguous memory for Wi-Fi/TLS without changing the public Brookesia service graph.
- Hardware validation on 2026-07-19 reached activation, Decoder/Encoder start, WakeNet model load, MCP initialization, server connection, microphone packet upload, and a stable running state without reboot. A second open/close cycle passed the stopped-boundary gate with `task_delta=0`; all bindings, subscriptions, Wi-Fi, capture, and playback owners were released.

## Task 1: Lock the memory policy with a failing contract test

**Files:**
- Create: `mac-codex-agent/tests/test_assistant_memory_policy_contract.py`
- Modify: `sdkconfig.defaults`
- Modify: `sdkconfig`

1. Assert that both configuration sources enable `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`.
2. Assert that the zero-byte normal-allocation preference and 32 KiB internal/DMA reserve remain fixed after hardware follow-up.
3. Run the focused test and confirm it fails before changing production configuration.
4. Add the three explicit settings to `sdkconfig.defaults` and align the tracked generated `sdkconfig`.
5. Re-run the focused test and confirm it passes.

## Task 2: Add contiguous internal-memory diagnostics

**Files:**
- Modify: `main/runtime/runtime_diagnostics.h`
- Modify: `main/runtime/runtime_diagnostics.cpp`
- Modify: `main/runtime/runtime_supervisor.cpp`
- Modify: `main/runtime/assistant/assistant_runtime.cpp`
- Modify: `mac-codex-agent/tests/test_assistant_memory_policy_contract.py`

1. Add a failing contract asserting snapshots capture and log `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)`.
2. Add `internal_largest_free_block` to `RuntimeDiagnosticSnapshot`, populate it on-device, and include it in supervisor logs.
3. Log consistent Assistant boundaries before resource acquisition, after Wi-Fi acquisition, after service setup, and on caught setup exceptions.
4. Run focused Python and native runtime-diagnostics tests.

## Task 3: Convert allocation exceptions into rollback

**Files:**
- Modify: `main/runtime/assistant/assistant_runtime.cpp`
- Modify: `mac-codex-agent/tests/test_assistant_memory_policy_contract.py`

1. Add a failing contract asserting Assistant setup and asynchronous action submission have `std::exception` and unknown-exception boundaries.
2. Wrap synchronous startup after session creation; on exception, log diagnostics and call the existing `failAndReturn()` cleanup path.
3. Wrap `submitAction()` because it is also called later from `update()` and `requestStop()`.
4. Preserve binding cleanup, subscription disconnect, Wi-Fi release, capture/playback release, and stopped state.
5. Run focused tests.

## Task 4: Build and verify the unified candidate

**Files:**
- Generated: `build-unified/*`

1. Run `./scripts/run_unified_offline_validation.sh`.
2. Generate firmware metrics and confirm capacity gates pass and static DIRAM remains near the isolated `186,795 B` result.
3. Run `git diff --check` and inspect the final diff for unrelated changes.

## Task 5: Flash and perform staged hardware validation

**Prerequisite:** Use the already verified full-flash backup `backups/watch-full-flash-20260718-173922.bin` with SHA-256 `b6ecc3da8f0dbd41de9722a1956566ca3ce0f5506e6439fe5c4a57366c9000dc`.

1. Flash the clean unified candidate using the repository candidate-flash script and its backup/hash guard.
2. Monitor boot and record reset reason plus baseline `internal`, `largest`, and PSRAM values.
3. Open Assistant and verify it passes Wi-Fi acquisition, NVS/SNTP binding, agent activation, and reaches a stable running or actionable network/authentication error state without rebooting.
4. Close and reopen Assistant, then inspect the stopped-boundary leak gate.
5. If a later stage still exhausts internal memory, use the new largest-block evidence before considering a separate Wi-Fi-buffer or allocation-policy change.

## Completed outcome

- The clean unified validation passed 133 Python tests, 6 host C++ test binaries, an isolated 2,332-unit firmware build, both capacity gates, and the whitespace gate.
- The final clean application is 5,998,336 bytes with 1,865,984 bytes (23.73%) headroom; static DIRAM is 185,415 bytes.
- Automated hardware control can list applications, open Assistant, and return Home over USB without a person touching the watch.
- Audible TTS, a physically spoken wake phrase, UI text/emotion observation, 10-cycle targeted testing, 100-cycle stress, and the full product regression remain separate manual/campaign gates.
