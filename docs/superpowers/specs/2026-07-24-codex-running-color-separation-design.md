# Codex Running Color Separation Design

## Goal

Make the `RUNNING` alert heading visually distinct from the project name on the
Codex status page without changing layout, typography, assets, data, or
interaction behavior.

## Approved visual direction

- `RUNNING` uses clear cyan `#49C9EE`.
- Project names retain Beacon Violet `#7C9DFF`.
- `PENDING` retains Alert Vermilion `#FF6248`.
- `COMPLETED` retains the existing green `#3CB371`.
- Empty alert summaries retain Etched Gray `#788391`.

The cyan is intentionally separate from both Beacon Violet and Quota Mint
`#53D7A4`. This keeps the project name as the strongest identity label while
giving the running state a recognizable semantic color that is not reused by
the quota bars.

## Firmware design

Add one named color token, `kRunningCyan`, to the Codex status layout palette.
Map `CodexTaskState::Running` alert headings to that token in
`AppCodexStatus::render_alert()`.

Do not change the top connection-status label: transport statuses such as
`BLE LIVE` and `LIVE` continue to use Beacon Violet. The new cyan token applies
only to the lower alert heading when the displayed task state is Running.

## Unchanged behavior

- Alert ordering, carousel timing, state parsing, and Pending glow behavior.
- Project-name color, ASCII/non-ASCII font selection, truncation, and alignment.
- Alert positions, font sizes, divider, battery, quota rows, logo, and pet.
- Pure-black background and all Pending, Completed, empty, transport, and error
  state colors.

## Verification

- Extend the native layout palette test to assert `kRunningCyan == 0x49C9EE`.
- Extend the firmware font/UI contract to require the Running branch to use
  `kRunningCyan` and to keep project names on `kBeaconViolet`.
- Run the focused layout and firmware contract tests.
- Run the Codex alert carousel, model, and Pending-glow regression tests.
- Build the unified firmware and verify artifact identity and capacity before
  any authorized device write.
- After flashing, verify boot separately from physical screen appearance, then
  confirm on-device that `RUNNING` is cyan and the project name remains violet.

## Scope boundary

This is a color-token and state-to-color mapping change only. It does not
introduce gradients, animations, additional labels, transport changes, or
general palette refactoring.
