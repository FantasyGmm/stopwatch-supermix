# macOS Agent release

Run `mac-codex-agent/build_release.sh` from the project root. The build writes
only the current release to `artifacts/macos-agent/latest/` and replaces the
previous contents after the new DMG, checksum, and metadata have all passed
validation.

The current build is ad-hoc signed because this Mac has no Developer ID
identity. On another Mac, drag the app from the DMG to Applications and use
the standard macOS manual-open approval if Gatekeeper requests it.
