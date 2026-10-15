# Published HND DMA64 source audit

This is an original factual map of selected source in TP-Link's exact Archer AX10 V2 GPL release. It supplies common contracts to check against the current stock driver; it does **not** establish that its compile-time choices, caller parameters, private structures, register bases, or reset branches match stock `wl` 17.10.121.37 / D11 core 130. No device was accessed and no source or binary payload is reproduced here.

## Provenance and scope

- Official model/revision page: <https://www.tp-link.com/us/support/download/archer-ax10/v2/>.
- Official archive: <https://static.tp-link.com/upload/gpl-code/2025/202503/20250317/ax10v2_GPL_20240317.tar.gz>.
- Archive: 644,861,617 bytes; SHA-256 `9670b07a1449dc7e338db9becbb019d154f0486569c1621a40f759843f655a87`.
- All paths below are archive member paths relative to `ax10v2_GPL/bcm675x/bcm963xx_router/bcmdrivers/broadcom/net/wl/impl59/main/src/`, abbreviated `S/`. Line numbers count the original extracted text, starting at 1. The duplicate `sys/src/include/sbhnddma.h` was not used for these citations.
- This archive's driver version is 17.10.86.5, older than the retained current stock driver. `S/shared/hnddma.c:22` identifies its source revision as 774125, dated 2019-04-11; `S/include/sbhnddma.h:22` is revision 767290, dated 2018-09-04.
- These seven audited files have explicit permissive Broadcom copyright/permission notices at their beginnings (for example `S/shared/hnddma.c:5–20` and `S/include/sbhnddma.h:5–20`). Archive placement alone is not a license determination for other files. This report contains original descriptions and identifier/numeric facts, not a source redistribution.

| Source member after `S/` | SHA-256 |
| --- | --- |
| `include/sbhnddma.h` | `674fe54f248324bd46dc6f774b09c381a1691c03af22718a8090a738abf90ed8` |
| `include/hnddma.h` | `7f4b09404f52823e508b341551f808f03ffff4494259c50ffba92f31172549e5` |
| `shared/hnddma_priv.h` | `62a098317881801b2b2af8a1da9313665010a5c55822ba03114da19de8fdae56` |
| `shared/hnddma.c` | `c58b0e1b039d71cad285c8652cfcd4bc43e5e3781ae2edac6c86c8bda4b3f6da` |
| `shared/hnddma_tx.c` | `6a5ad8359260ddeeb648488928fba7a0ec28e3c89da20c3d110d2d84f684d099` |
| `shared/hnddma_rx.c` | `09a998c8e8bfa8d7f48eb58639662d3ecf1d090af9842382398bdcd2ee31f5f9` |
| `include/bcmutils.h` | `eed92b33aa0cf6d7547f223560cfc47ef9bd2043dbfcf5b094fc79917db20a4d` |

## Descriptor and channel layout

`S/include/sbhnddma.h:218–231` defines one DMA64 channel as six consecutive 32-bit registers. The offsets **relative to the channel pointer supplied by its caller** are:

| Relative byte offset | Register | Source meaning |
| --- | --- | --- |
| `0x00` | control | enable/control flags |
| `0x04` | ptr | producer descriptor pointer |
| `0x08` | addrlow | ring base low word |
| `0x0c` | addrhigh | ring base high word |
| `0x10` | status0 | current descriptor and channel state |
| `0x14` | status1 | active descriptor and error state |

The paired type places TX then RX, but this does not locate any core-130 FIFO block or prove its actual register spacing: `dma_attach_ext` accepts TX/RX register pointers supplied by the driver (`S/include/hnddma.h:201–204`; `S/shared/hnddma.c:226–228`).

`dma64dd_t` is four consecutive 32-bit words: `ctrl1`, `ctrl2`, address low, address high, totaling 16 bytes. The source explicitly says hardware reads descriptors and never writes them back (`S/include/sbhnddma.h:240–249`). There is no generic descriptor ownership bit or per-descriptor completion writeback in this definition. Buffer data/status can still be written separately.

