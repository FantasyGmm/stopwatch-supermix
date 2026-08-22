# Codex Custom Pet Design

Date: 2026-08-22

## Goal

Let the user select one animated GIF in the macOS StopWatch Codex Agent,
preview the exact watch result, and publish it as the looping pet on the Codex
status page. The custom pet persists across watch reboots and ordinary firmware
updates. A failed, interrupted, stale, or unauthorized upload never replaces
the currently active pet.

## Locked Product Decisions

- The Agent menu gains a `Codex Pet...` action that opens a dedicated compact
  AppKit window. The upload controls do not expand the existing menu.
- There is one custom pet and one continuously looping animation. Version one
  does not define separate idle and working GIFs.
- The Codex logo, channel status row, bottom three-page indicator, and battery
  label retain their current positions.
- The pet display box becomes `144 x 156` pixels. The middle and lower content
  move down while the full task summary remains available.
- The built-in robot uses the same enlarged geometry when no valid custom pet
  is installed.
- The Agent converts GIF input into a bounded watch-native animation package.
  The watch does not enable or expose a general-purpose GIF decoder.
- `Restore Default` switches back to the firmware-bundled robot and removes
  custom-pet storage only after the watch acknowledges the operation.

## User Experience

### Agent window

The independent `Codex Pet` window contains:

- an animated preview inside a round-watch frame;
- the detected frame count, loop duration, processed dimensions, and package
  size;
- a drop target and file picker accepting one `.gif` file;
- connection/authentication state;
- `Restore Default` and `Send to Watch` actions;
- a determinate progress indicator with `Processing`, `Sending`, `Verifying`,
  `Applying`, `Installed`, and error states.

The preview is rendered from the converted package, not directly from the
source GIF. Therefore it is the same frame sequence, timing, transparency,
scaling, and palette that the watch will use.

The user may select and preview a GIF while the watch is offline. `Send to
Watch` is enabled only for an authenticated active Production connection.

### Watch presentation

The source animation is aspect-fitted and centered in a transparent
`144 x 156` canvas. It is never stretched or cropped. The current pixel-art
scaling style remains non-antialiased. The pet remains non-interactive and
does not change Codex gestures.

The Codex middle/lower information moves down enough to accommodate the new
pet box while keeping the two-line task summary, page indicator, and battery
label inside their physically verified circular safe areas.

The implementation starts from these locked coordinates, all in the existing
`466 x 466` coordinate space:

- pet box: `x=55, y=88, w=144, h=156`;
- beacon spine: `x=210, y=96, w=2, h=120`;
- telemetry column: `x=231`, with its current type sizes;
- quota row: `y=246`;
- alert divider: `y=282`;
- Pending/Running labels: `y=294`, counts: `y=320`;
- task index: `y=306`, project: `y=332`, two-line detail: `y=363`;
- page indicator: unchanged at `y=416`;
- battery: unchanged at `y=432`.

These coordinates are the browser-approved starting geometry, not a waiver of
the physical AMOLED comparison. If the real panel exposes clipping or optical
misalignment, adjust only within the same locked hierarchy and retain the
`144 x 156` pet box plus the fixed header/footer positions.

If the Codex page is visible when a new package commits, the renderer prepares
the new animation first and swaps it at a frame boundary. It must not expose a
blank frame or partially decoded package.

Animation pauses while the Codex page is not visible or the display is off.
It resumes from the retained frame position when the page becomes visible.
The animation and its buffers are released when the Codex app closes.

## Input and Conversion Contract

The Agent accepts GIF87a and GIF89a files up to 10 MiB. File size alone is not
a sufficient safety boundary. Before full decoding it also enforces:

- maximum logical canvas: `1024 x 1024`;
- maximum source frames: 240;
- maximum cumulative decoded pixels: 64 million;
- valid bounded frame delays and loop metadata;
- successful full-frame coalescing, including GIF disposal and transparency
  semantics.

The Agent coalesces frames before resizing. It removes identical consecutive
frames by accumulating their delays. If more than 24 frames remain, it samples
the animation to at most 24 frames while preserving total loop duration as
closely as the minimum frame delay permits. Each output delay is clamped to
`60..1000 ms`. The exact resulting animation is shown in the preview.

