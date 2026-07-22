# Single-System Flash Partition Optimization Design

**Date:** 2026-07-22
**Status:** Approved for implementation planning
**Target:** M5Stack StopWatch, ESP32-S3, 16 MiB Flash

## Goal

Reallocate the current single-system firmware layout so that most Flash capacity is available to the unified application while preserving practical space for writable files, existing Assistant assets, diagnostics, and future growth.

The product will continue to ship one merged system application. It will not add A/B OTA slots or automatic rollback. Firmware, partition-table, and asset updates remain a verified USB full-release operation.

## Confirmed Product Constraints

- Launcher, Codex, Assistant, and all other watch applications remain compiled into one `factory` application image.
- Remote OTA and inactive application slots are out of scope.
- Future Codex voice input is started by a physical button.
- Codex voice input does not add a wake-word model, local speech recognition, or complex denoising models.
- Codex audio is buffered in PSRAM, encoded, and streamed to the associated Mac. It is not continuously recorded to Flash.
- Existing Xiaozhi resources remain supported; this design does not remove its current `srmodels.bin` asset.

## Current Baseline

The current partition table assigns 7.5 MiB to App, 1 MiB to Storage, and 4 MiB to Assets, with approximately 3.3125 MiB left unallocated.

The latest measured build snapshot is:

| Artifact | Current size |
|---|---:|
| Unified application | 6,035,344 bytes, about 5.756 MiB |
| Generated assets | 291,198 bytes, about 284 KiB |

The application therefore has only about 1.744 MiB of physical partition headroom, while Assets uses less than 7% of its current partition and more than 3 MiB remains outside all partitions.

## Approved Layout

```csv
# Name,   Type, SubType, Offset,    Size
nvs,      data, nvs,     0x9000,    0x10000
phy_init, data, phy,     0x19000,   0x1000
app,      app,  factory, 0x20000,   0xDC0000
storage,  data, fat,     0xDE0000,  0x100000
assets,   data, spiffs,  0xEE0000,  0xC0000
coredump, data, coredump,0xFA0000,  0x10000
```

| Region | Capacity | Purpose | Current utilization |
|---|---:|---|---:|
| NVS | 64 KiB | Wi-Fi credentials, settings, alarms, persistent application state | Runtime-dependent |
| PHY | 4 KiB | Radio calibration data | System-managed |
| App | 13.75 MiB | Unified executable and resources linked into the firmware | About 41.9% |
| Storage | 1 MiB | Writable FAT filesystem mounted at `/spiflash` | Runtime-dependent |
| Assets | 768 KiB | Existing external Assistant model bundle and similar read-only assets | About 37.0% |
| Coredump | 64 KiB | Crash diagnostics | Empty during normal operation |
| Trailing reserve | 320 KiB | Alignment and future partition-table adjustment | Unallocated |

This increases App headroom to about 7.99 MiB while leaving approximately 484 KiB free in Assets.

## Rationale

### App: 13.75 MiB

All product applications and most UI assets are linked into one binary, so future feature growth primarily increases the App image. A 13.75 MiB partition gives the unified system the largest useful share of Flash without constraining the writable or model partitions.

The partition size is a hard ceiling, not a license for uncontrolled binary growth. The existing App regression gate should remain at `0x660000` until an intentional feature requires a measured increase. Any later increase must be reviewed explicitly and must never exceed a structural release ceiling of `0xC40000` (12.25 MiB), preserving at least 1.5 MiB inside the partition.

### Storage: 1 MiB

Storage remains at 1 MiB because Codex voice audio is not persisted. PCM and encoded frames stay in PSRAM only for the active session. Storage continues to serve writable files, cache, and application data; small settings remain in NVS.

If a future feature requires saved recordings or large downloadable content, it must introduce a new capacity proposal instead of silently consuming this partition.

### Assets: 768 KiB

The current generated asset image is about 284 KiB. Codex voice adds no wake-word, offline recognition, or denoising model, so a 4 MiB partition is unnecessary. A 768 KiB partition still provides roughly 2.7 times the current capacity and about 484 KiB of growth room for the existing Assistant asset pipeline.

