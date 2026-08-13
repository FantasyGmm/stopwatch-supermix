# StopWatch Project Working Agreements

This file records project-specific decisions and lessons learned. Follow it for
all future work in this repository. When an older plan, spec, or README
describes a superseded workflow, this file takes precedence unless the user
explicitly changes the decision.

## 1. Optimize the Feedback Loop for the Risk

- Do not apply the heaviest delivery process to every change. Classify the
  change before acting.
- For a small, localized change such as a timing constant, shortcut modifier,
  label, packaging entry, or documentation correction:
  1. reproduce the exact symptom;
  2. inspect the smallest relevant code path;
  3. make one minimal change;
  4. run the directly related test;
  5. validate the real behavior immediately when the device/app is available.
- A real two-minute hardware or desktop experiment is often more informative
  than a broad regression suite. Prefer the real experiment as the primary
  feedback loop for hardware timing, Accessibility behavior, physical UI, BLE
  connectivity, and application shortcuts.
- After the real experiment succeeds, run focused regression tests for the
  touched seam. Do not run every Python test, every host stage, a clean firmware
  build, and a full review merely to validate a one-line local correction.
- Escalate to broader validation when the change crosses subsystem boundaries
  or affects authentication, replay protection, credential lifecycle, BLE
  services, partitions, persistent storage, firmware boot, resource ownership,
  packaging/signing, or release tooling.
- Use the full release gates for a release candidate or a genuinely
  cross-cutting change, not as the default inner development loop.
- Automated validation and real validation answer different questions. A test
  can prove the policy or event shape; only the real Codex UI can prove that a
  desktop action actually happened, and only the watch can prove physical
  timing and appearance.
- Do not mistake activity for evidence. Establish a red-capable feedback loop
  for the user's exact symptom before proposing a fix.
- Once the user confirms a live experiment works, stop expanding the solution.
  Perform only the necessary focused cleanup, verification, and documentation.

### Practical risk tiers

- **Tier 1 — local/low risk:** constants, exact mappings, labels, docs, module
  packaging lists. Use one focused test plus the shortest real check.
- **Tier 2 — behavioral:** gesture state machines, lifecycle logic, desktop
  policy, UI navigation. Use focused TDD, relevant host/unit tests, and a real
  acceptance check.
- **Tier 3 — protocol/safety/release:** cryptography, nonce/replay rules, UUIDs,
  credentials, partitions, Kconfig/dependencies, flashing, signing, or merges
  across long-lived branches. Use contract tests, host tests, clean builds when
  relevant, and end-to-end validation.

## 2. Work Autonomously Without Repeated Ceremony

- After a solution is agreed, write and self-review the design artifact without
  asking the user to review or reconfirm it, unless self-review exposes a
  material ambiguity that genuinely requires a decision.
- After an implementation plan is confirmed, execute it automatically using
  the recommended method. Do not ask the user to choose the execution method
  again.
- Once the user has approved a behavior or explicitly asked for a fix, do not
  repeatedly ask for confirmation of the same decision.
- Keep progress updates short and outcome-oriented. State what the latest
  evidence proves and what the next quick check is.
- Do not create a large design/plan/review chain for a trivial compatibility
  fix. Record material architectural decisions, but keep small fixes small.
- Do not claim success from a compile, a log line, or an Agent return value when
  the user-visible behavior can be checked directly.
- If the user is actively pressing the watch or observing Codex, prioritize
  that live loop over unrelated tests.
- Preserve user-owned work. The repository may contain unrelated dirty or
  untracked files, especially `.superpowers/`; never stage, rewrite, or delete
  them unless explicitly requested.
- Use `rg`/`rg --files` for discovery and `apply_patch` for manual edits.
- Before committing, inspect the exact diff and stage only files belonging to
  the current task.

## 3. Source of Truth and Evidence

- Inspect the current checked-out code, current installed Agent bundle, current
  Codex app resources/configuration, and current device logs. Do not assume that
  a design document or memory still matches the installed software.
