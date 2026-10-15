# First BCM63178 driver components

These are original, offline C components for the core 130 investigation. They
have no kernel binding, MMIO backend, device firmware uploader or RF operation.
They are useful building blocks and test oracles, not a functioning Wi-Fi driver.
All original code here uses the repository's MIT license.

## Implemented

- `ucode_select.c`: select primary/extended microcode symbol names and pinned
  lengths for D11 core 130 minor 0/1, using bit 0 of the correct slice's
  boardflags. Unknown cores/minors fail with empty output. Selection does not
  establish readiness or permission to upload; the stock downloader also has
  runtime skip/ownership conditions not implemented by this pure selector.
- `init_table.c`: decode the pinned little-endian 8-byte initialization-record
  format and validate the entire input before any recording callback. Preserve
  widths, order and repeated offsets; stop on callback failure without retry.
  Only offsets within a caller-bounded window of at most `0x1000` are accepted.
- `scripts/radio_firmware_inventory.py`: pin the exact stock ELF digest, resolve
  selected file-backed objects through section indices, reject relocations into
  those objects, check microcode size companions, and emit metadata only.
- `ucode_upload.c`: reproduce the fresh-chunk core-130 transfer trace, one
  simulated I/O event per step. Both input lengths and the core/minor are
  validated before callbacks. Input chunk state must be supplied explicitly
  as zero. Failure/cancellation is terminal; no automatic retry or reset occurs.
- `ai_reset.c`: bounded core-130 AI-wrapper reset trace, including optional
  secondary-first ordering, readback-derived values and stuck-reset diagnostics.
  Trace completion does not claim reset success.
- `psm_start.c`: main/extended startup-suspended acknowledgement and SHM FIFO
  verification, with explicit control caches/flags and fail-stop policy.
- `psm_control.c`: wake, main/extended suspend/enable, generic reason set/clear
  and actual wake-control action with current-mask predicate integration; exact
  budgets, cache/mirror commitment and explicit unavailable-state/callout dependencies.
- `dma_reset.c`: actual current TX/RX reset paths, including indirect selection,
  bounded state polls and separate observed-disabled versus returned-success.
- `clock_control.c`: core-130 D11 clock/PLL/secondary traces with separate
  samples, exact wait/final-read contracts and observation freshness. HND
  fallback remains explicit in this low-level API; the separate WL caller
  composes it with actual SI children.
- `chipcommon_clock.c`: retained HND clock-operation body, typed ChipCommon/PCI
  resources, crystal paths and exact mode/poll semantics. The separate SI/WL jobs
  now compose selection/IRQ and cached-mode/inline-reason updates.
- `phy_register.c`: direct PHY read/modify, analog/reset pulse and one/two-core
  TSSI snapshot/restore with explicit partial validity and commit boundaries.
  High reads and three/four-core TSSI expose table dependency.
- `phy_table.c`: table29/count1/width32 wrapper using real PHY and PSM children,
  packed32 header writes, counter-dependent flushes and explicit partial words.
- `phy_tssi.c`: all-core-count callback with a four-slot buffer, real repeated
  table reads for extra slots and direct lower-slot masked restores. Outer
  MAC/PHY-entry/wake nesting is a separate implemented job.
- `phy_guard.c`: byte nesting with actual reason-2 PSM override children, bounded
  transitions and retained partial state.
- `idle_tssi.c`: optional callback wrapper with actual MAC suspend → PHY enter →
  all-core callback → PHY exit → MAC enable; no invented failure unwind.
- `wake_policy.c`: exact ordered pure stay-awake predicate, including RRM,
  association, current override and three BSS passes. Explicit known inputs
  distinguish reached missing state from an actual false predicate.
- `phy_clock.c`: AI core-flags, PHY clock enable/disable, bandwidth preparation
  and optional secondary-core workaround, with typed owned wrapper resources.
- `phy_reset.c`: composed target AC/band-PHY51 reset, outer snapshot before
  preparation, pulse/delay100, late bandwidth store, analog, restore and real BF.
- `bf_clean.c`: main/extended MAC and ordered metadata scans with actual core130
  no-SHM leaf paths; explicit reached bounds/availability and strict 256-slot cap.
