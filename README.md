# TP-Link Archer AX10 v2 OpenWrt research

Evidence and reproducible research for the **Archer AX10 hardware v2.0**,
Broadcom BCM63178/BCM963178. Findings checked on 2026-10-15.

**Research and offline component development in progress. There is no flashable
OpenWrt image or functioning AX radio driver in this repository.** This is a device-specific investigation;
AX10 v1, v2, and v3 are not interchangeable.

**Continue here:** [current progress and exact next task](PROGRESS.md),
[full implementation roadmap](ROADMAP.md), and
[agent continuation instructions](AGENTS.md).

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
- [PHY nesting and complete outer idle-TSSI wrapper](docs/PHY_GUARD_IDLE_MODEL.md)
- [Pure wake policy and explicit runtime inputs](docs/WAKE_POLICY_MODEL.md)
- [Actual ARM wake-policy/load-trace comparisons](docs/WAKE_POLICY_ARM_CHECKS.md)
- [SI core selection, mapping caches and PCI windows](docs/SI_SELECT_MODEL.md)
- [HND resource/IRQ/clock composition](docs/SI_CLOCK_MODEL.md)
- [Complete WL clock-selection caller](docs/WL_CLOCK_MODEL.md)
- [Composed target PHY reset](docs/PHY_RESET_MODEL.md)
- [Core130 beamforming cleaner and metadata bounds](docs/BF_CLEAN_MODEL.md)
- [Exact PHY-reset and beamforming-cleaner ordering](docs/DRIVER_PHY_RESET_BF_AUDIT.md)
- [Wake/suspend/enable model and policies](docs/PSM_CONTROL_MODEL.md)
- [Current DMA reset model and limits](docs/DMA_RESET_MODEL.md)
- [Composed stock-ARM comparisons and relocation limits](docs/PHY_COMPOSED_ARM_CHECKS.md)
- [Composed table-29 read and partial state](docs/PHY_TABLE_MODEL.md)
- [All-core PHY-51 TSSI callback](docs/PHY_TSSI_MODEL.md)
- [PHY-clock/core-flags and RSDB workaround](docs/PHY_CLOCK_MODEL.md)
- [PHY-register/TSSI/reset model and stock-ARM comparison](docs/PHY_REGISTER_MODEL.md)
- [ChipCommon clock body and ownership limits](docs/CHIPCOMMON_CLOCK_MODEL.md)
- [AC table-29 read and packed PHY-write contract](docs/DRIVER_AC_TABLE_READ_AUDIT.md)
- [Clock/PLL model, observations and dependencies](docs/CLOCK_CONTROL_MODEL.md)
- [Actual HND fallback and resource-selection contracts](docs/DRIVER_CLOCK_FALLBACK_AUDIT.md)
- [PHY-register wake override and RSDB workaround](docs/DRIVER_PHY_WAKE_ADDENDUM.md)
- [Existing Broadcom reverse-engineering projects and methods](docs/EXISTING_REVERSE_ENGINEERING.md)
- [Clock, PLL, PHY/TSSI, wake policy and DMA reset evidence](docs/DRIVER_RESOURCE_AUDIT.md)
- [Published common DMA64 contracts](docs/DMA64_SOURCE_AUDIT.md)
- [Offline reset/start model boundaries](docs/LIFECYCLE_MODEL.md)
- [Reset, startup, suspend and wake evidence](docs/DRIVER_LIFECYCLE_AUDIT.md)
- [Effective initialization semantics and tests](docs/EFFECTIVE_INIT_AUDIT.md)
- [Runtime context, object-memory and SI/calibration source notes](docs/D2_CONTEXT_SOURCE_NOTES.md)
- [Exact microcode upload trace and its limits](docs/DRIVER_UPLOAD_AUDIT.md)
- [Upload register/source corroboration](docs/UPLOAD_SOURCE_NOTES.md)
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

Driver-first offline development is the current priority. Selection, raw-table
replay, effective initialization, upload simulation and reset/start subsets are
implemented, including the target PHY-reset coordinator and real beamforming
cleaner. SI core selection, HND resource/IRQ/clock and the complete WL clock
caller are now composed offline. The next task is higher device-reset composition
and actual runtime/operating-system resources.
Outer PHY nesting and idle-TSSI now join the tested table/TSSI, PHY-clock,
D11/ChipCommon, direct PHY/reset, PSM and DMA-reset subsets. The complete pure
wake predicate and its control action are implemented with explicit runtime
inputs. Actual selected ARM functions match C under synthetic emulation,
including the predicate's complete software-state load order. Runtime input
acquisition, MBSS/fatal recovery, radio initialization, DMA data path, RX/TX and
Linux WLAN integration remain unfinished.

The separate board workstream establishes console/recovery, minimal OpenWrt RAM
boot, Ethernet and radio resources before hardware validation. Persistent image
and upgrade support follow validated board/radio behavior. The optional 2.4 GHz
BCMA/b43 investigation remains independent.

[ROADMAP.md](ROADMAP.md) defines all milestones and acceptance criteria;
[PROGRESS.md](PROGRESS.md) records completed work, limits and next-session steps.

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