- Distinguish these states explicitly:
  - repository source is correct;
  - packaged application contains the source;
  - installed application is the new package;
  - process restarted and loaded the new package;
  - transport accepted the action;
  - the user-visible desktop/device behavior actually occurred.
- A log such as `Codex input action ...: accepted` means the Agent accepted and
  attempted the action. It does not prove Electron/Codex handled it.
- Build success, flash success, boot success, BLE connection, and physical UI
  correctness are separate gates. Report them separately.
- Prefer primary runtime evidence: source code in the installed Codex
  `app.asar`, the user's actual `~/.codex/config.toml`, live Accessibility
  attributes, BLE logs, serial boot logs, and verified flash hashes.
- Never encode a current app assumption merely because a synthetic fixture
  passes. Add fixtures that match the real Accessibility or protocol shape.

## 4. Firmware Build and Flashing

- **Normal project flashing must not create or require a firmware backup.** The
  user explicitly chose this workflow. Do not reintroduce backup gates into the
  normal build/flash path.
- Historical recovery scripts and old documents may still mention full-flash
  backups. Treat them as optional recovery/migration tools, not prerequisites
  for normal flashing.
- Do not erase NVS, credentials, pairing state, storage, or the entire flash
  unless the requested task specifically requires it and the destructive scope
  is made explicit.
- Use the repository's capacity-gated candidate artifacts. Verify the exact
  bootloader, partition table, application, and assets offsets/sizes before
  writing.
- Detect and use the exact connected `/dev/cu.usbmodem*` target; do not guess a
  broad device path.
- After flashing, verify written hashes and then verify a new boot from serial
  output. Do not infer boot success from `esptool` success.
- For fast parameter experiments, prefer an incremental build when dependency
  state is already known-good. Use a clean build when validating dependency,
  Kconfig, reproducibility, or release state.
- Always set a real ESP-IDF path explicitly for host/build commands, for
  example `IDF_PATH=/path/to/esp-idf`, and source its `export.sh` when needed.
  A failure caused by a stale default IDF path is an environment failure, not a
  source failure.
- The project targets ESP-IDF 5.5.4. Keep component lockfiles and immutable
  dependency commits authoritative.
- `CODEC_UAC_SUPPORT` is a compatibility Kconfig symbol declared by this
  project with `default n`. It exists so the optional
  `espressif/usb_host_uac` dependency can be evaluated without breaking a clean
  configure. Do not enable or add that unused dependency just to silence the
  configuration error.
- Do not claim that host tests prove the ESP-IDF application compiles. Host
  tests, contract mode, clean firmware build, flash, and boot each prove a
  different layer.
- Generated directories such as `build/`, `build-unified/`,
  `managed_components/`, `components/`, and `third_party/` are disposable and
  should not be committed.

## 5. Codex Upper-Right Button Contract

- These semantics are global inside the watch Codex app only. They must not
  leak into Launcher, Assistant, Clock, or other apps.
- Current gesture contract:
  - one short click: submit after the 300 ms double-click window;
  - two short clicks within the window: emit exactly one forced-steer action;
  - hold for 300 ms: start push-to-talk voice;
  - A+B Home uses its separate 90 ms chord arbitration guard.
- The old 90 ms voice threshold was wrong: an ordinary deliberate click can
  last about 226 ms and was misclassified as voice. Keep chord arbitration and
  voice-hold timing as separate constants and concepts.
- The watch, not the Mac Agent, owns physical gesture classification because it
  sees button edges without BLE scheduling jitter. The Agent owns desktop
  meaning because it can inspect Codex state.
- Single/double-click actions are available at any time in the Codex app. They
  are not armed by a previous voice session and do not depend on Typeless,
  BlackHole, or audio readiness.
- A double click must never emit a single submit first. A second press that
  turns into a hold cancels the pending click and becomes voice.
- Wake consumption, A+B Home, app close, disconnect, and global input
  suppression must clear stale gesture state.
- Suppression while voice is active must produce exactly one voice cancel; it
  must not leave recording active after the gate resets.
