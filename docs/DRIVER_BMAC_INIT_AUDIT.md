# BMAC initialization order and mandatory SMC path

This is an original static account of the pinned current ARM driver, followed by
an offline implementation of one newly closed mandatory component. It does not
report executing a driver, accessing a router, resetting hardware or transmitting
RF. The source identities and selected actual Evidence references are in
[DRIVER_BMAC_INIT_EVIDENCE.json](../findings/DRIVER_BMAC_INIT_EVIDENCE.json).
Full analysis records and proprietary payloads remain outside this checkout.

## Enclosing caller and state distinctions

The retained full `wlc_bmac_init` pseudocode was reused. New actual assembly
establishes source order and the distinction between three unrelated fields:

- Hardware word150 is supplied to `si_setcore(SI,812,unit)` by attach. This
  corroborates its D11 core-unit role. Its zero test gates the main/extended
  microcode, conditional VASIP and SMC download calls in BMAC initialization.
- Hardware byte186 is the separate download-skip guard inside the existing
  core128-and-newer microcode-download path.
- Hardware byte4dd is SMC's own already-loaded cache, with distinct reset/load
  behavior described below. It is neither word150 nor byte186.

`wlc_bmac_init` itself does not call the implemented core-reset wrapper.
Reset is an earlier lifecycle requirement that must be composed from its actual
caller; it must not be inserted into this function merely because the driver
needs it. The new assembly confirms the following ordered groups for core130.
Branches and runtime metadata remain explicit dependencies rather than defaults.

1. Save the original full fast-clock byte and request actual WL mode0 only when
   it was zero. Selected chip identities call PMU RF-LDO enable; BCM63178 is not
   in that observed set. Select band from chanspec bitsc000, update the PHY's
   previous/current radio chanspec fields, store the hardware chanspec halfword,
   then call actual calibration-manager initialization. Clear timeout word580.
2. A runtime DMA-mode transition can detach and reattach DMA/PIO metadata, with
   intervening mode-byte/word stores. Run BTC mode setup and the capability/
   boardflag/chip-dependent coexistence initialization, then BTC initialization.
   These helpers are prerequisites, not successful placeholders.
3. Only core-unit word150 equal0 calls microcode download, conditional VASIP
   support/init and SMC support/download. The actual SMC support predicate is
   **core revision greater than127**, so it is true for target130. The SMC call
   cannot be skipped by pretending it is an optional unsupported feature.
4. Call actual startup/acknowledgement, then main mctrl(c000,0). The stock caller
   ignores startup's return. Existing offline startup retains its declared
   fail-stop behavior; a future coordinator must label that narrower policy.
   Coexistence GPIO/MHF work follows, then eligible AMT entries are cleared,
   optional auxiliary PMQ reset runs, and a four-byte object-memory value is saved.
5. Core130 writes primary and extended initialization tables in order, then BMC
   initialization. An enabled MBSS branch clears its bounded object-memory
   entries. SHM values8 and10 precede receive-lazy setup, actual main-control
   update, interrupt/status register writes and optional BTC-task initialization.
6. Six queue interrupt/mask groups are cleared. A paired queue can require actual
   indirect queue selection before its additional register write. RX mask setup,
   actual SI coreflags and fast-power-up delay calculations follow. Later SHM/
   object-memory writes depend on actual descriptors, feature bytes, boardflags,
   stored hardware fields and optional extended banks.
7. Actual primary TX initialization visits the runtime count. Paired mode has its
   own secondary TX loop and mode-register update. RX independently visits0..3,
   runs actual RX eligibility, initializes eligible DMA channels and fills them.
   Allocation/reclaim, DMA mappings and RX-fill resources are not established by
   knowing this order. Chip-specific MAC frequency work and address programming
   follow. Address/calibration data is not published here.
8. More coexistence SHM setup and runtime counter/statistics initialization precede
   ratelink-memory initialization and target130 offload initialization. The caller
   updates several software fields and a direct register, calls actual radio
   initialization, performs chip-dependent MAC-frequency selection, then band
   initialization. Core130 band initialization writes primary and extended band
   tables and invokes actual PHY channel/calibration work; those leaves remain
   separate implementation requirements.
9. Call SI low-power optimization. Original fast0 requests actual WL mode2. A
   runtime public flag can request actual main suspend/enable, then revisions
   above127 request PMU MAC resources using core-unit word150. Finally clear the
   separate hardware tail byte23f.

This is a caller contract, not a complete initialization implementation. Object
helpers, upload/start/reset components, and the new SMC/table-write components
must be composed alongside the remaining BMC, coexistence, PHY/radio/calibration,
DMA/RX-fill/offload, resource and OS dependencies. No reached dependency can be
replaced with a callback that simply reports success.

## Implemented SMC wrapper and PHY51 reset

`driver/smc.c` now implements the actual core130/AC PHY51 load wrapper and both
reset leaves, composing the new [actual table writer](PHY_TABLE_WRITE_MODEL.md)
and the existing PHY-register modify child. The input pointer graph explicitly
binds band, PI, SMC state, callback table, AC callback context and its PI pointer.
Known null required pointers fail; unknown reached bindings stop as dependencies.
The context's PI must match the callback context's PI. Callback fields identify
the recovered actual AC implementation; the model executes its real child
operations rather than invoking an arbitrary success callback.

