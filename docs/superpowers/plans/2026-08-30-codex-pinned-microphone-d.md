# Codex Pinned Microphone Direction D — Implementation Plan

**Goal:** implement the approved Direction D in the existing firmware without
changing pinned-microphone session semantics.

## 1. Lock behavior and geometry with failing tests

- Extend `pinned_microphone_controller_test.cpp` with real-level meter mapping
  and the Active prompt.
- Update the two firmware UI contract tests to require Direction D geometry,
  grille segments, mirrored external meter objects, and the absence of an
  internal animated meter.
- Run only those focused tests and confirm they fail for the missing D code.

## 2. Add bounded meter presentation logic

- Add a small platform-independent meter mapper beside the pinned microphone
  controller.
- Map the four newest PCM history values to bounded 12–64 px heights with a
  quiet floor; inactive states hide all meter segments and return zero heights.
- Compile this mapper into the existing pinned-microphone host-test stage so
  the validation stage count remains unchanged.

## 3. Implement Direction D in LVGL

- Replace the old capsule/yoke dimensions with the locked D geometry.
- Add three static horizontal grille segments.
- Add four mirrored external meter bars per side.
- Reuse `StopWatchAudioBackend::inputLevels()` only while Active and update the
  eight meter objects after static-state rendering.
- Reset all object pointers and render caches during view destruction.

## 4. Focused verification

- Run the C++ pinned-microphone host test.
- Run both Python firmware contract modules and the unified-validation metadata
  test that covers the modified host-test source list.
- Run `git diff --check` and inspect the exact diff, excluding unrelated user
  changes.

## 5. Device build and physical gate

- Build with the project ESP-IDF 5.5.4 environment and the capacity gate.
- Flash only the capacity-gated candidate artifacts to the exact connected
  `/dev/cu.usbmodem*` device; do not erase NVS or make a firmware backup.
- Verify hashes and a new serial boot.
- Compare Ready and Active AMOLED states against the locked design region by
  region. Do not call the UI visually complete before this check.

## Self-review

- The plan keeps the change in the UI/presentation seam and does not modify the
  voice transport or capture lifecycle.
- It provides a red-capable test for level mapping instead of relying only on
  source-string assertions.
- It avoids a new validation stage and therefore avoids stale stage counts.
- It separates build/flash evidence from physical visual acceptance.
