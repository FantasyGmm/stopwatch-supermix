---
name: knowledge-framework-integration
description: >
  Covers framework integration: Mooncake app ability system, smooth_ui_toolkit, LVGL,
  ESP-IDF build system, HAL abstraction, and app lifecycle management.
  Navigate when: creating new apps, modifying framework integration, debugging initialization
  issues, understanding app lifecycle, or working with UI rendering.
  Excludes: individual app implementations (see apps/), hardware drivers (see hal/drivers/).
  Keywords: mooncake, AppAbility, smooth_ui_toolkit, LVGL, ESP-IDF, app_main, HAL,
  installApp, onCreate, onOpen, onRunning, onClose, LvglLockGuard.
---

## Module Structure

Framework integration layer connects ESP-IDF, Mooncake app framework, smooth_ui_toolkit,
and LVGL to provide a unified application runtime for the smartwatch.

### Directory Layout
- `main/main.cpp` — Application entry point, app registration, main loop
- `main/apps/apps.h` — Central app header aggregating all app includes
- `main/hal/` — Hardware Abstraction Layer
  - `hal.h` — HAL interface with LVGL integration, BootLogo class
  - `hal.cpp` — HAL initialization sequence
  - `hal_display.cpp` — Display, touch, LVGL initialization with dual-buffer rendering
- `CMakeLists.txt` — Project-level CMake configuration
- `sdkconfig.defaults` — ESP-IDF default configuration (16MB flash, PSRAM, LVGL settings)
- `main/CMakeLists.txt` — Component registration with embedded assets

### Key Entry Points
- `app_main()` in `main/main.cpp` — ESP-IDF entry point, initializes HAL and apps
- `Hal::init()` in `main/hal/hal.cpp` — Hardware initialization sequence
- `GetMooncake().installApp()` — App registration API
- `GetMooncake().update()` — Main loop app lifecycle update

## Branching Table

| Dimension | Development | Production |
|-----------|-------------|------------|
| App registration | All apps installed in main.cpp | Same (no conditional compilation) |
| Guide page | Shown first 5 launches via settings | Same behavior |
| Boot sound | Played on first open | Same |
| Logging | mclog::level_info | Can be adjusted at runtime |

## Affected Scope
- `main/apps/*/` — All app implementations depend on framework lifecycle
- `main/hal/` — HAL provides services to all apps
- `main/apps/common/status_bar/` — Status bar managed by launcher lifecycle
- `main/apps/common/key_manager/` — Key input abstraction

## Gotchas
- LVGL operations MUST be wrapped with `LvglLockGuard` or manual `lvglLock()/lvglUnlock()` calls to prevent race conditions with the LVGL RTOS task (`main/hal/hal_display.cpp`)
- LVGL buffers are allocated in SPIRAM (PSRAM) using `heap_caps_malloc` with `MALLOC_CAP_SPIRAM` flag — insufficient PSRAM will cause silent allocation failures (`main/hal/hal_display.cpp`)
- `lvgl_flush_cb` uses chunked pixel transmission (8K chunks) to avoid M5GFX SIMD optimization issues with large transfers (`main/hal/hal_display.cpp`)
- Apps must call `close()` to exit — the framework does not auto-close apps on button press unless explicitly handled (`main/apps/app_template/app_template.cpp`)
- `AppLauncher` is a special launcher app that extends `AppLauncherBase` and manages other apps' lifecycle — regular apps extend `AppAbility` (`main/apps/app_launcher/app_launcher.h`)
- Status bar creation is delayed by 800ms on first boot to avoid visual glitches during guide page display (`main/apps/app_launcher/app_launcher.cpp`)
- Guide page shows only for first 5 launches tracked via NVS settings — resetting NVS will reset the counter (`main/hal/hal.cpp`)

## Architecture
- Three-layer architecture: ESP-IDF → HAL → Mooncake/App Layer
- Mooncake framework provides app ability system with lifecycle callbacks: onCreate, onOpen, onRunning, onClose
- smooth_ui_toolkit provides C++ wrapper classes for LVGL (uitk::lvgl_cpp namespace)
- LVGL runs in dedicated FreeRTOS task with 10ms tick timer and mutex-protected rendering
- Dual-buffer LVGL rendering with partial mode for memory efficiency
- HAL singleton pattern provides unified hardware access via `GetHAL()`

## Decisions
- Chosen ESP-IDF 5.5.4 with FreeRTOS for real-time capabilities and hardware abstraction
- Selected LVGL 9.x with custom C++ wrappers for UI rendering with hardware acceleration support
- Used Mooncake framework for app lifecycle management inspired by smartphone app models
- Allocated 8KB main task stack and 4096*4 bytes for LVGL task to handle deep call stacks
- Configured 1000Hz FreeRTOS tick rate for precise timing

## Patterns
- All apps follow `App<Name>` naming convention and extend either `AppAbility` or `AppLauncherBase`
- App lifecycle: constructor → onCreate → onOpen → [onRunning loop] → onClose
- UI creation happens in onOpen with LvglLockGuard, cleanup in onClose
- Key handling uses KeyManager abstraction for consistent input processing
- Settings persistence uses NVS with namespace "system"

## Conventions
- App headers included via central `main/apps/apps.h` aggregation header
- Firmware version defined in `main/apps/common/common.h` as `FirmwareVersion`
- All apps must set `setAppInfo().name` in constructor for logging identification
- LVGL font and image assets use LVGL's C array format in `main/assets/`

## Dependencies
- ESP-IDF 5.5.4 with ESP32-S3 target
- LVGL 9.x with custom configuration (1MB cache, multiple font sizes)
- smooth_ui_toolkit for C++ LVGL wrappers
- Mooncake framework for app ability system
- M5GFX for display driver abstraction
- FreeRTOS for task management

## Build System
- ESP-IDF component-based build with `idf_component_register`
- Custom partition table enabled (`CONFIG_PARTITION_TABLE_CUSTOM`)
- 16MB flash configuration with custom partition layout
- Embedded binary files (boot_sfx.bin) and text files (HTML) via CMake
- PSRAM enabled in octal mode at 80MHz for LVGL buffers

## Performance Characteristics
- LVGL task runs at priority 1 with 10ms period (100Hz update rate)
- Display SPI runs at 80MHz write frequency
- Touchpad I2C runs at 100kHz
- Main loop calls `feedTheDog()` (1ms delay) and `GetMooncake().update()` continuously
- Framebuffer uses partial rendering mode with 120-line buffers in PSRAM
