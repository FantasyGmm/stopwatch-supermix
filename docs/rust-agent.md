# Rust macOS Agent

`mac-agent-rust/` contains the native Rust menu bar Agent. The historical
`mac-codex-agent/` Python implementation remains in the repository as a
behavioral baseline; its documented install and release shell entry points now
forward to the Rust implementation. The shipped `.app` contains the universal2
Rust executable and does not bundle Python source or a Python runtime. Python
is still used by some firmware development tools.

For the 1.0.9 connection fix, installation steps, system-language menus, and
persistent logs, see [Mac Agent 连接修复与更新](mac-agent-connection.md).

## Build and package

On an Apple Silicon Mac, install Rust with `rustup` and install Xcode Command
Line Tools. The Agent package script builds both `aarch64-apple-darwin` and
`x86_64-apple-darwin` targets, combines them into a universal2 binary, signs
the app (ad-hoc by default), and checks the packaged runtime. Cargo dependencies
are locked by `mac-agent-rust/Cargo.lock`.

The firmware toolchain and firmware build are separate from the Agent package:

```sh
./scripts/install_idf_mirror.sh
python3 ./fetch_repos.py
./scripts/build_fusion.sh --clean
```

These commands install the pinned ESP-IDF tools and build the firmware candidate
with its capacity gates. They do not flash the watch. The firmware build uses
Python as part of the ESP-IDF toolchain; this does not add Python to the Agent
app bundle. Its default build output is `build-unified/`.

Build a macOS release image into the assigned output directory:

```sh
./mac-codex-agent/build_release.sh \
  --dest /Users/fantasy/tmp/m5stack-stopwatch-c152-fusion/outputs-package
```

For packaging without executing the built binary, set
`STOPWATCH_SKIP_RUNTIME_TESTS=1` before the release or install command. This
delivery uses that mode following the user's instruction to stop tests.

The command writes the DMG, `release.json`, and `SHA256SUMS.txt` below
`outputs-package/latest/`. It builds and validates the package; it does not
install or launch the app. If the release input fingerprint changed, it may
advance `mac-codex-agent/VERSION`. The historical entry point forwards to
`mac-agent-rust/scripts/build_release.sh`.

To build an app bundle without installing it, use an explicit prefix:

```sh
./mac-codex-agent/install_launch_agent.sh \
  --prefix /Users/fantasy/tmp/m5stack-stopwatch-c152-fusion/outputs-app
```

This creates
`outputs-app/StopWatch Codex Agent.app`. The same entry point with no options
builds and installs to `~/Applications`; it does not launch the app or create a
login item. Open the installed app manually. `--dest /path/StopWatch Codex Agent.app`
is an alternate isolated build-only destination.

## Command-line modes

Run commands through Cargo with the native runtime feature enabled:

```sh
cargo --config mac-agent-rust/.cargo/config.toml run --locked --manifest-path mac-agent-rust/Cargo.toml \
  --features native-runtime -- --help
```

The supported diagnostic and conversion modes are:

```sh
cargo --config mac-agent-rust/.cargo/config.toml run --locked --manifest-path mac-agent-rust/Cargo.toml \
  --features native-runtime -- --self-test

cargo --config mac-agent-rust/.cargo/config.toml run --locked --manifest-path mac-agent-rust/Cargo.toml \
  --features native-runtime -- --collect-once \
  --codex-root /absolute/path/to/synthetic-codex \
  --state-dir /absolute/path/to/temporary-agent-state \
  --trae-root /absolute/path/to/synthetic-trae

cargo --config mac-agent-rust/.cargo/config.toml run --locked --manifest-path mac-agent-rust/Cargo.toml \
  --features native-runtime -- --convert-pet \
  /absolute/path/to/input.gif /absolute/path/to/output.cxp
```

