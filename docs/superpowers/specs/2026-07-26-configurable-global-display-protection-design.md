# Configurable Global Display Protection Design

## Goal

Make display burn-in protection a single global policy shared by Launcher and every application. The watch must wake when external power changes, remain awake on external power by default, and expose configurable dim and screen-off timeouts in Settings.

## Confirmed Product Behavior

### Inactivity timing

Both thresholds are measured from the most recent user activity or policy reset.

- Dim timeout choices: 10 seconds, 15 seconds, 30 seconds, 1 minute, 2 minutes, and 5 minutes.
- Screen-off timeout choices: 30 seconds, 1 minute, 2 minutes, 5 minutes, and 10 minutes.
- Defaults: dim after 15 seconds and screen off after 1 minute.
- The screen-off timeout is an absolute inactivity threshold. It is not an additional delay after dimming.
- The screen-off timeout must always be greater than the dim timeout.

When the user raises the dim timeout to or beyond the current screen-off timeout, the screen-off timeout advances to the next valid choice. When the user lowers the screen-off timeout to or below the current dim timeout, the dim timeout moves to the previous valid choice.

### External power

- Automatic dimming and screen-off are disabled on external power by default for Launcher and every application, including Codex.
- Settings exposes `Charging Protection`, defaulting to off. When enabled, external power uses the same configured dim and screen-off thresholds as battery operation.
- Every confirmed insertion or removal of external power immediately restores the current user brightness and restarts inactivity timing.
- Insertion wakes the display even when charging protection is enabled and even when Codex was manually turned off.
- Removing external power starts a fresh battery inactivity period. It does not reuse idle time accumulated while connected.
- Disabling charging protection affects only automatic protection. A user can still manually turn off Codex while external power remains connected.

### Existing keep-awake behavior

Assistant semantic interaction, a running Stopwatch, and a ringing Alarm retain their existing keep-awake behavior. A static animation by itself does not count as activity.

Codex keeps its long-left-button manual screen-off gesture, but the gesture delegates to the global service rather than maintaining an independent power policy.

## Architecture

### Single policy owner

`GlobalDisplayProtectionService` becomes the only owner of:

- persisted display-protection configuration;
- effective automatic-protection state;
- external-power state and transitions;
- inactivity timing;
- dim, restore, and screen-off transitions;
- user-input wake handling;
- explicit manual screen-off requests; and
- application keep-awake reasons.

Codex must stop polling PMIC state and stop calling `setAutomaticProtection()`. Its manual-off gesture calls a global `screenOffNow()` operation. Pending-glow visibility derives from the global display state, so brightness and Codex presentation cannot disagree.

The public API that allows an application to override automatic protection should be removed or made private. Applications may only publish a keep-awake reason or request an explicit manual screen-off.

### Configuration model

A pure `DisplayProtectionConfig` model owns:

- `dim_choice`;
- `screen_off_choice`; and
- `protect_while_charging`.

It maps choice indices to milliseconds, sanitizes persisted values, and enforces the ordering constraint. The logic state machine receives already-sanitized timeout values and does not know about UI indices or NVS.

The NVS namespace and keys must remain within ESP-IDF's 15-character limit. Suggested names are:

- namespace: `disp_guard`;
- dim choice: `dim_idx`;
- screen-off choice: `off_idx`; and
- charging policy: `chg_guard`.

Missing values use the defaults. Invalid values snap to a legal choice. If an interrupted write leaves a mismatched pair, sanitization produces a valid ordering at the next load.

### External-power sampling

The existing boolean charging API conflates an I2C failure with confirmed battery operation. Display protection therefore uses a cached three-state result:

- `Unknown`;
- `Battery`; and
- `ExternalPower`.

The PMIC background task samples VIN every 250 milliseconds and publishes a thread-safe cached state. Battery-voltage sampling may remain at its existing one-second cadence.

- A valid external-power sample confirms insertion immediately, giving a worst-case wake latency of about 250 milliseconds.
- Three consecutive valid battery samples confirm removal, filtering connector bounce.
- An invalid read retains the previous valid state and does not generate a power transition.
- If no valid sample has ever been obtained after boot, the display service uses battery policy as the conservative fallback.
- External power is determined from VIN, not active-charge status, so a fully charged watch is still treated as connected.

The global service reads only the cache; it does not perform PMIC I2C operations from the UI loop.

### Policy calculation

Automatic protection is effective when:

```text
initialized
and not held awake
and (power is not external or protect_while_charging is true)
```

An unknown power state follows battery policy. Keep-awake reasons prevent automatic dimming and screen-off but do not delete timer configuration.

Every power transition and every saved configuration update restores normal brightness and resets inactivity timing before recalculating the effective policy.

### Event priority

Events are handled in this order:

1. Confirmed external-power insertion or removal restores the display and resets timing.
2. A newly activated wake-producing application event, including Alarm or Assistant activity, restores the display.
3. Button or touch activity restores the display. The first input from screen-off is consumed.
4. Codex manual screen-off honors explicit user intent, including while connected to external power.
5. Active keep-awake reasons suppress subsequent automatic transitions.
6. Normal dim and screen-off thresholds apply when no higher-priority rule prevents them.