The first format version uses magic `CXP1`, a fixed-width little-endian header,
one global palette of at most 255 RGB565 colors plus a reserved transparent
index, and a fixed-width frame table. Every table entry contains a `60..1000`
ms duration plus bounded payload offset and length. Each payload is one complete
`144 x 156` indexed frame compressed with a small deterministic PackBits-style
RLE codec; the watch never needs to reproduce GIF disposal rules. The header
also carries encoded length, uncompressed pixel budget, and SHA-256 over the
canonical header-without-digest plus frame table, palette, and payload bytes.
The final package may not exceed 256 KiB. Offsets, lengths, products, frame
counts, palette counts, decoded sizes, and non-overlapping payload ranges are
validated before allocation on both Mac and watch.

The source GIF loop-count field does not control watch behavior. A valid custom
pet always loops indefinitely, matching the single-pet product decision.

If conversion cannot satisfy the output bounds without producing an invalid
animation, the Agent rejects it locally and explains the violated limit.

## Transport and Authorization

Allocate a new authenticated bulk-resource GATT surface after the existing
`...000a` allocation:

- custom-pet service: `...000b`;
- custom-pet control/data write characteristic: `...000c`;
- custom-pet acknowledgement/status notification characteristic: `...000d`.

Only the active authenticated Production connection may access this surface.
Pairing, pending, unauthorized, stale-generation, or disconnected Agents
cannot inspect, upload, commit, restore, or delete a pet.

Control messages and data chunks use the current Production credential and
crypto context, a fresh transaction nonce, exact bounded sequence/offset
fields, and replay protection. A transaction carries the package version,
length, SHA-256, frame count, dimensions, and transaction identifier.

Chunk writes are offset-addressed and idempotent within one live transaction,
so an acknowledgement lost in transit can be retried without appending data
twice. Commit is accepted exactly once. A commit, failure, disconnect,
credential epoch change, Agent switch, or timeout retires the transaction and
prevents late callbacks from entering a new session.

Only one upload transaction exists at a time. Queues and per-loop work remain
bounded so asset transfer cannot starve input actions, status, voice, or the UI.

The Agent window reports sent bytes only after acknowledged progress. It shows
`Verifying` until the watch has validated and committed the complete package.

Closing the window does not terminate an already acknowledged transfer in the
background. An explicit `Cancel` action retires the current transaction on both
sides before another upload can begin. Agent quit, reconnect, or credential
replacement uses the same bounded cancellation path and may never report a
stale late acknowledgement as success.

## Rollout Compatibility

Agent and firmware upgrades are independent. The Agent discovers the custom-pet
service after authentication; if `...000b` is absent, the pet window reports
`Firmware update required`, disables send/restore, and leaves status, voice,
input-action, pairing, and reconnect behavior unchanged. A firmware containing
the service continues to operate normally with an older Agent that never
subscribes to or writes the characteristics. Failure to initialize, subscribe,
or clean up the optional pet channel is isolated from all existing channels.

The first successful status acknowledgement reports the selected source
(`built-in` or `custom`), supported package schema range, active generation,
active SHA-256, and whether the active custom package is usable. These fields
are authoritative for capability gating, local-preview cache selection, and
Restore Default state.

## Persistent Storage and Atomic Commit

Custom pets live under `/spiflash/codex_pet`. Storage is shared with Badge and
other user content, so the watch checks real free space before accepting a
transaction. Before `Begin`, available space must cover the proposed staging
package and still leave at least 128 KiB free. The existing active package is
already reflected in the measured available-space value.

Use two versioned package slots plus a small generation/hash/slot record in an
NVS namespace whose name and keys remain within ESP-IDF's 15-character limit:

1. write chunks only to the inactive staging slot;
2. flush and close the file;
3. parse and validate the complete package with bounded allocations;
4. verify SHA-256 and all metadata;
5. commit the new active slot, generation, schema, hash, and record CRC as one
   versioned NVS blob update;
6. acknowledge success;
7. reclaim the older inactive slot when safe.

At boot, the loader validates the NVS active-slot record and selected package.
If the record or selected file is invalid, it scans both slots and chooses the
highest complete valid generation. If neither is valid, it uses the built-in
robot. Temporary or incomplete files are never rendered and are removed during
bounded startup cleanup. This avoids relying on replacement rename semantics
in the FAT filesystem for the commit point.

`Restore Default` first commits the built-in selection, then removes both
custom slots. A storage cleanup failure leaves the built-in pet selected and
surfaces a bounded warning; it does not reactivate stale custom data.

The package format includes a schema version. New firmware may continue to
read supported versions. An unsupported future or obsolete version falls back
to the built-in pet without deleting the file automatically.

Ordinary project flashing does not write the `storage` partition, so the pet
survives the normal firmware workflow. Full storage erase remains explicitly
destructive and outside this feature.

