# Remote OTA Disabled and Persistent Update Notice Design

**Date:** 2026-07-22
**Status:** Approved for implementation planning

## Goal

The watch must never attempt a remote firmware update with the current single-app partition layout. When the XiaoZhi bootstrap service reports a newer firmware version, the Assistant page must instead show a persistent informational notice:

`发现新版本 vX.X.X`

The notice is display-only. It has no button, click action, download action, automatic restart, or partition-switch behavior.

## Current constraints

- `partitions.csv` contains one `factory` application partition. It has no `otadata`, `ota_0`, or `ota_1` partition.
- Application rollback is disabled, so there is no safe inactive image target or automatic recovery path.
- The XiaoZhi information endpoint is also used for activation and MQTT/WebSocket bootstrap. The endpoint cannot be disabled wholesale merely because its Kconfig name contains `OTA`.
- The current XiaoZhi component parses firmware metadata but the product has no supported download-and-install path.
- The product-visible firmware version currently comes from `common::FirmwareVersion` and must remain the single version source used for comparison and display.

## Architecture

### 1. Bootstrap metadata remains enabled

The existing XiaoZhi information request continues to run when Assistant starts. Activation, server time, MQTT, and WebSocket configuration retain their current behavior.

The request supplies the normalized local firmware version derived from `common::FirmwareVersion`. Version comparison accepts an optional `v` or `V` prefix and one to three numeric components; missing components are treated as zero. For example, `V0.5` is equivalent to `0.5.0`.

Malformed or empty remote versions are ignored and logged. They are never displayed or persisted.

### 2. Remote installation is hard-disabled

Firmware metadata is treated only as update-notice data:

- A returned firmware URL is not downloaded or retained for later execution.
- A returned `force` flag cannot trigger installation behavior and cannot override the product's local-version comparison.
- Product code must not call ESP OTA write, boot-partition switch, or OTA restart APIs.
- The single-factory partition table remains unchanged and acts as an additional structural guard against remote OTA.
- The misleading `CONFIG_XIAOZHI_OTA_URL` setting remains only because it is the shared XiaoZhi bootstrap endpoint; it is not an authorization to update firmware.

No remote updater, progress screen, confirmation dialog, retry worker, or background update service is introduced.

### 3. Product-owned update notice state

A small StopWatch-owned update notice component separates server metadata from UI state. It owns:

- normalized local-version comparison;
- the latest known target version;
- NVS persistence;
- publication of a display-only Assistant runtime event.

The persisted NVS value stores only the normalized target version. It does not store the download URL or the `force` flag.

State rules:

1. When the server reports a valid target newer than the running firmware, save it and publish it to the current Assistant session.
2. If a later response reports an even newer target, replace the saved value.
3. If the server is unavailable, omits firmware metadata, or reports an older target, retain the saved newer target.
4. After boot, and at the latest before the Assistant page first renders, compare the saved target with the running firmware. Repeat the comparison on every Assistant open.
5. Clear the saved target only when the running firmware is equal to or newer than it. This normally happens after a USB firmware upgrade.
6. An NVS read or write failure is logged. It must not prevent Assistant from starting. A newly received in-memory target is still displayed for the current session even if persistence fails.

The runtime mailbox gains a dedicated update-notice event instead of overloading conversation, error, or lifecycle events. Update notice state must not be dropped when transient conversation events overflow.

### 4. Assistant page presentation

`AssistantView` gains an independent, centered bottom notice label below the existing key hint. When a pending target exists, it displays exactly:

`发现新版本 vX.X.X`

The server version is normalized before formatting, so the UI always has one lowercase `v` prefix.

The label remains visible for the entire time the Assistant page is open. It is independent of the primary state and secondary conversation text, so Connecting, Activating, Listening, Thinking, Speaking, Closing, retry, and error flows continue to work unchanged.

When no pending target exists, the label is hidden or empty. There is no animation, timeout, button, focus state, or interaction.

## Data flow

1. Assistant activation requests XiaoZhi bootstrap information with the local firmware version.
2. The XiaoZhi integration extracts only a valid remote version and whether the server considers it available.
3. The product update notice component compares and persists the target without consuming the URL as executable input.
4. The Assistant runtime publishes the target through its mailbox.
5. `AppAssistant` forwards the target to the independent bottom label.
6. On later opens, the persisted target is published immediately, even before a new network response completes.
7. After a USB upgrade, the new firmware clears any saved target that is no longer newer than itself.

## Failure behavior

- Network or bootstrap failure: preserve any saved notice; retain existing Assistant error behavior.
- Invalid server version: ignore and log it; do not replace a valid saved target.
- NVS unavailable: display a newly received target in memory for the current session and log the persistence failure.
- Event delivery pressure: preserve the update notice independently of transient conversation events.
- UI teardown: destroy the notice label with the rest of `AssistantView`; no background UI access is allowed.
- Server-provided URL or force flag: never initiates download, flash write, restart, or boot selection.

## Verification

Automated contract and unit coverage must prove:

- local and remote versions compare correctly, including `V0.5` versus `v0.5.0`;
- malformed versions cannot create a notice;
- a newer server version is persisted and renders exactly `发现新版本 vX.X.X`;
- the notice survives Assistant close/open and device restart without a successful network request;
- an equal or newer running firmware clears the persisted notice;
- an older, missing, or invalid later server result does not erase a pending notice;
- a newer later target replaces the older pending target;
- NVS failures do not block Assistant startup;
- update notices do not replace lifecycle, conversation, or error text;
- no product path invokes firmware download, OTA write, boot-partition switch, or OTA restart behavior;
- the partition table still contains no OTA application slots or OTA data partition.

Build verification must include the normal unified firmware build and existing offline contract suite. Hardware validation, when the watch is available, must cover one server-reported update, an Assistant close/open cycle, a reboot with the network unavailable, and clearing the notice after flashing an equal or newer local version.

## Out of scope

- Remote firmware download or installation
- OTA partition creation
- Automatic rollback
- Update buttons or update detail pages
- Release notes, download progress, or mandatory-update blocking
- Background update polling while Assistant is closed
- Changes to XiaoZhi activation or transport selection
