---
name: knowledge-shared-components
description: >
  Shared UI components and utilities used across multiple apps in the system.
  Navigate when: implementing new apps, adding UI components, debugging shared component behavior,
  understanding component lifecycle, or extending base functionality.
  Excludes: app-specific implementations (see individual app nodes), HAL layer (see hal/).
  Keywords: status_bar, audio, key_manager, arc_top_clock, loading_page, shared, common, component, widget.
---

## Module Structure

Shared components provide reusable UI widgets and utilities consumed by multiple applications.
All components are located under `main/apps/common/` and follow a consistent pattern:
- Header file defines the public API
- Implementation file contains component logic
- Components are component-like modules with clear API surfaces

### Directory Layout
- `main/apps/common/` — Shared components root
  - `status_bar/` — Battery status bar with gesture control
    - `status_bar.h` — Public API for status bar creation and control
    - `status_bar.cpp` — Implementation with gesture detection and animation
  - `audio/` — Audio playback utilities
    - `audio.h` — Public API for tone and melody playback
    - `audio.cpp` — Implementation with waveform generation
    - `common.h` — Audio-specific constants
  - `key_manager/` — Hardware button input abstraction
    - `key_manager.h` — Key event types and manager class
    - `key_manager.cpp` — Button state processing and event generation
  - `arc_top_clock/` — Arc-shaped clock display
    - `arc_top_clock.h` — Clock widget class definition
    - `arc_top_clock.cpp` — Implementation with rotated digit labels
  - `loading_page/` — Full-screen loading overlay
    - `loading_page.h` — Loading page component with customizable message
  - `common.h` — Shared constants (firmware version)

### Key Entry Points
- `view::create_status_bar()` in `main/apps/common/status_bar/status_bar.h` — Create system status bar
- `audio::play_tone()` in `main/apps/common/audio/audio.h` — Play audio feedback
- `input::KeyManager::update()` in `main/apps/common/key_manager/key_manager.h` — Process button input
- `view::ArcTopClock::init()` in `main/apps/common/arc_top_clock/arc_top_clock.h` — Initialize arc clock
- `view::LoadingPage` in `main/apps/common/loading_page/loading_page.h` — Show loading overlay

## Gotchas
- Status bar auto-hides after 6 seconds (1.8s on first show) — do not rely on it being visible (`main/apps/common/status_bar/status_bar.cpp`)
- Audio playback is silently skipped if speaker volume is 0 — check volume before calling if feedback is required (`main/apps/common/audio/audio.cpp`)
- KeyManager requires `GetHAL().updateButtonStates()` to be called before `update()` unless `updateButtonStates=false` is passed (`main/apps/common/key_manager/key_manager.cpp`)
- ArcTopClock displays 'O' instead of '0' for visual styling — this is intentional, not a bug (`main/apps/common/arc_top_clock/arc_top_clock.cpp`)
- LoadingPage must be destroyed before long-blocking operations complete to avoid UI freeze artifacts (`main/apps/app_badge/app_badge.cpp`)

## Architecture
- All shared components are designed as reusable building blocks — extend via composition, not modification (`main/apps/common/`)
- Components use smooth_ui_toolkit for LVGL C++ wrappers — consistent with rest of codebase (`main/apps/common/`)
- Status bar uses singleton pattern with internal state — only one instance can exist at a time (`main/apps/common/status_bar/status_bar.cpp`)

## Patterns
- Component initialization follows two-phase pattern: constructor (lightweight) then `init()` (UI creation) (`main/apps/common/arc_top_clock/arc_top_clock.h`)
- Audio tones use MIDI note numbers for pitch specification — consistent with music theory conventions (`main/apps/common/audio/audio.h`)
- Key events use enum class for type safety — None, GoHome, GoPrevious, GoNext (`main/apps/common/key_manager/key_manager.h`)

## Conventions
- UI components live in `view::` namespace — input components in `input::` namespace (`main/apps/common/`)
- Components that need LVGL context require `LvglLockGuard` when used from non-LVGL threads (`main/apps/app_launcher/app_launcher.cpp`)
- Color parameters use uint32_t hex values (0xRRGGBB) — consistent across all components (`main/apps/common/status_bar/status_bar.h`)

## Child Knowledge Nodes
- `./status-bar/SKILL.md` — Navigate when: modifying status bar appearance, debugging gesture detection, changing auto-hide timing
- `./audio-manager/SKILL.md` — Navigate when: adding sound effects, changing audio feedback, debugging audio playback
- `./key-manager/SKILL.md` — Navigate when: handling button input, adding navigation controls, debugging key events
- `./arc-top-clock/SKILL.md` — Navigate when: customizing clock display, changing time format, debugging arc layout
