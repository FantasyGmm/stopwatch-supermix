# Agent Menu and Latest Release Design

## Goal

Make the macOS StopWatch Codex Agent menu easier to scan without changing the
meaning of its existing data, and produce one portable, current Agent release
artifact under a stable project directory.

## Menu information model

Codex and Trae are independent, simultaneous status sources. They are not a
source selector and there is no global task collection.

- Codex owns model, effort, five-hour quota, seven-day quota, and its own
  Pending, Running, and Completed task alerts.
- Trae owns source freshness, model, effort, project, current task state,
  current task title, and task detail.
- Connection, pairing, Watch identity, Bluetooth, and voice readiness belong
  to the Agent/Watch connection, not to either source.

## Approved menu direction: A, dual-source overview

The locked visual source of truth and measurable native implementation contract
are stored in
`docs/superpowers/designs/2026-08-29-agent-menu-a/`. The root menu must use
custom native rows matching that contract; a default single-line `NSMenuItem`
summary is not an acceptable approximation because it loses the approved
two-line hierarchy and source-attached task status.

The root menu presents:

1. A compact Agent health header with connection state, Agent name, voice
   readiness, and Agent version.
2. A Codex summary row containing its model/effort/quota summary and Codex task
   counts. Its submenu contains the complete Codex status and grouped Codex
   alerts.
3. A Trae summary row containing its freshness/model summary and current Trae
   project/task state. Its submenu contains the complete Trae status and task
   detail.
4. A Device & Voice row whose submenu contains connection, pairing,
   verification, Watch, Bluetooth, device, and voice details.
5. The existing Codex pet GIF action, which opens the complete conversion,
   preview, upload, restore, and cancellation window.
6. Existing reconnect, connection toggle, rename, pairing repair, log, and
   quit actions. Contextual repair actions appear only when their matching
   error state is active.

The root menu must never contain an unlabeled global `Task Alerts` entry.
Codex and Trae task summaries must remain visually and structurally attached
to their source.

## Release artifact

The project keeps only the most recent macOS Agent release:

```text
artifacts/macos-agent/latest/
```

The directory contains one versioned DMG, its checksum, and machine-readable
release metadata. A successful release atomically replaces the entire prior
`latest` directory, so stale packages cannot survive. Historical release
directories are not created.

The DMG contains the self-contained universal2 `StopWatch Codex Agent.app` and
an Applications shortcut for drag installation. The application must resolve
its Python runtime and resources relative to its own bundle, with no build-user
or installation-path strings in `Info.plist`.

The current machine has no Developer ID identity. The release therefore uses
the existing ad-hoc application signature and records that signing mode in its
metadata. The package is portable but another Mac may require the standard
Gatekeeper manual-open approval. The build remains ready for Developer ID
signing when credentials are added later.

## Version contract

- `mac-codex-agent/VERSION` starts at `1.0.0` and is the current released
  marketing version.
- The first successful release build is `1.0.0`.
- When release inputs have changed since the package in `latest`, the next
  successful build increments only the patch component: `1.0.1`, `1.0.2`, and
  so on.
- Rebuilding unchanged release inputs is idempotent and keeps the same version.
- Failed builds do not change `VERSION` or replace the current artifact.
- `CFBundleShortVersionString`, `CFBundleVersion`, the DMG filename, and release
  metadata must agree. `CFBundleVersion` uses the numeric patch release
  sequence plus one, so `1.0.0` maps to build `1`.

Release inputs include production Python modules, native audio sources,
requirements, the app installer/builder, and the release script. Tests,
documentation, caches, and generated artifacts do not trigger a version bump.

## Safety and validation

- Preserve the existing self-contained runtime smoke test, universal2 Mach-O
  scan, no-external-symlink check, no-key-material check, package hash check,
  code-sign verification, and atomic local app publication.
- Add focused tests for source ownership in the menu, submenu contents,
  version agreement, patch increment/idempotence, absence of absolute bundle
  paths, latest-only replacement, and DMG/checksum metadata.
- Stage and commit only files belonging to this feature. Preserve unrelated
  `.superpowers/` content and the existing task-detail font test modification.
