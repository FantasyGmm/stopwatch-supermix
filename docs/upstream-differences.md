# Upstream Differences

## Frozen references

| Input | Pinned source |
| --- | --- |
| Firmware and Mac Agent baseline | [`lukeluccc/stopwatch-supermix`](https://github.com/lukeluccc/stopwatch-supermix/tree/1c42aea1470efe6bbf1beb3462018077ed48258f), `1c42aea1470efe6bbf1beb3462018077ed48258f` |
| Usage and host-collection reference | [`wujin941005/stopwatch-hub`](https://github.com/wujin941005/stopwatch-hub/tree/2e611f347b48683c7d7212f40af67a8bc6ece4a5), `2e611f347b48683c7d7212f40af67a8bc6ece4a5` |
| ESP-IDF | v5.5.4, `735507283d5b2f9fb363a1901172dbd9e847945d` |

Work is on `feat/c152-usage-host-fusion` in a separate checkout. The two
repositories are not merged. The firmware remains based on supermix and keeps
its existing M5Stack StopWatch target, app architecture, component pins, and
partition table.

## Behavior and architecture

| Area | Supermix baseline | Hub reference | Fusion result |
| --- | --- | --- | --- |
| Firmware app shell | Mooncake/LVGL launcher, Assistant, and StopWatch apps | Separate generated UserDemo checkout and app set | Keeps the launcher and utility apps; Clock and standalone Assistant are excluded from the current firmware profile. Codex voice and shared HAL/audio remain. System time, RTC and shared status clocks remain. Codex/TRAE task pages follow the Mac's installed-Agent manifest. |
| Codex Agent | Python menu bar Agent with task status, BLE, voice/input, and pet support | CC Island bridge and provider/host usage collection | Native Rust Agent preserves the existing workflows; the Python source remains as a comparison baseline, while the packaged app contains no Python runtime or Python source. |
| Codex tasks | Model, reasoning effort, task alerts, reminders, and protected task details | Different task and provider model | Existing task fields and authenticated detail flow remain; compact usage and host data extend the existing status protocol. |
| Usage and quota | Existing remaining-quota percentages | Usage, quota windows, resets, and provider collection | Shows 7D quota/reset, today's tokens and API-equivalent USD estimate, and switchable model token/cost/performance details. CC Switch session TPS/TTFT are labeled estimates with sample coverage; unknown prices and timings stay unavailable. |
| Mac host | No dedicated watch page | CPU, memory, disk, and network statistics | Host code is retained but its watch page, normal-runtime sampling worker and transmitted fields are temporarily disabled. |
| Optional providers | No Claude/OpenCode summaries | Provider monitoring concepts | Claude is an explicit-token, read-only GET; OpenCode is an opt-in read-only JSON import. Both are off by default and neither refreshes or writes provider credentials. |
| BLE and Wi-Fi | Existing secure BLE pairing, encrypted voice, task input, and WebSocket fallback | Hub-specific NUS/HTTP bridge and Wi-Fi service | Negotiates MessagePack status payloads on the existing authenticated RX characteristic; old peers retain JSON. Keeps pairing, voice/input/pet formats, transport and Wi-Fi ownership. No new service or characteristic. |
| Flash layout | Original 16 MiB single-app layout | Two OTA app slots and Hub storage layout | Retains supermix partitions: NVS at `0x9000`, app at `0x20000`, FAT storage at `0xDE0000`, Assistant assets at `0xEE0000`, and coredump at `0xFA0000`. Hub OTA partitions are excluded. |
| Build and packaging | ESP-IDF firmware and Python Agent packaging | Hub-specific firmware generation and bridge deployment | Adds the pinned ESP-IDF mirror/build scripts and a universal2 Rust Agent package. Builds do not flash the device. |
| Optional environmental unit | No ENV-III application | Not imported | Adds the Chinese environment app for temperature, humidity and pressure, independent presence/error handling and automatic reprobe. Port A remains owned by the existing HAL. The simplified page has enlarged readings and no redundant title or normal-state copy. |
| Pending animation | Breathing red perimeter and status text | Not imported | Keeps a static red perimeter at the existing peak opacity and pulses only the small status/count targets, avoiding continuous large-area halo invalidation. |
| Screen-off rendering | Slower LVGL/background cadence | Not imported | Suspends display refresh and nonessential sensor/FFT work while off; releases the awake 240 MHz CPU lock for DFS (80 MHz minimum, runnable work can boost), reacquires it before display wake. Input, transport and alerts remain active. No light/deep sleep. Power savings have not been measured. |

The watch carousel is microphone, Codex, optional installed TRAE, Usage.
TRAE code is retained and its page appears only when the Mac reports it installed.
Host's logical ID and implementation remain reserved; no blank page is inserted.
Usage keeps its assigned carousel position.
The Mac menu follows the first system language: Chinese variants use Simplified
Chinese, English uses English, and other languages fall back to English.
Its Codex submenu is a compact summary, without the conversation list. Watch
short A/B presses navigate pages; long A submits/queues, long B is push-to-talk,
and A+B returns Home. Short presses no longer trigger desktop actions.

The complete status payload limit remains 2048 encoded bytes, with legacy JSON
v1 and negotiated MessagePack v2 framing. `telemetry_v: 1` carries
only display values, timestamps, and `ok`/`partial`/`stale`/`error`/`unavailable`
states. Usage, quota, host, and optional-provider values have separate freshness
windows. The Codex and TRAE fields remain part of the existing status flow;
task-detail content continues through its existing authenticated, encrypted
channel. Telemetry does not carry OAuth tokens, cookies, prompts, source code,
or tool output.

The codec changes byte representation, not refresh frequency. Task collection
and the BLE send tick remain two seconds. Native MessagePack bytes replace
Base64 ciphertext and hexadecimal nonce in task-detail envelopes; the existing
AES-CCM and canonical AAD remain unchanged. See [binary status protocol](ble-status-codec-v2.md).

An optional version-1 `usage.d` extension carries bounded model details over
the same status link. Older firmware ignores it; older Agents retain the basic
weekly/total view on the new firmware. Model rows are limited to six and reduced
further if required by the 2048-byte message budget, with omitted rows reported
explicitly. See [Usage details](usage-details.md).

The public price source is `models.dev`'s `api.json` catalog. It is refreshed at
most every six hours with an eight-second timeout and an 8 MiB limit. A public
price older than seven days produces partial estimate coverage. Costs are
USD-equivalent API estimates, not provider invoices. Optional Claude quota uses
only a supplied `STOPWATCH_CLAUDE_USAGE_TOKEN` for a GET request. OpenCode has no
stable quota API in this integration; its optional
`STOPWATCH_OPENCODE_USAGE_JSON` import is read-only and capped at 4 KiB.

## Reuse and license boundary

The project keeps the original supermix/M5Stack MIT basis and the pinned
firmware dependencies. The Hub reference is limited to MIT-covered StopWatch
usage and host-collection behavior associated with CC Island. The Rust
implementation follows that behavior without importing the Hub firmware tree,
its app shell, NUS/HTTP bridge, Wi-Fi service, or OTA layout.

The Hub repository contains material with distinct licensing. PrintSphere-
derived code and assets under FNCL-1.1 are excluded, as are non-commercial
files. No PrintSphere code or assets are present in this integration. The
MIT-attribution record for the referenced bridge is maintained separately in
`third_party_notices/stopwatch-hub-usage.md`; it records the Hub notice's
copyright attribution to wangjiacheng.

The optional ENV-III sensor driver adapts the MIT-licensed M5Stack M5Unit-ENV
QMP6988 compensation code at `737e5180ea7b84d8f22fe49ae4fc62cf06cbc3a2`.
The complete library, Arduino I²C owner and extra framework dependencies are not
imported. Attribution is in `third_party_notices/m5unit-env.md`.

This comparison describes source and protocol behavior. A successful build does
not establish physical display, BLE, Accessibility, voice, pet-transfer, or
heap/stack acceptance; those still require validation on the intended Mac and
watch.
