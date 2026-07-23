# Codex Status Density Refinement Design

## Goal

Refine the approved **A · Beacon Spine** Codex status page so its lower-half typography feels less oversized and uses the space below the alert divider more evenly.

This is a typography and vertical-rhythm adjustment only. It does not change the page's information, ordering, colors, assets, state behavior, or interactions.

## Approved Direction

The user selected **A · Balanced Descent**.

- Keep the alert divider fixed at its current position.
- Reduce the visual size of the quota percentages.
- Reduce the visual size of alert-state text such as `PENDING`, `HANDLED`, `RUNNING`, and `STOP`.
- Reduce the visual size of ASCII project names such as `StopWatch`.
- Move the alert text group downward without moving the divider or battery.
- Preserve the current pure-black background, Codex logo, pet animation, Beacon Spine composition, telemetry, quota tracks, colors, and status semantics.

## Exact Layout

Coordinates use the 466 × 466 screen's top-left origin.

### Unchanged geometry

- Alert divider: `x=82, y=304, w=302`.
- Battery: centered at `y=438`.
- Quota rows: `y=220 / 260`.
- Quota percentage column: `x=351, w=60`, right aligned.
- All geometry above the divider remains unchanged.

### Refined alert geometry

- Alert heading: centered at `y=328`, width `310`.
- Project name: centered at `y=357`, width `340`.
- Alert detail: centered at `y=389`, width `320`.

Compared with the current positions, the heading moves down 13 px, the project moves down 11 px, and the detail moves down 7 px. The offsets are deliberately not uniform: they produce a visually even stack after the primary fonts become smaller while preserving comfortable clearance above the battery.

## Typography

The browser mockup uses a 21 px alert heading and 22 px project/percentage text. The firmware maps this to the closest already-enabled LVGL fonts:

- Alert heading: `lv_font_montserrat_20`.
- ASCII project name: `lv_font_montserrat_22`.
- Quota percentage: `lv_font_montserrat_22`.
- Non-ASCII project name: retain `font_puhui_14_1` for glyph coverage.
- Alert detail: retain `font_puhui_14_1` at 14 px.

The status heading remains uppercase and state-colored, so reducing it from the current 26 px font does not weaken recognition. The project remains one visual step stronger than the detail. Percentages remain right-aligned and readable without competing with model, effort, or task status.

No new font asset is introduced.

## Vertical Rhythm

The fixed divider continues to separate quota data from task information. The larger gap below it is intentional: it prevents the divider from visually attaching to the alert heading.

The refined stack reads in this order:

1. Divider at `y=304`.
2. Alert state at `y=328`.
3. Project at `y=357`.
4. Detail at `y=389`.
5. Battery at `y=438`.

This uses the lower arc more evenly while preserving enough space between the detail and battery for a clean circular-screen edge.

## Behavior Preservation

The refinement must not change:

- BLE/Wi-Fi status handling or transport fallback.
- Codex model, effort, quota, or alert data mapping.
- Alert carousel ordering and timing.
- Pending glow behavior or state-dependent colors.
- Pet frames, animation durations, position, scale, or antialiasing.
- Codex logo asset or brand geometry.
- Edge controls, screen-off behavior, or battery updates.
- Text truncation, alignment widths, or ASCII/non-ASCII project-font selection.

## Verification

- Update the native layout contract to require `kAlertHeadingY=328`, `kAlertProjectY=357`, and `kAlertDetailY=389`.
- Keep assertions requiring `kAlertDividerY=304` and `kBatteryY=438`.
- Update the firmware font contract to require `lv_font_montserrat_20` for the alert heading and `lv_font_montserrat_22` for ASCII project names and quota percentages.
- Verify that percentage labels remain at `x=351, w=60` with right alignment.
- Run the focused layout, font, Codex model, alert carousel, and Pending-glow tests.
- Run the full Mac Agent test suite.
- Build the unified ESP-IDF firmware and pass the capacity gate.
- Compare the physical watch against the approved A mockup after flashing; build success alone does not prove visual fidelity.