## Runtime and Memory

When Codex opens, the runtime validates the active package and loads its
compressed payload into PSRAM. It owns one reusable `RGB565A8` frame buffer for
the `144 x 156` canvas and decodes one bounded frame at a time. Package memory,
frame memory, and LVGL object ownership belong to a dedicated pet runtime, not
raw view pointers held by BLE callbacks.

BLE completion publishes only an immutable generation/hash notification. The
UI thread decides when to prepare and swap the renderer. App close, Home,
screen-off, page navigation, upload, and credential changes cannot race a view
through a transport-owned pointer.

The built-in pet remains compiled into the firmware as the unconditional
fallback. Loading, decoding, or timing failure stops the custom animation once,
records a bounded diagnostic, and returns to the built-in pet without rebooting.

## Multiple Agents and Local Cache

Any currently active authorized Agent may replace or restore the watch pet.
The pet is watch customization and is not bound to the Agent credential that
uploaded it; credential rotation does not delete it.

The watch is authoritative for active package version and SHA-256. An Agent
may cache the converted preview under Application Support keyed by watch
identity and package hash. It displays that preview only when the watch reports
the same hash. If another Agent changed the pet and the local cache does not
match, the window says `Custom pet installed on watch` without inventing a
thumbnail. The original user GIF does not need to be retained after conversion.

## Errors and Recovery

- Invalid, oversized, or hostile GIF: reject locally before BLE transfer.
- Offline or unauthenticated watch: preview remains available; sending is
  disabled.
- Insufficient watch storage: reject `Begin` with required and available byte
  counts; keep the active pet.
- Disconnect, timeout, reboot, or epoch change: retire the transaction and
  retain the active pet.
- Duplicate or out-of-order chunk: accept only an exact idempotent duplicate;
  otherwise reject the transaction.
- Hash, schema, dimension, frame-table, or decompression failure: never commit;
  discard staging data and retain the active pet.
- Runtime decode failure after a prior valid install: fall back to the built-in
  pet and report the active package as unusable.
- Restore failure: continue showing the actual watch state and never claim
  success from a local cache change.

Logs contain transaction identifiers, bounded sizes, state transitions, and
redacted error reasons. They do not contain image payloads, keys, nonces, or a
copy of the GIF.

## Verification and Acceptance

### Deterministic tests

- Python converter tests cover GIF87a/GIF89a, transparency, every disposal
  mode, variable delays, duplicate frames, aspect fitting, sampling, corrupt
  tables, oversized canvas/frame/pixel budgets, and package-size rejection.
- C++ and Python package/protocol tests share fixed vectors for headers,
  chunks, acknowledgements, SHA-256, palette transparency, and decoded frames.
- Security tests cover authorization, fresh nonces, replay, exact duplicates,
  out-of-order chunks, malformed bounds, queue limits, disconnect, timeout,
  Agent switch, and credential epoch changes.
- Storage tests interrupt every write/flush/validate/active-marker boundary and
  prove that boot selects either the old complete pet or the new complete pet,
  never staging data.
- Runtime tests cover default fallback, frame timing, pause/resume, app close,
  screen-off, live generation swap, decode failure, and bounded PSRAM release.
- Installer tests prove every new production module and Pillow dependency is
  included in the installed Agent bundle.

### Real acceptance

- Use the supplied six-frame `192 x 208`, 180 ms GIF as the primary acceptance
  fixture.
- Verify the Agent preview, processed metadata, progress, success, reconnect,
  and Restore Default flows in the installed macOS bundle.
- Upload over the authenticated production link and verify the animation
  changes without a blank frame.
- Reboot the watch and confirm the custom pet persists.
- Perform an ordinary firmware flash and confirm storage persistence.
- Interrupt one upload and confirm the previous pet remains active.
- Fill shared storage enough to trigger the reserve gate and confirm a precise
  failure without data loss.
- Inspect the physical AMOLED for default and custom pets. Compare the locked
  `144 x 156` geometry, transparency, frame cadence, circular clipping,
  typography, task-summary wrapping, page indicator, and battery position.

Browser mockups and source-level coordinates are design evidence, not physical
acceptance. The firmware UI remains incomplete until the running AMOLED has
been compared region by region with the approved layout.

## Out of Scope

- multiple installed custom pets;
- separate idle/working animations;
- on-watch GIF editing or file picking;
- audio attached to a pet;
- arbitrary video formats;
- publishing or sharing pets between users;
- erasing the entire storage partition as part of upload, restore, build, or
  flashing.
