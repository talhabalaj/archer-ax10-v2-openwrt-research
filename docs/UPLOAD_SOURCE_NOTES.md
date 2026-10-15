# Core 130 upload/readiness source notes

Static source corroboration, 2026-10-15. Input is the [official AX10 V2 GPL archive](https://static.tp-link.com/upload/gpl-code/2025/202503/20250317/ax10v2_GPL_20240317.tar.gz), SHA-256 `9670b07a1449dc7e338db9becbb019d154f0486569c1621a40f759843f655a87`. No router access, MMIO, binary execution, or proprietary payload publication occurred.

Paths below start at `ax10v2_GPL/`. `WL/` abbreviates `bcm675x/bcm963xx_router/bcmdrivers/broadcom/net/wl/impl59/`. `OLD/` abbreviates `bcm675x/bcm963xx_router/kernel/linux-4.1/drivers/net/wireless/brcm80211/brcmsmac/`.

## Published impl59 facts

The exact published impl59 source contains a useful **DHD diagnostic subset**, not the core-130 uploader. References in this section are to `WL/sys/src/dhd/sys/dhd_macdbg.c` unless stated otherwise.

| Fact | Source lines | Qualification |
| --- | --- | --- |
| Primary object address/data ports are byte offsets `0x160` / `0x164`. | 1619-1620 | Constants are expressed as 32-bit array indices after division by four. They are relative to a selected D11 core, not physical addresses. |
| Primary MAC control/mask are `0x120` / `0x12c`. | 1610-1611 | Fixed diagnostic subset, independently usable to compare against current binary analysis. |
| PSMx control/mask are `0x1b4` / `0x1c4`. | 1613-1615 | The source's PSMx helper supports core 65 or >=128: lines 1894-1897. |
| PSMr1 control/mask are `0x324` / `0x334`. | 1616-1618 | The source's PSMr1 helper supports only 128/129: lines 1899-1902. Do not apply every >=128 secondary bank to core 130. |
| PSM-run is bit 1, IHR-enable bit 10, wake bit 26. | 1626-1628 | Exact impl59 diagnostic definitions. |
| Object selection occupies a field shifted by 16: ucode=0, SHM=1, scratch=2, IHR=3, AMT=4, extended SHM=9, extended scratch=10, extended IHR=11. | 1630-1638 | No UCODEX selector is defined here. In particular, **do not infer UCODEX=8 solely from extended SHM=9**. |
| Object read address is word-indexed from a byte address. | 425-454 | `_dhd_get_d11obj32()` checks four-byte alignment and combines selector with `objaddr >> 2`; it performs an address-register readback before reading data. This is a read/debug path. |
| Read-increment behavior uses selection flag `0x02000000`. | 458-480 | Cache logic handles the next four-byte location without rewriting the address. This corroborates a read-increment flag, not the complete write-upload protocol. |

The diagnostic bus wrapper uses `0x1160` / `0x1164` at lines 431/451, with IHR window addressing formed as `0x1000 | addr` at lines 388-403. Those bus-debug addresses must not be mistaken for the D11-relative `0x160` / `0x164`, a physical MMIO mapping, or the PCI BAR base.

`dhd_macdbg_halt_psms()` at lines 1911-1948 disables masks and writes control registers. Its PSMx path intentionally omits the wake flag. This is explicitly a halt-for-dump operation; it cannot be copied into an upload/start sequence simply because the registers are the same.

## Important source gaps

The complete archive lacks the active proprietary `main/src/wl/sys/wlc_bmac.c`, complete generated D11 headers/register-map source, and the core-130 microcode uploader/selector. `WL/main/components/router/hnd_wl/wl_generic.mk:22-27,266-275` selects the prebuilt object path when the core source is absent.

Consequently, the following are **not established by published impl59 source**:

- Write-increment flag and its use for the current core-130 uploader.
- UCODEX selector, variant selection, upload order, word packing, post-upload barriers, and required SHM state.
- Current core-130 PSM jump/reset/start sequence.
- Current core-130 self-suspend interrupt bit, status-register acknowledgement behavior, readiness timeout, and recovery sequence.

The `wlioctl.h` reinitialization reason enum includes PSM-run/jump and MAC-suspend failures, but those enum identifiers do not define register bits or a readiness protocol.

## Older driver comparison: useful lineage, not core-130 proof

These constants and sequences exist in the separately included **older brcmsmac** implementation:

| Older fact | Source reference |
| --- | --- |
| `macintstatus` byte offset `0x128` | `OLD/d11.h:125-128` |
| PSM jump-to-zero bit 2; PSM-run bit 1 | `OLD/d11.h:478-479` |
| Gracefully suspended interrupt bit 0 (`MI_MACSSPNDD`) | `OLD/d11.h:491-493` |
| Ucode selector zero, write increment `0x01000000`, read increment `0x02000000`, combined auto increment `0x03000000` | `OLD/d11.h:594-604` |
| Set object address to auto-increment ucode, read address register back, stream decoded little-endian 32-bit words through object-data port | `OLD/main.c:2259-2274` |
| Reset PSM, download, start PSM, wait for self-suspend | `OLD/main.c:3193-3212` |

Its selector at `OLD/main.c:2288-2307` handles core revisions 17/23/24 and older PHY families. These facts must remain marked **comparative** until corroborated by current core-130 binary/control-flow evidence. The old wait loop's one-second bound is not an established core-130 timing requirement.

## Shared helpers do not prove firmware readiness

- `WL/main/src/shared/siutils.c:3562-3575` dispatches `si_iscoreup()` by backplane type. For AI, `aiutils.c:871-879` checks wrapper clock/forced-clock and reset state. That is core clock/reset availability, **not** confirmation that ucode uploaded, started, or self-suspended.
- `WL/main/src/shared/hnddma.c:1587-1602` checks a DMA suspension request/control flag. `dma_txsuspendedidle()` at lines 1881-1899 additionally checks a DMA idle state. These concern descriptor DMA, **not** MAC PSM self-suspend or firmware readiness.
- Shared `siutils` and `hnddma` therefore cannot fill the missing core-130 writer/start protocol merely because they are published source.

## Consequences for an offline simulator

Use separately named states for core clock/reset availability, object upload completion, PSM start, MAC self-suspend, and DMA idle. A successful synthetic readback in one state must not automatically satisfy the others.

Keep register offsets, selectors, increment flags, readiness mask, acknowledgement behavior, and timeout policy in an explicitly versioned protocol description. Mark each field as current-binary-confirmed, impl59-source-corroborated, comparative-only, or unresolved. Reject an executable protocol when required fields remain unresolved; an offline model may still exercise clearly labelled hypotheses.

Synthetic callbacks may model address selection, readback ordering, word writes, delayed readiness, timeout, and error propagation without hardware or vendor data. Preserve word count and ordering. Use bounded/injected readiness responses and expose the resulting trace as simulator behavior. A passing simulator test validates that model and its state machine; it does not validate the model's unconfirmed hardware assumptions or demonstrate a running MAC.

This report provides original commentary and public source facts only. It does not include an upload payload, calibration data, a device-execution recipe, or a working wireless driver.
