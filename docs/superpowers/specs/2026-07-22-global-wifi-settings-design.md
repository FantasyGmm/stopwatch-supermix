# Global WiFi Settings Design

Date: 2026-07-22
Status: Approved for implementation

## Purpose

Move WiFi configuration out of the Assistant-only flow and make it a global
Settings capability. A WiFi network configured once should be saved in the
shared WiFi credential store and be available to Assistant, Codex, Badge, and
future networked apps through the existing shared WiFi runtime.

The global feature owns two separate responsibilities:

- Settings owns credential management and the explicit pairing entry point.
- `SharedWifiService` owns runtime STA/AP arbitration for every app.

An app receives connectivity only while it holds a shared WiFi lease. Closing
Assistant or any other networked app releases that app's lease; the saved
credentials remain global, but the app's active connection ownership does not.

The feature deliberately reuses the Xiaozhi WiFi configuration AP web flow for
adding new networks. The watch will not implement an on-device password keyboard
in this iteration.

## Current Context

The firmware already includes these relevant pieces:

- `AppSetup` and `SelectMenuPage` provide the Settings app menu structure.
- `78/esp-wifi-connect` provides `WifiManager`, `WifiStation`,
  `WifiConfigurationAp`, and `SsidManager`.
- `SsidManager` persists credentials in the `wifi` NVS namespace.
- `WifiStation` scans nearby APs and matches saved SSIDs. Upstream 3.1.3 orders
  candidates by RSSI, not by `SsidManager` order, so the firmware applies a
  fail-closed generated-source overlay that gives saved-list position priority
  and uses RSSI only between BSSIDs with the same saved priority.
- `WifiConfigurationAp` exposes the existing captive portal flow, including
  `/scan`, `/saved/list`, `/saved/set_default`, `/saved/delete`, and save
  handling.
- `SharedWifiService` and `ResourceSupervisor` arbitrate STA and AP ownership
  between runtime users.

The design keeps that ownership model. Settings becomes another controlled
consumer of the same WiFi service rather than bypassing it.

The existing generic `SharedWifiService::acquireAp()` path is not the Xiaozhi
configuration portal. It grants exclusive AP ownership to Badge, whose code
starts and stops its own raw AP sockets. Settings therefore requires a distinct
config-AP session that starts and stops `WifiManager::StartConfigAp()` and
`WifiManager::StopConfigAp()` under the same resource arbitration.

## User Experience

Settings gains a new top-level item:

```text
Settings
  Device
  Time & Date
  WiFi
  Firmware
```

Opening WiFi shows a dedicated WiFi page rather than a one-button worker. The
page shows:

- connection state: `Connected`, `Connecting`, `Not connected`, or `Pairing`;
- current SSID when connected or connecting;
- IP address and RSSI when connected;
- saved WiFi networks from `SsidManager`;
- `Add WiFi`;
- `Stop Pairing` only while the config AP is active.

If no networks are saved, the page shows `No saved WiFi` and makes `Add WiFi`
the primary action. Merely opening this page must not start STA or config AP.

If saved networks exist but none is connected, opening the page acquires a
Settings-owned STA lease. The page shows `Not connected`, keeps the saved list
visible, and allows adding a new network while the existing station retry and
scan behavior continues in the background.

## Add WiFi Flow

`Add WiFi` starts the existing Xiaozhi-style configuration AP:

1. Settings releases its STA lease, if held.
2. Settings requests a dedicated config-AP session through the shared WiFi
   service. This is separate from the Badge/raw-AP session API.
3. The WiFi driver starts config AP mode through
   `WifiManager::StartConfigAp()` and verifies that config mode became active.
4. The WiFi page displays the AP SSID and web URL returned by `WifiManager`.
5. The user connects from a phone or computer and uses the captive portal to
   scan, choose SSID, and enter the password.
6. The portal validates the network and saves the credential through
   `SsidManager`, using the same global
   NVS namespace.
7. The user finishes the portal flow, which exits config AP.
8. Settings detects config-mode exit by polling the service snapshot, releases
   the config-AP session, refreshes the saved list, and acquires STA if at least
   one credential now exists.
