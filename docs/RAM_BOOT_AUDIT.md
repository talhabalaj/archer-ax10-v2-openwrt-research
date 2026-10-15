# AX10 v2 RAM boot and recovery evidence audit

Checked 2026-10-15. Original source research only. No router connection, opening, console attachment, reboot, RAM execution, configuration change or flash operation was performed. No image was built. Public-safe report; source snapshots remain local analysis inputs.

## Conclusion

A minimal ARMv7 Linux image with an embedded initramfs is architecturally suitable for a RAM-only proof of life on this SoC. **Whether the existing AX10 v2 bootloader can load and start it without flash writes is unverified.** Upstream reference-board support proves neither the installed loader's commands nor its authentication policy. A browser firmware-rescue upload is a different, flash-writing workflow.

The shortest conditional route is original bootloader -> RAM-resident Linux zImage + AX10-specific DTB + embedded initramfs. A RAM-resident U-Boot chainloader is a possible second route if direct Linux handoff is unavailable and arbitrary RAM execution is supported. Neither route is demonstrated on AX10 v2. The reference U-Boot must not be flashed or treated as an AX10-ready image.

## Fixed source revisions

- Linux: `0fcb05b72344f40806cbe810de8872f1698bf053`.
- U-Boot: `ece349ade2973e220f524ce59e59711cc919263f`.

These are the pinned snapshots used by the preceding board audit, not a claim that either is the latest default branch today. The U-Boot snapshot was obtained from its `master` ref; the project's current default branch is `main`. All conclusions below are scoped to the pinned code.

## What reference support establishes

