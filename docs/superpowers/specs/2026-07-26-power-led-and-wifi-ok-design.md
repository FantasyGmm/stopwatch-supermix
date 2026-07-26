# Power LED Setting and WiFi OK Design

## Scope

This change is intentionally limited to two settings surfaces:

1. Add a dedicated `Power LED` entry under the `Device` section.
2. Replace the WiFi page's compact `Done` control with the same `OK` button used by the existing Button, Brightness, Volume, Time, and Date pages.

No other settings pages or menu styles change.

## Power LED Behavior

The HAL owns three mutually exclusive M5PM1 status LED modes:

- `Off`: LED output remains low.
- `On`: LED output remains high.
- `Blink`: the LED emits a short 160 ms pulse every 2 seconds.

It loads the `pwr_led_mode` integer from the existing `system` NVS namespace during PMIC initialization and applies it with `M5PM1::setLedEnLevel`. Unknown stored values fall back to `Off`, which is also the default for devices without a saved value. Blink scheduling runs from the existing firmware main loop and performs I2C writes only when the output changes.

The `Power LED` page contains three Button-style option rows labeled `Off`, `On`, and `Blink 2s`, with one selected indicator at a time, plus one existing-style green `OK` button. Selecting a row previews the mode immediately. Pressing `OK` persists the value and returns to the settings menu. If the page is destroyed without confirmation, the worker restores the mode that was active when the page opened.

Download-mode indication is unaffected because application-level LED configuration runs only after the firmware and PMIC HAL initialize.

## WiFi Page

The existing WiFi title, state details, saved-network list, connection selection, and pairing action remain unchanged. The bottom `Done` button is renamed to `OK` and receives the same geometry and visual properties as the Button page confirmation button:

- size `374 x 130`;
- centered at vertical offset `175`;
- radius `77`;
- background `0x4AD78C`;
- Montserrat 28 label in `0x0F5831`;
- no border or shadow.

The pairing action moves upward only as much as required to avoid overlapping the unified `OK` button.

## Validation

Firmware contract tests cover the NVS key, mode validation, HAL apply/persist methods, blink timing constants and main-loop update, menu registration, Power LED worker behavior, and exact WiFi `OK` style. Existing WiFi controller/model tests continue to cover runtime behavior. A complete ESP-IDF unified build verifies the hardware-facing integration.
