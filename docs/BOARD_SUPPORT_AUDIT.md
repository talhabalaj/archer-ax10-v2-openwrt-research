# Archer AX10 v2.0 board support audit (2026-10-15)

Read-only source audit using a separately collected, read-only device inventory. No firmware was built or flashed.

## Result

No Archer AX10 profile, BCM63178 target, or BCM963178 target was found in a full text search of current OpenWrt main (snapshot HEAD 6c8798939303bbe8e19bd50eb810170cfadd0daf, dated 2026-10-14). Upstream Linux already supplies substantial BCM63178 CPU/platform and flash-controller infrastructure. The hard gaps for a proper current-kernel OpenWrt port are AX10 board description/boot format, Ethernet data path, PCIe host integration, and the BCM63178 AX radio. The external BCM43217 2.4 GHz radio has an existing, partially supported b43 route.

## Evidence scope

- Downloaded OpenWrt main source archive into the private analysis workspace and searched the complete tree for `archer.?ax10|bcm63178|bcm963178`: no matches. An additional BCMBCA search found bcm4908 and generic references only.
- Linux source files downloaded from master; checked HEAD 0fcb05b72344f40806cbe810de8872f1698bf053 (2026-10-14).
- U-Boot current defconfig downloaded; checked HEAD ece349ade2973e220f524ce59e59711cc919263f (2026-07-06).
- Fresh read-only device inventory is summarized in `findings/HARDWARE_INVENTORY.json`. Raw device output stays private. This is device evidence, not evidence of upstream support.

## Exact device evidence

Stock kernel is ARMv7 Linux 4.1.52. Device tree compatible is `brcm,bcm963178`. Stock modules include proprietary `wl`, `archer`, `pktflow`, and `bcmvlan`, plus `hnd`, `bcm_enet`, and `bcm_pcie_hcd`. See the separately inspected module license declarations in `findings/MODULE_METADATA.json`.

- wl0: chipnum 0xf6ca = 63178, PHY type 0xb, PHY rev 51.0, core rev 130.1; logical device4492; actual PCI endpoint14e4:f6ca.
- wl1: chipnum0xa8d1 = 43217, PHY type4, PHY rev17.0, core rev30.0; logical device43a9; actual PCI endpoint14e4:a8db. PCI bridge14e4:0675.
- `/proc/mtd` exposes only rootfs size0x00c84000 with erase size0x10000. This is insufficient to infer total flash capacity, bootloader layout, writable firmware regions, or calibration locations.

Do not treat BCM4492 as the actual chip identity, or infer the AX radio is BCM43684 from generic Broadcom marketing examples.

## Reusable support and missing pieces

| Layer | Confirmed current source state | What is still required |
|---|---|---|
| CPU/platform | Linux `ARCH_BCMBCA` plus `ARCH_BCMBCA_CORTEXA7`; bcm63178.dtsi has three Cortex-A7 cores, GIC, PSCI, architectural timer, PMU and PL011 UART. | OpenWrt ARMv7 target/config and AX10-specific RAM/boot conventions; verify PSCI from actual boot firmware. |
| Base peripherals | BCM63178 DTS includes watchdog, eight GPIO banks, RNG, HSSPI, brcmnand, LED controller and PL081 DMA. | AX10 pins/resets/LED/buttons/flash-device geometry and safe partition definitions. |
| Boot | U-Boot already has BCM63178/bcm963178 reference support. | Preserve existing bootloader; first test a RAM-loaded kernel/initramfs only after proving a recovery/console path. Reference U-Boot is not an AX10-ready bootloader. |
| Ethernet | bcm63178.dtsi has no Ethernet node. Existing bcm4908_enet OF table matches only bcm4908. bcm_sf2 matches bcm4908/7445/7278 variants. | Establish AX10 MAC/switch/PHY architecture from vendor GPL and register maps; port minimal non-accelerated DMA/MAC+switch/MDIO/PHY operation. No compatibility with bcm4908 is established. |
| PCIe | bcm63178.dtsi has no PCIe node; brcmstb OF table has bcm4908 but no63178. | Vendor bcm_pcie_hcd register/reset/clock/PHY/interrupt/DMA-range analysis and an upstream-style host controller integration. Similar names are not compatible evidence. |
| 2.4GHz BCM43217 | BCMA PCI table explicitly matches0xa8db with BCM43217 comment. b43 matches core0x1e (30); NPHY version check accepts17; firmware paths include ucode30_mimo, n16initvals30 and n16bsinitvals30. PMU/TSF contain BCM43217 branches. | Recover correct SPROM/NVRAM/calibration; verify actual board bring-up, AP/data path and stability. Official docs describe0xa8db as partially supported. Do not promise stock N/300Mbps capabilities. |
| 5GHz BCM63178 AX | brcmfmac PCI table/chip IDs have no63178/f6ca/4492 route. b43 has no core130/AX PHY51 support. | New hardware support and firmware/protocol understanding or a vendor-driver/BSP integration; adding a PCI ID is insufficient. |

