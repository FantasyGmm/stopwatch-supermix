# Multi-Agent Secure Pairing Validation

Date: 2026-07-30

- Secure pairing implementation: Automated validation passed
- Mac Agent package: Built, signed, installed, and launched
- Firmware release candidate: Clean build passed
- Physical watch pairing: Pending; no watch USB serial port was present
- Secret handling: No keys, verification codes, public-key packets, or ciphertext are recorded here

## Automated Evidence

The implementation through commit `67a7d4da6252a43b2d5306e6d6523be2aa0a7664`
passed the complete offline validation without a compiled voice key:

| Evidence | Result |
|---|---:|
| Python tests | 786 passed |
| Unified validation stages | 53/53 passed |
| ESP-IDF target | ESP32-S3 |
| Application bytes | 6,275,552 |
| Application partition bytes | 14,417,920 |
| Application headroom | 8,142,368 bytes (56.47%) |
| Application SHA256 | `8c91fadc04fb20a870159bfeced1cd164e006694d17f7e73ec786dd77afccf97` |
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

The newly installed Agent was launched successfully. Process sampling shows
that macOS is currently holding the main thread in `SecItemCopyMatching` while
waiting for the user to approve access to its existing Keychain item. The new
process has therefore not reached CoreBluetooth scanning yet. Before the
restart, the prior Agent process repeatedly scanned normally but observed no
`StopWatch-Codex` advertisement.

## Physical Acceptance Status

No `/dev/cu.usbmodem*` device was present, and no verified 16 MiB recovery
backup was found under the current project tree. The backup-gated flash script
was therefore not run. This preserves the existing watch contents and avoids
claiming a physical result from source or simulator evidence.

`run_unified_hardware_validation.sh` now writes
`secure-pairing-acceptance.md` after its automated hardware stages. The
checklist deliberately records only pass/fail, non-secret notes, and key epoch
numbers. Physical acceptance remains pending for:

- two-Agent enrollment with matching six-digit display and watch approval;
- a pending enrollment that does not interrupt the active Agent;
- pull-down active-Agent switching and signed display-name changes;
- removal with online rekey, offline rekey on reconnect, and old-epoch rejection;
- eight-Agent capacity enforcement and Reset All Agents;
- Mandarin voice input quality and input restoration;
- Assistant recovery plus BLE and internal-memory stability.

Flash and physical acceptance must use one explicit watch port and a
checksum-verified complete 16 MiB backup. Verification codes and cryptographic
material must not be copied into the resulting evidence.
