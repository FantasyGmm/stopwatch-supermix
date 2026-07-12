# Remove Lucky Wheel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the Lucky Wheel application and all project-specific assets and references while preserving unrelated working-tree changes.

**Architecture:** Remove the application at its registration boundary, then physically delete its isolated implementation and image assets. Finish by updating documentation and proving that compilation and linking no longer require any Lucky Wheel symbol.

**Tech Stack:** C++17, ESP-IDF 5.5.x, LVGL 9, CMake, Git, ripgrep

---

## File Map

- Modify `main/main.cpp`: remove runtime installation.
- Modify `main/apps/apps.h`: remove the application include.
- Modify `main/assets/assets.h`: remove image declarations.
- Delete `main/apps/app_lucky_wheel/`: remove the application and views.
- Delete `main/assets/images/icon_lucky_wheel.c` and `main/assets/images/lucky_wheel_pointer.c`: remove firmware images.
- Delete `launcher_icon_exports/luckywheel_icon_lucky_wheel.png`: remove the exported design asset.
- Modify `README.md`: remove feature and launcher-order references.

### Task 1: Remove Registration And Declarations

**Files:**
- Modify: `main/main.cpp`
- Modify: `main/apps/apps.h`
- Modify: `main/assets/assets.h`

- [ ] **Step 1: Capture current references**

```bash
rg -n "AppLuckyWheel|icon_lucky_wheel|lucky_wheel_pointer" main/main.cpp main/apps/apps.h main/assets/assets.h
```

Expected: one installation, one include, and two image declarations.

- [ ] **Step 2: Remove these exact boundary references**

```cpp
GetMooncake().installApp(std::make_unique<AppLuckyWheel>());
#include "app_lucky_wheel/app_lucky_wheel.h"
LV_IMG_DECLARE(icon_lucky_wheel);
LV_IMG_DECLARE(lucky_wheel_pointer);
```

- [ ] **Step 3: Verify boundary removal**

Run the Step 1 command again. Expected: exit status 1 and no output.

- [ ] **Step 4: Commit**

```bash
git add main/main.cpp main/apps/apps.h main/assets/assets.h
git commit -m "Remove Lucky Wheel registration"
```

### Task 2: Delete Implementation And Assets

**Files:**
- Delete: `main/apps/app_lucky_wheel/app_lucky_wheel.cpp`
- Delete: `main/apps/app_lucky_wheel/app_lucky_wheel.h`
- Delete: `main/apps/app_lucky_wheel/view/selection.cpp`
- Delete: `main/apps/app_lucky_wheel/view/wheel.cpp`
- Delete: `main/apps/app_lucky_wheel/view/view.h`
- Delete: `main/assets/images/icon_lucky_wheel.c`
- Delete: `main/assets/images/lucky_wheel_pointer.c`
- Delete: `launcher_icon_exports/luckywheel_icon_lucky_wheel.png`

- [ ] **Step 1: Record the existing image size**

```bash
stat -f "%z" build/StopWatch-UserDemo.bin
```

Expected: record the byte count if the existing compatible artifact is present. If absent, note that no baseline is available; do not rebuild before deletion because unrelated changes already exist.

- [ ] **Step 2: Delete only the listed files and empty application directories**

Do not delete any other file under `launcher_icon_exports`.

- [ ] **Step 3: Verify paths are absent**

```bash
test ! -e main/apps/app_lucky_wheel
test ! -e main/assets/images/icon_lucky_wheel.c
test ! -e main/assets/images/lucky_wheel_pointer.c
test ! -e launcher_icon_exports/luckywheel_icon_lucky_wheel.png
```

Expected: every command exits successfully.

- [ ] **Step 4: Commit tracked deletions**

```bash
git add -u main/apps/app_lucky_wheel main/assets/images/icon_lucky_wheel.c main/assets/images/lucky_wheel_pointer.c
git commit -m "Delete Lucky Wheel implementation"
```

The exported PNG was untracked and does not appear in the commit.

### Task 3: Clean Documentation And Verify Firmware

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README content**

Change:

```markdown
- full-screen utility apps for clock, stopwatch, alarm, badge, IMU, FFT, lucky wheel, and setup;
```

to:

```markdown
- full-screen utility apps for clock, stopwatch, alarm, badge, IMU, FFT, and setup;
```

Remove the `Lucky wheel` launcher entry and renumber the following `Settings` entry so the list remains consecutive.

- [ ] **Step 2: Search for residual references**

```bash
rg -n -i "lucky[ _-]?wheel|AppLuckyWheel|icon_lucky|lucky_wheel_pointer" . -g '!build/**' -g '!.git/**' -g '!docs/superpowers/**'
```

Expected: exit status 1 and no output.

- [ ] **Step 3: Validate the diff**

```bash
git diff --check
```

Expected: exit status 0 and no output.

- [ ] **Step 4: Build firmware**

```bash
idf.py build
```

Expected: compilation and linking succeed, producing `build/StopWatch-UserDemo.bin` without unresolved Lucky Wheel symbols.

- [ ] **Step 5: Record the new image size**

```bash
stat -f "%z" build/StopWatch-UserDemo.bin
```

Expected: record the byte count and reduction when a compatible baseline exists; otherwise report only the new size.

- [ ] **Step 6: Commit documentation**

```bash
git add README.md
git commit -m "Document Lucky Wheel removal"
```

- [ ] **Step 7: Review final scope**

```bash
git status --short
git log -4 --oneline
```

Expected: three Lucky Wheel implementation commits follow the design and plan commits; pre-existing unrelated changes remain unstaged.