9. A newly added network is treated as the default because `SsidManager::AddSsid`
   inserts new credentials at the front of the list.

The watch UI does not collect WiFi passwords.

If the portal updates the password of an already saved SSID,
`SsidManager::AddSsid()` preserves that SSID's current list position. The
Settings page does not guess which credential the portal submitted. The user
can select the saved row afterward to make it the default. This avoids changing
the shared component's established overwrite semantics without reliable input
about portal intent.

## Saved Network Switching

Saved networks are shown as selectable rows. Selecting a saved network performs
an immediate switch:

1. Call `SsidManager::SetDefaultSsid(index)` to move the selected credential to
   the front of the saved list.
2. Restart STA through the shared WiFi path.
3. Show `Connecting <ssid>`.
4. When connected, show the current SSID, IP, and RSSI.

This is intentionally immediate. The user should not have to wait for a later
app launch or natural reconnect to know whether the selected network works.

The reorder and restart are one exclusive shared-service transaction:

1. verify Settings is the sole STA lease owner and no AP session is active;
2. stop STA so its scan callback no longer reads the credential vector;
3. move the selected saved item to the front;
4. restart STA and let the saved-priority connection queue select it first.

If another STA owner is present, no credential is changed and the page reports
`WiFi busy`. Saved rows are hidden and cannot be selected while pairing, so the
portal task and Settings never mutate `SsidManager` concurrently.

## Global Runtime Contract

All networked apps use the same global saved WiFi credentials:

- Assistant atomically acquires a configured STA through `SharedWifiService`.
- Codex WiFi fallback acquires STA through the same shared service.
- Badge/config AP paths continue to use AP ownership through the resource
  supervisor.
- Future apps should not read or write WiFi credentials directly except through
  the global WiFi settings/service boundary.

Returning from Assistant to the desktop should release Assistant-owned
resources, including its WiFi lease. The Settings UI does not need a user-facing
"Assistant is using WiFi" conflict flow because the user reaches Settings from
the desktop. Code-level arbitration remains in place for abnormal or debug
states.

Launching Assistant with no saved credentials must not start pairing. Its STA
startup fails immediately with `Configure Wi-Fi in Settings`; pairing is
entered only from Settings. Assistant also fails and releases its lease if STA
does not connect within the bounded startup timeout. This removes the last
Assistant-owned configuration behavior without introducing a hidden background
AP or an indefinite `Connecting` state.

The Assistant must not check `SsidManager` and then acquire STA as two separate
operations. `acquireConfiguredSta` checks AP ownership before reading saved
credentials, acquires the lease, and starts STA under one shared-policy lock.
This prevents debug or abnormal Assistant startup from racing the config portal
while it writes credentials. The result distinguishes no credentials, busy,
and station-start failure.

## Components

### Settings UI

Add a WiFi entry to `AppSetup`. Selecting it opens a WiFi worker/page pair:

- `WifiSettingsWorker` owns page lifetime and invokes WiFi operations.
- `WifiSettingsPage` renders the status, saved rows, and actions.

The existing `SelectMenuPage` remains the top-level Settings menu. The WiFi page
should use compact round-screen layout patterns and avoid nested card-heavy UI.

### WiFi Settings Service

Add a small firmware-local adapter around `WifiManager`, `SsidManager`, and
`SharedWifiService` for Settings usage. It should expose focused operations:

- read current state snapshot;
- list saved SSIDs without passwords;
- start pairing AP;
- stop pairing AP;
- select saved network by index and reconnect;
- start STA when saved credentials exist;
- poll immutable WiFi state snapshots.

This adapter keeps Settings UI code from directly mixing NVS, WiFi manager, and
resource ownership calls.

The adapter uses polling snapshots rather than installing a
`WifiManager::SetEventCallback`. `WifiManager` has one process-wide callback
slot, so Settings must not replace callbacks used by another runtime or leave a
callback targeting a destroyed page.

### Shared WiFi Service

