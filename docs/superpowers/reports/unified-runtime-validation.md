# Unified Runtime Validation

Date: 2026-07-19

- Automated tests: Passed (144 Python tests and 9 host C++ test groups)
- Unified clean build: Passed (isolated build, capacity gates, and whitespace gate)
- Targeted hardware startup/lifecycle: Passed
- Audible and UI conversation checks: Pending manual observation
- Five-cycle Assistant graceful-close campaign: Passed
- 10-cycle, 100-cycle, and full product regression: Pending campaign

This report separates automated build evidence, targeted hardware evidence, and broader manual campaigns. The targeted run establishes that the merged Launcher/Assistant firmware can boot, activate Xiaozhi, start its audio path, exchange data with the server, and return Home without rebooting or retaining runtime owners. It does not substitute for audible TTS, spoken wake-word, display-content, stress, or full application regression testing.

## Automated evidence

`scripts/run_unified_offline_validation.sh` completed successfully on 2026-07-19. The final hardware image was rebuilt from committed runtime source `7cc2f034f17342324c6ca7a6e10f51cfabe2b478`, then passed the test, build, capacity, flash, and hardware gates described below.

The values below come from `build-unified/unified-runtime-metrics.json` (schema 2).

| Evidence | Value |
|---|---:|
| Application image SHA256 | `3f3b44e3a2ac4dac9736bb322ddf2889deff5df1ccea9f1723616cc69c7ae43b` |
| Application bytes | `6,006,768` (`0x5ba7f0`) |
| Application partition bytes | `7,864,320` (`0x780000`) |
| Application headroom | `1,857,552` bytes (`23.62%`) |
| Application hard gate | `6,684,672` (`0x660000`) |
| Assets image SHA256 | `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f` |
| Assets bytes | `291,198` |
| Assets hard gate | `4,194,304` (`0x400000`) |
| Static DIRAM bytes | `188,015` |
| Capacity gate | Passed |

The candidate keeps the locked Brookesia 0.7 component family. The Assistant product path does not switch boot partitions, call `esp_restart`, or reference `ota_1`. The legacy dual-OTA partition table, recovery path, and complete-backup restore helper remain available; this work does not remove that fallback.

## Targeted hardware evidence

| Field | Result |
|---|---|
| Device | ESP32-S3 watch, MAC `44:1b:f6:c1:86:5c`, serial port `/dev/cu.usbmodem1101` during the run |
| Complete Flash backup | 16 MiB `backups/watch-full-flash-20260718-173922.bin`; SHA256 `b6ecc3da8f0dbd41de9722a1956566ca3ce0f5506e6439fe5c4a57366c9000dc` |
| Preserved assets | SHA256 `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f` |
| Automated control | `debug app open assistant`, `debug assistant home`, `debug assistant status`, and trace commands acknowledged by the watch |
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
| Repeated lifecycle | Five consecutive sessions (`sid=3..7`) reached Running, closed, reopened Launcher, and passed the steady-state stop gate |

The original physical-button failure was isolated to legacy button audio feedback reopening the shared playback/I2S path while Assistant still owned it. Button vibration and the `GoHome` key event are retained, but the sound effect is now suppressed whenever supervised playback has an owner. The exact physical gesture and the USB command converge on the same one-shot graceful-close controller.

The five-cycle hardware transcript is `/tmp/assistant-graceful-close-20260719-v5.log`. Every cycle recorded the causal sequence `close-request -> stopping -> stopped callback -> release -> closed`, returned bindings/subscriptions/Wi-Fi/capture/playback to zero, and reopened Launcher. The second stop gate passed, as did all later steady-state gates: snapshots reported `task_delta=0` and `gate=true`; internal loss was 288, 288, 288, 288, and 376 bytes, while PSRAM loss stayed between 21,080 and 21,412 bytes. No reset, panic, or watchdog marker was observed.

One earlier attempt captured an external activation HTTP timeout before the Assistant reached Running; the trace made that failure distinguishable from shutdown behavior, and a subsequent five-cycle run passed. A GMF `Got NULL Pointer` diagnostic occurs during Audio deinitialization in every cycle, but the stop sequence continues, all owners reach zero, the resource gate passes, and no reset follows. It remains a tracked non-fatal upstream teardown warning.

The exact image listed above was flashed after all four address ranges passed esptool write-hash verification. The watch then completed five consecutive automated open/run/graceful-Home lifecycles against that image.

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
| Repeated entry | Passed | Five consecutive automated opens reached Running |
| Post-stop runtime residue | Passed | Owners zero; `task_delta=0` and steady-state gate true in all five cycles |
| Reboot/crash during targeted run | Passed | None observed |

## Remaining campaigns

- Manually exercise the exact two-button gesture once; the shared application path and button-audio ownership policy are automated, but the physical switches were not robotically actuated.
- Run 10 mixed open/close cycles, including Starting, Listening, recording, TTS playback, and reconnect windows.
- Run the planned 100-cycle stress/leak campaign and evaluate cumulative internal/PSRAM trends.
- Manually validate spoken wake phrase, recognized text, assistant text/emotion, and audible TTS.
- Run full Launcher, Clock, Codex, Badge, IMU, FFT, Setup, boot-sound, display, buttons, sleep/wake, power, BLE/Wi-Fi coexistence, and recovery regression.
