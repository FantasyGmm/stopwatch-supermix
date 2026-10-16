# Unified Firmware Artifact Export

`scripts/export_fusion_firmware.sh` packages the already-built unified ESP32-S3
candidate. It does not build, install, or flash firmware.

From the repository root, export the default `build-unified` output to the
fusion workspace's `../deliverables/firmware` directory:

```bash
./scripts/export_fusion_firmware.sh
```

You can select another build directory and a new output directory:

```bash
./scripts/export_fusion_firmware.sh \
  --build-dir build-unified \
  --dest ../deliverables/firmware-review
```

Relative arguments are resolved from the current directory. Export validates
the raw partition table and tracked `partitions.csv` against the supported
16 MiB layout, checks every input path from `flasher_args.json` stays within the
build directory, requires passing runtime capacity metrics, and recomputes the
application and assets sizes and SHA256 values. The metrics must identify the
currently checked-out source commit; rebuild after changing the source commit.
Metrics artifact paths are normalized to package-relative filenames.

Publication is staged beside the destination and completed with an atomic
no-replace directory rename. The destination must not already exist. The
exporter never deletes or replaces an existing destination, and it removes only
its own staging directory after an error.

The package contains:

- `bootloader/bootloader.bin`, `partition_table/partition-table.bin`,
  `StopWatch-UserDemo.bin`, and `generated_assets.bin`;
- portable `flasher_args.json` and `portable_flash_map.json` with relative file
  names and reviewed offsets;
- `sdkconfig.unified`, normalized `unified-runtime-metrics.json`, and
  `source-idf-manifest.txt` with source and ESP-IDF version evidence;
- `SHA256SUMS` covering every package file except itself; and
- `flash-command.txt`, a text-only command template with manual safety gates.

Build trees, ESP-IDF archives, toolchains, managed components, and an automatic
installer are not included. `sdkconfig.unified` is included as requested and is
copied byte-for-byte; review compile-time integration values before distributing
the package.

## Firmware write and optional backup

`flash-command.txt` is informational text; this package contains no executable
flash entry point. Each write requires the user's explicit authorization for
that specific operation. A complete 16 MiB flash read is optional recovery
material, not a prerequisite. Run the capacity-gated write script from the
repository root with the exact detected connected serial port:

```bash
./scripts/flash_unified_candidate.sh --port "/dev/cu.usbmodemNNNN"
```

Replace the example with the full path of the connected watch; do not guess or
use a placeholder unchanged. The write script checks application/assets
capacity and partition limits, then writes only the bootloader, partition table,
application, and assets at the offsets in `portable_flash_map.json`. It does not
erase the entire Flash or write NVS, authorized-Agent credentials, `storage`, or
`coredump`. After writing, verify readback hashes for all four images against
the package map and confirm a fresh firmware boot in serial output.

If a full-flash recovery copy is useful, run the existing backup script
separately with a new private output path:

```bash
./scripts/backup_watch_flash.sh \
  --port "/dev/cu.usbmodemNNNN" \
  --output "/path/to/new-private-backup/watch-full-flash.bin"
```

Replace the example port with the exact detected device. The script reads offset
`0x0` for `0x1000000` bytes (16,777,216 bytes), checks the output length, and
writes a `.sha256` companion. Its `--dry-run` option only prints the esptool
read command. The script removes an existing `OUTPUT.sha256` before a real
read, so use an unused output path and keep the full image private. The exporter
never invokes either the backup or flash command.