- `si_select.c`: actual bounded ID/unit/index search, cache/mapping stores,
  typed OS-map/PCI requests and used zero returns; SB is still a dependency.
- `si_clock.c`: fast/ordinary CC selection, real body and selectors, IRQ token
  and explicit post-callback SI state, independent restore guard/index handling.
- `wl_clock.c`: complete D11-versus-HND clock-select caller; fallback stores
  cache byte before inline reason0x10 without adding PSM helpers.
- `core_reset.c`: enclosing clock/core-up/DMA/FIFO/low-reset/cache/PHY/PLL
  sequence, with original-fast tail decisions and explicit reached metadata.
- `core_reset_low.c` / `core_reset_low_ai.c`: actual lower callback/auxiliary
  core849 path, generic AI reset and real primary/secondary selection.
- `si_core.c`: actual selected-wrapper core-up and coreflags protocols.
- `mctrl.c`: exact cached main-control/mirror and reset software-field helpers.
- `object_memory.c` / `init_io.c`: actual COPY/COPY32/SET32/WRITE16 object ports,
  discarded selector readbacks, halfword lanes and effective-init lowering with
  whole-helper record commitment. Own-addend aliases survive restarted begins.
- `phy_table_write.c`: actual generic AC table-write wrapper for widths8/16/32/
  42/48/60/64 and direct packed PHY writes, real PSM/PHY guards and flush counters.
- `si_pmu_gpio.c`: actual PMU vreg/GPIO corereg map/cache, PCI windows, ordinary
  SI selection and IRQ-state/restore rules; actual resource inputs are explicit.
- `phy_init.c`: complete registered PHY51 table-init callback, using actual PHY,
  table and PMU/GPIO children with known OTP and caller-owned private spans.
- `smc.c`: actual core130/PHY51 mandatory SMC wrapper, reset0/pulse/table47-width48/
  reset1 and source-ordered loaded stores. Already-loaded still executes reset1.
- `dma_descriptor.c`: current sixteen-byte formatter/codec, low-address extension,
  parity and strict ring/status/range arithmetic. Pure values imply no DMA owner.
- `dma_tx_post.c`: actual descriptor-only posting with fresh parity memory reads,
  out-before-producer and late available stores. No normal packet TX/completion.
- `dma_rx_take.c`: actual status/cached-current/force gates, slot read/clear,
  consumer/availability stores and mode-dependent sentinel skip. Returns labels.
- `radio_identity.c`: actual high-first radio query, registered AC context->PI
  binding and separate AC/common decoders. No actual board radio ID is assumed.
- `radio_control.c`: actual target63178 OFF, radio register read/write/modify and
  recovered identity50e0 ON prefix. Every ON remains a reached dependency.
- `phy_scheduler.c`: capacity23 registry insertion, full16 ordered init vector
  and real closed software/PHY/radio/table/PSM/SI children. Full target stops at
  required radio7; isolated shorter vectors do not establish target initialization.
- `dma_rx_list.c` / `dma_rx_status_list.c`: current closed bulk leaves, fresh
  discarded descriptor reads, distinct NEXT store widths and commit ordering.
- `rx_packets.c` / `rx_status.c`: real bulk/status children, packet length/trim/
  continuation/drop and status timeout/pool-return bodies, typed actual OS
  services and explicit unknown registered pool-callback dependency.
- `effective_init.c`: context-dependent initialization semantics with explicit
  `0x130` addend, width truncation and a separate semantic object-copy operation.
  Whole-table preflight validates caller-supplied bounds before callbacks.
- `status_poll.c`: independent caller-configured status polling with an injected
  monotonic timestamp. Each step performs at most one read. This is a generic
  test facility, with no claimed BCM63178 ready register, mask or start sequence.

The record validator intentionally rejects nonzero upper bits of 16-bit values,
nonzero terminator fields, trailing records and misalignment. Those restrictions
are stricter than the observed stock consumer, which stops on offset `0xffff`
and truncates 16-bit writes. All four inspected stock tables satisfy the stricter
subset. This is not a general emulation of every vendor table.

## Effective initialization and remaining gaps