Extend the shared service only as much as necessary to support Settings:

- add an explicit `Settings` runtime owner for arbitration and diagnostics;
- add a distinct RAII config-AP session that acquires the existing exclusive AP
  resource and invokes config-portal driver methods;
- expose current STA details or provide them via the settings adapter;
- provide a reconnect operation that only succeeds while the caller holds a STA
  lease, is the sole STA owner, and no AP session is active;
- provide an atomic configured-STA acquisition for Assistant that never reads
  credentials while a config AP owns WiFi;
- provide an exclusive stop-configure-start transaction for saved-network
  selection.

Avoid duplicating the WiFi scan/connect loop already present in `WifiStation`.
Keep the existing Badge/raw-AP behavior unchanged.

### Lifecycle and Cleanup

`WifiSettingsWorker` owns all Settings WiFi leases and sessions. Its close and
destructor paths must:

1. stop and release an active config-AP session;
2. release the Settings STA lease;
3. destroy the page only after callbacks can no longer target it.

The worker uses a two-phase update. It consumes requests and renders snapshots
under `LvglLockGuard`, while controller construction, pairing start/stop,
network selection, polling, and runtime release run outside that lock. The view
is destroyed only after runtime release completes and under the LVGL lock. This
prevents a potentially blocking WiFi or `httpd_stop()` operation from holding
the display lock while a portal request is finishing.

Leaving Settings while pairing must stop the config AP. A portal-triggered
config-mode exit follows the same release path before STA is reacquired. These
rules apply to normal back navigation, app close, and construction/start
failure.

## Data Model

Saved credentials remain in the current `wifi` NVS namespace:

```text
ssid, password
ssid1, password1
...
ssid9, password9
```

The UI never displays passwords. The saved list displays only SSIDs.

The selected default network is the first item in the `SsidManager` list. Adding
a new SSID inserts it at the front. Selecting any saved SSID calls
`SetDefaultSsid` and moves it to the front. Updating an existing SSID through the
portal preserves its current position until the user selects it.

## Error Handling

The WiFi page should show simple actionable states:

- AP session rejected: show `WiFi busy` and keep the user on the WiFi page.
- Config AP failed to start: show `Pairing failed`.
- Config AP exits without a saved credential: return to `No saved WiFi` or
  `Not connected` without starting an empty STA session.
- No saved networks: show `No saved WiFi`.
- Selected network not found during scan: show `Searching <ssid>` or `Not found`
  while the underlying station retry continues.
- Connection failure after retries: show `Not connected` and keep `Add WiFi`
  available.

The station overlay clears its attempted SSID after exhausting the current
candidate queue. The next scan can return the page to `Connecting`; the wait
between scans is represented as `Not connected` instead of an indefinite
connection attempt.

The implementation should not erase NVS or saved credentials as a recovery
shortcut.

The UI remains English to match the existing Settings app and avoid depending
on CJK glyph coverage for core WiFi controls.

## Testing

Host tests should cover:

- WiFi settings model state transitions for no credentials, pairing, connecting,
  connected, and disconnected;
- selecting a saved SSID moves it to the front and requests reconnect;
- the actual station candidate queue respects saved-list order before RSSI;
- another STA owner blocks selection before NVS or the saved list is changed;
- start pairing requires AP ownership and does not bypass resource blocking;
- opening Settings with no saved credentials does not implicitly start STA or
  config AP;
- the dedicated config-AP session starts and stops the Xiaozhi portal, while the
  Badge/raw-AP session retains its current behavior;
- leaving Settings during pairing stops the config AP and releases ownership;
- portal-triggered config exit releases AP before reacquiring STA;
- updating an existing SSID preserves its current position, while selecting it
  explicitly moves it to the front;
- saved list snapshots omit passwords;
- Settings does not install or retain the process-wide WiFi event callback;
- Settings owner does not conflict with released Assistant resources.
- Assistant without saved credentials fails with a Settings action and a
  bounded connection timeout.

Firmware contract tests should cover:

