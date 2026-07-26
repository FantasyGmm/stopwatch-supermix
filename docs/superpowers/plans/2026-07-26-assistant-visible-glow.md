# Assistant Visible Glow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the selected H2 Assistant icon's centered cyan-violet glow visibly survive RGB565 conversion and the watch display.

**Architecture:** Keep the existing 86 px radial field, H2 colors, five-bar geometry, and per-bar diagonal gradient. Change only the two radial alpha stops to the approved B medium strength, protect the result with a decoded RGB565 pixel contract, regenerate the deterministic C asset, then incrementally build and directly flash the watch.

**Tech Stack:** Python 3, Pillow, `unittest`, LVGL RGB565, ESP-IDF 5.5.4, esptool-based repository scripts.

---

## File structure

- Modify `mac-codex-agent/tests/test_assistant_icon_asset.py`: require a visible, symmetric background glow while keeping dark canvas edges.
- Modify `scripts/generate_assistant_icon.py`: raise only the two H2 radial alpha stops.
- Modify `main/assets/images/icon_assistant.c`: regenerate the deterministic 200 x 200 RGB565 asset.

### Task 1: Add the visible-glow contract

**Files:**
- Modify: `mac-codex-agent/tests/test_assistant_icon_asset.py`

- [ ] **Step 1: Add a failing RGB565 glow assertion**

Add the following assertions to `test_has_approved_five_bar_aurora_waveform` after the radial symmetry check:

```python
for pixel in (glow_left, glow_right):
    self.assertGreaterEqual(brightness(pixel), 28)
    self.assertGreater(pixel[2], pixel[0])
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  -m unittest mac-codex-agent/tests/test_assistant_icon_asset.py
```

Expected: the glow brightness assertion fails because the current decoded RGB565 sample has brightness 16.

### Task 2: Render and verify B medium glow

**Files:**
- Modify: `scripts/generate_assistant_icon.py`
- Modify: `main/assets/images/icon_assistant.c`
- Test: `mac-codex-agent/tests/test_assistant_icon_asset.py`

- [ ] **Step 1: Raise only the radial alpha stops**

Change the glow constants to:

```python
GLOW_STOPS = (
    (0.0, (56, 220, 233, 140)),
    (0.58, (115, 86, 237, 77)),
    (1.0, (0, 0, 0, 0)),
)
```

These are the integer alpha equivalents of the approved 55% and 30% opacities.

- [ ] **Step 2: Regenerate the asset and preview**

Run:

```bash
python3 scripts/generate_assistant_icon.py \
  --output-c main/assets/images/icon_assistant.c \
  --preview /tmp/assistant-aurora-wave-visible-glow.png
```

Expected: the waveform is unchanged, the centered glow is visibly stronger, and no circular boundary appears.

- [ ] **Step 3: Run the focused test and verify GREEN**

Run the Task 1 command again.

Expected: all focused asset tests pass.

- [ ] **Step 4: Verify deterministic output and commit**

Regenerate to `/tmp/icon_assistant-visible-glow.c`, compare it byte-for-byte with the tracked asset, run `git diff --check`, and commit only the test, generator, generated asset, and this plan with message `fix: strengthen Assistant icon glow`.

### Task 3: Build, flash, and validate

**Files:**
- Verify: `build-unified/StopWatch-UserDemo.bin`

- [ ] **Step 1: Incrementally build the committed firmware**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
./scripts/build_unified_candidate.sh
```

Expected: capacity gate passes and the build version equals the new commit.

- [ ] **Step 2: Directly flash the detected `/dev/cu.usbmodem*` device**

Use `scripts/flash_unified_candidate.sh` with the existing verified backup argument required by that script. Do not read or create another backup.

Expected: all written regions pass esptool hash verification.

- [ ] **Step 3: Validate boot and host-agent state**

Run `scripts/validate_watch_boot.py` with the ESP-IDF Python environment, then confirm the StopWatch Codex Agent process remains running.

Expected: the watch reports `launcher=open`, the booted app version equals the new commit, and the Mac Agent process exists. Physical glow appearance remains the final on-watch acceptance check.
