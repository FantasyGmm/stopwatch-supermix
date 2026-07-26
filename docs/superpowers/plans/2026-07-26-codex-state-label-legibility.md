# Codex State Label Legibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the `PENDING` and `RUNNING` state groups easier to read on the physical watch with true 16 px bold labels and centered counts, while keeping the groups slightly away from the middle.

**Architecture:** Keep the existing Scheme F hierarchy, carousel semantics, colors, divider, pulse animation, and lower task content unchanged. Encode symmetric label and count centers in `codex_status_ui_layout.h`, render both state words with a minimal Montserrat Bold 16 subset, and protect the geometry and font choice with native and Python contract tests.

**Tech Stack:** C++17, LVGL, Python `unittest`, native `assert` tests.

---

### Task 1: Enlarge and Inset the State Groups

**Files:**
- Create: `main/assets/fonts/MontserratBold16.c`
- Modify: `main/assets/assets.h`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `tests/codex_status_ui_layout_test.cpp`
- Modify: `mac-codex-agent/tests/test_codex_status_f_layout_contract.py`

- [x] **Step 1: Write failing layout and font contracts**

Change the expected state-group coordinates to `62`, `308`, `78`, and `324`; require matching label/count center lines, centered text alignment, and `MontserratBold16` for both state words.

- [x] **Step 2: Run the focused tests and verify they fail**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_codex_status_f_layout_contract.py
source "$HOME/esp/esp-idf-v5.5.4/export.sh" >/dev/null 2>&1
g++ -std=c++17 -I main tests/codex_status_ui_layout_test.cpp -o /tmp/codex_status_ui_layout_test && /tmp/codex_status_ui_layout_test
```

Expected: both suites fail against the old edge-aligned 10 px layout.

- [x] **Step 3: Implement the approved B geometry and typography**

Set:

```cpp
constexpr int kPendingLabelX = 62;
constexpr int kRunningLabelX = 308;
constexpr int kPendingCountX = 78;
constexpr int kRunningCountX = 324;
```

Use `&MontserratBold16` and centered alignment for both state-word labels. Center each count inside its 64 px box directly under the corresponding 96 px label. Leave all vertical coordinates, count fonts, colors, and animation targets unchanged.

- [x] **Step 4: Run focused and regression verification**

Run the Python contract, native layout test, Pending glow native test, carousel native test, and `git diff --check`. Expected: every command exits 0.

- [x] **Step 5: Build the exact unified firmware**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
./scripts/build_unified_candidate.sh
```

Expected: the ESP32-S3 candidate builds and the capacity gate passes.