- Do not infer suppression only from the instantaneous A/B sample. Preserve and
  consult the edge gate's suppression state so cross-release sequences cannot
  leave a stale double-click latch.
- An independent left-button press must not cancel an already completed right
  single click. Only the actual A+B chord/suppression state has Home priority.
- Consume the legacy right-button `GoNext` navigation inside the Codex app so a
  click does not both navigate the carousel and submit. Do not change the
  shared KeyManager behavior for other apps.
- Use wrap-safe unsigned elapsed-time arithmetic in gesture timing tests.
- Host tests should cover ordinary 226 ms clicks, the 299/300 ms boundary,
  single/double click, second-click-to-hold, A+B, independent A, wake, HAL
  suppression, active-voice cancellation, cross-release, disconnect, and timer
  wrap.

### Pinned microphone mode

- The page order is pinned microphone ← Codex status → TRAE. One right swipe
  from the Codex status page reaches the microphone switch directly.
- Pinned mode reuses the authenticated encrypted audio transport, but it is an
  external-microphone session, not push-to-talk. The Mac keeps the virtual
  microphone route fed and must not start or stop Typeless for this mode.
- Once enabled, capture continues without holding the upper-right button and
  survives page navigation and display-off.
- The latch is session-only. Home/app close, BLE disconnect, Agent/credential
  epoch change, unusable voice link, controller error, or reboot clears it and
  stops/cancels capture exactly once.
- While pinned, single/double right clicks still submit/steer, but a right hold
  must not create a second voice session.

## 6. BLE Services, Protocol, and Security

- Preserve the UUID allocation:
  - secure pairing service/control/TX: `...0006`, `...0007`, `...0008`;
  - input action control/TX: `...0009`, `...000a`.
- UUID conflicts are semantic merge conflicts even when Git shows no textual
  conflict. Check the complete service map whenever branches add GATT fields.
- Input actions and voice are independent subscriptions and lifecycles. Voice
  unavailability must not disable click actions.
- Input notification is available only to an authenticated, active Production
  connection. Pairing or unauthorized connections must not receive it.
- Use the transport's runtime production crypto/credential context. Do not
  restore removed static `device_context()` APIs or embed production keys.
- Every connection gets a fresh nonce and replay guard. Reject stale nonces,
  duplicate/out-of-order sequence numbers, malformed frames, booleans where an
  integer is required, and numeric values outside the exact protocol bounds.
- Keep protocol encoding/decoding mirrored between C++ and Python with fixed
  vectors. A change to one side requires updating and running both sides.
- Use bounded queues (currently eight slots on the Agent side and four pending
  actions on the watch controller where designed). Overflow drops safely; it
  must not become unbounded memory growth.
- Delivery is at-most-once. Commit replay state before desktop dispatch and
  never retry an ambiguous desktop result, handler exception, disconnect, or
  notification failure; retrying could submit twice.
- Disconnect/epoch/credential changes retire the old generation, clear queued
  actions, stop both subscriptions exactly once, and prevent late callbacks
  from entering the new session.
- Keep action cleanup bounded and idempotent. One failed cleanup must not skip
  voice or BLE cleanup.
- Key material belongs in Keychain on the Mac and in the authorized watch
  credential flow. Packaging must never contain keys, provisioning files, or
  private-key markers.
- Removing an authorized Agent rotates the shared key and advances the epoch.
  Preserve this lifecycle during merges or runtime refactors.

## 7. macOS Codex Accessibility Submission

- Fail closed. Never activate Codex, switch applications, guess a target, or
  send a global Return.
- Require the current foreground bundle to be exactly `com.openai.codex` and
  require Accessibility permission before accepting an action.
- `ACCESSIBILITY_REQUIRED` has priority over voice/driver readiness for click
  actions. Missing Typeless or BlackHole must not hide the actual reason a
  desktop click cannot be delivered.