`--self-test` uses generated in-memory fixtures and does not inspect the home
directory, Codex settings, credentials, Bluetooth, Accessibility, audio, or a
watch. `--collect-once` requires explicit input roots and an explicit writable
state directory. Point it only at synthetic fixtures: it reads task and
telemetry files under those roots, writes its cursor/alert state under
`--state-dir`, prints the resulting status snapshot, and also samples the
current Mac's host metrics. Keep `auth.json` out of fixture roots to prevent a
credential-backed quota request. Do not point it at a real Codex or Trae profile.
`--convert-pet` reads the named GIF and writes a CXP1 package at the named
output path; it does not connect to a watch.

With no arguments on macOS, the executable opens the menu bar Agent. The app
keeps the existing secure BLE pairing and device selection, Codex task/model/
reasoning-effort status, TRAE status, Typeless voice and pinned-microphone
flows, task input controls, pet preview/transfer, and native macOS preferences.
Accessibility permission is needed for guarded Codex composer actions. The
Agent does not activate Codex or send a task into an unrecognized or non-editable
composer.

The Codex submenu shows only model, effort, quota and task counts, not individual
conversations. On the watch, short A/B presses move to the previous/next page;
holding A for 300 ms submits/queues the current composer once, and holding B
continues push-to-talk. Releasing a hold does not turn the page. A+B returns to
Launcher. These local bindings do not change the authenticated input protocol
or the Agent's guarded submit implementation.

## Local data and telemetry

During a normal launch the Agent reads Codex state under `~/.codex` and TRAE
state under `~/Library/Application Support/Trae`. It reads the Codex config,
SQLite state databases, and session JSONL files needed for model/effort,
conversation status, token usage, and quota. The input files are read-only. It
imports legacy TRAE cursor/ledger files from an explicitly supplied `~/.trae-cn`
directory on normal launch; diagnostic fixture collection has no implicit home
directory migration.
It
stores its own cursors, alert ledger, public-price cache, preferences, and pet
cache under `~/Library/Application Support/StopWatch Codex Agent`.

During a normal launch, daily Codex tokens preferentially use CC Switch's
`~/.cc-switch/cc-switch.db` when that database exists. The Agent opens it
read-only and reads only today's `codex_session` request statistics, using the
Mac's local-day boundary. It does not add proxy records or historical rollups,
read provider settings, or trigger another API request. Cache reads already
included in input are not counted twice; cache writes and reasoning output
follow the stored input-token semantics. The supported database schema is 20
(observed with CC Switch 4.0.13); incompatible schemas produce an error and stale
data rather than silently switching totals to a different source. The query has
a two-second deadline and a 250,000-request bound. Request-limit truncation
retains the existing partial/lower-bound presentation; query failures or
timeouts invalidate the cached result instead of showing a fresh zero.

Keep CC Switch running to continue importing activity. A successful database
read reports the statistics currently imported by CC Switch, not a guarantee
of its importer health or account-wide OAuth coverage. An old request timestamp
alone is not treated as stale, since an idle day is valid. When no CC Switch
database exists, the bounded native session collector remains available.
`--collect-once` never discovers the real CC Switch database implicitly.

Costs are recalculated on the Mac for each imported request, including its
context tier and separate fresh-input/cache/output buckets. The Agent does not
reuse CCS's stored dollar total or its custom multiplier, so its estimate can
differ from the CCS usage page. Public model rates come from
`https://models.dev/api.json`; the
Agent refreshes its public catalog every six hours with an eight-second request
timeout and an 8 MiB response limit. A cached public rate older than seven days
makes the estimate `partial`. A model with no known rate is left unpriced rather
than guessed. This is an equivalent API-price estimate, not an account bill.
Incomplete token scans are displayed as lower bounds (`>=`). Missing or stale
prices mark the cost coverage as partial without making a complete token count
a lower bound. The model-detail extension keeps those two states separate.
An optional explicit `STOPWATCH_PRICING_JSON` override uses version 1 of the
documented USD model format in `mac-agent-rust/model-pricing.json`.