| Descriptor fact | Value | Source |
| --- | --- | --- |
| Ring terminator EOT | ctrl1 bit 28 | `S/include/sbhnddma.h:425` |
| Interrupt on completion IOC | ctrl1 bit 29 | `:426` |
| Frame EOF / SOF | ctrl1 bits 30 / 31 | `:427–428` |
| Fixed burst / coherent / NOTPCIE / delay suspend | ctrl1 bits 16 / 17 / 18 / 19 | `:421–424` |
| Core-specific field | ctrl1 bits 20–27; semantics absent here | `:420`, `:436–437` |
| Buffer byte count field | ctrl2 mask `0x7fff`; header separately limits real data length to at most 16 KiB | `:431` |
| Address extension / parity | ctrl2 bits 16–17 / bit 18 | `:432–434` |

The 15-bit count field is **not** proof that 32-KiB buffers are supported. Frame chaining, core-specific flag meanings and the current hardware's transfer limit need their own validation.

## Address units, ring capacity and ownership

1. Ring arithmetic uses power-of-two descriptor counts and mask-based wrapping (`S/shared/hnddma.c:217–224`; `S/shared/hnddma_priv.h:319–337`). Zero is separately used for an absent direction; it must not be interpreted as a populated zero-sized ring.
2. Producers write the **next free descriptor position**, converted from index to bytes: `ptr = pointer_base + index * 16`. The TX commit macro is at `S/shared/hnddma_priv.h:364–370`; RX posting at `S/shared/hnddma_rx.c:1129–1141`; TX posting and one-slot-reserved accounting at `S/shared/hnddma_tx.c:428–437`. Despite the register comment's wording, the operational code does not write `index - 1`.
3. `txin`/`rxin` identify the next descriptor to reclaim; `txout`/`rxout` the next slot to post (`S/shared/hnddma_priv.h:202–227`). Capacity accounting reserves one slot: initial availability is `n - 1` (`S/shared/hnddma.c:856–857`, `:1456–1457`). Empty and full must remain distinguishable through software indices.
4. Completion comes from hardware current/active descriptor pointers, not modified descriptors. TX uses masked `status0`, subtracts `xmtptrbase`, masks again, then divides by 16; its `TRANSFERED` range additionally consults `status1` and can stop before the active descriptor (`S/shared/hnddma_tx.c:934–978`). `ALL` instead chooses software `txout` and is not proof of hardware completion. RX similarly converts `status0`, and ordinary reclaim stops when `rxin` reaches that current index (`S/shared/hnddma_rx.c:1364–1379`). Software packet tracking and mapping lifetimes remain separate from descriptor memory.
5. The nominal header limits are an 8-KiB ring/boundary and 512 descriptors, with large support permitting 4096 descriptors within 64 KiB (`S/include/sbhnddma.h:251–267`). These are not universal alignment settings: attach probes writable address/pointer bits and sets per-direction status pointer masks (`S/shared/hnddma.c:332–407`); it probes alignment separately (`:720–732`). The allocator chooses the 8-KiB or 64-KiB boundary from the runtime mask (`:1730–1766`). Alignment can be 8 KiB, 4 KiB for small legacy tables, or 16 bytes in the non-MIPS relaxed path, with a descriptor-only workaround requiring 4 KiB (`:462–489`). The actual core-130 result must be identified; do not hardcode the nominal `0x1fff` pointer mask.
6. Relaxed-base operation includes the low physical ring address in `xmtptrbase`/`rcvptrbase` (`S/shared/hnddma.c:572–575`, `:768–773`). Thus pointer/status fields are byte-address positions relative to the implemented boundary, not necessarily pure zero-based byte offsets.
7. Ring addresses and buffer addresses have distinct low/high translation offsets. SI-style buses use the zero-offset path while PCI/PCIe paths can add windows (`S/shared/hnddma.c:421–452`). Base programming handles address extension (`:781–814`). Descriptor construction separately translates buffer addresses, writes via `BUS_SWAP32`, optionally computes parity, performs selected descriptor-cache maintenance and executes `DMB` before posting (`S/shared/hnddma_rx.c:1249–1306`). Current CPU virtual addresses cannot simply be substituted for mapped DMA addresses.

