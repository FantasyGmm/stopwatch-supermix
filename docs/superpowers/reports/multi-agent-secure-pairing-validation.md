# Multi-Agent Secure Pairing Validation

Date: 2026-07-30

- Secure pairing implementation: Automated validation passed
- Mac Agent package: Built, signed, installed, and launched
- Firmware release candidate: Clean build, flash, boot, and application smoke passed
- Physical watch pairing: Passed for one Agent; approval remained stable without reboot
- Remaining physical matrix: Multi-Agent switching/rekey and end-to-end voice input pending
- Secret handling: No keys, verification codes, public-key packets, or ciphertext are recorded here

## Automated Evidence

The implementation through commit `769d1c4ad0062149f80b4885dc6e1cd1b0fdbd08`
passed the complete offline validation without a compiled voice key:

| Evidence | Result |
|---|---:|
| Python tests | 786 passed |
| Unified validation stages | 53/53 passed |
| ESP-IDF target | ESP32-S3 |
| Application bytes | 6,275,552 (isolated clean validation); 6,274,720 (flashed candidate) |
| Application partition bytes | 14,417,920 |
| Application headroom | 8,142,368 bytes (56.47%) |
| Isolated clean application SHA256 | `a8b647c9c204343b34d6ec4c6b4f73f00a072eb2f95882758e9feb83efb01e0c` |
| Flashed application SHA256 | `42a3cf84fee52a11a0f5236ae9ecab75b49402a5ac557eddad550fd40a1d7d89` |
| Assets bytes | 291,198 |
| Assets SHA256 | `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f` |
| Static DIRAM | 191,999 bytes |
| Capacity gate | Passed |

The test suite covers P-256 ECDH pairing, six-digit SAS derivation,
physical-approval state transitions, a persistent watch signing identity
pinned during SAS, signed mutual authentication and renaming, rekey-envelope
binding to the signed challenge,
credential persistence, eight-Agent registry capacity, active/pending BLE
routing, master-key epochs, removal rotation, online and offline rekeying,
old-epoch rejection, interrupted-enrollment recovery, stale local credential
removal, watch Agent switching, and Mac menu behavior. It also covers pending
connection expiry, 32-bit timer wraparound, signed enrollment identity claims,
active in-place rekey recovery, cross-connection transaction isolation,
disconnect retry races, and production subscription denial before Agent
authorization.

The first physical approval attempt on the previous candidate reproduced a
main-task stack overflow immediately after the user selected Allow. Live USB
output showed `***ERROR*** A stack overflow in task main has been detected`, a
corrupted backtrace, and `RTC_SW_CPU_RST`; no pairing code called
`esp_restart`. The registry persistence path had placed both registry slots
and its maximum-size encoded and payload work buffers on the 8 KiB main-task
stack. Commit `769d1c4` moves that bounded workspace to a checked heap
allocation. A regression test observes all storage reads and writes larger
than 256 bytes and fails if their buffer is within 64 KiB of the test stack.
The test failed before the change and passes afterward.

Production CMake and the offline build no longer generate or include
`voice_input_device_key.h`, and they do not read `STOPWATCH_VOICE_KEY_FILE`.
The remaining migration utility only removes the exact obsolete Keychain item;
it cannot create, import, or print a key.

## Packaged Agent

`install_launch_agent.sh` installed
`~/Applications/StopWatch Codex Agent.app`. `codesign --verify --deep --strict`
passed, and the executable is a universal Mach-O containing `x86_64` and
`arm64`. The bundle includes the Agent identity, watch credential, secure
pairing protocol, wire codec, cryptography, and pairing client modules. Direct
comparisons confirmed that the packaged pairing, credential, runtime, and
voice-session sources match the validated repository sources.

The newly installed Agent was launched successfully. After the user approved
its existing Keychain item, it discovered the watch, completed secure pairing,
and reported `Bluetooth state: connected`. One subsequent CoreBluetooth
`Resources are insufficient` write failure triggered the normal reconnect
path, which returned to `connected`. The Agent then reported
`Voice state: driver_required`; the BlackHole receipt and Core Audio device are
absent, so end-to-end voice input cannot be accepted until the bundled,
checksum-pinned driver is installed and macOS is restarted if requested.

## Physical Acceptance Status

Exactly one watch was present at `/dev/cu.usbmodem21201`: ESP32-S3 revision
0.2, MAC `44:1b:f6:c1:86:5c`. The user explicitly waived a recovery backup.
Firmware `769d1c4` was flashed only to bootloader `0x000000`, partition table
`0x008000`, application `0x020000`, and assets `0xee0000`, preserving NVS.
Boot validation observed application version `769d1c4`, FAT mounting, Launcher
opening, and no reset. The six-application product smoke passed for Codex,
Clock, Badge, IMU, Audio.FFT, and Settings.

Codex then advertised over BLE and accepted the pending Agent. Selecting Allow
completed pairing; live serial capture continued for more than two minutes
without stack overflow, watchdog, panic, or reset, while encrypted
notifications continued. This accepts the reported approval-reboot defect and
one-Agent secure enrollment, but does not substitute for the remaining
multi-Agent and voice matrix below.

During an earlier manual flash in this session, assets were mistakenly written
once at `0xe00000` instead of `0xee0000`. That write overlapped the FAT storage
partition at `0xde0000..0xedffff`; NVS was not overlapped. The correct assets
were subsequently reflashed at `0xee0000`, and FAT now mounts, but previous FAT
contents such as saved alarms were lost. Because the user waived backup, those
contents are not recoverable from this validation session.

`run_unified_hardware_validation.sh` now writes
`secure-pairing-acceptance.md` after its automated hardware stages. The
checklist deliberately records only pass/fail, non-secret notes, and key epoch
numbers. Physical acceptance remains pending for:

- a second-Agent enrollment with matching six-digit display and watch approval;
- a pending enrollment that does not interrupt the active Agent;
- pull-down active-Agent switching and signed display-name changes;
- removal with online rekey, offline rekey on reconnect, and old-epoch rejection;
- eight-Agent capacity enforcement and Reset All Agents;
- Mandarin voice input quality and input restoration;
- Assistant recovery plus BLE and internal-memory stability.

Future flash and physical acceptance should use one explicit watch port and a
checksum-verified complete 16 MiB backup unless the user explicitly waives it.
Verification codes and cryptographic material must not be copied into the
resulting evidence.
