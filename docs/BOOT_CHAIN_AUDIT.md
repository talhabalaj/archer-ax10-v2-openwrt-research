# AX10 v2 boot-chain investigation

Checked 2026-10-15 against the official 250828 image, exact vendor GPL archive,
and read-only observations from the running AX10 v2. No reboot, recovery entry,
RAM execution, firmware installation, bootloader change or flash write was
performed in this investigation. Raw device output and proprietary inputs stay
outside this repository.

## Result

We can now parse the stock OS image, verify both Broadcom CRCs, identify its
compressed CFE stage, and describe the vendor-declared partition layout. The
running kernel confirms a NOR rootfs view with the same size as the image tag.
**A recoverable RAM boot is still unproved.** Console accessibility, active
bootloader policy and a modern-kernel handoff remain the next evidence gaps.

## Inputs and reproducibility

- [Official exact-revision firmware source page](https://www.tp-link.com/us/support/download/archer-ax10/v2/):
  `Archer AX10_V2_250828/ax10_ax1500_all-up-ver1-3-12-P1[20250828-rel26873]_2025-08-28_16.32.42.bin`.
  Size 15,171,889 bytes; SHA-256
  `e3c78e25a23d661219b1934d3b94ba7d0844cd357fede8a830b90e60fd0a910c`.
- [Exact GPL source audit](BOOT_SOURCE_AUDIT.md), with archive identity and
  paths/line references. It corroborates a TP-specific 256 KiB boot reservation
  and BCM63178 NOR packaging, but lacks CFE implementation source.
- [OpenWrt safeloader parser source](https://github.com/openwrt/firmware-utils/blob/0179ab1e367f756ac108a485f26585661d4c528c/src/tplink-safeloader.c#L130)
  describes the Cloud header and payload offset. Its
  [partition-table producer](https://github.com/openwrt/firmware-utils/blob/0179ab1e367f756ac108a485f26585661d4c528c/src/tplink-safeloader.c#L3583)
  explains the fixed four-byte marker. This is a format reference, not AX10
  board support or proof of authentication policy.
- [Reproducible image inventory](../findings/BOOT_IMAGE_INVENTORY.json) and
  [original inspection script](../scripts/boot_image_inventory.py):

  ```sh
  python3 scripts/boot_image_inventory.py /path/to/official-firmware.bin
  ```

The script reads the caller-selected image, checks region bounds and layout,
validates both Broadcom CRCs, and performs bounded LZMA decompression in memory.
It executes no target code and writes no extracted firmware. Validation against
the exact input passed; four modified/truncated variants were rejected.

## Container and OS structure

The TP-Link Cloud container declares its complete size in a big-endian word,
then carries a digest/header area. The `fwup-ptn` table begins at file offset
`0x1014`; its offsets are relative to that location. They must not be confused
with flash addresses.

| Image component | Absolute file offset | Bytes | Interpretation |
| --- | ---: | ---: | --- |
| Partition-table payload | `0x1814` | 2,048 | Fixed `00 04 00 00` marker followed by declared flash partitions. |
| `fs-uboot` | `0x2014` | 82,330 | BRCM header and LZMA-compressed ARM CFE stage. Its name does not establish U-Boot. |
| `os-image` | `0x161ae` | 15,037,429 | Broadcom FILE_TAG v7, rootfs, compressed kernel, DTB and one trailing zero byte. |
| Rootfs inside OS | `0x162ae` | 13,123,584 | SquashFS 4.0, compression ID 4 (XZ), 1 MiB blocks. |
| Kernel inside OS | `0xc9a2ae` | 1,908,452 | BRCM header and LZMA, decompressing to 5,001,120 bytes. |
| DTB inside OS | `0xe6c192` | 5,136 | Valid flattened-device-tree header/total length. |

The Broadcom tag identifies chip `63178`, board `TP6750_AX1500V1`, and no CFE
component inside the OS image. Its total payload is 15,037,172 bytes. Both tag
and payload CRC32 fields match the source-described algorithm and byte order.
These are corruption checks, not cryptographic signature validation.

The usual salted MD5 formula from the upstream safeloader producer does not
match this image's outer digest field. Cloud-header signing/checksum coverage
remains unresolved; no regenerated or modified image is claimed acceptable.

## Declared flash map

These are **official image declarations**, not a raw flash dump or independent
measurement of chip capacity. The complete 21-entry map is in the JSON inventory.

| Region | Start | Size | Implication |
| --- | ---: | ---: | --- |
| `factory-boot` | `0x000000` | `0x028000` | First-stage area declared by vendor; this update image does not carry its contents. |
| `fs-uboot` | `0x028000` | `0x018000` | Second boot-stage slot; vendor source describes dual CFERAM boot reservation. |
| `os-image` | `0x040000` | `0xec0000` | OS slot, consistent with source's `0x40000` boot reservation. |
| Identity/product fields | `0xf00000` | through `0xf10000` | Includes named MAC/PIN/device/product regions; contents were not read or published. |
| Version/profile/table/defaults | `0xf10000` | through `0xf50000` | Vendor metadata and default configuration slots. |
| `user-config` | `0xf50000` | `0x020000` | Device configuration region. |
| `icons` | `0xf70000` | `0x020000` | Vendor-declared slot. |
| `router-config` | `0xf90000` | `0x020000` | Device configuration region. |
| `certificate` | `0xfb0000` | `0x010000` | Named certificate slot; contents remain private/uninspected. |
| `radio_bk` | `0xfc0000` | `0x010000` | Calibration-related candidate by name; semantics and contents unverified. |
| `ag-config` | `0xfd0000` | `0x020000` | Final declared region ends at `0xff0000`. |

The declared map fits within 16 MiB but does not prove a 16 MiB physical chip,
or explain the remaining `0xff0000`–`0xffffff` range. Do not invent a partition
there or assume it is disposable. Calibration may exist in additional locations.

## Read-only live corroboration

[Selected live metadata](../findings/LIVE_BOOT_METADATA.json) records:

- MTD `rootfs`: NOR, 13,123,584 bytes, 65,536-byte erase size. The kernel's
  reported write/subpage size is also 65,536; this is not a physical chip
  page-program specification. Only this rootfs view is exposed through MTD.
- SPI device modalias `spi:bcm_HSSpiDev0`. This corroborates the vendor HSSPI
  path but does not identify the physical flash chip or its capacity.
- DT memory declaration: base zero, 256 MiB. FDT reservations include
  `[0x00000000, 0x00008000)` and `[0x04000000, 0x04100000)`.
- Console argument `ttyAMA0`. It does not identify a PCB pinout or establish an
  accessible interruptible console.
- Live CFE version-record bytes match the official image's `cfe-v` record at
  decompressed CFE offset `0x570`. Matching version metadata does not establish
  byte-for-byte identity of the installed loader or its runtime policy.
- Secondary CPUs use `brcm,bca-smp`; the upstream reference DTS expects PSCI.
  Preserving firmware services and providing a correct SMP handoff require
  investigation before substituting a modern DTB. An initial uniprocessor
  proof is a possible design choice, not a demonstrated boot result.

The kernel ring buffer no longer held the early flash/boot initialization
messages. No reboot was performed to collect them. Full chip geometry and the
installed first-stage contents remain unknown.

## CFE and RAM-boot boundary

The official `fs-uboot` decompresses to 341,024 bytes, SHA-256
`1a9ca1a9e8f52e6e5a6ef655fb255c63c08584b3972c3e33b33d9c12a4311487`.
Its BRCM header declares load/entry `0x00f00000`. Static REA analysis of this
exact input is documented in [the CFE analysis](BOOT_REA_AUDIT.md).

The OS kernel wrapper separately declares load/entry `0xc0018000`. The tag's
rootfs/kernel/DTB addresses are legacy image-address fields. **None of these
numbers is a validated physical placement recipe for a modern kernel.**
The loader's translation, resident regions, decompression footprint and entry
protocol all matter.

The binary contains RAM-loading/transfer command handlers and a secure-mode
host-boot rejection path. Registration of relevant commands is itself policy
dependent. Static existence of these handlers does not establish that a user
can invoke them, that the active policy permits host boot, or that the target
handoff will succeed. No policy bypass is claimed or attempted.

[The upstream RAM-boot audit](RAM_BOOT_AUDIT.md) identifies a suitable conditional
first milestone: serial output from an ARMv7 kernel with an embedded initramfs.
Exclude MTD, NAND and SPI storage drivers from that proof image: reference NAND
bad-block-table initialization can contain implicit flash writes. Stock web
rescue documentation describes firmware upgrading, not a RAM-only experiment.

## Evidence checklist and next milestone

| Question | Status | What remains |
| --- | --- | --- |
| Stock wrapper/OS component layout | Answered for inspected image | Outer Cloud authentication still unresolved. |
| Vendor partition declaration | Answered for inspected image | Physical chip capacity, installed table and full calibration locations. |
| Live boot-stage family | Corroborated | Full installed-loader identity and first stage. |
| RAM-load capability in shipped CFE | Static candidates found | Console availability, active command policy, transport and actual accepted formats. |
| Modern Linux entry contract | Partly mapped | AX10-safe memory plan, SMP/firmware services and verified console. |
| Recovery after failed RAM execution | Unresolved | Device-specific demonstration of return to stock without persistent changes. |

The next hardware evidence is a **passive boot-console capture**, followed by
observing available loader help if an authorized console can be interrupted.
That requires exact-board voltage/pinout evidence and a planned maintenance
window. It has not been performed or prescribed as an immediate command.
There is no flashable image or validated RAM-boot payload in this repository.
