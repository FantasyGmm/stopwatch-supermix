# Unified Runtime Validation

Date: 2026-07-19

- Automated tests: Passed (133 Python tests and 6 host C++ test binaries)
- Unified clean build: Passed (isolated build, capacity gates, and whitespace gate)
- Targeted hardware startup/lifecycle: Passed
- Audible and UI conversation checks: Pending manual observation
- 10-cycle, 100-cycle, and full product regression: Pending campaign

This report separates automated build evidence, targeted hardware evidence, and broader manual campaigns. The targeted run establishes that the merged Launcher/Assistant firmware can boot, activate Xiaozhi, start its audio path, exchange data with the server, and return Home without rebooting or retaining runtime owners. It does not substitute for audible TTS, spoken wake-word, display-content, stress, or full application regression testing.

## Automated evidence

`scripts/run_unified_offline_validation.sh` completed successfully on 2026-07-19 from working source based on `1c2244d88747fe7d5d91431a5a04c07eff9d01a7`. The run passed 133 Python tests, all 6 host C++ test binaries, a clean 2,332-unit firmware build, both image-size gates, and `git diff --check`.

The values below come from `build-unified/unified-runtime-metrics.json` (schema 2).

| Evidence | Value |
|---|---:|
| Application image SHA256 | `10f1c267ba95a34fa01863fff3963018d185589d0d1d2c2bf88ce051dba62b36` |
| Application bytes | `5,998,336` (`0x5b8700`) |
| Application partition bytes | `7,864,320` (`0x780000`) |
| Application headroom | `1,865,984` bytes (`23.73%`) |
| Application hard gate | `6,684,672` (`0x660000`) |
| Assets image SHA256 | `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f` |
| Assets bytes | `291,198` |
| Assets hard gate | `4,194,304` (`0x400000`) |
| Static DIRAM bytes | `185,415` |
| Capacity gate | Passed |

The candidate keeps the locked Brookesia 0.7 component family. The Assistant product path does not switch boot partitions, call `esp_restart`, or reference `ota_1`. The legacy dual-OTA partition table, recovery path, and complete-backup restore helper remain available; this work does not remove that fallback.

## Targeted hardware evidence

| Field | Result |
|---|---|
| Device | ESP32-S3 watch, MAC `44:1b:f6:c1:86:5c`, serial port `/dev/cu.usbmodem1101` during the run |
| Complete Flash backup | 16 MiB `backups/watch-full-flash-20260718-173922.bin`; SHA256 `b6ecc3da8f0dbd41de9722a1956566ca3ce0f5506e6439fe5c4a57366c9000dc` |
| Preserved assets | SHA256 `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f` |
| Automated control | `debug app list`, `debug app open assistant`, and `debug app home` acknowledged by the watch |
| Wi-Fi | Acquired `192.168.50.238` before heavy service startup |
| Xiaozhi activation | Passed; endpoint reported activation successful |
| Audio startup | Decoder and Encoder started; light feeder/recorder opened |
| Wake model | `wn9_nihaoxiaozhi_tts` loaded from the `assets` partition |
| Server path | Agent connected; MCP initialize and tools/list received; audio channel opened |
| Microphone path | Continuous 180-byte encoded packets emitted |
| Runtime state | Reached `first-running`, state 2, without reset or reboot |
| Running resources | 2 bindings, 5 subscriptions, Wi-Fi/capture/playback owned as expected |
| Stop/Home | Agent, Encoder, Decoder, NVS, AgentXiaoZhi, AgentManager, Audio, Device, SNTP, and Wi-Fi stopped |
| Stopped resources | Bindings/subscriptions/Wi-Fi/capture/playback all zero |
| Second lifecycle | Reopen succeeded; second stop gate passed with `internal_loss=2560`, `psram_loss=39564`, `task_delta=0`, `gate=true` |

The first stopped snapshot included one-time persistent Wi-Fi/service initialization and therefore did not satisfy the paired leak gate. The second complete open/close session did satisfy it. A GMF `Got NULL Pointer` diagnostic occurs during Audio deinitialization, but the stop sequence completes, no reset follows, and the second-cycle resource gate passes. Downlink logs also contain occasional wrong-sequence warnings; neither warning blocked startup, audio-channel establishment, or cleanup.

The exact clean image listed above was flashed after all four address ranges passed esptool write-hash verification. The watch then completed two automated open/run/Home lifecycles against that image. The first stopped snapshot includes one-time persistent Wi-Fi/service initialization; the second is the steady-state leak gate quoted above.

## Compatibility and lifecycle matrix

| Capability | Status | Evidence or remaining check |
|---|---|---|
| Existing activation | Passed | Activation completed without generating a new user-visible identity |
| Manual/listen startup | Passed | Agent session and server audio channel opened |
| Microphone upload | Passed | Continuous encoded audio packets observed |
| MCP initialization | Passed | Initialize and tools/list received |
| Wake phrase `你好小智` | Pending manual | Speak phrase and observe WakeStart |
| User text | Pending manual | Observe recognized text on UI/log |
| Assistant text/emotion | Pending manual | Observe UI callbacks |
| TTS audio | Pending manual | Confirm audible, stable playback |
| Stop while active recording | Passed | Automated Home stopped encoder/recorder and released capture |
| Stop while idle/playing/reconnecting | Pending campaign | Exercise each timing window explicitly |
| Successful second entry | Passed | Second automated open reached running |
| Post-stop runtime residue | Passed | Owners zero; `task_delta=0` on second cycle |
| Reboot/crash during targeted run | Passed | None observed |

## Remaining campaigns

- Run 10 mixed open/close cycles, including Starting, Listening, recording, TTS playback, and reconnect windows.
- Run the planned 100-cycle stress/leak campaign and evaluate cumulative internal/PSRAM trends.
- Manually validate spoken wake phrase, recognized text, assistant text/emotion, and audible TTS.
- Run full Launcher, Clock, Codex, Badge, IMU, FFT, Setup, boot-sound, display, buttons, sleep/wake, power, BLE/Wi-Fi coexistence, and recovery regression.