The bundled JSON is an offline fallback when no usable public-price cache is
available. Its 2026-10-16 models.dev snapshot includes GPT-6.1 Sol, GPT-6 Astra /
Sol / Luna, and GPT-5.6 Sol / Terra / Luna plus the `gpt-5.6` alias. These entries
use standard API USD rates per million tokens, including cache reads/writes and
the catalog's exact 272,000-input-token context tier. The source and check date
are recorded in the JSON; rates are not embedded in Rust code. The three earlier
GPT-5 entries remain unchanged for historical-session compatibility and carry
their earlier snapshot provenance. The fallback is not a subscription tariff or
Fast/Ultrafast rate table, and it does not replace an existing cache or explicit
user override.

Quota collection first uses recent local Codex session events. If those do not
provide a usable quota sample, it may read the access token from Codex's local
`auth.json` and send it as a bearer credential in an HTTPS GET to
`chatgpt.com/backend-api/wham/usage`. The token is used for that request only:
it is not written to Agent state, logged, or sent to the watch. The Agent does
not refresh or modify Codex credentials or cookies.

Claude and OpenCode summaries are optional and disabled by default:

* Setting `STOPWATCH_CLAUDE_USAGE_TOKEN` enables a read-only HTTPS GET to
  Anthropic's OAuth usage endpoint. The supplied token is used only as the
  request bearer credential; the Agent does not discover, refresh, or write
  provider credentials.
* Setting `STOPWATCH_OPENCODE_USAGE_JSON` enables a read-only import from that
  JSON file. It is limited to 4 KiB and must contain exactly `at`, `s`, `h`,
  `w`, `hr`, and `wr`. The OpenCode integration does not make a live quota
  request or modify the imported file.

The watch receives the existing Codex/Trae status plus the compact
`telemetry_v: 1` display fields: quota percentages and resets, today's token
count and estimate coverage, freshness
statuses, and optional provider quota summaries. Credentials, cookies, prompt
text, source files, raw session logs, and tool output are not telemetry fields.
Codex task details continue to use the existing authenticated and encrypted
detail channel. Unknown values remain unavailable/null; they are not converted
to full quota or zero.

Host is temporarily disabled in normal menu bar runtime: no Host worker is
created, the snapshot and encoded messages omit `host`, and the watch hides its
Host page. The collector/codec implementation remains available for restoration
and explicit legacy diagnostic callers; `--collect-once` retains its documented
host-sampling behavior.

Usage and quota workers wait 60 seconds after collection, independently of task
collection. Usage and quota samples expire after 120 seconds; optional provider
samples expire after 120 seconds. New local usage
collection does not extend quota freshness. The watch must get a fresh status
after reconnection before showing the data as live.

### Weekly quota and model details

The Usage page displays the 7D quota and reset countdown, today's total tokens
and API-equivalent USD estimate, and one model's details at a time. Tap the
on-page arrows to switch models; short A/B presses switch whole app pages, while
touch swipes retain their meanings. Short project names on the Codex home page are centered;
long names keep single-line scrolling.

Model details include fresh input, output, cache reads and cache writes, the
model's token total and estimated cost, and estimated TPS / median TTFT with
valid-sample counts. CC Switch session timing is inferred from local log events,
so it is shown as an estimate. No performance samples means unavailable, not
zero. The rollout fallback retains token and pricing details but does not invent
timing values. Daily stats use one source and never add proxy traffic to imported
session totals.

The 5H values remain in the existing Agent/quota protocol for older clients, but
are no longer displayed on the Usage page. No quota request is added. See
[Usage details and wire fields](usage-details.md) for formulas, field bounds,
sample coverage, and compatibility.

Task replay accepts JSONL rows up to 16 MiB. Oversized rows are never parsed as
truncated completions; that rollout remains in classifier retry while other
threads continue. After an oversized row, a further 2 MiB scan can find its
newline and subsequent records. Larger remainders remain unavailable to the
classifier until the source changes. Task detail sent to the watch remains
bounded at 12 KiB, regardless of reply size.