The raw `init_table.c` recorder remains intentionally limited. Use the separate
`effective_init.c` model for the recovered runtime adjustment at register
`0x130` and offsets at or above `0x1000` routed to object memory. These APIs have
different validation policies; neither callback should be wired directly to
register writes. The effective model emits helper arguments for object copies;
`init_io.c` now lowers them through the actual `object_memory.c` port operations.
The caller must explicitly provide the runtime addend when used; no active
board value is assumed. See [physical lowering](../docs/OBJECT_MEMORY_MODEL.md)
for resource, source-size and whole-helper completion requirements.

See [effective-init semantics and tests](../docs/EFFECTIVE_INIT_AUDIT.md) for
exact termination, widths, address units, policies and cancellation behavior.

Hardware microcode upload, higher initialization around the implemented reset lifecycle,
runtime wake-policy acquisition/fatal/MBSS handling, complete register/SHM typing, resource
ownership, radio/channel/calibration, remaining PHY initialization callbacks and
Linux packet TX/RX integration
remain absent. Offline wrapper reset and startup subsets are described in
[their implementation boundaries](../docs/LIFECYCLE_MODEL.md). PHY type 11 and
the name `ax51` do not establish an
upstream driver implementation. These components execute no device microcode.

See [REA attach/selection evidence](../docs/DRIVER_ATTACH_AUDIT.md),
[upload writer evidence](../docs/DRIVER_UPLOAD_AUDIT.md),
[source map](../docs/DRIVER_SOURCE_MAP.md),
[upstream baseline](../docs/DRIVER_BASELINE_AUDIT.md), and
[object inventory](../findings/RADIO_FIRMWARE_INVENTORY.json).

## Run the checks

Build outputs stay outside the public checkout. A pinned stock `wl.ko` is needed
only for the second, private-input check; it is not distributed here.

```sh
sh scripts/test_driver.sh
# Optional additional comparison using caller-supplied private stock input:
sh scripts/test_driver.sh /private/path/to/wl.ko
```

The checks passed on 2026-10-15:

- The shared suite includes43 ASan/UBSan runtime programs; all44 C objects and
  all45 public headers compile with strict freestanding ARMv7 warnings.
- The added list ARM checker matches3672 cases; the composed RX checker matches
  304 cases through four actual functions, including48 real continuation refills
  and no external success stubs. Radio read/write/modify matches200 ARM cases.
  Power sequences, status timeout/pool and imported OS operations retain their
  separately documented literal C/source coverage and remaining dependencies.

- All public headers compile together before runtime tests. AI reset uses
  `axd_ai_reset_*` types and `AXD_AI_*` constants to avoid the PHY reset namespace.
  Function names and numeric transport/state values are preserved and checked.
  Add newly introduced public headers to `tests/check_driver_headers.c`.

- Synthetic malformed/truncated/missing-terminator, width, alignment, bounds,
  duplicate-order, sentinel-only and callback-failure cases under ASan/UBSan.
- Exact C-versus-Python raw replay equality for 1,404 records across four stock
  tables, including a sentinel-only extended band table.
- Effective-init full operation tuples match 4,212 records across the four
  stock tables under three explicit synthetic addends. Thirteen mixed callback
  positions each cover failure, cancellation and simultaneous error/cancellation.
- Eight selection cases match the stock primary/extended array names and sizes;
  five unsupported core/minor identities return empty output.
- The transfer trace matches 104,440 little-endian words across four stock pairs
  and 208,912 callback events. A deliberately different selector readback value
  is accepted because the recovered writer discards it.
- Synthetic upload tests cover 15 failure points across both banks and each
  operation type, cancellation before and during transfer, invalid inputs and
  missing callbacks. Seven callback-triggered cancellations include the final
  data write; a callback error takes precedence. Calls after terminal
  states produce no additional I/O.
- Generic fake-clock status tests cover immediate/delayed success, waiting for
  bits to clear, exact deadline precedence, no extra reads with a stalled clock,
  backward-clock rejection, read failure, cancellation and invalid predicates.
  Cancellation from a successful matching read remains cancelled; a read error
  takes precedence when the callback also requests cancellation.
- Startup/reset literal traces, cache/mirror behavior, independent poll/retry
  boundaries, non-echo readbacks, FIFO predicates, stuck-reset diagnostics and
  every-event error/cancellation checks pass under ASan/UBSan.
