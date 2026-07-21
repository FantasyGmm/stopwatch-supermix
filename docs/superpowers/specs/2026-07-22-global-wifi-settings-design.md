# Global WiFi Settings Design

Date: 2026-07-22
Status: Approved for implementation planning

## Purpose

Move WiFi configuration out of the Assistant-only flow and make it a global
Settings capability. A WiFi network configured once should be saved in the
shared WiFi credential store and be available to Assistant, Codex, Badge, and
future networked apps through the existing shared WiFi runtime.

The feature deliberately reuses the Xiaozhi WiFi configuration AP web flow for
adding new networks. The watch will not implement an on-device password keyboard
in this iteration.

## Current Context

The firmware already includes these relevant pieces:

- `AppSetup` and `SelectMenuPage` provide the Settings app menu structure.
- `78/esp-wifi-connect` provides `WifiManager`, `WifiStation`,
  `WifiConfigurationAp`, and `SsidManager`.
- `SsidManager` persists credentials in the `wifi` NVS namespace.
- `WifiStation` scans nearby APs, matches saved SSIDs, and connects according to
  the saved order.
- `WifiConfigurationAp` exposes the existing captive portal flow, including
  `/scan`, `/saved/list`, `/saved/set_default`, `/saved/delete`, and save
  handling.
- `SharedWifiService` and `ResourceSupervisor` arbitrate STA and AP ownership
  between runtime users.

The design keeps that ownership model. Settings becomes another controlled
consumer of the same WiFi service rather than bypassing it.

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
the primary action.

If saved networks exist but none is connected, the page shows `Not connected`,
keeps the saved list visible, and allows adding a new network. STA can continue
its existing retry and scan behavior in the background.

## Add WiFi Flow

`Add WiFi` starts the existing Xiaozhi-style configuration AP:

1. Settings requests a WiFi AP session through the shared WiFi service.
2. The WiFi driver stops STA if needed and starts config AP mode.
3. The WiFi page displays the AP SSID and web URL returned by `WifiManager`.
4. The user connects from a phone or computer and uses the captive portal to
   scan, choose SSID, and enter the password.
5. The portal saves the credential through `SsidManager`, using the same global
   NVS namespace.
6. Config AP exits.
7. Settings releases the AP session and starts or restarts STA.
8. The new network is treated as the default because `SsidManager::AddSsid`
   inserts new credentials at the front of the list.

The watch UI does not collect WiFi passwords.

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

## Global Runtime Contract

All networked apps use the same global saved WiFi credentials:

- Assistant acquires STA through `SharedWifiService` and uses the saved list.
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
- poll or receive WiFi events.

This adapter keeps Settings UI code from directly mixing NVS, WiFi manager, and
resource ownership calls.

### Shared WiFi Service

Extend the shared service only as much as necessary to support Settings:

- represent a `Settings` runtime owner or equivalent internal owner;
- expose AP-session acquisition suitable for config pairing;
- expose current STA details or provide them via the settings adapter;
- provide a restart/reconnect operation that respects resource ownership.

Avoid duplicating the WiFi scan/connect loop already present in `WifiStation`.

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
a new SSID or selecting an existing SSID moves it to the front.

## Error Handling

The WiFi page should show simple actionable states:

- AP session rejected: show `WiFi busy` and keep the user on the WiFi page.
- Config AP failed to start: show `Pairing failed`.
- No saved networks: show `No saved WiFi`.
- Selected network not found during scan: show `Searching <ssid>` or `Not found`
  while the underlying station retry continues.
- Connection failure after retries: show `Not connected` and keep `Add WiFi`
  available.

The implementation should not erase NVS or saved credentials as a recovery
shortcut.

## Testing

Host tests should cover:

- WiFi settings model state transitions for no credentials, pairing, connecting,
  connected, and disconnected;
- selecting a saved SSID moves it to the front and requests reconnect;
- start pairing requires AP ownership and does not bypass resource blocking;
- saved list snapshots omit passwords;
- Settings owner does not conflict with released Assistant resources.

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
   and reconnects immediately.
5. Saved network unavailable: page remains usable and `Add WiFi` still works.
6. Assistant open/close after WiFi setup: Assistant releases resources when
   returning to desktop, and Settings WiFi can open afterward.

## Non-Goals

- No watch-side WiFi password keyboard.
- No custom replacement for the existing Xiaozhi captive portal in this
  iteration.
- No separate Assistant-only WiFi credential store.
- No automatic clearing or migration that risks deleting existing saved WiFi
  credentials.
- No attempt to support Settings WiFi changes while Assistant is actively open.

## Acceptance Criteria

- Settings has a global WiFi entry.
- Current connection details are visible when connected.
- No-saved-WiFi state guides the user to start pairing.
- Add WiFi starts the existing AP web configuration flow.
- Saved networks can be listed and selected.
- Selecting a saved network immediately attempts to reconnect to that network.
- Saved credentials are shared globally by all WiFi-using apps.
- Assistant does not own or duplicate WiFi configuration after the merge.