- Match the real current-conversation composer, not an arbitrary editable text
  area with a nearby Send button. Current anchors include:
  - focused, enabled, settable `AXTextArea`;
  - exact supported English/Simplified-Chinese composer description;
  - `ProseMirror` class;
  - `_RichTextInput_ -> _ComposerLayoutBody_` ancestor shape;
  - stable `ChromeAXNodeId` tokens;
  - one unique enabled, visible Send/Queue/Steer button in a bounded subtree.
- The Chromium attribute literal is `ChromeAXNodeId`, not
  `AXChromeAXNodeId`. A fixture that repeats a misspelled production literal can
  make every test green while the real feature never works.
- PyObjC commonly returns bridged NSArray values, not Python `list`/`tuple`.
  Normalize safe non-string iterables for AX classes/children and fail closed
  on iteration errors.
- Reject explicitly hidden composers, ancestors, and buttons. Missing
  `AXHidden` is tolerated because transparent Chromium nodes may omit it.
- Empty Codex composers can expose placeholder text in `AXValue`; use the real
  placeholder subtree/shape instead of `value.strip()` alone.
- Real button semantics may be in `AXDescription` while `AXTitle` is empty.
  Fixtures must reflect that shape.
- Bound Accessibility traversal by depth and node count, deduplicate bridged AX
  wrappers by equality/stable identity rather than Python object identity, and
  break cycles.
- Prepare then revalidate immediately before dispatch: foreground PID, focused
  token, value, button token, visibility, and semantic must all remain stable.
- Normal Send/Queue/Steer uses `AXPress` on the verified button.
- Queue-to-forced-Steer uses Codex's alternate follow-up shortcut posted with
  `CGEventPostToPid` to the revalidated PID. For the current default
  Return-to-send composer mode this is **Command+Return**, not
  Command+Shift+Return.
- The alternate shortcut depends on Codex's `composerEnterBehavior`. Inspect
  the installed Codex resources and the user's live configuration before
  changing it; do not hard-code a remembered shortcut. If the user changes to
  a Command-based send mode, the opposite shortcut may require Shift again.
- `followUpQueueMode = "queue"` means the normal running action queues and the
  forced action must use the opposite/steer path. When the visible semantic is
  already Steer, force-steer uses normal verified `AXPress`.
- `CGEventPostToPid` reduces cross-application risk but cannot make
  Accessibility revalidation and same-process focus atomic. Preserve
  at-most-once behavior and never retry this residual race.
- Automated Accessibility probes must remain read-only. They may inspect and
  prepare, but must not call `AXPress` or post a real key. Skip the live probe
  for normal unsupported states (Codex background, no supported focus, empty
  composer) while deterministic fixtures enforce fail-closed behavior.

## 8. Mac Agent Packaging and Runtime

- Repository source being correct does not update an already installed Agent.
  Always check the installed bundle when live behavior disagrees with tests.
- Every production module imported at runtime must be listed in
  `install_launch_agent.sh`. The initial click implementation failed live
  because `codex_submit.py` and `input_action_protocol.py` were absent from the
  installed app even though they existed in the repository.
- After an Agent change:
  1. verify the installer packages the module;
  2. install the new bundle atomically;
  3. restart the process;
  4. verify the installed file contains the intended change;
  5. wait for `Agent started`, status ready, and BLE connected logs;
  6. perform the real action.
- Reinstalling/signing the Agent can trigger a macOS Keychain authorization
  prompt. A process may exist while startup is blocked before the first log
  line. Ask the user to choose “Always Allow” when this expected prompt appears,
  then verify startup from logs rather than assuming it is running.
- Preserve atomic app publication, validation, rollback of the previous app
  bundle on installer failure, universal2 Mach-O checks, code signing, and the
  self-contained runtime smoke test.
- Do not confuse the installer's temporary rollback copy of the Mac app bundle
  with a firmware backup. Atomic desktop installation may keep a temporary app
  rollback during publication; normal watch flashing still uses no firmware
  backup.
- Runtime readiness changes must not tear down the independent input action
  channel unless authentication/session ownership changes.
- An action handler exception or `False` result is consumed once, logged, and
  not retried.