- Wake/suspend/enable paths cover literal traces, nesting, maximum budgets,
  unexecuted callout requirements and every-event errors/cancellation. DMA reset
  covers direct/indirect TX/RX, both wait budgets, selector/cache semantics and
  failed/successful returns without inventing status observation.
- Restart/chaining regressions preserve aliased `&job.context`; removal mutants
  fail these checks. Startup/reset/control context is copied before job clearing.
- Clock/PLL traces cover both polling bounds, A/B distinctions, final-only
  ready/final loss, guarded/skipped paths, unit-1 ownership and per-success
  observation freshness, including every-event errors/cancellation.
- Direct PHY/TSSI/reset and ChipCommon tests cover exact traces, software commit
  boundaries, mode/ownership gates and callback exits under ASan/UBSan.
- The optional Unicorn2.1.4 harness executes actual selected stock ARM functions:
  264 synthetic PHY read/modify/analog cases match C, widths, barriers and
  bookkeeping. Mutants and rejection probes fail as expected.
- Composed table/TSSI/PHY-clock independent tests cover actual child calls,
  partial contexts/results, exact widths/ordering, all core counts, fresh samples
  and callback error/cancellation boundaries.
- A separate composed ARM harness matches 512 all-count TSSI and 270 table /
  high-selector C cases using ten actual selected functions; 16 standalone
  packed-write literal fixtures and seven call/dispatch rejection probes pass.
  MAC is inactive in those comparisons; active paths remain C trace tests.
- Actual pure-policy ARM comparisons match 373 decisions/full ordered load traces
  and 139 missing-field prefixes, including the 218-load longest case. Five actual
  functions run; rejected calls/relocations and required-null probes fail closed.
- Guard/outer idle-TSSI and override/action tests cover current cleared masks,
  optional absence, all core counts, partial buffers/depth/cache, mirror prefix
  timing and every selected event error/cancellation/coincident errors.
- Target PHY reset/BF cleaner tests check literal composition, metadata/binding
  boundaries, partial contexts and callback fault/cancellation behavior.
- Enclosing/lower reset, mctrl and SI-core tests check actual composition,
  lazy pointer bindings, queue gates, independent reads, software commitment,
  callback failure/cancellation and aliases. The 72-queue limit and immutable
  copied metadata are explicit model policies; stock reloads live TX count.
- SI/WL composition tests check map/PCI/IRQ ordering, independent restore guards,
  post-disable index changes, unknown SI freshness, separate software commits and
  partial callback effects; no native kernel/backend behavior is implied.
- New object/table/PMU/SMC/PHY-init/DMA tests use independent literal traces and
  every callback exit. Full private SMC comparison matches1903rows/5709halves/
  11460operations and ignores exactly the stock trailing two bytes.
- Optional strict current ARM comparisons match864 formatter,456 object-writer,
  1408 descriptor-post and2304 RX slot-take cases. No complete packet path or
  physical device executes. Exact commands/results are in [PROGRESS.md](../PROGRESS.md).
- All portable C files cross-compile with Clang for `armv7-none-eabi` in freestanding
  mode with warnings treated as errors. This proves compilation, not linking
  into Linux, execution on ARM or hardware behavior.

## Transfer and status semantics

For each bank, the recovered fresh-chunk trace is store barrier, selector write
to `0x160`, readback of `0x160`, full barrier, then a store barrier and a write to
`0x164` for every source word. Main selector is `0x03000000`; extended selector
is `0x03080000`. Both use the same ports; main precedes extended. There is no
extra final readback or barrier. Barriers are recorded events on the host, not
ARM instructions executed by this library.

`axd_upload_step()` makes at most one callback. `TRANSFERRED` means the software
trace ended. It does not mean firmware ran or reported ready. Both buffers and
the backend must remain immutable/stable during a job. Selection/size checking
does not authenticate firmware; the private-input tool supplies digest-pinned
provenance. Nonzero chunk state is rejected instead of inventing continuation
preconditions. The simulator also rejects wrong lengths instead of silently
discarding trailing bytes like the low-level stock writers.

