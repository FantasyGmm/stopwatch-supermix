# C152 Usage / Host fusion — phase 1

## Source baseline and reuse boundary

| Input | Repository | Frozen commit |
| --- | --- | --- |
| Main firmware / macOS Agent | https://github.com/lukeluccc/stopwatch-supermix | `1c42aea1470efe6bbf1beb3462018077ed48258f` |
| Usage / host reference | https://github.com/wujin941005/stopwatch-hub | `2e611f347b48683c7d7212f40af67a8bc6ece4a5` |
| ESP-IDF | official full release ZIP via Espressif China mirror, v5.5.4 | `735507283d5b2f9fb363a1901172dbd9e847945d` |

The development branch is `feat/c152-usage-host-fusion`. The repositories are
separate checkouts; no Git merge is performed. Supermix keeps its MIT license.
Hub has mixed licensing: bridge/CC Island are MIT; PrintSphere-derived code is
FNCL-1.1. Only bridge usage/host collection concepts are adapted; the MIT notice
is retained in `third_party_notices/stopwatch-hub-usage.md`.

Supermix uses `main/apps/`, the shared `main/hal/`, Mooncake, LVGL and runtime
resource supervisors. Its `mac-codex-agent/` contains the task collector,
combined collector, secure BLE central, voice/input routing and atomic macOS
application installer. Hub generates a factory checkout from M5Stack UserDemo
V0.5 (`6b4aa125288b6fe9dca661f10159f6e1e5ee785c`), adds CC Island and PrintSphere,
and uses its own NUS/HTTP bridge and shared Wi-Fi service. Those infrastructure
and OTA changes are not imported.

## Dependencies and flash layout

Both firmware foundations declare these releases: Mooncake 2.3.3, Mooncake Log
1.5.0, Smooth UI Toolkit 2.12.1, M5GFX 0.2.19, ArduinoJson 7.4.3, LVGL 9.5.0,
M5IOE1 1.0.8, M5PM1 1.0.6, BMI270/BMM150 0.1.2. Supermix additionally locks each
source dependency to an immutable SHA in `repos.json` and registry dependencies
in `dependencies.lock`; these remain authoritative. Brookesia/XiaoZhi runtime
dependencies and checked patches remain unchanged.

| Partition | Supermix offset / size (retained) |
| --- | --- |
| NVS | `0x9000` / `0x10000` |
| PHY | `0x19000` / `0x1000` |
| factory app | `0x20000` / `0xDC0000` (13.75 MiB) |
| FAT storage | `0xDE0000` / `0x100000` |
| Assistant assets | `0xEE0000` / `0xC0000` |
| coredump | `0xFA0000` / `0x10000` |

Hub instead has two 6 MiB OTA slots at `0x20000` and `0x620000`, with storage at
`0xC20000` and coredump at `0xFF0000`. This layout is not used. The existing
supermix application gate stays `0x700000`, assets gate `0xA0000`, with the
original partition retained. Clock and the standalone Assistant are now excluded
from the default profile by later user decisions; Codex voice remains enabled.

## Status extension contract

Existing task/model/effort/alert fields and secure pairing, encrypted voice,
input actions and task-detail channels remain intact. New data travels in the
existing status JSON over BLE. No second
service or Wi-Fi owner is introduced.

The JSON limit remains 2048 UTF-8 bytes. Existing BLE framing has a six-byte
header, uses negotiated write capacity, and supports at most 255 fragments.
Telemetry is optional and trimmed before task data when the size budget is
exhausted. Authentication materials, cookies, prompts, tool output and source
text are not telemetry fields.

`telemetry_v: 1` enables optional objects:

- `usage`: `s` collection status; `q` quota status; `at` usage sample time; `qa`
  quota sample time; `h` and `w` remaining percentages; `hr` and `wr` reset UTC
  epoch seconds; `t` today's tokens; `c` estimated equivalent API USD; `cs`
  estimate coverage (`ok`, `partial`, `unavailable`); `day` local calendar date;
  optional `p` contains up to two provider summaries.
- `host` (retained protocol, disabled in normal runtime): `s`, `at`, `cpu`,
  `mem`, `disk` (percent), `up` and `down` (KiB/s).