## 9. Merge and Conflict Lessons

- Resolve conflicts by behavior, not by selecting “ours” or “theirs” blindly.
- During the global-button merge, both branches independently used UUIDs 0006
  and 0007. The correct resolution preserved secure pairing at 0006/0007/0008
  and moved input actions to 0009/000a.
- A branch also referenced a removed static production crypto API. The correct
  resolution injected the transport's dynamic production crypto into the
  input controller; reintroducing the removed API would have silently weakened
  the security design.
- Preserve unrelated new behavior from both branches, such as secure pairing,
  Agent panels, navigation, status generations, and lifecycle cleanup. A file
  that compiles after conflict resolution may still have lost one branch's
  state machine.
- After resolving a protocol/lifecycle merge, run exact contract lists and real
  host compile/run stages. Fixed stage counts and expected source lists must be
  updated together when stages are intentionally added.
- Check all mirrors of a shared constant: firmware, Agent, tests, validation
  scripts, and documentation.

## 10. Testing and Validation Discipline

- TDD is useful when the test can reproduce the real bug at the correct seam.
  First watch the focused test fail for the expected reason, then make the
  smallest production change, then watch it pass.
- Do not substitute a shallow unit test for a real integration symptom. For
  example, testing that key-event flags were assigned did not prove Codex
  accepted the shortcut; live Codex validation exposed the wrong modifier.
- When a human action is unavoidable, turn it into a tight loop: start a
  timestamped log tail, tell the user exactly one action to perform, correlate
  the event, and immediately ask whether the visible result occurred.
- Prefer focused commands during iteration. Examples:
  - desktop submit: `test_codex_submit.py`;
  - input wire format: `test_input_action_protocol.py` plus the C++ vector test;
  - installer packaging: `test_install_script.py`;
  - button timing: the voice/gesture host tests;
  - firmware contract: the specific contract module.
- Use `run_unified_offline_validation.sh --contract` to validate the declared
  stage structure without pretending it executed behavior.
- Use `--host-tests-only` with a valid `IDF_PATH` to prove real host
  compile/run stages.
- Run full Python discovery and a clean ESP-IDF build when preparing a release,
  modifying shared infrastructure, or after a large merge. Do not use them as
  the first check for every local edit.
- Expected exception logs inside fail-closed tests are not test failures; rely
  on the unittest result and exit code.
- Keep validation documentation honest. When tests or stages are added, update
  hard-coded counts, exact compile/run lists, and current evidence together.
  Stale evidence is worse than no evidence.
- A live test must account for legitimate UI states. An empty running composer
  may expose Stop/UNKNOWN and should safely produce `prepare=None`; do not make
  the suite flaky by requiring Send/Queue/Steer in that state.
- `git diff --check`, syntax/compile checks, focused tests, installation, and
  real acceptance are the normal closing sequence for a small desktop fix.

## 11. UI, Lifecycle, and Hardware Observations

- Physical display acceptance cannot be inferred from screenshots, host tests,
  or successful flashing. Inspect the actual AMOLED when layout, typography,
  color, animation, brightness, or burn-in protection changes.
- Screen wake is an input-consuming state transition. It must not leak the wake
  press into app navigation, submit, voice, or Home behavior.
- A+B Home and app close are lifecycle boundaries. Pending gestures, voice,
  BLE input state, panels, and per-page state must be cleared deterministically.
- Assistant shutdown is asynchronous. Keep the UI responsive while closing and
  let the runtime/resource supervisors release audio, network, bindings, and
  tasks; do not force a reboot or duplicate resource ownership.
- Shared KeyManager/HAL behavior affects every app. Prefer a Codex-local policy
  filter when changing only Codex semantics.
- Preserve NVS settings and authorized Agent state during ordinary application
  updates. Do not use erase as a routine way to make a test pass.
- Compact status payloads, negotiated BLE frames, and bounded work per loop are
  important on the ESP32-S3. Avoid unbounded traversal, queues, allocation, or
  polling tasks in UI and transport paths.