When the original full loaded byte is nonzero, load still executes reset1 and
only then stores loaded1. It skips reset0, reset pulse and payload transfer.
When loaded is zero, ordered behavior is:

1. Actual reset0 uses `phy_utils_write_phyreg(140b,0)`. This is a packed
   ST/W32(3fc,140b), including the actual u16 counter/threshold version-read flush
   when the supplied shared-bus word equals1. It is not a modify-with-full-mask
   substitute and introduces no extra PHY data read.
2. R16(7c0)/SY, then ST/W16(original|80), then ST/W16(original&~80). Both writes
   use the same original narrowed sample; there is no read between them. The
   pinned core130 descriptor fieldf6 was independently read to establish port7c0.
3. Actual table-write wrapper for table47, offset0, width48 and the derived row
   count. This includes the real main-control sample, optional PSM suspend,
   PHY19e guard and its restoration, data writes and optional PSM enable.
   Only after the table child returns does AC SMC state byte8 become1.
4. Actual reset1 makes four real PHY modifies, in order: 1409/8000/8000,
   1410/2/2, 140b/1/1 and 140b/2/2. Each preserves its own fresh read and actual
   barrier/write protocol. Only after reset1 returns does hardware loaded become1.

The public image interface borrows immutable caller bytes and source byte count.
The actual pinned `d11smc1` object has11420 bytes, SHA-256
`0e791de621714ef23006b41687ae8338c24a034097f9f18a174f0441225784a4`.
Stock computes floor(size/6) and keeps its low16, yielding1903 rows; its last2
bytes are not transferred. The implementation preserves this arithmetic and
bounds-checks reached data without padding or including the private array.
Caller-supplied synthetic fixtures can exercise shorter counts; the caller must
authenticate actual payload provenance before integrating a real transport.

Canonical hardware state propagates before user callbacks and after every child
step, including partial failure. Pre-I/O counter resets survive failed barriers.
At most one synchronous callback occurs per step. Callback error wins over
simultaneous cancellation; successful cancelling callbacks retain their effects.
A child cancellation does not execute later AC/loaded stores, guard cleanup,
reset1, retries or other invented unwinding. Completion means the software trace
returned, not that the SMC ran or a radio became ready.

`tests/test_smc.c` independently supplies literal operations/read replies using
a synthetic two-row image with two ignored trailing bytes. It covers both load
branches, real packed reset0/flush, reset1 reads, table48 composition, callback
failure/cancellation/both at every event, retained flags/counters, deferred
bindings/data bounds, aliases, one callback per step and terminal silence.
Strict ASan/UBSan host checks and freestanding ARMv7 compilation pass. Whole
stock-function execution, physical uploads and device state remain unverified.

## Private source and full-transfer comparison

The inventory retains its eight-entry `MICROCODE` API and adds a separate SMC
record with its size-companion metadata. The pinned module digest, ELF symbol
bounds, exact11420-byte size and SMC digest are checked before use. Default
output remains metadata only. Optional payload extraction requires a new
external directory and includes the SMC object; existing destinations and
locations inside this checkout are rejected.

`tests/check_smc_stock.py` uses caller-supplied pinned stock bytes and a trusted
host library containing the tiny `smc_stock_adapter.c` ABI shim. Python derives
an independent complete table48 trace and supplies literal pulse, guard and
reset read queues. The actual C SMC job matches1903 rows,5709 halfword data
writes and11460 ordered operations including reset0, pulse, table wrapper and
reset1. Completion flags and row/data counts are checked only after whole-child
return. Changing only the final2 private bytes produces the same complete
transfer; changing the source module is rejected before object resolution.
No payload values are printed or persisted by this checker. This compares the
original C model with private data and explicit synthetic hardware replies; it
does not execute the stock instructions, validate physical timing or upload to
a device.

## Additional closed observations and remaining gaps

The receive-lazy leaf is two actual writes: ST/W32(receive-lazy-port,value),
then, only with nonzero public FIFO3 byte, ST/W32(port+c,value). The synth delay
for target AC/core130 is1024 except the source's earlier chip-specific1200
branches; other PHY/core/chip branches have different constants. The actual
update-fast-power-up wrapper calls HND delay acquisition, stores its low16 and
writes its descriptor-resolved port, then adds synth delay and selected chip
cache-only corrections. The HND delay function was captured but has real PMU,
SI selection/IRQ/clock metadata dependencies. It is not implemented here as a
constant or successful callback. These timing helpers are lower priority than
the mandatory SMC path and remain reusable static evidence for the next package.

Calibration-manager initialization has many actual software stores and a reached
registered callback; its completion flag follows that callback. Radio init,
band init, BMC init, DMA allocation/RX fill and PMU/offload callbacks remain
required work. Pending hardware identity/recovery limits eventual device proof;
it does not turn these offline gaps into a complete working driver.