Adding any new keep-awake reason while the display is dimmed or off restores it, even if another keep-awake reason was already present. This ensures an Alarm can wake a manually dark display while Stopwatch is already running.

If power and input transitions occur in the same main-loop iteration, the display is restored once and any input that began while the screen was off remains consumed. A power transition must never synthesize an application input event.

### Brightness ownership

Protection uses temporary brightness only. It never overwrites the user's saved brightness.

- Restore always reads the latest user brightness instead of a stale value captured before dimming.
- A brightness gesture while dimmed first counts as user activity, then applies the requested user brightness.
- A brightness or volume gesture that begins while screen-off is consumed as a wake gesture.
- Settings continues disabling global edge controls while a settings worker is open, preventing display-protection sliders from changing brightness or volume.

## Settings Experience

Add `Display Protection` immediately after `Brightness` in Settings → Device.

The screen uses the approved single-page layout and the existing settings-subpage visual language:

- 466 by 466 black panel;
- existing typography and spacing;
- an existing-style switch row labeled `Charging Protection`;
- `Dim After: <value>` with a discrete slider;
- `Screen Off After: <value>` with a discrete slider;
- gray tracks, green indicators, centered white knobs; and
- the existing green `OK` button.

Sliders operate on choice indices and cannot stop between choices. Labels render concise values such as `15s`, `1m`, and `10m`. Linked adjustment updates both sliders and labels immediately.

The worker owns an in-memory draft. It does not preview runtime policy changes. `OK` writes the three settings and applies the sanitized configuration. Leaving Settings without `OK` discards the draft and changes neither NVS nor the running policy.

Saving always restores the display and restarts inactivity timing. If the watch is connected:

- saving with charging protection off leaves automatic protection disabled;
- saving with charging protection on starts a fresh protected interval.

The Settings screen itself remains subject to the currently active global protection policy. Touching a switch or slider is normal user activity and refreshes the current inactivity timer.

## Diagnostics

Extend `debug display protection` with:

- display state;
- effective and user brightness;
- sanitized dim and screen-off choices;
- charging-protection setting;
- cached power state and whether it is valid;
- effective automatic-protection state;
- current inactivity duration;
- time remaining until dim and screen-off;
- active keep-awake reasons; and
- the latest wake/reset reason.

Log only state transitions, accepted power transitions, configuration changes, and rate-limited PMIC-read health changes. The 250-millisecond sampler must not produce routine per-sample logs.

## Failure Handling

- Missing NVS values use defaults.
- Invalid NVS values are sanitized before use.
- Partial configuration writes cannot create an invalid runtime pair.
- A single PMIC read failure cannot simulate unplugging.
- Repeated invalid PMIC reads retain the last known policy and emit rate-limited diagnostics.
- An unavailable PMIC from boot uses conservative battery protection.
- Applying a shorter timeout never immediately darkens the current screen because configuration application restores and resets timing.
- Millisecond arithmetic remains wrap-safe across `uint32_t` rollover.

## Verification Strategy

### Test-driven implementation

Each behavior starts with a focused failing test before production changes.

Pure host tests cover:

- every timeout choice and label;
- configuration sanitization;
- bidirectional linked-slider adjustment;
- configurable absolute inactivity thresholds;
- `uint32_t` rollover;
- explicit manual screen-off;
- power transition restore and timer reset;
- unknown and invalid power samples;
- insertion acceptance and removal debounce;
- multiple simultaneous keep-awake reasons; and
- adding Alarm while Stopwatch is already active and the screen is manually off.

Firmware contract tests cover:

- Settings menu placement;
- switch, two discrete sliders, linked updates, and `OK` behavior;
- draft cancellation without NVS or runtime mutation;
- short valid NVS keys;
- cached PMIC power-state publishing;
- global service ownership;
- removal of Codex PMIC polling and automatic-policy overrides;
- Codex manual-off delegation;
- Pending glow following global display state; and
- preservation of Assistant, Stopwatch, and Alarm keep-awake behavior.

Integration validation runs the full Python suite, every native host stage, an ESP-IDF firmware build, and firmware-capacity gates.

### Hardware verification

On-device verification covers:

1. Battery operation dims and turns off at the selected absolute thresholds.
2. Screen-off insertion wakes within the sampling bound and stays awake by default.
3. Removal restores brightness and starts a fresh battery timer.
4. Charging protection causes dim and screen-off while connected.
5. Codex manual-off works while connected; a later insertion transition wakes it.
6. The first input from screen-off wakes without navigating or changing brightness or volume.
7. Launcher, Settings, Codex, Assistant, Clock, and other applications share the same automatic policy.
8. Assistant interaction, Stopwatch timing, and Alarm ringing retain their keep-awake rules.
9. Alarm wakes the display when Stopwatch already holds a keep-awake reason.
10. A new user brightness is restored after later dim, screen-off, and power transitions.
11. Repeated connector bounce and injected PMIC read failures do not flap the policy.
12. Saved configuration survives restart; canceled edits do not.

Flash/hash/boot validation remains separate from visual and behavioral confirmation on the physical watch.

## Scope Boundaries

This change does not add ambient-display modes, per-application timeout settings, separate battery and charging timeout values, a user-selectable dim brightness, or background OTA behavior. The single global policy is intentional.