The normal TX path maps each data buffer, handles a possible scatter/gather list, sets SOF/EOF and EOT, and normally requests IOC on the committed last frame (`S/shared/hnddma_tx.c:229–320`). Reclaim removes packet tracking, conditionally unmaps according to platform/cache build choices, and advances `txin` (`:984–1066`). RX uses analogous platform-dependent buffer mapping/cache paths (`S/shared/hnddma_rx.c:926–978`) and reclaim (`:1418–1446`). `BCA_HNDROUTER` enables bulk descriptor flushing in this published header (`S/include/hnddma.h:40–43`); generic unmap branches are deliberately compiled out in some router cases. The current driver's coherent mapping, buffer-pool and barrier behavior must be checked as a unit.

## Reset, suspend and interrupts

Channel state is the high nibble of status0: disabled 0, active 1, idle 2, stopped 3, suspend-pending 4. Enable is control bit 0, TX suspend request bit 1 and TX flush request bit 4 (`S/include/sbhnddma.h:283–327`, `:329–354`, `:384–402`). These state values are encoded at bit 28, not unshifted MMIO values.

| Published routine/branch | Contract and limits |
| --- | --- |
| `dma_txreset`, direct | Empty TX direction succeeds without I/O. Already-disabled status succeeds early. Otherwise writes **control = suspend request**, polls until disabled/idle/stopped or the budget ends, writes **control = 0**, then polls disabled. A failed first wait still reaches disable. A failed final wait records a diagnostic/assertion and, if execution continues, delays 300 us and returns false. `S/shared/hnddma.c:1793–1845`. |
| `dma_txreset`, indirect packet DMA | Forces queue selector because core reset may have returned it to zero; writes control = 0; deliberately skips channel-status polling. Its initialized local status remains disabled, so success does not attest a hardware observed-disabled state. Same source range, especially `:1803–1807`, `:1823–1838`. |
| `dma_txreset`, indirect descriptor-only | Forces queue selector; skips the direct suspend phase but does execute the disabled-state poll. Same source range. |
| `dma_rxreset` | Empty RX direction succeeds without I/O; otherwise writes control = 0 and polls RX disabled, returning the last observed result. This body does not itself select an indirect queue. `S/shared/hnddma.c:1863–1877`. |
| `dma_txsuspend/resume` | Direct sets/clears control suspend bit; indirect changes the appropriate common `SuspReq` bitmap. `S/shared/hnddma.c:1544–1583`. |
| `dma_txsuspended` | Reports the request bit/bitmap, **not** completed suspension. `:1586–1602`. `dma_txsuspendedidle` separately requires direct control suspend plus idle status after selecting a queue if needed (`:1880–1899`). |
| `dma_txflush/clear` | Indirect changes common `FlushReq`; direct requests suspend+flush, then clear waits for disabled/idle/stopped before removing flush. This clear wait does not return a success/failure result. `:1605–1651`. |
| `dma_txchan_reset` / `dma_rxchan_reset` | Different helpers: toggle enable off/on and clear software packet-pointer arrays, without waiting. They are not substitutes for the above reset routines. `:2765–2789`. |
| `dma_chan_enable(false)` | Another distinct paired-channel path: preserves other control bits while suspending/disabling TX, then clears suspend and RX enable. `:2954–2995`. |

`SPINWAIT` is a delay-count budget, not an elapsed-time deadline. With the unoverridden default period 10 us, a 10,000-us budget performs at most 1000 delay callbacks and can evaluate its predicate 1001 times because the predicate is evaluated before the budget comparison (`S/include/bcmutils.h:108–124`). The effective current build might override that period. Assertions and logs are build dependent; a final diagnostic is not a recovery procedure.

These reset bodies depend on polling and delay callbacks; they do not await or acknowledge an interrupt themselves. IOC requests completion notification, but this source does **not** establish core-130 MAC interrupt routing, masking/acknowledgment, interrupt-to-FIFO mapping, NAPI/tasklet scheduling, or how packet transmission status gates buffer lifetime. A DMA current-pointer advance alone is insufficient evidence of successful over-the-air transmission. The two named TX reclaim ranges also show that data transfer and later completion are distinct concerns.

