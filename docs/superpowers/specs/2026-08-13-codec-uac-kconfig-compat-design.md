# CODEC UAC Kconfig Compatibility Design

## Problem

The ESP-IDF component manager evaluates an optional `espressif/usb_host_uac`
dependency using `CONFIG_CODEC_UAC_SUPPORT`. A clean checkout has no tracked
Kconfig definition for that symbol, so dependency discovery exits twice with
`Missing required kconfig option after retry` before application sources are
compiled. A local ignored `components/kconfig_compat/Kconfig` previously hid
the defect, but cannot make builds reproducible.

## Decision

Define `CODEC_UAC_SUPPORT` in the tracked `main/Kconfig.projbuild` compatibility
menu with `default n`. The watch does not use USB Audio Class hosting, so the
symbol exists only to let the component manager deterministically reject the
optional dependency. The change must not add `usb_host_uac`, enable the symbol,
or modify generated `components/`, `managed_components/`, or `sdkconfig` files.

## Verification

A firmware contract test will require the tracked definition, its disabled
default, and the absence of an enabling override in both tracked sdkconfig
defaults files. The fix is accepted only after the focused contract passes, a
fresh clean-checkout build produces the unified firmware and assets, the full
57-stage host validation passes, and `git diff --check` is clean.

## Safety

This change affects dependency discovery only. It does not alter runtime USB,
audio, BLE, button, partition, or flashing behavior. Flashing remains a
separate explicit operation after a successful build.