- `AppSetup` exposes a WiFi menu entry;
- Settings WiFi flow uses `SsidManager` and the existing `WifiConfigurationAp`
  path rather than introducing a second credential store;
- networked runtime paths still use `SharedWifiService`;
- no implementation path logs or renders saved passwords.

Hardware validation should cover:

1. Fresh device with no saved WiFi: Settings shows no saved networks and can
   start pairing AP.
2. Add a WiFi through the AP portal: credential is saved globally and STA
   connects.
3. Reboot: saved WiFi remains available and Assistant can connect without
   reconfiguration.
4. Multiple saved networks: selecting a different network moves it to default
   and reconnects immediately, including when the selected network has weaker
   RSSI than another saved network.
5. Saved network unavailable: page remains usable and `Add WiFi` still works.
6. Assistant open/close after WiFi setup: Assistant releases resources when
   returning to desktop, and Settings WiFi can open afterward.
7. Leave Settings while pairing: the `StopWatch-*` configuration AP disappears
   and reopening WiFi starts from a clean state.
8. Open Assistant on a fresh device: it does not silently start the
   configuration AP; Settings remains the only pairing entry point.
9. Submit portal credentials and immediately leave Settings: UI rendering stays
   responsive while the portal stops, and reopening starts from a clean state.
10. Leave the saved list scrolled while RSSI changes: rows, touch state, and
    heap usage remain stable because RSSI-only updates do not rebuild the list.

## Why This Architecture

The preferred solution keeps credential storage, scanning, retry, captive
portal, and application leases in their existing owners. A direct-connect
implementation in Settings would duplicate station behavior and create a
second connection state machine; a tracked fork of the managed component would
increase upgrade cost. Exact-anchor generated overlays are the smallest
reproducible correction for the two upstream defects required by this product:
password logging and RSSI-first saved-network selection. Dependency upgrades
fail at configure time until both corrections are reviewed against the new
upstream source.

## Non-Goals

- No watch-side WiFi password keyboard.
- No custom replacement for the existing Xiaozhi captive portal in this
  iteration.
- No separate Assistant-only WiFi credential store.
- No automatic clearing or migration that risks deleting existing saved WiFi
  credentials.
- No attempt to support Settings WiFi changes while Assistant is actively open.
- No watch-side saved-network deletion in this iteration. The existing portal
  endpoint remains unchanged, but the watch UI only lists, adds, and selects.
- No always-on background WiFi service after all runtime leases are released.

## Alternatives Considered

### Reuse the Existing Generic AP Session

Rejected. That session is intentionally only an ownership boundary for Badge;
its driver does not own the raw AP lifecycle. Making it also start the Xiaozhi
portal would couple two different AP implementations and risk leaving one
running when the other releases the shared session.

### Let Assistant Continue Starting Pairing When No Credentials Exist

Rejected. It keeps configuration application-specific, creates an unexpected AP
when any app requests STA, and contradicts the global Settings entry point.

### Subscribe the Settings Page to WiFi Events

Rejected for this iteration. `WifiManager` exposes one callback slot rather
than a multi-subscriber API. A 500-1000 ms snapshot poll is sufficient for a
human-operated settings page and has a smaller lifecycle and regression risk.

### Replace the Portal with a Watch Keyboard

Rejected. It adds a password-entry UI, round-screen keyboard complexity, and
credential handling code without improving the global ownership model. Reusing
the validated portal is smaller and preserves compatibility.

## Acceptance Criteria

- Settings has a global WiFi entry.
- Current connection details are visible when connected.
- No-saved-WiFi state guides the user to start pairing.
- Add WiFi starts the existing AP web configuration flow.
- Opening WiFi without saved credentials never starts pairing automatically.
- Saved networks can be listed and selected.
- Selecting a saved network immediately attempts to reconnect to that network.
- Saved credentials are shared globally by all WiFi-using apps.
- Assistant does not own or duplicate WiFi configuration after the merge.
- Closing the WiFi page always releases Settings-owned STA/config-AP resources.
- Existing Badge AP behavior and other runtime WiFi consumers remain unchanged.