Incremental task collection scans 2 MiB per poll and persists the start of an
unfinished row separately from the scan position. A complete row up to 16 MiB
is processed when its newline is reached, including rows larger than one scan
chunk. Rows exceeding 16 MiB are discarded across bounded scans until their
newline, so later lifecycle events are not permanently blocked. An unfinished
row is never interpreted as a completion. Existing stuck cursors resume without
clearing the alert ledger or pairing state.

### BLE encoding and refresh cadence

Task collection and the BLE send tick each retain their two-second period.
Changing the wire encoding does not increase collection or request frequency.
CC Switch must also import the source activity before a later Agent read can
show it.

After authentication, new peers negotiate MessagePack on the existing status
RX characteristic. Old firmware without READ uses JSON, and new firmware still
accepts old Agents' JSON. A READ-capable peer with a failed or malformed
capability descriptor produces an explicit connection error rather than a
silent downgrade. The selected codec and write capacity appear once per
connection in the persistent log.

To investigate a delayed update, compare the `Status synchronization checkpoint`
lines in `~/Library/Logs/stopwatch-codex-agent.log`. `phase="collected"` records
the task revision/counts and the usage sample time/token total from collection;
`phase="sent"` records the same summary after all public status GATT writes have
been acknowledged. A sent checkpoint does not confirm on-device parsing or
rendering. Checkpoints contain no task titles or replies, log changes at most
once per ten seconds, and otherwise emit a one-minute heartbeat. Session errors
are also logged before reconnecting, rather than losing the worker's error.

Status, inventory, telemetry, time sync and reminder events use the negotiated
encoding. Encrypted task details send nonce/ciphertext as native binary instead
of hex/Base64; authentication and canonical AAD are unchanged. The WebSocket
fallback, legacy public APIs and historical Python client keep JSON. See
[binary status protocol](ble-status-codec-v2.md) for framing and bounds.

## Installed task-Agent pages

Normal runtime checks LaunchServices every 45 seconds for installed Codex and
TRAE applications, validating the registered bundle ID and existing app path.
Standalone Codex CLI installations also count: discovery checks executable files
on PATH and common Homebrew / user-local paths, without executing them or
counting binaries embedded inside another app.
It does not infer installation from old profiles or a running process. Installed
but stopped applications retain their offline entry; absent applications are
hidden from the Mac menu and watch carousel. Until discovery succeeds, existing
legacy visibility is preserved.

An optional version-1 `agent_inventory` status message travels over the existing
BLE status channel before the normal status messages. Older firmware ignores
its explicit unknown source; older Agents without a manifest retain the watch's
Mic / Codex / Usage layout. New firmware conditionally inserts supported
Agent pages without blank slots. TRAE implementation remains in the source.

## Device-write boundary

Firmware build and Agent package commands above do not write to the watch. A
direct firmware write for this delivery has explicit user authorization; that
authorization is scoped to this operation. A complete 16 MiB Flash backup is
not required. Use the exact detected connected serial port and the
capacity-gated candidate:

```sh
./scripts/flash_unified_candidate.sh --port /dev/cu.usbmodemXXXX
```

The write script checks application and asset capacity, then writes only the
bootloader, partition table, application, and assets at the offsets in
`portable_flash_map.json`. It does not erase the entire Flash or write NVS, authorized-Agent
credentials, storage, or coredump. After writing, verify the readback hashes for
all four images and confirm a fresh firmware boot in serial output. Record the
write, readback, and boot results separately.

A full-flash backup remains available as optional recovery material, not a
prerequisite:

```sh
./scripts/backup_watch_flash.sh \
  --port /dev/cu.usbmodemXXXX \
  --output /path/to/new-private-backup/watch-full-flash.bin
```

The backup must be exactly `0x1000000` bytes; the script writes a SHA-256 file
beside it. A full image may contain credentials, so keep it private and choose
an unused output path. The script removes an existing `.sha256` companion before
reading. BLE, display, menu interaction, voice, pet transfer, and memory
high-water measurements still require device acceptance.