## Multiple FIFOs and descriptor-only variants

- `dma_attach` is the legacy no-common-state wrapper. It forwards zero flags and queue index zero to `dma_attach_ext`; it cannot be assumed to describe a modern multi-FIFO configuration (`S/shared/hnddma.c:634–646`).
- The indirect common object holds a shared queue selector, cached last selection and up to three suspend/flush bitmap-register pointers (`S/shared/hnddma_priv.h:135–149`; `S/include/hnddma.h:184–186`). For queue `q`, attach selects array element `q / 32` and mask bit `q % 32` (`S/shared/hnddma.c:196–209`). This API capacity is not proof that core 130 instantiates 96 FIFOs.
- A selector write can be skipped when the cached selection matches; override forces it (`S/shared/hnddma.c:112–143`). The helper contains no locking. Caller serialization and invalidation of cached selection after other agents/core resets must be established separately.
- Descriptor-only queues have no ordinary packet payload ownership contract, expose `dma_txdesc`, and have distinct workarounds. `dma_txdesc` takes partially formatted descriptor fields and adds NOTPCIE/coherent/EOT where applicable before posting (`S/shared/hnddma_tx.c:621–702`). It is not the normal packet-mapping TX path.
- The header describes a descriptor-current-pointer workaround for descriptor-only CT mode (`S/include/sbhnddma.h:367–372`), and the source explicitly discusses AQM descriptor alignment (`S/shared/hnddma.c:481–486`). Another special active-descriptor synchronization loop is restricted to **core 65**, not core 130 (`:1487–1506`). These conditions must not be generalized to every modern core.
- Channel-switch support is advertised for D11 revision 64+, and extended RX offset for revision 128+, but attach enables them from **caller flags** (`S/include/hnddma.h:169–174`; `S/shared/hnddma.c:242–250`). The caller must establish current core-130 policy.
- Split RX can consume two descriptors per buffer, with linked FIFO ownership rules and optional status-FIFO/HWA/bulk paths (`S/shared/hnddma.c:854–865`; `S/shared/hnddma_priv.h:265–269`; `S/include/hnddma.h:178–182`, `:230–235`). These are alternatives, not evidence that AX10 uses all of them.

## Questions the current-binary investigation must resolve

1. Which current functions implement packet TX/RX, descriptor-only/AQM TX, reset, queue selection and reclaim? Do their instructions corroborate the older definitions and wait counts?
2. What are the exact core-130 TX/RX channel bases, indirect selector/common bitmap register addresses, instantiated queue count and FIFO-to-channel mapping? Which channels are absent, direct, indirect or AQM?
3. What attach flags, ring depths, masks and alignment values does BCM63178 actually select? Which address-width/window offsets and descriptor core-specific bits are active? Does current code probe or use fixed capabilities?
4. What is the current descriptor and data-buffer DMA mapping/coherency model, including barriers and cache flush/invalidate, buffer-pool ownership and scatter/gather segmentation? Which bus abstraction is used for the embedded WLAN device despite its PCI-like enumeration?
5. How are TX headers, AQM/CT descriptors, RX prefixes and PHY/status buffers formatted for core 130? Which software completion or transmission-status event permits each buffer and descriptor to be reclaimed?
6. What current global locks serialize queue selection against interrupts, tasklets, other FIFO users and reset? When is cached selection invalidated?
7. Which reset branch is actually called, what surrounding MAC/FIFO suspend/drain order is required, and what happens after each timeout? Is forced reclaim reached only after hardware can no longer access buffers?
8. Which core/MAC interrupts correspond to DMA completion/error, how are they masked and acknowledged, and what recovery follows protocol/descriptor/transfer/core errors? The generic status enums do not provide this integration.

A useful original offline first component can validate descriptor layout, flag bounds, byte/index conversion, power-of-two ring accounting and separate producer/reclaimer positions against synthetic data. It must not imply that queue locations, firmware ABI, live DMA safety, packet transmission or a complete core-130 driver have been established.
