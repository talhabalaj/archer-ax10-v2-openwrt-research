# D2 context, object memory, reset, and calibration source notes

Original static-source findings, 2026-10-15. Source: [official AX10 V2 GPL archive](https://static.tp-link.com/upload/gpl-code/2025/202503/20250317/ax10v2_GPL_20240317.tar.gz), SHA-256 `9670b07a1449dc7e338db9becbb019d154f0486569c1621a40f759843f655a87`. No device access, register writes, target execution or proprietary payload copying occurred.

Paths start at `ax10v2_GPL/`. `WL/` abbreviates `bcm675x/bcm963xx_router/bcmdrivers/broadcom/net/wl/impl59/`. Unless otherwise stated, these references are published impl59 support source, not older brcmsmac comparisons.

## Object-memory addressing: facts and bounds

References in this subsection are to `WL/sys/src/dhd/sys/dhd_macdbg.c`.

| Fact | Lines | Boundary |
| --- | --- | --- |
| Object address/data are D11-relative byte offsets `0x160` / `0x164`. | 1619-1620 | Do not confuse with the diagnostic bus window at `0x1160` / `0x1164`, used at 431/451. |
| Object selectors are shifted by 16; primary IHR selector is `3 << 16`, extended IHR is `11 << 16`. | 1630-1638 | Selector names describe different address spaces. |
| A 32-bit debug object read receives a **16-bit byte address**, checks four-byte alignment, and writes `selector | (byte_address >> 2)` to the object-address port. | 425-454 | The code prints an alignment error rather than providing a hard rejecting validator. A caller should not treat that behavior as safe input validation. |
| A 16-bit object read selects a containing 32-bit word and chooses its low/high half according to byte-address bit 1. | 458-481 | Address units and halfword lanes matter; a word-indexed API and a byte-indexed API cannot share raw offsets without conversion. |
| Extended IHR debug addresses use `(logical_address - 0x400) << 1` before the object-read helper; PSMx selection includes `0x020b0000`, PSMr1 uses `0x02130000`. | 514-520 | This corroborates the `0x400` origin and halfword-indexed IHR scheme in a **debug read path**. It does not prove the current init writer's threshold or exact truncation behavior. |
| Scratch indices are multiplied by four, while SHM addresses are passed as byte addresses. | 522-544 | Each selector has its own addressing convention. A single untyped integer API can hide mistakes. |
| `0xffff` is used internally to read the current data port without reprogramming the address. | 433-435,467-469 | It is a debug-helper sentinel, not evidence that address `0xffff` is a valid application access. |

The independent current-object analysis reports an init-writer route for offsets >=`0x1000` that computes the **u32 byte address** `((offset - 0x400) >> 1) << 2`, passes the **value's low 16 bits** with byte length 2, and uses IHR selector `0x30000`. The address is not truncated to 16 bits. The source above is consistent with IHR indexing concepts, but **does not independently establish that writer's `0x1000` threshold, width-specific callbacks, input range, or low-16-bit semantics**. Those remain binary/control-flow facts and must be cited to their separate evidence.

A 16-bit argument type is not a hardware capacity specification. No authoritative full core-130 object-memory capacity or complete valid-register range was found in published source. An original offline implementation should validate the intended logical range and alignment *before* narrowing, distinguish byte addresses from word/halfword indices, and detect unexpected wraparound. Do not silently accept an overflowing caller address just because the older debug reader accepts a 16-bit address argument. The current init writer passes a u32 address; only its value is narrowed.

## Runtime addend `hw + 0x260` remains unnamed

`WL/sys/src/wl/sys/wlc_types.h:165-167` only forward-declares `wlc_hw`, `wlc_hw_info`, and `wlc_hwband`. Their active definitions and the proprietary common-init/BMAC source are absent. The GPL `wl.o` has no `.debug*`, `.zdebug*`, `.BTF`, or `.ctf` type-information sections from which this field can be named.

The current consumer reads a 32-bit addend from the **host hardware object at `hw + 0x260`** when a table record targets the descriptor-selected D11 register `0x130`. The available support source does not establish that addend's semantic name, acquisition, lifetime or ownership. Keep it an explicit opaque runtime input until stores/readers are traced in the exact binary. No host structure field at `+0x130` is established by this observation.

There is an unrelated hardware constant: `WL/sys/src/dhd/sys/dhd_macdbg.c:100-101` names D11 MMIO offsets `0x130`/`0x134` as template write pointer/data. This can describe the target register, but does **not** name the host addend at `+0x260` or prove its meaning. Hardware register offsets and host C structure offsets are different address spaces.

## BCM63178 address-space and SI prerequisites

`WL/main/src/shared/aiutils.c:136-154` explicitly explains the embedded hard WLAN macro on BCM63178/47622. Its EROM addresses describe identical hardware instances, while UBUS translates them into distinct host CPU address ranges. The helper converts nonzero AXI addresses as:

`CPU address = AXI address - SI_ENUM_BASE_BP_2x2AX + instance enum_base`

The base constant is `0x84000000` in `WL/main/src/include/hndsoc.h:52`. `ai_scan_uniq_cpu_addresses()` at `aiutils.c:159-177` applies this correction to core, wrapper, OOB-router, and bridge-wrapper addresses; the SI-bus EROM path at lines 198-204 also corrects the EROM address. The code describes `enum_base` as the physical address from the pseudo-PCIe BAR0. A complete driver must model this translation rather than treating all EROM values as final CPU addresses.

Published reset support exists:

- `WL/main/src/shared/aiutils.c:871-879`: `ai_iscoreup()` checks clock/forced-clock state and wrapper reset state. It does not establish MAC/PHY or ucode readiness.
- Same file, lines 1043-1078: generic disable waits for pending backplane operations, asserts reset, reads back control, then applies caller-provided control bits.
- Same file, lines 1081-1149: generic reset distinguishes persistent `bits` from reset-only `resetbits`, waits around reset/control operations, enables clocks, deasserts reset with a bounded retry, and contains a conditional D11 ucode-corruption workaround.
- Same file, lines 1154-1166: a core with a second wrapper is reset through both wrappers. A single-wrapper model cannot be assumed universally.
- `WL/main/src/include/hndsoc.h:295-296` defines forced gated clock `0x2` and clock enable `0x1`; `aidmp.h:372` defines reset bit `1`.

These generic helpers do not provide the proprietary caller's BCM63178/core-130 `bits`, `resetbits`, workaround build flags, call ordering, ownership, or PHY-specific clock requirements. The active caller and current HND implementation require separate binary evidence; generic source alone does not establish them.

Clock-resource support is more specific than a generic legacy chip assumption:

- `WL/main/src/shared/hndpmu.c:281-283,331-334` selects the BCM63178 embedded-AX resource positions.
- `WL/main/src/include/sbchipc.h:2414,2418` identifies HT-available resource position 6 and MAC/PHY-clock-available resource position 10. These are resource indices, not direct control-register offsets.
- `hndpmu.c:3257-3269` includes embedded AX devices in a branch that checks HT availability and waits for PMU steady state. It belongs to a larger resource-transition operation; it is not by itself a standalone bring-up recipe.

Thus shared source is useful for an original SI/PMU model, but hardware address translation, wrapper state, clock resources, PSM state, and PHY readiness must remain distinct. A successful check of one does not prove the others.

## Calibration schema is partly published; target semantics remain incomplete

The archive does contain reusable schema information, rather than only opaque private calibration blobs:

- `WL/main/src/include/bcmsrom_tbl.h:31-37` defines schema entries by variable name, revision mask, flags, word offset, and bit mask.
- Same file, lines 39-49 and 54-65, describes continuation, signed/hex/country-code/address/array formatting, and table termination. Lines 77-98 give revision-aware board identity/flag examples. These are schema facts, not target calibration values.
- `WL/main/src/include/bcmsrom_fmt.h:1072-1083` defines the SROM18 layout, signature, size, CRC/revision position, and expanded PCIe segment.
- `WL/main/src/shared/bcmsrom.c:7098-7117` formats values according to schema flags, and lines 7131 onward select per-path layouts by revision.
- `WL/main/src/shared/siutils.c:4880-4888,4902-4924` explains instance-prefixed NVRAM variables; embedded AX chooses an instance for its SI-bus device path. Calibration cannot be treated as one global unqualified dictionary.
- `WL/main/src/shared/bcmsrom.c:7352-7355,7462-7477` documents SROM CRC handling and conditional external/device-path NVRAM fallback.

An original offline schema/parser can use these published format facts and synthetic data. Still unresolved are the physical target's exact storage format, revision, variable precedence under the current build, chain/radio mapping, required PHY51 coefficients, units/allowed ranges for every field, dynamic calibration algorithm, and valid regulatory/power behavior. Published generic defaults and reference-board files do not replace those requirements.

No target calibration values were read or published for this source task. A schema-valid input is not automatically correct for this board, sufficient for PHY51 initialization, or safe for radio transmission.

## What this advances

This source audit supports typed offline object-memory addressing, explicit unknown context fields, instance-aware SI address translation, wrapper/reset/clock state models, and a revision-aware calibration schema parser. It does not supply a completed wireless driver or permission to execute register operations. The exact proprietary context-field and caller semantics remain work for independent binary analysis, and PHY51 hardware behavior remains unresolved.