The fake backend checks repeated port writes, selectors and source words against
an independently specified trace. This does not prove physical bank contents,
cursor wrap, auto-increment behavior, reset state or radio execution. Injected
errors may have already caused backend effects: `attempted_ops` counts callbacks
entered, while `completed_ops` counts callbacks returning success. Neither counter
proves hardware commitment. Failure/cancellation causes no invented rollback.
The APIs are single-threaded. Callbacks may request cancellation, but may not
reenter begin/step or modify job fields. Successful callbacks still count toward
progress when they request cancellation; callback errors take precedence over a
simultaneous request. This is cooperative cancellation, not a thread-safe signal.

`axd_poll_step()` takes a caller timestamp and never sleeps. Timeout occurs before
a read when elapsed time is at least the configured timeout. A readiness value
at the exact deadline therefore does not win over timeout. A stagnant clock does
not advance the deadline, but each call remains bounded and no redundant poll
occurs before its scheduled interval. Clock wrap/backward movement is rejected.
Terminal states remain terminal until an explicit new begin call.

## Private object extraction

Default inventory prints only hashes, offsets, sizes and counts:

```sh
python3 scripts/radio_firmware_inventory.py /private/path/to/wl.ko
```

The optional `--private-output /private/path/to/new-directory` exports the
selected proprietary objects with private permissions. The directory must be
new and outside this checkout. No extracted objects or table values belong in
Git. The tool rejects a different module digest, including the older GPL object;
firmware/table versions must not be silently mixed.

## Next implementation boundary

Use the implemented reset/object/SMC/PHY-table jobs when following the actual
[BMAC initialization groups](../docs/DRIVER_BMAC_INIT_AUDIT.md) and earlier reset
caller. Finish remaining init-registry/radio/channel/calibration callbacks and
radio power-on's actual logen/PLL/RCAL continuation. The full registry order and
RX list/packet/status bodies now exist; finish the status-pool callback,
RX fill, BMAC parsing/IRQ and Linux delivery described in the
[RX call path](../docs/DRIVER_RX_PATH_AUDIT.md). Acquire actual runtime
catalog/queue/resources and implement OS mapping/PCI/IRQ/MMIO adapters. Policy/
metadata acquisition, synchronization, MBSS/fatal recovery and optional higher
initialization remain unfinished; reached dependencies stay explicit. See
[enclosing reset](../docs/CORE_RESET_MODEL.md),
[lower reset](../docs/CORE_RESET_LOW_MODEL.md),
[main-control/reset fields](../docs/MCTRL_MODEL.md),
[SI selection](../docs/SI_SELECT_MODEL.md),
[HND wrapper](../docs/SI_CLOCK_MODEL.md),
[WL caller](../docs/WL_CLOCK_MODEL.md),
[target PHY reset](../docs/PHY_RESET_MODEL.md),
[BF cleaner](../docs/BF_CLEAN_MODEL.md),
[PHY nesting/outer wrapper](../docs/PHY_GUARD_IDLE_MODEL.md),
[reset/beamforming audit](../docs/DRIVER_PHY_RESET_BF_AUDIT.md),
[wake policy](../docs/WAKE_POLICY_MODEL.md),
[actual ARM predicate checks](../docs/WAKE_POLICY_ARM_CHECKS.md),
[resource evidence](../docs/DRIVER_RESOURCE_AUDIT.md),
[clock model](../docs/CLOCK_CONTROL_MODEL.md),
[ChipCommon model](../docs/CHIPCOMMON_CLOCK_MODEL.md),
[PHY model and optional ARM comparison](../docs/PHY_REGISTER_MODEL.md),
[table-read contract](../docs/DRIVER_AC_TABLE_READ_AUDIT.md),
[table model](../docs/PHY_TABLE_MODEL.md),
[all-core TSSI](../docs/PHY_TSSI_MODEL.md),
[composed ARM comparisons](../docs/PHY_COMPOSED_ARM_CHECKS.md),
[PHY-clock model](../docs/PHY_CLOCK_MODEL.md),
[external research and harness plan](../docs/EXISTING_REVERSE_ENGINEERING.md),
[PSM control policies](../docs/PSM_CONTROL_MODEL.md),
[DMA reset policies](../docs/DMA_RESET_MODEL.md) and
[current checkpoint](../PROGRESS.md). PHY51 radio/channel/calibration, complete
packet data path, kernel integration and hardware execution remain required.
