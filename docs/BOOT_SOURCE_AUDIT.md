# AX10 V2 boot and image-construction source audit

Inspected 2026-10-15. Original findings from the exact [official AX10 V2 GPL archive](https://static.tp-link.com/upload/gpl-code/2025/202503/20250317/ax10v2_GPL_20240317.tar.gz), linked by [TP-Link's AX10 V2 support page](https://www.tp-link.com/us/support/download/archer-ax10/v2/). Archive SHA-256: `9670b07a1449dc7e338db9becbb019d154f0486569c1621a40f759843f655a87`. This audit inspected local archive members only. No router access, target execution, image construction, boot, recovery, or flash was attempted.

All source references below are relative to `ax10v2_GPL/`. `BSP/` abbreviates `bcm675x/bcm963xx_router/`. Line numbers refer to original archive file contents.

## Decisive findings

1. **The source explicitly describes TP-Link dual CFE RAM loaders and reserves `0x40000` bytes before the OS.** `BSP/shared/opensource/include/bcm963xx/bcm_hwdefs.h:186-203` selects the BCM63178/47622 branch; its TP image comment describes two CFERAM copies, and `CONFIG_TP_IMAGE` sets `FLASH_LENGTH_BOOT_ROM = 64 * 4 * 1024`. This is useful corroboration for an independently parsed stock OS partition starting at `0x40000`. It does not describe every TP-Link partition.
2. **A BCM63178 SPI-NOR boot construction path exists.** `BSP/hostTools/scripts/bcmImageMaker:569-571` reserves four bytes for the CFERAM offset. Lines 633-641 calculate and append that offset, append an unsecure headered CFEROM, and append a `.cferamlz` compressed CFERAM payload. `BSP/build/Makefile:1175-1187` contains the corresponding NOR unsecure boot-ROM packaging branch.
3. **The actual CFE implementation is absent.** Complete archive inventory finds zero `.c`, `.h`, `.S`, or Makefile files under `BSP/cfe/`; `BSP/cfe/cfe/board` is absent. Only three prebuilt files occur under its build tree, all in NAND directories. `BSP/build/Makefile:882-888` explicitly makes dynamic CFE building conditional on that missing source directory, otherwise leaving the target empty.
4. **Published profiles are not an exact production boot-layout specification.** The generic TP6750 profile and embedded BSP kernel configuration select NAND/UBI, whereas the AX10 V2 product kernel configuration disables NAND/UBI and enables the NOR-oriented image settings. Those differences must be resolved before using this tree to produce an image.
5. **No verified recovery procedure follows from this archive alone.** Generic CFERAM binaries contain TFTP/RAM-run help and secure-mode denial strings. They are not proof that the installed TP-Link SPI-NOR bootloader exposes the same commands, permits an interrupted console, or accepts an unsigned RAM payload.

## Build and format layers

| Layer | Evidence | What it establishes |
| --- | --- | --- |
| Product entry point | `Iplatform/build/Makefile:7` and `product_configs/ax10v2/{basic.config,product.mk}` | AX10 V2 selects the TP6750/Broadcom BSP. |
| TP-Link boot reservation | `BSP/shared/opensource/include/bcm963xx/bcm_hwdefs.h:186-203` | TP image path reserves 256 KiB for dual CFERAM; generic non-TP path reserves 192 KiB. |
| Flash image base adjustment | Same header, lines 221-228; product `kernel.config:2467` | BCM63178 uses `IMAGE_OFFSET=0` when the zero-offset option is selected. Otherwise the generic path inserts `0x10000`. |
| Generic downloadable Broadcom image | `BSP/shared/opensource/include/bcm963xx/bcmTag.h:39-96` | Version `7`, 256-byte `FILE_TAG`, ASCII chip/board/length/address fields, kernel/rootfs/DTB fields, and 20-byte validation-token slots. |
| Generic whole-flash trailer | Same header, lines 98-132 | `WFI_TAG` contains five 32-bit words: CRC, version, chip ID, flash type, flags. It distinguishes NOR from several NAND erase sizes. |
| NOR OS assembly | `BSP/build/Makefile:1145-1153` | Strips and converts kernel ELF, produces `vmlinux.lz`, invokes `bcmImageBuilder` with rootfs/kernel/DTB/board/chip, then initializes NVRAM and adds a whole-image token. |
| NOR CFEROM/CFERAM assembly | `BSP/hostTools/scripts/bcmImageMaker:550-641` | Contains explicit unsecure boot-ROM path, four-byte CFERAM offset handling, and appended compressed RAM loader for BCM63178/47622 NOR. |
| Installed SPI rootfs view | `BSP/kernel/linux-4.1/drivers/mtd/maps/bcm963xx.c:145-162` | Generic SPI/HS-SPI MTD driver reads `FILE_TAG` and derives rootfs/kernel addresses from it plus boot/image offsets. |

The 256-byte Broadcom tag is a plausible explanation for a squashfs signature 256 bytes after an OS partition payload starts. Correct identification requires parsing the fields and checking both CRCs; matching an offset alone is insufficient. This source audit did not independently parse the stock file.

The outer TP-Link `fwup` container and named partition table are separate from these generic Broadcom structures. This archive does not establish an implementation of that exact outer wrapper. The included `Iplatform/openwrt/tools/firmware-utils/src/mktplinkfw.c:5,63` describes an older WR941 V2 header/checksum tool; its presence is not evidence that it builds AX10 V2 `fwup` containers. No AX10 V2 wrapper generator or `mkflash` implementation was identified in the complete filename inventory or the selected image-source inspection.

## Boot media and configuration disagreement

`BSP/targets/TP6750/TP6750` contains:

- Lines 57-61: NAND, pure UBI, UBIFS and squashfs-in-UBI selections.
- Lines 68-71: LZMA kernel and both 128/256 KiB NAND image variants.
- Line 82: RAM-disk boot enabled.
- Lines 451-461: dynamic CFE, GEN3 secure-boot architecture, boot-ROM-only mode, a 1024 KiB NAND boot partition, six images with 128 KiB allocation, but `BUILD_SECURE_BOOT` unset.

The embedded `BSP/kernel/linux-4.1/.config:1167-1194` similarly enables Broadcom NAND and UBI. In contrast, `Iplatform/build/product_configs/ax10v2/kernel.config:1131-1141` disables NAND and UBI, while lines 2466-2467 set the NOR raw-partition restriction and zero image offset. Product `product.mk:17-19` has its kernel-config copying command commented out. These are concrete reasons not to treat the shipped generic profile as the exact installed AX10 V2 configuration.

`BTRM_BOOT_ONLY=y` and `SECURE_BOOT_ARCH=GEN3` do **not** independently prove that hardware authentication is enabled. The scripts explicitly support GEN3 **unsecure** boot-ROM images. Conversely, `BUILD_SECURE_BOOT` being unset in this old generic profile does not prove that the currently installed device's OTP state or TP-Link image validation is unlocked.

## CFE, compressed RAM loader, and recovery evidence

The archive contains these relevant local inputs:

- `BSP/targets/cfe/cfe63178.bin` — generic prebuilt CFE image.
- `BSP/cfe/build/broadcom/build_cferom_nand/cfe63178rom.bin` — generic NAND CFEROM.
- `BSP/cfe/build/broadcom/build_cferam_nand/{cfe63178.bin,cfe63178ram.bin}` — generic NAND CFERAM.

`BSP/make.common:427-457` names different NOR and NAND CFE build paths. In particular the dynamic NOR path expects `cfe/build/broadcom/build_cferom_nor/cfe63178.bin`, which is absent. This limits how closely the included binaries can represent the TP-Link NOR boot chain.

Static printable strings in the NAND CFEROM mention `cferam.000`, boot-image fallback, and LZMA decompression. The NAND CFERAM advertises the following command families:

| Advertised family | Meaning of help text | Verification boundary |
| --- | --- | --- |
| `r` / network RAM-run | Run from flash or host/TFTP; network form mentions kernel, RAM disk, and DTB inputs. | Not demonstrated on installed bootloader. |
| `loadb` | Load binary via network, serial/Kermit, or boot filesystem at an address. | Does not establish safe addresses or accepted payload format. |
| Address execution | Help text describes jumping to a supplied address. | Command spelling and invocation must be discovered on the exact bootloader. |
| DTB and boot-argument helpers | Help text advertises loading a DTB over TFTP and editing kernel arguments. | Console availability and permitted mode unknown. |
| Web recovery UI | Embedded strings include upload pages and Broadcom/raw/flash image distinctions. | Does not prove an accessible TP-Link recovery web server or its acceptance policy. |

These are static help strings, not operational recovery instructions. No load addresses, erase commands, flash commands, OTP changes, or end-user recovery sequence are prescribed here.

The same generic CFERAM includes messages about authenticating `vmlinux.lz`, refusal when authentication fails, and disabling host boot in secure mode. Source under `BSP/hostTools/PerlLib/BRCM/SBI.pm:233-240,369-388` describes distinct unsecure SBI framing: header length, mode eligibility, CRC, and empty signature space. This confirms multiple supported security paths, not which path the installed TP-Link loader chooses. The producer's SBI framing must also not be conflated with the separate `BRCM` compressed payload marker observed in stock firmware.

`BSP/shared/opensource/include/bcm963xx/bcm_hwdefs.h:296-300` contains a conditional TP CFE recovery default bootline. Since neither the controlling CFE build configuration nor CFE source is present, the condition does not prove that feature is active.

## Image validation that is actually visible in source

`BSP/userspace/public/libs/cms_util/image.c:83-178` implements generic Broadcom tag verification. It checks header CRC, exact tag version, chip ID, declared length versus supplied buffer, and full payload CRC. `BSP/bcmdrivers/opensource/char/board/bcm963xx/impl1/board_image.c:751-769` likewise checks the flash image tag CRC.

These generic CRC checks provide corruption detection. They do not establish absence of signatures, model/region restrictions, rollback checks, or additional authentication in TP-Link's outer updater or bootloader. The exact current stock validation path was not audited here.

Two important image tools, `BSP/hostTools/bcmImageBuilder` and `cmplzma`, are supplied as i386 Linux ELF executables. Their matching implementation source was not identified. They were not executed. The visible scripts therefore expose significant packaging logic while leaving the exact compiled tagging/compression implementation and the CFE consumer behavior partly opaque.

## Consequences for a port

The source is enough to identify useful boot-format anchors and the TP boot reservation. It is not enough to derive a safe flash recipe, prove unsigned RAM boot, regenerate the exact stock wrapper, or rebuild the installed bootloader. In particular, a generic NAND profile or CFE image must not be substituted for the TP-Link NOR chain.

Remaining evidence needed before any execution proposal: exact stock outer-container parsing and checksums; exact NOR bootloader disassembly or corresponding source; confirmed command/console access; authenticated-boot/OTP state; validated RAM load, entry, and reserved-memory ranges; full device-specific partition map and backup scope; and a tested recovery mechanism. These are unresolved research items, not authorization to change the device.

This report contains only original analysis, public source paths, and build metadata. No binary files, configurations, calibration/NVRAM data, credentials, or raw console captures accompany it.
