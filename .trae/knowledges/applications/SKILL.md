---
name: knowledge-applications
description: >
  Covers all user-facing applications in the smartwatch system: launcher, watch faces,
  stopwatch, alarm clock, badge viewer, IMU demo, FFT visualization, lucky wheel, and settings.
  Navigate when: adding new apps, modifying app lifecycle, debugging app transitions,
  implementing UI views, or handling user input across apps.
  Excludes: HAL layer (see hal/), system initialization (see main/), UI toolkit internals (see uitk/).
  Keywords: AppAbility, mooncake, app launcher, watch face, stopwatch, alarm, badge, IMU, FFT, lucky wheel, settings.
---

## Module Structure

Applications layer contains all user-facing apps built on the Mooncake framework. Each app extends `mooncake::AppAbility` and follows a standardized lifecycle pattern. The launcher serves as the entry point with an infinite-scroll icon grid.

### Directory Layout
- `main/apps/` — All application implementations
  - `app_launcher/` — Main app launcher with infinite scroll icon grid
  - `app_watch_face/` — Watch face app with multiple face variants
  - `app_stopwatch/` — Stopwatch with lap timing
  - `app_alarm_clock/` — Alarm clock with persistent storage
  - `app_badge/` — Badge/image viewer with AP mode editing
  - `app_imu/` — IMU sensor demo (accelerometer/gyroscope)
  - `app_fft/` — Audio spectrum visualization
  - `app_lucky_wheel/` — Random selection wheel game
  - `app_setup/` — Settings app with worker pattern
  - `app_template/` — Template for new app development
  - `common/` — Shared app utilities
    - `key_manager/` — Unified button input handling
    - `status_bar/` — System status bar overlay
    - `arc_top_clock/` — Arc-style clock widget
    - `audio/` — Audio playback utilities
    - `loading_page/` — Loading screen component
- `main/apps/apps.h` — Central app registry header

### Key Entry Points
- `AppLauncher::onLauncherOpen()` in `main/apps/app_launcher/app_launcher.cpp` — App launcher entry
- `AppAbility` lifecycle methods — Standard app interface (`onCreate`, `onOpen`, `onRunning`, `onClose`)
- `apps.h` — Include this to register new apps with the system

## Child Knowledge Nodes
- `./launcher/SKILL.md` — Navigate when: modifying launcher UI, app grid scrolling, quick controls
- `./watch-face/SKILL.md` — Navigate when: adding watch faces, gesture switching, time display
- `./stopwatch/SKILL.md` — Navigate when: timer logic, lap recording, stopwatch UI
- `./alarm-clock/SKILL.md` — Navigate when: alarm scheduling, trigger handling, persistent storage
- `./badge/SKILL.md` — Navigate when: image display, badge editing via AP mode
- `./imu-demo/SKILL.md` — Navigate when: IMU sensor visualization, motion handling
- `./fft-visualization/SKILL.md` — Navigate when: audio spectrum display, microphone handling
- `./lucky-wheel/SKILL.md` — Navigate when: selection UI, wheel animation
- `./setup/SKILL.md` — Navigate when: settings menus, worker-based UI patterns

## Gotchas
- All apps must use `LvglLockGuard` before any LVGL operations to prevent thread conflicts (`main/apps/app_template/app_template.cpp`)
- Apps must call `close()` to exit — never destroy app instance directly from within (`main/apps/app_template/app_template.cpp`)
- `KeyManager::update()` must be called every frame in `onRunning()` to process button inputs (`main/apps/common/key_manager/key_manager.cpp`)
- Status bar creation is delayed by 800ms on first boot to avoid visual glitch with boot logo (`main/apps/app_launcher/app_launcher.cpp`)

## Architecture
- All apps inherit from `mooncake::AppAbility` and override lifecycle callbacks (`main/apps/app_template/app_template.h`)
- Launcher extends `mooncake::templates::AppLauncherBase` for special launcher privileges (`main/apps/app_launcher/app_launcher.h`)
- Common `KeyManager` abstracts hardware buttons into semantic events: `GoHome`, `GoPrevious`, `GoNext` (`main/apps/common/key_manager/key_manager.h`)
- Shared status bar is created once by launcher and persists across app switches (`main/apps/common/status_bar/status_bar.h`)

## Patterns
- Standard app lifecycle: `onCreate()` → `onOpen()` → `onRunning()` (loop) → `onClose()` → optional `onDestroy()` (`main/apps/app_template/app_template.cpp`)
- Views are created in `onOpen()` and destroyed in `onClose()` using `std::unique_ptr` for automatic cleanup
- Button handling pattern: Check `KeyEvent::GoHome` to close app, `GoPrevious`/`GoNext` for navigation (`main/apps/app_stopwatch/app_stopwatch.cpp`)
- Worker pattern for settings: Each settings page is a `WorkerBase` subclass that runs until `isDone()` returns true (`main/apps/app_setup/workers/workers.h`)

## Conventions
- App name and icon set in constructor via `setAppInfo()` (`main/apps/app_template/app_template.cpp`)
- App header files use `#pragma once` guard (`main/apps/app_template/app_template.h`)
- All view classes live in `view::` namespace, models in `model::` (`main/apps/app_stopwatch/view/view.h`)
- UI components use `uitk::lvgl_cpp` C++ wrappers rather than raw LVGL C API (`main/apps/app_launcher/view/view.cpp`)

## Dependencies
- Mooncake framework for app lifecycle management (`<mooncake.h>`)
- LVGL for UI rendering (`<lvgl.h>`)
- Smooth UI Toolkit for C++ LVGL wrappers (`<smooth_ui_toolkit.hpp>`)
- HAL layer for hardware abstraction (`<hal/hal.h>`)