### Round-screen UI and gesture ownership

- For every UI design task, default to creating browser-based visual mockups;
  do not ask whether the user wants a visual companion first.
- Present multiple meaningfully different UI directions for the user to choose
  from before converging. Avoid cosmetic variants that preserve the same
  hierarchy and interaction model.
- Design for the circular safe area, not the nominal rectangular framebuffer.
  Long labels, bottom buttons, slider endpoints, and touch targets that fit in
  a desktop preview may still clip or become unreachable on the watch.
- Reuse already physically approved geometry and typography where possible.
  A one-off smaller control is not automatically safer than the established
  round-screen component.
- Treat physical appearance as an acceptance criterion. Inspect actual
  centering, clipping, contrast, glow, animation, and legibility on the AMOLED.
- Never equate successful compilation, coordinate-string contract tests, or a
  browser mockup with a faithful firmware UI. Before calling watch UI complete,
  capture or inspect the running AMOLED at the same state as the locked design
  and compare it region by region: geometry, type size/weight, color, icon,
  control state, and circular-safe-area clipping.
- When implementing from an approved design, keep that exact artifact as the
  sole visual source of truth. Do not silently redesign, substitute a nearby
  symbol, compress spacing, or shrink typography to fit available assets.
- Missing fonts or icons are implementation blockers, not permission to
  degrade. Add or generate the required glyph asset first. In particular, do
  not use `LV_SYMBOL_AUDIO` as a microphone: it is a music-note glyph and may
  also be absent from the selected firmware font.
- A UI verification must cover the actual rendered states, not just object
  presence in source: default/off, active, unavailable/error, and navigation
  position. If an automatic framebuffer capture is unavailable, use a named
  physical screenshot packet and user confirmation before accepting fidelity.
- The pinned-microphone miss is the concrete warning: the approved centered
  beacon specified a large Chinese title, readable state, and central microphone
  glyph, while the first firmware reused a 14 px Chinese font and an unverified
  audio symbol. Tests and flashing passed, but the visual result did not. This
  class of mismatch must be caught before flashing, not after user inspection.
- Decide gesture ownership before interpreting thresholds. A global edge
  brightness/volume recognizer must not steal a drag from an already visible
  Agent panel or child page.
- Gesture models own intent and direction; LVGL views own pixel position and
  animation. Keep those responsibilities separate so model tests do not depend
  on rendering.
- Direct-manipulation panels should follow the finger after a small dead zone,
  clamp to valid endpoints, and settle with one animation. Do not rebuild the
  panel tree on every open/close release; unnecessary cleanup/re-render causes
  visible flashing.
- Cancel old animations and restore global gesture ownership on close, Home,
  wake, or app destruction. A hidden panel must not leave the page in a mode
  that blocks brightness or volume.

### Global display protection

- Display protection is one global service, not a collection of per-app
  timers. Applications may publish keep-awake reasons or request an immediate
  screen-off; they must not each own brightness/protection policy.
- Protection dimming is temporary and must never overwrite the user's saved
  brightness.
- Automatic dim and automatic screen-off are independent stages. A disabled
  stage never fires, but its last valid time choice is retained so re-enabling
  restores a meaningful value.
- When both stages are enabled, screen-off must remain strictly later than dim.
  Sanitize persisted values before the runtime state machine sees them.
- Settings-page edits are drafts. Leaving without OK changes neither runtime
  policy nor NVS; OK applies and persists one complete sanitized configuration.
- Keep NVS namespace/key names within ESP-IDF's 15-character limit.
- Power insertion, Alarm activation, Assistant interaction, and a newly added
  keep-awake reason can wake the screen. Stopwatch and other existing
  keep-awake owners must continue to work together rather than overwrite one
  another.
- PMIC polling belongs in one bounded background owner with a thread-safe
  cached state. Log transitions and rate-limited errors, not every 250 ms
  sample.
- If power and touch arrive in the same loop, restore the display once and
  still consume an input that began while off. A power transition must never
  synthesize an application gesture.

### Voice and audio runtime