- Collection statuses are `ok`, `stale`, `error`, `unavailable`. Unknown values
  are null/absent rather than 100% or zero.

Fresh partial usage uses `s=ok,cs=partial`: token totals are a known lower
bound, displayed with `>=`, and cost remains an explicitly partial API
equivalent estimate. This can occur when history traversal or session-tail
limits prevent full coverage, rates are missing, or prices are stale.
The Agent drops out-of-range reset timestamps and integers that exceed JSON's
exact range rather than making the watch reject otherwise valid telemetry.

Usage and quota expire separately after 120 seconds; host data expires after
15 seconds. New local token collection does not refresh quota age. Countdown
uses the watch clock synchronized by the original authenticated Agent flow.
Old Agents omit telemetry and new pages show unavailable. Old firmware ignores
new fields. Unknown extension versions disable telemetry while retaining task
monitoring. Reconnection requires fresh status data; disconnected data cannot
appear live.

Current page order is pinned microphone, Codex tasks, optional installed TRAE,
Usage. Host is hidden without a blank slot and its normal Agent worker/messages
are disabled. Short A/B presses navigate pages; long A submits once, long B is
push-to-talk and A+B is Home. Touch and global edge controls are unchanged.

## Build and operational boundary

The Mac Agent migration is feasible and implemented in `mac-agent-rust/`.
Rust provides the collectors, BLE central, cryptography, cache, pricing and
runtime coordination; the retained macOS Objective-C++ bridges provide AppKit,
Accessibility and the Typeless audio boundary. The release has no Python
runtime. Historical Python sources remain solely as a behavior baseline and
offline protocol oracles. The original install/release shell entry points
delegate to the Rust build.

Task polling uses a separate worker from quota, daily usage and public
pricing requests. Last-good samples expire independently. Delivery receipts
coalesce until ledger persistence succeeds; collection failures publish
unavailable status and disable stale action routes. Reconnection isolates
transport sessions and requires authoritative pet state before restoring cached
previews. Codex/TRAE legacy ledgers are imported read-only into Agent-owned state.

Prices refresh from public `models.dev/api.json` and an optional validated USD
JSON override. Invalid updates retain the last-good catalog, unknown models
remain unpriced, and cache/context rates are validated separately. Pricing and
statistics are computed on the Mac; the device receives display results.

All installation and build data resides below
`/Users/fantasy/tmp/m5stack-stopwatch-c152-fusion/`. Use the supplied
`scripts/install_idf_mirror.sh` and `scripts/build_fusion.sh`; they do not flash.
The original installer and release packaging entry points are retained.

The current menu/button/standby revision was compiled and deployed with user
approval. The user confirmed connection, page navigation and wake behavior;
see [validation results](validation.md). No tests were added or run. Actual
power draw and voice/desktop input actions were not measured or exercised in
this update. Future device writes require explicit approval, the exact connected
port and capacity-gated artifacts; a full-Flash backup is optional recovery
material, not a normal prerequisite.

## Differences from upstreams

| Capability | Supermix baseline | Hub reference | Fusion |
| --- | --- | --- | --- |
| Codex task state/reminders/model/effort | Yes | Different CC Island model | Original supermix behavior retained |
| Secure pairing / encrypted voice / input | Yes | Different transport | Original supermix behavior retained |
| Five-hour / weekly quota | Percentage | Percentage and resets | Percentage, resets, independent freshness |
| Today's tokens / API equivalent estimate | No dedicated page | Yes | Usage page; explicit estimate coverage |
| CPU / memory / disk / network | No dedicated page | Yes | Code retained; page, normal collection and messages temporarily disabled |
| Claude / OpenCode | No | Provider monitoring | Optional isolated modules, default off |
| Xiaozhi and existing launcher apps | Yes | Different app set | Standalone Assistant/Clock excluded from current profile; Codex voice and other utility apps retained |
| PrintSphere / Hub OTA | No | Yes | Excluded in phase 1 |

Actual validation results and artifact hashes are delivered separately after
the final build; this document does not claim hardware acceptance.

See [Rust Agent instructions](rust-agent.md),
[upstream differences](upstream-differences.md), and
[firmware artifact instructions](firmware-artifacts.md).
