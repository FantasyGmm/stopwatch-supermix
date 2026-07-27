# Display Protection Off Options and Layout Fix Design

## Goal

Fix the round-screen clipping on the Display Protection Settings subpage and let users independently disable automatic dimming and automatic screen-off from the two existing sliders.

## Scope

This change modifies only the Display Protection Settings layout, its draft/config persistence model, the global display-protection policy needed for independently disabled stages, diagnostics, and their tests. Existing manual screen-off, wake-input suppression, power insertion wake, keep-awake reasons, brightness restoration, and application ownership remain unchanged.

## Layout

Use the approved two-line charging layout:

1. Center `Charging Protection` on the first line inside the round display's safe area.
2. Center the charging switch on a separate second line.
3. Move the two label/slider groups downward and reduce unused vertical gaps.
4. Reduce and reposition the OK button so its complete shape remains inside the lower round-screen safe area.
5. Preserve the existing typography, black background, dark tracks, green indicators, centered white knobs, switch styling, and green OK button.
6. Keep the page fixed and non-scrollable.

The two value labels remain:

- `Dim After: <value>`
- `Screen Off After: <value>`

At the leftmost slider position, `<value>` is `Off`.

## Configuration Model and Compatibility

Add two explicit booleans to `DisplayProtectionConfig`:

- `dim_enabled`, default `true`;
- `screen_off_enabled`, default `true`.

Keep the existing `dim_choice` and `screen_off_choice` meanings and indices unchanged. The UI maps slider position zero to the disabled state and maps each later position to the existing choice index plus one.

Persist the booleans as new fields in the existing `disp_guard` settings namespace. When upgrading from firmware that has no enable fields, missing values default to `true`. Existing saved choice indices therefore retain their original time meaning. The default remains dim after 15 seconds and screen off after 60 seconds.

Invalid persisted indices are clamped as before. If both stages are enabled and the persisted time ordering is invalid, sanitization restores a strictly later screen-off time. Disabled stages retain a valid remembered time choice so re-enabling them restores a real time value.

## Slider Behavior

Each slider has an independent `Off` position:

- Dim slider: `Off`, `10s`, `15s`, `30s`, `1m`, `2m`, `5m`.
- Screen-off slider: `Off`, `30s`, `1m`, `2m`, `5m`, `10m`.

Moving either slider to `Off` does not change the other slider.

When both stages are enabled, screen-off must remain strictly later than dim:

- moving dim to a time equal to or later than screen-off advances screen-off to the first later choice;
- moving screen-off to a time equal to or earlier than dim moves dim to the last earlier choice.

If the other stage is disabled, no time-order adjustment is needed. Re-enabling a stage reapplies the ordering rule using both remembered choices.

Edits remain draft-only. Leaving the page without OK changes neither runtime behavior nor NVS. OK applies the complete sanitized config once and persists it.

## Runtime Policy

The inactivity policy evaluates the two stages independently:

1. If screen-off is enabled and its timeout expires, enter `ScreenOff`.
2. Otherwise, if dim is enabled and its timeout expires, enter `Dimmed`.
3. If a stage is disabled, it never triggers.

This produces four supported combinations:

| Dim | Screen-off | Result |
| --- | --- | --- |
| On | On | Dim, then screen off |
| Off | On | Stay at user brightness, then screen off directly |
| On | Off | Dim and remain dim until activity or another wake event |
| Off | Off | No automatic display transition |

The global power rule remains unchanged: automatic policy is allowed on battery and, by default, suppressed on external power. Enabling Charging Protection allows the selected stages while external power is present. If both stages are off, Charging Protection has no automatic action to perform.

Manual screen-off remains available even when both automatic stages are disabled. Touch wake consumption, power insertion wake, keep-awake reasons, and brightness restoration retain their existing semantics.

## Diagnostics

Extend display-protection diagnostics with the two enable states while retaining the chosen time indices and resolved timeout values. This makes direct screen-off, dim-only, and fully disabled configurations distinguishable in hardware logs.

## Error Handling

- Missing new NVS fields use compatibility defaults of `true`.
- Invalid booleans use the settings API's boolean fallback.
- Invalid indices are clamped.
- Invalid ordering is repaired only when both stages are enabled.
- A configuration with both stages disabled is valid and does not fall back to timed protection.

## Verification

Use test-driven development. Each behavior change starts with a failing test and then receives the minimum implementation required to pass.

Automated coverage must include:

1. defaults and migration from settings without enable fields;
2. config sanitization for all four enable combinations;
3. slider-position mapping and labels, including both `Off` positions;
4. linked ordering while both stages are enabled;
5. no unwanted linked changes while either stage is disabled;
6. direct screen-off with dim disabled;
7. persistent dim with screen-off disabled;
8. no automatic transition when both stages are disabled;
9. unchanged manual screen-off and wake behavior;
10. the approved two-line charging geometry and round-screen-safe OK button;
11. draft-only edits and single OK persistence;
12. diagnostics containing both enable states;
13. the full unified offline-validation gate and clean firmware build.

Hardware validation must reapply volume zero, use a checksum-valid full 16 MiB backup before flashing, validate boot, and then visually confirm the revised layout and all four automatic-stage combinations. Flash/boot evidence remains distinct from physical display observations.