- Voice transport must be UI-independent. Closing or rerendering the Codex app
  must not race the BLE/audio worker through raw view-owned pointers.
- Snapshot active voice link/session state atomically before asynchronous work.
  Serialize start, stop, cleanup, and delivery across Agent/key epochs so an
  old worker cannot use a new session.
- Publish capture stop before waking TX cleanup. Otherwise a worker can observe
  stale active state and send after release.
- Buffer bounded audio during route startup instead of dropping the first
  syllable while BlackHole/Typeless becomes ready.
- Bound congestion using elapsed time and queue size rather than relying on a
  fixed number of scheduler iterations. BLE scheduling jitter is normal.
- Microphone/I2S channel assumptions must match the actual mono capture path;
  format code that compiles can still produce silence or corrupted speech.
- A successful voice start owns a matching stop/cancel. Every Home,
  disconnect, epoch change, app close, route error, and Agent quit path must
  restore the previous default input and release audio resources exactly once.
- Cleanup deadlines are bounded. A stuck worker should fail the operation and
  surface evidence rather than block shutdown indefinitely.
- Validate voice with intelligible real speech, paired start/stop behavior,
  previous-input restoration, reconnect, and Assistant recovery. Packet tests
  alone cannot establish usable audio.

### Secure pairing and persistence

- Pairing is explicit and physical: show the same verification code on Mac and
  watch and require approval on the watch before authorization.
- A pending new pairing must not interrupt the currently active authorized
  Agent. Keep active and pending connection state separate.
- Store a bounded authorized-Agent registry (currently up to eight) and keep
  active-agent selection, names, keys, and epochs consistent across reboot.
- NVS serialization/deserialization must be bounded and avoid large stack
  objects. Persistence code has previously failed from stack pressure even
  though the protocol logic was correct.
- Sign mutable identity fields such as Agent names before the watch stores or
  displays them.
- Recovery and rekey logic is part of the pairing contract: offline authorized
  Agents receive the current epoch after their next signed connection, while
  removed Agents cannot regain access with stale credentials.

### Codex status, alerts, and classification

- Keep collection, classification, reduction, persistence, transport, and UI
  presentation as separate responsibilities. A wording/parser change should
  not require firmware rebuilding or flashing.
- Structured unfinished-plan and input-request evidence has priority over text
  heuristics. For text, use a bounded scan of the complete final response;
  looking only at the last sentence misses real handoffs.
- The classifier intentionally favors high recall for actionable Pending
  requests, while excluding quoted/example questions, rhetorical questions,
  resolved confirmations, and questions attributed to someone else.
- Parse evidence with source offsets, clauses, ownership, polarity, and bounded
  action context. Keyword presence alone is not enough for completion or
  Pending decisions.
- `task_started`, `task_complete`, and `turn_aborted` are different lifecycle
  events. Repair stale Running records when an abort arrives and do not infer
  Completed from the absence of a final assistant sentence.
- Preserve the latest current-day state for each task. Pending has display and
  chime priority; when Pending is empty, Running may rotate; Completed is the
  idle fallback.
- Persist classifier version/migration state even when no visible alert
  changes. Reclassify stored recent evidence when semantics change, but never
  replay an already-notified Pending chime merely because the Agent restarted.
- Validate classifier changes against a bounded real rollout corpus and report
  counts/IDs, not private prompt/response content.
- Watch payloads contain rendered status fields, not OAuth credentials, source
  code, prompts, or full responses.

### Time synchronization and status transport

- On every successful Agent BLE connection, send time synchronization before
  the first status payload.
- Capture Unix milliseconds and the effective UTC offset immediately before
  serialization. Validate time and offset ranges before changing any watch
  state.
- Apply the time to the system clock, persisted fixed-offset timezone, and
  RX8130 RTC as one logical operation. Reconnect refreshes daylight-saving or
  travel offset changes; the first version does not pretend to store an IANA
  timezone with future transitions.
- BLE is the primary status transport. Wi-Fi/WebSocket fallback starts only
  after the configured BLE wait and must not alter the authenticated input or
  voice security model.
