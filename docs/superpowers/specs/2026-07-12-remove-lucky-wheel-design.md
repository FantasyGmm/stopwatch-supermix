# Remove Lucky Wheel Design

## Goal

Remove the Lucky Wheel application and every Lucky Wheel-specific asset, registration, declaration, export, and user-facing reference from the StopWatch project. Git history remains the recovery mechanism; no archived copy remains in the working tree.

## Scope

Delete the complete `main/apps/app_lucky_wheel/` application directory, including its selection and spinning-wheel views. Delete the firmware image sources `main/assets/images/icon_lucky_wheel.c` and `main/assets/images/lucky_wheel_pointer.c`. Delete the untracked exported design asset `launcher_icon_exports/luckywheel_icon_lucky_wheel.png`.

Remove `AppLuckyWheel` installation from `main/main.cpp`, its include from `main/apps/apps.h`, and both LVGL image declarations from `main/assets/assets.h`. Remove Lucky Wheel from the README feature list and launcher order, then renumber the remaining launcher entries.

No other application, shared component, generated build output, or unrelated working-tree change is part of this removal.

## Build Behavior

The firmware component uses recursive source globs for application and asset sources. Physically deleting the Lucky Wheel sources and assets removes them from compilation without adding exclusion rules or maintaining a separate source list.

## Verification

After deletion:

1. Search tracked and untracked project files for `Lucky Wheel`, `LuckyWheel`, `lucky_wheel`, `luckywheel`, `icon_lucky`, and the relevant wheel symbols. No functional or documentation reference may remain.
2. Run the existing host-side tests that are relevant to firmware registration or asset contracts.
3. Rebuild the firmware with the existing project build command.
4. Confirm the linker succeeds without Lucky Wheel symbols.
5. Compare the resulting application image size with the pre-removal image when a compatible baseline is available.

## Acceptance Criteria

- Lucky Wheel no longer appears in the launcher.
- No Lucky Wheel source, compiled image asset, exported icon, declaration, registration, or README entry remains.
- The firmware builds successfully.
- Existing unrelated working-tree changes remain intact.