## BCM43217 firmware packaging

Current OpenWrt b43 package defaults to wl5.100.138 firmware666.2 and preserves core30 in its firmware-squash defaults. The matching b43-fwcutter extraction table includes all three core30 files above. Existing package machinery can be reused. b43 official documentation states no HT/aggregation/40MHz support, and source does not expose HT capability; using it may mean basic2.4GHz service rather than stock802.11n performance.

## Shortest technically viable port sequence (inference)

1. Resolve exact AX10 V2 GPL source, vendor board parameters/DTS, full flash geometry and boot image format offline. Official source URL: https://static.tp-link.com/upload/gpl-code/2025/202503/20250317/ax10v2_GPL_20240317.tar.gz . Availability is confirmed by the official V2 page; the completed content audit is in [GPL_AUDIT.md](GPL_AUDIT.md).
2. Create a new ARMv7 BCMBCA target (or a carefully separated32-bit BCMBCA subtarget), Cortex-A7, current Linux. `bmips` is MIPS; `bcm4908` is AArch64; `bcm53xx` targets Cortex-A9 BCM47xx/53xx. None is a drop-in AX10 target.
3. Add board DTS from upstream bcm63178.dtsi; make a serial-console/initramfs research build first, with flash writes disabled. Use original boot firmware to load it into RAM when an actual RAM boot/recovery path has been demonstrated.
4. Bring up Ethernet without packet acceleration, then PCIe host. Use GPL source/register evidence first and REA only for missing behavior, reset sequencing and API/control dependencies.
5. Try existing BCMA+b43 for the BCM43217, with known firmware and exact board calibration. Keep the AX radio disabled until separately implemented.
6. Only after functional boot/network validation, implement correct AX10 image wrapper, upgrade partitioning, MAC/calibration preservation and fail-safe recovery, then hardware regression tests.

The shortest route to preserving both radios may instead be a custom vendor-BSP OpenWrt fork using the original4.1.52 kernel and matching proprietary modules. That is a separate approach; it does not establish a supported current-kernel OpenWrt port or an open driver. It depends on a reproducible matching BSP/toolchain, licensing, kernel symbols/ABI and vendor network hooks. REA can assist analysis but cannot by itself convert the5.8MB shipped proprietary kernel module into a validated maintainable modern driver.

## Primary source links

- OpenWrt checked tree: https://github.com/openwrt/openwrt/tree/6c8798939303bbe8e19bd50eb810170cfadd0daf/target/linux
- OpenWrt bcm4908 target: https://github.com/openwrt/openwrt/blob/6c8798939303bbe8e19bd50eb810170cfadd0daf/target/linux/bcm4908/Makefile
- OpenWrt bmips target: https://github.com/openwrt/openwrt/blob/6c8798939303bbe8e19bd50eb810170cfadd0daf/target/linux/bmips/Makefile
- OpenWrt b43 package/firmware: https://github.com/openwrt/openwrt/blob/6c8798939303bbe8e19bd50eb810170cfadd0daf/package/kernel/mac80211/broadcom.mk
- Linux BCM63178 DTS: https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/arch/arm/boot/dts/broadcom/bcm63178.dtsi
- Linux BCM963178 reference board: https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/arch/arm/boot/dts/broadcom/bcm963178.dts
- Linux BCMBCA configuration: https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/arch/arm/mach-bcm/Kconfig
- Linux BCMA PCI IDs: https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/bcma/host_pci.c#L302
- Linux b43 core/firmware/PHY support: https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/b43/main.c
- Linux brcmfmac PCI IDs: https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/wireless/broadcom/brcm80211/brcmfmac/pcie.c#L2735
- Linux PCIe: https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/pci/controller/pcie-brcmstb.c#L2015
- Linux Ethernet: https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/ethernet/broadcom/bcm4908_enet.c#L781
- Linux switch: https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/net/dsa/bcm_sf2.c#L1348
- b43 official support/limits: https://wireless.docs.kernel.org/en/latest/en/users/drivers/b43.html
- b43-fwcutter firmware extraction table: https://github.com/mbuesch/b43-tools/blob/master/fwcutter/fwcutter_list.h
- U-Boot BCM63178 reference config: https://github.com/u-boot/u-boot/blob/ece349ade2973e220f524ce59e59711cc919263f/configs/bcm963178_defconfig
- TP-Link exact V2 firmware/GPL page: https://www.tp-link.com/us/support/download/archer-ax10/v2/
- Related ARM BCMBCA target discussion (not an AX10 port): https://github.com/openwrt/openwrt/issues/20411
