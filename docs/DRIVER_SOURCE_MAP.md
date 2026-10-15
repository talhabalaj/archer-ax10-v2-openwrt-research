# BCM63178 / D11 core 130 driver source map

Original source-facts ledger, inspected 2026-10-15. Input: [official AX10 V2 GPL archive](https://static.tp-link.com/upload/gpl-code/2025/202503/20250317/ax10v2_GPL_20240317.tar.gz), SHA-256 `9670b07a1449dc7e338db9becbb019d154f0486569c1621a40f759843f655a87`.

No router access, hardware writes, binary execution, or public-checkout edits occurred. This document publishes source paths, factual constants, and symbol metadata only. It contains no vendor implementation, microcode, initialization-table values, or calibration contents.

Paths are relative to archive root `ax10v2_GPL/`. `BSP/` means `bcm675x/bcm963xx_router/`; `WL/` means `BSP/bcmdrivers/broadcom/net/wl/impl59/`; `OLD/` means `BSP/kernel/linux-4.1/drivers/net/wireless/brcm80211/brcmsmac/`.

## Identity and capability evidence

| Fact | Original source reference | Scope / limitation |
| --- | --- | --- |
| Broadcom PCI vendor ID is `0x14e4`. | `WL/sys/src/include/bcmdevs.h:29` | Vendor identity only. |
| Embedded BCM63178 2x2 AX device ID is `0xf6ca`; logical 2.4/5 GHz IDs are `0x4491`/`0x4492`. | Same file, lines 382-384 | The header explicitly labels the embedded device 802.11ax. Logical band IDs must not be substituted for the actual PCI ID. |
| BCM63178 chip ID is decimal 63178, hexadecimal `0xf6ca`. | Same file, lines 608-614 | The embedded AX family macro also includes BCM47622; family membership is not board interchangeability. |
| D11 MAC core ID is `0x812`. | `WL/main/src/include/hndsoc.h:195` | Backplane core ID, distinct from PCI and chip IDs. |
| PHY type `11` is named `PHY_TYPE_AC`. | `WL/main/src/include/bcmsrom_tbl.h:1164-1166` | A vendor PHY-family name. It does not mean an embedded AX device is limited to 802.11ac. |
| Runtime identity includes chip/core/PHY revisions and their minor revisions. | `WL/main/components/wlioctl/include/wlioctl.h:2009-2034` | `wlc_rev_info_t` supplies separate fields; do not collapse these identifiers. |
| Build-generated debug inventory lists core revisions 129, 130, 131. | `BSP/kernel/linux-4.1/autoregs_d11revs_dbg.txt:13-15` | Evidence the shipped build includes those revisions, not a complete source register map or compatibility guarantee. |

The independently observed target tuple is BCM63178, core 130.1, PHY type 11/revision 51.0. No published source statement was found proving the entire tuple from chip ID alone. A capability checker should accept the individual observed fields as input and report matched identities and unresolved support separately. It should not infer that all `0xf6ca` devices have the same minor revisions, tables, calibration, or driver ABI.

The archive has parallel `main/` and `sys/` header trees. The embedded AX identity definitions above are in the **sys** copy; they are not interchangeable with the older `main/src/include/bcmdevs.h` contents. Pin the exact path as well as the archive digest.

## MAC registers: useful subset, missing complete map

`WL/sys/src/dhd/sys/dhd_macdbg.c:1594-1638` explicitly describes its definitions as a **dump/debug subset**. The source uses array indices derived from byte offsets; the offsets below are byte offsets relative to a selected D11 core mapping:

| Register / port | Byte offset | Source lines |
| --- | --- | --- |
| MACCONTROL / MACINTMASK | `0x120` / `0x12c` | 1610-1611 |
| Alternate MAC interrupt mask | `0x16c` | 1612 |
| PSMx MACCONTROL / interrupt mask | `0x1b4` / `0x1c4` | 1613-1614 |
| PSMx alternate interrupt mask | `0x1cc` | 1615 |
| PSMr1 MACCONTROL / interrupt mask | `0x324` / `0x334` | 1616-1617 |
| PSMr1 alternate interrupt mask | `0x33c` | 1618 |
| Object address / data ports | `0x160` / `0x164` | 1619-1620 |
| MAC hardware capability 1 | `0x1a4` | 1621 |
| PHY register address / data ports | `0x3fc` / `0x3fe` | 1622-1623 |

The same source defines PSM-run bit 1, IHR-enable bit 10, and wake bit 26 at lines 1626-1628. Object-memory selectors at lines 1630-1638 distinguish primary ucode/SHM/scratch/IHR and extended SHM/scratch/IHR. These facts describe interfaces; they do not authorize writes or establish a reset/start sequence for core 130.

Two revision predicates are especially useful in an offline capability report:

- `dhd_macdbg_psmx_hw_supported()`, lines 1894-1897: true for core 65 or revisions >=128.
- `dhd_macdbg_psmr1_hw_supported()`, lines 1899-1902: true only for 128 or 129, therefore false for 130 in this implementation.

Do not assume that every >=128 core uses every secondary PSM bank. These are this source's debug-path rules, not a replacement for a complete hardware specification. `dhd_macdbg_halt_psms()` at lines 1911-1948 changes interrupt masks and MAC control; it is a **mutating halt/debug path**, not a safe probe or initialization recipe.

`WL/main/src/include/hnddma.h:172-174` separately documents a >=128 receive-frame-offset extension covering offsets 128–255 bytes. That is a DMA format capability, not a definition of all MAC/DMA register layouts.

`WL/sys/src/include/d11reglist_proto.h:26-79` defines typed debug register lists and separate primary/extended/PSMr1 spaces. Its `d11regs_list_t` describes dump ranges/bitmaps; it is **not** the `<offset,width,value>` initialization-record type.

The complete generated register-offset structures and revision-130 shared-memory definitions are absent as source. Static metadata in the proprietary `WL/main/src/wl/linux/prebuilt/wl.o` exposes their existence:

| Object symbol | Bytes |
| --- | ---: |
| `d11regs_struct_d11rev_130` | 24 |
| `regs_common_lt40_130` | 580 |
| `regs_common_ge40_lt64_130` | 412 |
| `regs_common_ge64_lt80_130` | 270 |
| `regs_common_ge80_lt128_130` | 192 |
| `regs_common_ge128_lt129_130` | 1,262 |
| `regs_common_ge129_130` | 72 |
| `d11shm_struct_ucode_std_d11rev_130` | 112 |

These names demonstrate that core 130 uses multiple generated register groups, including >=128 additions. Their names and byte sizes do not reveal field ordering, member names, or usable register semantics. No offset-map payload is published here.

## Initialization-record format: source corroboration and exact-core gap

The proprietary implementation's `wlc_bmac.c`, generated D11 headers, and `d11init_t` definition are absent from impl59 source. `WL/sys/src/wl/sys/wlc_types.h:101` only forward-declares `struct d11init`. `WL/main/components/router/hnd_wl/wl_generic.mk:22-27,266-275` selects prebuilt `wl.o` when the core source is absent.

The **older in-tree brcmsmac driver** provides explicit format lineage:

- `OLD/main.c:255-259`: `struct d11init` has little-endian 16-bit address, little-endian 16-bit size, and little-endian 32-bit value, giving an 8-byte record.
- `OLD/main.c:679-700`: iteration stops on address `0xffff`; size 2 invokes a 16-bit bus write, size 4 invokes a 32-bit write, and another size breaks iteration. It decodes each field from little endian.

This supports an offline `<HHI` decoder when combined with structural validation of the exact input. It does **not** establish that the modern proprietary writer has identical error handling, ordering barriers, delay behavior, or peripheral preconditions. Exact core-130 writer semantics still need independent binary/control-flow evidence.

The GPL archive's older `wl.o` metadata contains:

| Table symbol | Bytes | 8-byte slots, including terminator if present |
| --- | ---: | ---: |
| `d11ax51initvals130` / corresponding `_1` symbol | 8,600 each | 1,075 |
| `d11ax51initvalsx130` / corresponding `_1` symbol | 2,144 each | 268 |
| `d11ax51bsinitvals130` / corresponding `_1` symbol | 688 each | 86 |
| `d11ax51bsinitvalsx130` / corresponding `_1` symbol | 8 each | 1 |

Sizes alone do not establish a valid terminator or equal table contents. These sizes differ from independently inventoried current-stock tables, so digest/version pinning is essential. The archive's wireless version is 17.10.86.5, while current stock was independently observed as 17.10.121.37.

## Microcode format and startup evidence

The proprietary object contains revision-130 microcode data symbols and size companions, rather than published source arrays:

| GPL `wl.o` symbol | Bytes |
| --- | ---: |
| `d11ucode130` / `d11ucode130_1` | 61,264 / 60,920 |
| `d11ucodex130` / `d11ucodex130_1` | 25,632 / 25,632 |
| `d11ucode_mu130` / `d11ucode_mu130_1` | 63,896 / 63,552 |
| `d11ucodex_mu130` / `d11ucodex_mu130_1` | 26,360 / 26,360 |

An original offline ELF inventory can identify these objects, verify section bounds/type/size, and distinguish primary, extended, and MU-labelled variants. It cannot establish the correct runtime variant from a symbol name alone. Some other revision symbols are only 1- or 4-byte placeholders, so presence alone is not support evidence.

`OLD/main.c:2259-2274` documents an older ucode writer: little-endian 32-bit words, object-address auto-increment, a readback of the address register, then object-data writes. Lines 3193-3212 show its reset-PSM, download, run, and wait-for-self-suspend sequence. Crucially, that driver's selector at lines 2288-2307 handles cores 17/23/24 and older PHY families. It is **not** a core-130 startup algorithm.

The exact impl59 source does not expose core-130 microcode variant selection, loading order, PSMx setup, SHM ABI initialization, reset handshake, completion conditions, or timeout/recovery procedure. A data inventory and an offline replay trace are meaningful first components; they are not firmware loading or working Wi-Fi.

## PHY51 and calibration dependencies

No PHY51/AC implementation source was found in impl59. The proprietary object's file/function metadata names AC PHY pieces such as channel management, radio setup, PLL configuration, RX/TX IQ calibration, TSSI, PAPD, VCO calibration, temperature handling, transmit-power control, and HE capabilities. `phy_ac_attach` is compiled code in the object. These names identify missing implementation areas; they do not supply an open PHY51 driver.

The separately included old kernel `b43/phy_ac.c` is not an implementation of this target's PHY51. Do not treat the shared family label `AC` as proof of compatibility.

Published support code explains why initialization is board-dependent:

- `WL/main/src/include/bcmsrom_tbl.h:1191-1197` maps AC PHY power-amplifier variables by band and chain. Its wider table includes board flags, antenna/power and regulatory variables.
- `WL/main/src/include/bcmsrom_fmt.h:1072-1083` defines SROM18 as 1,024 words with a signature, a CRC/revision word, and an expanded PCIe portion. This is a supported format, not evidence that the target actually stores its calibration using SROM18.
- `WL/main/src/shared/bcmsrom.c:7352-7355` explicitly discusses SROM CRC versus device-path flash variables; lines 7462-7477 allow device-path/global `sromrev` and external NVRAM branches under build conditions.
- Same file, lines 7481-7498, contains gated fallback to manufacturing defaults after CRC failure. These generic defaults must not be substituted for actual board calibration.

The exact target's source of calibration, variable precedence, chain/radio mapping, and regulatory constraints require separate evidence. This audit did not inspect or publish device calibration values. An offline parser can validate a supplied format and report required fields without claiming the data are valid for a particular board or safe for radio operation.

## Supported first original offline component

The source facts support an implementation that performs **no hardware access**:

1. Accept an explicitly identified input object and record its digest, ELF class/endianness/machine, driver version, chip/core/PHY tuple, and origin.
2. Inventory a small allowlist of table/ucode/register-map symbols using ELF metadata and section bounds. Report placeholder-sized objects separately.
3. Decode 8-byte little-endian initialization records only after pinning the expected input and independently validating the exact modern format. Validate complete length, terminal record, widths 2/4, alignment, and configured bounds before invoking any callback.
4. Replay validated records only to a synthetic trace bus. Preserve order, duplicates, width, and value truncation semantics; do not coalesce writes or silently widen 16-bit operations. Reject malformed tables before producing a partial trace.
5. Label identity matches, debug-path revision rules, and actual supported runtime functions separately. A recognized BCM63178/core-130 identity is not a completed PHY or MAC implementation.

Synthetic tests can cover malformed/truncated records, missing or premature termination, illegal widths, misalignment, out-of-range offsets, duplicate/order preservation, empty/sentinel-only tables, wrong ELF architecture, and expected symbol/version mismatches. Do not commit vendor payloads as fixtures; synthetic records and public-safe metadata are sufficient.

The next unresolved dependency for hardware work remains the exact modern writer/selector/control-flow and complete PHY/MAC initialization behavior. This report supports an offline decoder and evidence ledger, not a kernel driver, loader, radio probe, or device-execution proposal.

The separate [current-binary attach audit](DRIVER_ATTACH_AUDIT.md) now resolves
the selected initialization-record consumer and microcode selector through REA.
The [original offline components](../driver/README.md) implement their bounded
decoder/selection subset. Full upload/startup and PHY programming remain open.
