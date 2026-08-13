# Codex External Microphone and UI Fidelity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move fixed microphone to the left of Codex, stream it to the Mac virtual input without Typeless, and make the firmware screen faithfully match the approved centered-beacon design.

**Architecture:** Add an authenticated session mode to the existing encrypted Begin payload so the Mac can arm either route+Typeless or route-only. Keep one BLE audio transport and one BlackHole route. Reorder the three-page strip to pinned microphone → Codex → TRAE and replace undersized/fallback UI assets with dedicated Chinese fonts and LVGL microphone geometry.

**Tech Stack:** ESP-IDF C++, LVGL, AES-CCM voice protocol, Python Mac Agent, CoreAudio/BlackHole, C++ and Python tests, real ESP32-S3 build/flash.

---

### Task 1: Lock corrected navigation and visual contracts

- [x] Change swipe tests to require Codex right → fixed microphone, Codex left → TRAE, and no wrap at either outer edge; run them to observe the old ordering fail.
- [x] Change firmware UI contract tests to require pinned x=0, Codex x=466, TRAE x=932, initial page index 1, 24 px title, 22 px state, and geometric microphone objects; run them to observe the old layout fail.
- [x] Implement only the page-order and initial-offset changes; rerun focused swipe/UI contracts to green.

### Task 2: Add authenticated external-microphone session mode

- [x] Add C++ and Python fixed-vector tests requiring Begin mode `PushToTalk` or `ExternalMicrophone`, rejecting unknown/reserved values and preserving exact encryption/AAD validation; verify RED.
- [x] Extend the Begin body by one mode byte and increment the production protocol version on both sides; update exact sizes and encoders/decoders; verify C++/Python protocol GREEN.
- [x] Extend `VoiceInputController::start_continuous` to choose external mode while right-button start chooses PTT; verify decoded Begin modes in the controller host test.

### Task 3: Route-only Mac session

- [x] Add Mac `VoiceSessionService` tests proving PTT calls `route.start()` and `shortcut.start()`, external mode calls only `route.start()`, external audio writes continuously, and End/error cleanup never calls a shortcut that was not armed; verify RED.
- [x] Pass Begin mode into `_begin`, branch shortcut arming only for PTT, and retain the existing bounded route cleanup; verify focused voice session GREEN.
- [x] Add runtime readiness tests proving external-microphone availability depends on the route/driver but not Typeless process state.

### Task 4: Restore the approved centered-beacon UI

- [x] Generate dedicated 24 px and 22 px Chinese LVGL fonts containing exactly the approved title/status/error glyphs; register them in `main/CMakeLists.txt` and declare them near the Codex view.
- [x] Replace `LV_SYMBOL_AUDIO` with native LVGL microphone geometry: capsule, U-shaped cup, stem, base, and state beacon centered on x=233; keep exact off/active colors and fixed geometry.
- [x] Render off, active, unavailable, and error without geometry changes; rerun UI contract tests.
- [x] Update browser reference and design contract only when measurements from the original approved design are being captured, never as a new visual direction.

### Task 5: Verification and real acceptance

- [x] Run fixed protocol vectors, Mac voice session tests, swipe tests, pinned controller tests, firmware/UI contracts, unified contract, and real host validation.
- [x] Run full Mac Agent discovery in the packaged Agent Python runtime, then build the ESP-IDF application and confirm capacity.
- [x] Install/restart the Mac Agent because the protocol and route behavior changed.
- [x] Flash the connected watch directly without firmware backup or NVS erase.
- [ ] Compare the physical AMOLED against the locked design at 466 × 466 for off, active, and unavailable states; record every remaining geometry/type/icon mismatch before acceptance.
- [ ] Select BlackHole 2ch in a Mac recorder, enable fixed microphone, verify continuous audio without Typeless activation, navigate away/back, disable, and verify route cleanup.

Physical review note, 2026-08-14: the first user-supplied unavailable-state
photo confirmed the corrected 24 px title, 22 px state, centered geometric
microphone, and left page indicator, but also exposed three render-only defects
that source-coordinate tests had missed: a stray vertical beacon segment above
the microphone, an opaque blue beacon disc instead of a soft glow, and a blue
unchecked switch track inherited from the LVGL theme. The follow-up candidate
removes the segment, keeps the beacon center transparent with shadow-only glow,
and explicitly styles the unchecked indicator graphite. Final off/active photo
comparison remains open until the reflashed candidate is photographed.
