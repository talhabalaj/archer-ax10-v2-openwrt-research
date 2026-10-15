# Official Archer AX10 V2 GPL audit

Inspected 2026-10-15. Scope: official AX10 hardware V2.0 archive, static inspection only. No router access, build, target binary execution, or flashing was performed by this audit.

## Source and revision

The [official US AX10 V2 support page](https://www.tp-link.com/us/support/download/archer-ax10/v2/) labels its GPL link **Archer AX10_V2_GPL**. The link is [ax10v2_GPL_20240317.tar.gz](https://static.tp-link.com/upload/gpl-code/2025/202503/20250317/ax10v2_GPL_20240317.tar.gz).

| Item | Observed value |
| --- | --- |
| Archive bytes | 644,861,617 |
| SHA-256 | `9670b07a1449dc7e338db9becbb019d154f0486569c1621a40f759843f655a87` |
| HTTP Last-Modified | 2025-03-17 07:14:00 GMT |
| HTTP ETag | `56fcbbc6736cb20d69d51ccc550354af` |
| Tar members | 121,525 |
| Sum of regular member bytes | 1,830,215,245 |
| Root | `ax10v2_GPL/` |
| BSP version | `5.02.07test9` in `bcm675x/bcm963xx_router/version.make` |

`Readme.txt` explicitly names Archer AX10 2.0 and gives `cd Iplatform/build` followed by `make`. `Iplatform/build/Makefile` defaults to `PRODUCT_NAME=ax10v2`. This is the exact vendor V2 archive, not a substituted V1 repository or archive.

## Matching hardware, older wireless release

Paths below are relative to archive root unless prefixed with `BSP/`. `BSP/` abbreviates `bcm675x/bcm963xx_router/`.

- `Iplatform/build/product_configs/ax10v2/kernel.config` identifies Linux/ARM **4.1.52**, `CONFIG_BCM_CHIP_NUMBER=63178`, `CONFIG_BCM963178=y`, `CONFIG_BCM_WLAN=m`, `CONFIG_BCM_WLAN_IMPL=59`, `CONFIG_BCM_ENET_IMPL=7`, ARM EABI, and `CONFIG_BCM_CPU_ARCH_NAME="arma7"`.
- `Iplatform/build/product_configs/ax10v2/basic.config` selects `PROFILE=TP6750` and the Broadcom `bcm963xx_router` BSP.
- `BSP/targets/TP6750/TP6750` selects chip 63178, board TP6750, wireless implementation 59, and the vendor Ethernet/packet-flow/Archer acceleration modules.
- The included cross-toolchain package is `bcm675x/toolchain/crosstools-arm-gcc-5.5-linux-4.1-glibc-2.26-binutils-2.28.1.Rel1.10.tar.bz2`. It was inventoried, not unpacked or executed.
- `BSP/bcmdrivers/broadcom/net/wl/impl59/main/src/include/epivers.h` identifies wireless **17.10.86.5 (r775262)**. The included `wl.ko` confirms that version. It differs from the independently observed current stock driver **17.10.121.37** in firmware build 20250828.

Therefore the archive matches the V2 board/kernel family but is not an exact source release for the newer installed wireless stack. Identical kernel version strings do not prove compatible internal ABIs.

## Wireless source versus blobs

Let `WL/` abbreviate `BSP/bcmdrivers/broadcom/net/wl/impl59/`.

| Component | Concrete evidence | Result |
| --- | --- | --- |
| Wireless core | `WL/main/src/wl/linux/prebuilt/wl.o` and `wl.ko` | Precompiled ARM ELF objects/module |
| Core build selection | `WL/main/components/router/hnd_wl/wl_generic.mk:22` checks for `main/src/wl/sys/wlc.c`; lines 266–275 fall back to prebuilt `wl.o` | Core source path absent; binary fallback selected |
| Missing core files | No `WL/main/src/wl/sys/{wlc.c,wlc_bmac.c,wl_linux.c,wl_linux.h}` in complete archive inventory | Cannot rebuild this core from published source |
| Integration source | `WL/sys/src/wl/sys/{wl_cfg80211.c,wl_cfgvendor.c,wldev_common.c,wl_core.c}`, plus `BSP/bcmdrivers/broadcom/net/wl/shared/impl1/` | Partial source; does not replace missing core |
| HND / HAL support source | `WL/main/src/shared/{linux_osl.c,siutils.c,hndpmu.c,hndpci.c,aiutils.c,hnddma.c,bcmutils.c}` | Useful source for OS, DMA, backplane, and power interfaces; HND prebuilt artifacts also included |
| Acceleration / shared libraries | `BSP/bcmdrivers/broadcom/char/{archer,pktflow,bcmlibs,wlcsm_ext}/impl1/*TP6750.o_save` | Object-only components, with Makefiles copying saved objects into build |

The only other archive files named `wlc.c` and `wl_linux.c` are an old OpenWrt wireless **control utility** and a small router userspace **ioctl utility**. They are not the absent driver core.

Pinned wireless artifacts:

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `WL/main/src/wl/linux/prebuilt/wl.o` | 7,832,348 | `45e1b4127c2ad9ad1786306a57a3f2566d7310d196d0bb9f8d5032406a298b0e` |
| `WL/main/src/wl/linux/prebuilt/wl.ko` | 7,833,296 | `191367a22920c0e258e1788d5fe15bf97d6f578bc0fe1cb73bcfaaabe0b8ebd8` |
| `BSP/targets/TP6750/filesystem.tgz` member `./lib/modules/4.1.52/extra/wl.ko` | 7,819,512 | `04c626e4a42dc187cdef3a5b7e6ad747b22b8d86b9b83d0e54dffce9ae9f2c5d` |

`wl.ko` declares `license=Proprietary`, `depends=hnd,emf,cfg80211,igs,wfd`, and `vermagic=4.1.52 SMP preempt mod_unload ARMv7 p2v8`. Static ELF inspection of `wl.o` finds 432 undefined global/weak symbols, including vendor `blog`, `wfd`, `si_*`, `osl_*`, and packet-chain interfaces. `CONFIG_MODVERSIONS` is disabled and the object has no `__versions` section. The absence of symbol CRC checks does not make kernel structure layouts or calling conventions compatible.

## Useful BSP paths for a port investigation

- Board: `BSP/shared/opensource/boardparms/bcm963xx/boardparms_63178.c`, especially `g_TP6750` at line 864. It records five SF2/GPHY ports, board buttons/LEDs, wireless-reserved GPIOs, and 256 MiB DDR configuration.
- SoC description: `BSP/kernel/dts/63178/963178.dts`; model `Broadcom BCM963178`, compatible `brcm,bcm963178`, Cortex-A7 CPU descriptions, vendor memory reservations, PCIe/peripheral nodes.
- ARM machine source: `BSP/kernel/linux-4.1/arch/arm/mach-bcm963xx/`.
- PCIe host: `BSP/bcmdrivers/opensource/bus/pci/host/impl1/arm/{pcie-bcm963xx.c,pcie-vcore.c,pcie-bcm963xx.h}`.
- Ethernet: `BSP/bcmdrivers/opensource/net/enet/impl7/{enet.c,sf2.c,sf2_standalone.c,bp_parsing.c,archer.c}`. The `archer.c` integration source is separate from the object-only accelerator above.
- Hardware definitions: `BSP/shared/opensource/include/bcm963xx/{63178_map_part.h,63178_cpu.h,63178_intr.h}`.
- Image construction: `BSP/build/Makefile`, `BSP/hostTools/createimg.pl`, and the included host-tools tree. These concern vendor image construction; acceptance by the installed TP-Link updater has not been demonstrated.

## License observations

The [TP-Link GPL page](https://www.tp-link.com/us/support/gpl-code/) describes products as containing software under multiple third-party licenses. The archive's GPL label does not establish one license for every included component.

- Published wireless support files such as `linux_osl.c` have a permissive Broadcom notice allowing use, copying, modification, and distribution subject to retaining notices.
- `shared/impl1/wl_linux_dslcpe.c` and Ethernet `enet.c` have Broadcom GPLv2 notices marked `DUAL/GPL:standard`. Their exact notices must be retained and assessed for the intended distribution; these inspected headers do not contain an additional linking exception.
- Included module metadata labels `wl`, `wlcsm`, `bcmlibs`, `pktflow`, and `archer` **Proprietary**; `hnd` declares **GPL and additional rights**; `bcm_enet` and `bcm_pcie_hcd` declare **GPL**.
- This audit did not establish a complete redistribution license for the proprietary core objects or a legal conclusion about the vendor's GPL compliance. Requesting the exact current GPL/BSP release and applicable binary redistribution terms is a concrete unresolved dependency.

## Build and portability conclusion

**Confirmed:** there is a vendor build skeleton, ARM toolchain package, Linux 4.1.52 tree, AX10 V2 board profile, image tools, support source, and prebuilt vendor modules. The userland build contains OpenWrt-derived material; `Iplatform/openwrt/include/version.mk` defaults to **12.09-rc1 / Attitude Adjustment**.

**Inference:** rebuilding a compatible vendor BSP or combining selected OpenWrt userspace with the vendor kernel is substantially more plausible than moving this proprietary wireless object onto a modern upstream kernel. That route retains the vendor kernel and binary dependencies. It is not evidence of current upstream AX10 V2 support or a complete modern OpenWrt Wi-Fi port.

**Unverified:** the documented vendor build has not been run; external package availability, host compatibility, complete-image reproducibility, exact current-stock driver ABI, wireless stability, TP-Link image authentication/acceptance, calibration preservation, and a recovery path remain untested. The archive cannot rebuild the wireless core from source, and its older core must not be silently substituted for the current stock release.

## Reproduction and public-safety boundary

Run `python3 scripts/inventory_gpl.py /path/to/ax10v2_GPL_20240317.tar.gz` from the repository root to generate the deterministic `GPL_INVENTORY.json` inventory. The script performs only standard-library hashing and tar inspection, emits an allowlisted filename/hash inventory, and extracts or executes nothing.

Only this report, `inventory_gpl.py`, and `GPL_INVENTORY.json` are intended for public publication. The archive, extracted binaries, firmware files, configs, target filesystem, calibration/NVRAM material, and full raw indexes remain local audit inputs.
