# TP-Link Archer AX10 v2 OpenWrt research

Evidence and reproducible research for the **Archer AX10 hardware v2.0**,
Broadcom BCM63178/BCM963178. Findings checked on 2026-10-15.

**Research and offline component development in progress. There is no flashable
OpenWrt image or functioning AX radio driver in this repository.** This is a device-specific investigation;
AX10 v1, v2, and v3 are not interchangeable.

## What we know

| Component | Evidence and current conclusion |
| --- | --- |
| Stock platform | ARMv7 Linux 4.1.52; device-tree compatible `brcm,bcm963178`. |
| Current wireless driver | Proprietary `wl`, version 17.10.121.37. Its shipped ARM ELF module has 6,952 defined function symbols and 438 undefined symbols. |
| 5 GHz Wi-Fi 6 | BCM63178, PCI `14e4:f6ca`, D11 core 130.1, PHY revision 51. No matching current brcmfmac/b43 support found in the checked sources. |
| 2.4 GHz | BCM43217, PCI `14e4:a8db`, core 30, NPHY revision 17. Existing BCMA/b43 support is a candidate, with documented feature limitations and no board validation yet. |
| OpenWrt target | No AX10/BCM63178/BCM963178 profile found in the complete checked OpenWrt source snapshot. Upstream Linux and U-Boot provide useful BCM63178 platform/reference-board code. |
| Official vendor GPL | Exact AX10 v2 archive contains board/kernel/PCIe/Ethernet support and a build skeleton, but the wireless core is a prebuilt object. Its WL 17.10.86.5 is older than current stock. |
| Flash/recovery | Live rootfs is a NOR MTD view. The official image declares 21 flash regions and contains a CFE stage; both Broadcom OS CRCs verify. Full chip geometry, active loader policy, calibration coverage and a working recovery/RAM-boot path remain unverified. |

Source links, checked commits, exact filenames, artifact digests, and search
boundaries are in the reports. A missing match in a source snapshot does not
prove that an unpublished or later port cannot exist.

## Research files

- [Board and upstream support audit](docs/BOARD_SUPPORT_AUDIT.md)
- [Exact vendor GPL source audit](docs/GPL_AUDIT.md)
- [Boot-chain findings and evidence checklist](docs/BOOT_CHAIN_AUDIT.md)
- [Vendor boot/image-construction source](docs/BOOT_SOURCE_AUDIT.md)
- [REA analysis of the shipped CFE stage](docs/BOOT_REA_AUDIT.md)
- [RAM-boot requirements and recovery boundary](docs/RAM_BOOT_AUDIT.md)
- [Current stock driver metadata and ABI boundary](docs/DRIVER_BINARY_AUDIT.md)
- [REA/Ghidra setup and scope](docs/REA_SETUP.md)
- [Initial REA native-driver findings](docs/REA_INITIAL_FINDINGS.md)
- [First offline driver components and validation](driver/README.md)
- [Core 130 attach, table and microcode-selection analysis](docs/DRIVER_ATTACH_AUDIT.md)
- [Vendor driver source map](docs/DRIVER_SOURCE_MAP.md)
- [Upstream driver baseline and missing blocks](docs/DRIVER_BASELINE_AUDIT.md)
- [Pinned radio firmware/table object inventory](findings/RADIO_FIRMWARE_INVENTORY.json)
- [Existing boot/LuCI work and its limits](docs/BOOT_AND_LUCI.md)
- [Public-safe hardware inventory](findings/HARDWARE_INVENTORY.json)
- [Portable module metadata](findings/MODULE_METADATA.json)
- [Deterministic vendor archive inventory](findings/GPL_INVENTORY.json)
- [Sanitized REA Evidence summary](findings/REA_EVIDENCE_SUMMARY.json)
- [Publication boundary](docs/PUBLICATION_POLICY.md)

## Intended implementation sequence

Offline driver development can proceed while boot/recovery evidence is pending.
The initial components are implemented and tested against private stock input;
the sequence below describes dependencies for eventual hardware validation.

1. Establish exact board, flash, boot and recovery evidence.
2. Add an ARMv7 BCMBCA OpenWrt target and AX10-specific device tree; prepare an
   initramfs research image with no flash-upgrade path.
3. Validate a recoverable RAM boot before testing hardware drivers.
4. Bring up Ethernet and PCIe using vendor source and upstream conventions.
5. Evaluate existing BCMA/b43 for the BCM43217 with correct board calibration.
6. Use REA/Ghidra for missing BCM63178 radio/core behavior, then implement and
   test that support separately.
7. Only after board/network validation, implement the correct persistent-image
   wrapper, upgrade layout, calibration preservation and recovery tests.

A vendor-BSP fork that retains the old kernel and proprietary modules is a
different option. It could preserve more stock functionality but does not
establish a modern upstream OpenWrt port or an open wireless driver. Compiling
a package or decompiling a function alone proves neither board compatibility
nor working Wi-Fi.

## Reproduce static inventories

These scripts read caller-supplied files and execute no firmware or driver:

```sh
python3 scripts/inventory_gpl.py /path/to/ax10v2_GPL_20240317.tar.gz > gpl-inventory.json
python3 scripts/module_inventory.py --summary /path/to/wl.ko > module-metadata.json
python3 scripts/boot_image_inventory.py /path/to/official-firmware.bin > boot-inventory.json
```

The archive URL and SHA-256 are documented in the GPL report. Native analysis
uses [morluto/rea](https://github.com/morluto/rea), pinned to `rea-agents@6.5.0`,
with Ghidra 12.1.4. REA is an analysis tool; it does not automatically recover
the original source or generate a validated driver.

Once the Ghidra/JDK environment is configured, named-function analysis can run
through a private stdio session without changing agent/MCP registration:

```sh
python3 scripts/rea_native.py /path/to/wl.ko /private/path/to/new-analysis \
  --procedure wl_cfg80211_attach --procedure wlc_attach
```

That output includes raw Evidence records and local paths. Keep it outside the
public repository and publish only reviewed findings.

## Publication scope

This repository contains research reports, links, portable metadata, original
inspection scripts and offline C components/tests. It contains no router credentials, private keys,
configuration backups, raw device dumps, MAC/serial identifiers, calibration
or NVRAM material, proprietary driver binaries, firmware images, or local
client/agent configuration. Original research/scripts use the MIT license;
linked third-party software keeps its own licenses.
