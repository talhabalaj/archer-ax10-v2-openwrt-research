# Existing stock-firmware boot and LuCI work

The related [personal bootstrap/package fork](https://github.com/talhabalaj/archer-ax10)
implements an SSH bootstrap and package feed based on
[archer-boot](https://archer-boot.pages.dev/) and
[lee-soft/archer-ax10](https://github.com/lee-soft/archer-ax10).

This runs on stock TP-Link firmware. Installing modern LuCI and an opkg feed
does not replace the vendor kernel, hardware drivers, bootloader or configuration
semantics with current OpenWrt.

## Compatibility fixes already published

[v18-personal](https://github.com/talhabalaj/archer-ax10/releases/tag/v18-personal)
contains a patched `ax10-luci` package (`1.0-4personal1`):

- Normalize legacy flat `rx_rate` / `tx_rate` fields at LuCI's association-list
  boundary. Both status and wireless tables tolerate absent rate values and
  omit unavailable channel widths instead of inventing values.
- Handle LED devices without a standard kernel `trigger` interface. LuCI shows
  a capability notice directing LED configuration to the vendor interface;
  this is not an implementation of kernel LED-trigger support.
- Give modern LuCI its own UCI configuration and compiled-module cache instead
  of overwriting or sharing the vendor GUI's locations.
- Update hard-coded browser asset versions together with the displayed LuCI
  version, preventing old JavaScript from masking an installed fix.
- Verify packaged rate rendering, LED behavior, GUI config/cache isolation and
  consistent asset versions before publishing the feed.

The Wi-Fi `rate` error was reproduced in a test using the actual served
association-list and renderer functions, then passed after patching. Overview
was also observed loading in the browser after the asset-version correction.
The LED `length` error was reproduced and its corrected renderer passed the
static call-path check. These are userspace/UI checks, not hardware-port tests.

The older installer was observed overwriting the vendor LuCI configuration and
sharing its compiled cache. Those conflicts were repaired. A separate stock-GUI
login problem continued after a reboot and worked in a fresh private browser
session; this supports a browser-profile/site-data explanation. The exact stale
storage item was not isolated, and the GUI login symptom must not be attributed
to the cache/config conflicts as a proven cause.

## Reboot and persistence

The persistent hook restores the minimal SSH bootstrap. SSH was verified after
a real reboot, with a LAN-interface allow rule and a drop rule for other sources.
The host private key and credentials stay on the router/private credential store;
they are not part of either public research repository.

The minimal boot script does not reinstall LuCI, opkg payloads or debloat.
Those RAM-only package changes disappear on reboot. No replacement firmware was
flashed, and no full OpenWrt hardware support is implied by the bootstrap.
