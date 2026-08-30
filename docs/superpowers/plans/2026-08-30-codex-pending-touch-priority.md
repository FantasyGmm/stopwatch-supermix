# Codex Pending Touch Priority Implementation Plan

**Goal:** Preserve the original Pending halo exactly and decouple CST820 touch
sampling from blocked LVGL rendering.

**Architecture:** A priority-2 adaptive sampler is the sole CST820 reader and
writes timestamped samples to a fixed 32-slot queue. LVGL consumes a compressed
Pressed/Move/Released sequence while Codex uses physical sample timestamps.

## Constraints

- Do not change halo rendering, color, geometry, opacity, invalidation, or
  animation timing.
- Do not pause the halo on touch or bind it to input events.
- Do not change gesture thresholds or global edge-control ownership.
- Do not erase NVS, pairing state, storage, or assets.
- Preserve unrelated Agent and Trae work in the shared worktree.
- Do not commit until the user requests submission.

## Steps

- [x] Reproduce the red-halo touch loss and identify blocked LVGL sampling as
  the root cause.
- [x] Validate independent 10 ms hardware sampling on the physical watch.
- [x] Implement and host-test the fixed, release-preserving touch queue.
- [x] Add adaptive 33 ms idle / 10 ms contact sampling.
- [x] Make the sampler the sole CST820 reader and protect queue access.
- [x] Propagate physical timestamps through LVGL into Codex gesture logic.
- [x] Clear stale/in-flight samples across screen-off lifecycle boundaries.
- [x] Remove all prototype samplers and timing logs.
- [x] Restore the original halo implementation and remove every rejected
  rendering/pause experiment.
- [x] Run focused queue, touch-gate, swipe, and original-halo regressions.
- [x] Build with ESP-IDF 5.5.4 and pass the capacity gate.
- [x] Flash only the application partition and verify its hash and boot.
- [ ] Confirm the original halo appearance and repeated swipes on the AMOLED.