1. [U-Boot BCM63178 Kconfig, lines8-11](https://github.com/u-boot/u-boot/blob/ece349ade2973e220f524ce59e59711cc919263f/arch/arm/mach-bcmbca/bcm63178/Kconfig#L8) explicitly names a Broadcom reference board. [Reference defconfig, lines4-14](https://github.com/u-boot/u-boot/blob/ece349ade2973e220f524ce59e59711cc919263f/configs/bcm963178_defconfig#L4) selects its own DT, code/load address and stack settings. Those addresses are reference configuration, not verified safe AX10 load addresses.
2. [Common BCMBCA board code, lines8-19](https://github.com/u-boot/u-boot/blob/ece349ade2973e220f524ce59e59711cc919263f/board/broadcom/bcmbca/board.c#L8) obtains DRAM size/base/banks from DT. It is not an AX10 DDR-training implementation. The [accepted upstream submission](https://lists.denx.de/pipermail/u-boot/2022-October/498733.html) describes loading U-Boot into initialized memory from a prior loader.
3. [Linux reference board, lines11-29](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/arch/arm/boot/dts/broadcom/bcm963178.dts#L11) describes a reference board with 128MiB RAM and UART at115200n8. Those settings require AX10 verification; the vendor AX10 board profile separately describes256MiB. Do not replace observed memory/reservation information with reference values.
4. [SoC CPU/PSCI nodes, lines20-41 and91-94](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/arch/arm/boot/dts/broadcom/bcm63178.dtsi#L20) expect firmware PSCI services using SMC. This DTS does not implement the required resident firmware. The AX10 handoff must preserve or provide whatever firmware services are actually available. Its [UART node, lines258-264](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/arch/arm/boot/dts/broadcom/bcm63178.dtsi#L258) supplies useful PL011 register/interrupt/clock data, not a board header pinout.

## Linux handoff requirements

The [pinned ARM boot protocol](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/Documentation/arch/arm/booting.rst) requires initialized RAM. For a DT boot, the loader passes the DTB address inr2; DTB and initramfs must avoid kernel decompression and other occupied memory. A RAM zImage is supported; raw uncompressed kernels have stricter placement. At entry, DMA must be quiesced, IRQ/FIQ disabled, MMU and data cache off, andr0 zero. Relevant sections: RAM lines27-40; DTB lines126-140; initramfs lines144-159; zImage/entry lines169-230.

An embedded initramfs reduces this to a kernel-plus-DTB transfer rather than a separate rootfs download. [Kernel initramfs documentation, lines94-102 and136-145](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/Documentation/filesystems/ramfs-rootfs-initramfs.rst#L94) describes the built-in archive and `/init`. The proof image needs a suitable static userspace, `/dev/console`, and a minimal `/init` that stays in RAM. No flash root, overlay, vendor startup or automatic update path is needed.

**Inference:** the first proof can omit Ethernet, PCIe and both radios. CPU/timer/interrupt controller, serial console and RAM userspace are the useful milestone. A serial transcript reaching `/init` would establish that layer only.

## Do not equate RAM loading with no flash writes

The unmodified Linux reference board [enables NAND and requests an on-flash BBT, lines36-43](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/arch/arm/boot/dts/broadcom/bcm963178.dts#L36). [NAND DT parsing, lines5511-5512](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/mtd/nand/raw/nand_base.c#L5511) selects flash BBT mode; [initial scan, lines6480-6481](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/mtd/nand/raw/nand_base.c#L6480) invokes BBT creation. [BBT source, lines939-947 and1049-1053](https://github.com/torvalds/linux/blob/0fcb05b72344f40806cbe810de8872f1698bf053/drivers/mtd/nand/raw/nand_bbt.c#L939) contains conditional creation/update writes. This establishes a possible implicit write path on responding NAND, not that this AX10 contains such NAND or has been written.

For a defensible initial no-flash image, disable SPI/NAND/MTD storage drivers and nodes entirely, omit persistent storage userspace, and audit the loader handoff too. Merely omitting a sysupgrade command or mounting a root partition read-only does not establish that no flash writes can occur. The U-Boot reference [defconfig lines21-29](https://github.com/u-boot/u-boot/blob/ece349ade2973e220f524ce59e59711cc919263f/configs/bcm963178_defconfig#L21) also enables NAND functionality.

## Direct boot versus a U-Boot chainloader

The [U-Boot bootz documentation, lines19-33 and59-62](https://github.com/u-boot/u-boot/blob/ece349ade2973e220f524ce59e59711cc919263f/doc/usage/cmd/bootz.rst#L19) describes kernel, optional initrd and DTB arguments, conditional on `CONFIG_CMD_BOOTZ`. The pinned reference defconfig does not select that option, and [cmd/Kconfig lines388-394](https://github.com/u-boot/u-boot/blob/ece349ade2973e220f524ce59e59711cc919263f/cmd/Kconfig#L388) gives it no default. Thus the reference configuration is not evidence of a ready-made zImage command. Any chainloader would need explicit configuration and AX10-safe memory/DT treatment. Its available input transport is also unproved; generic network-loading claims do not establish an AX10 Ethernet driver in this U-Boot configuration.

## Required AX10 evidence before a RAM-boot experiment

- Exact installed loader identity/version and an uninterrupted boot transcript, with all individual identifiers removed before publication.
- Proven console accessibility, signal voltage, pin mapping, baud settings and autoboot-interrupt behavior for this exact hardware revision. Upstream UART descriptions are not electrical/header evidence.
- Loader help/output establishing a RAM-only transfer and execution path, accepted image formats, DTB/boot-argument handling and any authentication restrictions. Do not infer commands from another AX10 revision or a related CFE device.
- Verified DRAM map, firmware/secure-monitor/reserved regions, decompression footprint and non-overlapping payload/DTB/stack areas. Reference load addresses are insufficient.
- A documented return to stock boot after unsuccessful RAM execution, including watchdog/power-cycle behavior and no saved environment or flash updates during the experiment. Power recovery must be understood before trusting a shell-only route.
- Offline review of the actual proof image: no storage drivers, no persistent mounting/updater tools, and no vendor startup scripts. The payload must not depend on working network drivers for observability.

These are evidence requirements, not actions performed by this audit. Existing Linux root/SSH alone does not establish loader access or a working kernel-replacement mechanism.

## Official recovery evidence and its boundary

TP-Link's [recovery FAQ1482](https://www.tp-link.com/us/support/faq/1482/), updated2026-04-17, lists Archer AX10 and describes an AX-series rescue page reached with a button-at-power-on sequence. It cautions that recovery support varies. Its process uploads a vendor firmware image and performs an upgrade. The page does not identify AX10v2-specific console commands, confirm unsigned RAM execution, or document a no-flash Linux boot. It is a candidate stock-recovery reference, not demonstrated recovery on this device. This audit did not enter rescue mode or upload anything.

The [exact V2 support page](https://www.tp-link.com/us/support/download/archer-ax10/v2/) remains the source for regional firmware and GPL links. Image authentication, downgrade restrictions and bootloader acceptance must be assessed for the installed release; no inference of bypassability is made here.