- Status framing must respect negotiated MTU/capacity and remain backward
  compatible where declared. Do not increase payload density without checking
  on-device parsing, scheduling, and UI cadence.

### Reproducible dependencies

- `repos.json`, immutable source commits, `dependencies.lock`, and checked
  patch hashes are the source of truth. Do not quietly build against whatever
  happens to exist in a developer's neighboring checkout.
- `fetch_repos.py --check` is read-only validation. Fetch/update operations are
  separate mutations and should be intentional.
- A clean-checkout build must be able to resolve tracked dependencies without
  requiring a complete unrelated Xiaozhi repository.
- A patch that no longer applies or whose hash changes is a hard signal to
  reconcile the upstream dependency; do not silently skip it.
- Record reproducible build size and artifact hash only after a fresh build.
  Do not present a dirty incremental artifact as clean-checkout evidence.

## 12. Definition of Done by Change Type

### Small desktop compatibility fix

- Exact live symptom reproduced.
- Minimal source change and focused regression test pass.
- Installed Agent bundle verified and restarted.
- BLE log shows the intended semantic action.
- User confirms the visible Codex result.
- Relevant diff/syntax checks pass.

### Small firmware timing/behavior fix

- Exact edge sequence reproduced in the focused host test.
- Relevant host tests pass.
- Incremental firmware build succeeds and size gate passes.
- Direct flash writes verified artifacts without making a firmware backup.
- Boot log and one physical gesture confirm the change.

### Protocol, security, or release change

- Python/C++ fixed vectors and malformed/replay cases pass.
- Lifecycle/generation/disconnect cases pass.
- Contract and real host stages pass.
- Clean ESP-IDF build and capacity gates pass when firmware is affected.
- Package/sign/install checks pass when the Agent is affected.
- Real end-to-end device-to-Codex acceptance passes.
- Documentation and explicit validation counts/lists are updated.

## 13. Communication at Handoff

- Lead with what now works and the evidence that proves it.
- State whether firmware was rebuilt/flashed and whether the Agent was
  reinstalled; do not leave the user guessing which side changed.
- Mention material blockers honestly and distinguish environment failures from
  source failures.
- Report any intentionally untouched user files or unrelated dirty state.
- Avoid a long process narrative unless it helps diagnose a remaining issue.
  For small changes, a concise root cause, change, real validation result, and
  commit/file reference are enough.

## 14. Formal Release Convergence

- The currently deployed watch firmware is a release candidate: release/size
  optimization, INFO maximum logging, assertions enabled, and
  `CONFIG_STOPWATCH_DEBUG_COMMANDS=y`. It is not a traditional Debug build,
  but it still contains development diagnostics.
- Treat the user's phrase “简化成正式版” (or an equivalent request to produce
  the formal production build) as the trigger for this established scope; do
  not ask the user to redefine it.
- Disable USB development control commands and ensure no debug command parser
  or control surface remains reachable in the production image.
- Remove temporary UI labels, diagnostic markers, one-off instrumentation,
  performance timestamps, counters, and high-frequency lifecycle logs that
  were added only for development or fault isolation.
- Reduce or remove nonessential INFO logs and keep only low-frequency,
  privacy-safe operational evidence needed to diagnose boot/reset/crash,
  authentication, BLE connection, storage migration, and unrecoverable voice
  or transport failures.
- Retain firmware/protocol/build identifiers, security validation, recovery
  state, NVS schema/migration markers, critical error codes, and other data
  required for compatibility, safe failure, or field diagnosis.
- Before deleting a label, command, or log, search for firmware, Mac Agent,
  test, script, validation, and compatibility consumers. Update or remove
  development-only consumers deliberately; do not break production contracts.
- Produce the formal build from the production configuration, then run the
  complete host/contract suite, clean release build and capacity gates,
  package/hash checks, boot validation, and physical smoke acceptance before
  flashing. Report exactly which diagnostics were removed and which minimal
  field diagnostics remain.