The build and flash asset gate should be lowered from 4 MiB to `0xA0000` (640 KiB), leaving 128 KiB of guard space inside the partition.

### NVS and reserve

NVS grows from 16 KiB to 64 KiB to reduce long-term pressure from Wi-Fi, Assistant, Codex, alarm, and system preferences. Its starting offset remains `0x9000`. The PHY partition moves to `0x19000`, and App keeps its existing `0x20000` offset.

The final 320 KiB remains deliberately unallocated. Recovering it would add little useful App capacity while eliminating all room for future alignment or small metadata partitions.

## Codex Voice Data Path

```text
Physical button
  -> microphone/I2S capture
  -> bounded PSRAM ring buffer
  -> lightweight audio encoder
  -> Wi-Fi stream to the associated Mac
  -> Codex transcription and processing on the Mac
```

The watch must not write normal voice frames to Storage or NVS. A transport failure stops or retries the live session within bounded RAM; it does not create an unbounded Flash-backed queue.

## Required Implementation Changes

1. Replace `partitions.csv` with the approved offsets and capacities.
2. Update build and flash capacity checks:
   - keep the current App regression gate initially;
   - add the 12.25 MiB structural App ceiling to the documented release policy;
   - lower the Assets gate to 640 KiB.
3. Remove the hard-coded Assets flash offset `0x8a0000` from `scripts/flash_unified_candidate.sh`. The script must obtain or verify `0xEE0000` from the generated partition table so the flashed address cannot drift from `partitions.csv`.
4. Update partition contract tests and release documentation.
5. Preserve the single `factory` App subtype and keep rollback/OTA support disabled.

Runtime code already locates Storage and Assets by partition label, so their new offsets do not require application-level address constants.

## Migration and Recovery

Changing the partition table is a destructive layout migration even when individual data ranges are not immediately overwritten.

- Create and checksum a complete 16 MiB Flash backup before the first write.
- Flash bootloader, partition table, App, and Assets as one release operation.
- Existing Storage data at `0x7a0000` does not move automatically to the new `0xDE0000` partition. Either accept a clean Storage filesystem or add a one-time, explicitly tested migration tool.
- Assets must be written at the new `0xEE0000` offset in the same operation.
- NVS keeps its starting offset, but preservation must still be verified on hardware because its size and neighboring PHY layout change.
- Rollback means restoring the verified full-Flash backup over USB; there is no automatic rollback slot.

## Failure Handling

- Build fails if App or Assets exceeds its active capacity gate.
- Flash tooling refuses to proceed without a complete, checksum-valid 16 MiB backup.
- Flash tooling validates partition offsets and artifact sizes before invoking `esptool`.
- Assets mapping failure remains a visible Assistant startup error and must not read from the old address.
- Storage mount failure follows the existing format-or-error policy and must not corrupt NVS or Assets.

## Verification

### Automated

- Partition-table generation succeeds with no overlap and remains within 16 MiB.
- The table contains exactly one `factory` App and no `otadata`, `ota_0`, or `ota_1` entries.
- A clean unified build passes.
- The App image fits its current regression gate and 13.75 MiB partition.
- `generated_assets.bin` fits the 640 KiB release gate and 768 KiB partition.
- Flash-script dry-run uses `0x020000` for App and `0xEE0000` for Assets.
- Existing partition, update-disabled, Assistant, Codex, Storage, and reproducible-build contract tests pass.

### Hardware

- Verify and retain a full 16 MiB backup before flashing.
- Flash the complete release and verify image hashes.
- Boot Launcher and open every retained application.
- Verify Wi-Fi, alarms, brightness, volume, Codex status, Assistant lifecycle, microphone, speaker, and existing Xiaozhi assets.
- Confirm Storage mounts at the new location and its reset or migration behavior matches the chosen release policy.
- Confirm NVS-backed settings survive when preservation is intended.
- Restore the backup once to prove the documented recovery path.

## Out of Scope

- A/B OTA, remote firmware installation, or automatic rollback
- Separate dynamically installed applications
- Persistent voice recordings on the watch
- Local Codex speech recognition
- New Codex wake-word, VAD, AEC, or denoising models
- Removing or redesigning the existing Xiaozhi asset pipeline
