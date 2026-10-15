# Higher core reset with real child protocols

`driver/core_reset.c` implements the recovered `wlc_bmac_corereset` order for
core 130 minor 0/1 using the actual portable WL clock, SI core predicates and
flags, per-slot DMA reset, lower core reset, main control, PSM wake, target PHY
reset and PLL children. It is an original offline coordinator with explicit
resources and partial software state. It provides no device backend, kernel
binding, physical reset proof or RF operation.

The [selected static evidence](../findings/DRIVER_CORERESET_EVIDENCE.json)
records exact source identities and actual retained Evidence IDs. The lower
wrapper and generic auxiliary AI reset are described in
[CORE_RESET_LOW_MODEL.md](CORE_RESET_LOW_MODEL.md), with their separate
[selected metadata](../findings/CORE_RESET_LOW_EVIDENCE.json). Main-control
cache/override/mirror semantics are documented in [MCTRL_MODEL.md](MCTRL_MODEL.md).

## Canonical state and ownership

`axd_core_reset_context.low.si/state` supplies canonical SI catalog, selected
index and mapping state. Each WL fallback starts from that current state and
propagates its result back, including partial effects. IRQ replies must supply
the actual post-call SI state; unavailable state becomes an explicit dependency.
The model does not assume that IRQ disable or a failed selection preserved the
original current index.

`control.psm` is canonical MAC state. `clock.d11` owns clock caches and predicates;
its duplicated wake-reason word is refreshed from canonical PSM state. PHY
children receive the current MAC/policy snapshots and propagate partial MAC
changes back after each step. The actual band pointer and discovered band
type/revision supply the PHY identity. The lower wrapper receives current control
state, target revision, flags with mask `0x4` added, and resetbits zero. It executes
real children rather than an always-successful reset callout.

DMA contexts are copied per primary TX, paired TX and RX slot. Every DMA event
retains queue number, primary/secondary choice and supplied ARM32 DMA identity.
The owner must bind that identity to the actual channel resource; the model does
not allocate queues, acquire registers or discover device handles.

Pointer facts use explicit known flags: unknown reached state is a dependency;
known null follows guarded skips. Required null bindings fail rather than
emulating a stock invalid access. Catalogs and external policy/BF data remain
borrowed immutable storage outside all active jobs. Physical current-wrapper
and unit1 ownership, IRQ-safe mapping/window lifetimes, serialization and
restoration remain caller obligations across the whole child sequence. A
relative PHY/PLL resource tag does not acquire a resource or validate a mapping.

## Ordered behavior

1. Flags `ffffffff` require the band binding. A nonnull PHY pointer selects band
   flags; an absent PHY selects zero. The model rejects a required null band.
   Save the original **full byte** fast-clock cache. Only original zero requests
   real WL mode0 before the first actual SI iscoreup. Nonzero bytes are retained
   without early Boolean normalization.
2. Actual iscoreup gates the DMA scans, independently of the later reset path.
   It uses fresh wrapper reads and barriers, not a cached core-up predicate.
   TX visits the supplied runtime count in order. In single mode a nonnull
   primary resets then clears its suspended-FIFO state. In paired mode a null
   primary skips its secondary; a present primary resets first, and a missing
   secondary skips the FIFO clear. A present pair resets secondary then clears.
3. Suspended-FIFO clear requires nonzero count and a set bit. Clear the bit,
   decrement count, and only when count reaches zero clear wake reason8. Only
   a resulting whole-word zero invokes the actual wake-control child. Other
   reasons suppress that policy call. RX independently visits indexes0..3:
   nonnull FIFO0 is eligible; FIFO3 requires the supplied full public byte to
   be nonzero; FIFO1/2 are skipped.
4. A nonzero no-reset byte clears software word164, performs actual main
   `mctrl(3,0)`, and returns. It performs no lower reset, control-cache reset,
   PHY reset, PLL enable, final mode2 request or word22c store. An earlier
   original-zero mode0 request is not restored on this branch.
5. Normal reset clears core-clock-valid **before** lower reset with
   `flags|4, resetbits0`. Only whole lower-helper return reaches the later
   core-clock-valid1 store. The lower helper's optional absent callback, actual
   D11/generic AI protocols, auxiliary849 selection, zero-map early return,
   restoration and bus0 mctrl/halfword tail remain observable child behavior.
6. A nonnull band discovers identity only when the combined type/revision word
   is zero. It makes two separate `R16(3e0),SY` samples. The first sample's
   bits11:8 supply type, with D mapped to B; the second sample's low8 supplies
   revision. Successful reads retain narrowed samples. Each subsequent software
   type/revision store is a separate internal step after its successful SY.
   Cancellation at SY therefore retains the sample but stops before that later
   store. Type0/revision-nonzero skips discovery.
7. A present PHY with present shared object stores shared-clock byte1. A null
   PHY/shared object skips that store; unknown reached bindings stop. This is
   the software shared-PHY helper, not a PHY-clock-ready indication. Band typeB
   then runs actual coreflags updates `300/100`, `6/0`, `4/4`, with fresh
   read/barrier/write/read/barrier protocols and no added delays.
8. Actual mctrl reset clears the main cache, exactly nine bitmap bytes, count,
   wake/mute state and reset halfword before `mctrl(ffffffff,04000400)`.
   Main and extended suspend depths survive. Extended support separately stores
   extended cache400 before `ST,W32(1b4,400)`; it adds no readback/depth clear.
9. Capability `10000000` requests another real WL mode0. A nonnull band and PHY
   invoke the full implemented target PHY reset: outer TSSI snapshot, clock
   preparation, pulse/delay, bandwidth, analog, restore and optional BF children.
   Reached unsupported band/PHY revisions fail explicitly rather than silently
   skipping a required PHY call.
10. PLL enable obtains a **new** actual iscoreup when clock-valid and inhibit
    guards permit it. It does not reuse the first sample or infer up from lower
    reset. Actual PLL/RSDB/secondary behavior runs through the real child.
    Finally clear word164 and the separate tail byte; original fast-zero then
    requests real WL mode2. Only after that request returns does word22c become
    `ffffffff`.

The retained higher body has no direct VASIP/SMC call. No extra operation is
inserted to stand in for later initialization work.

## Explicit model policies

The coordinator copies queue metadata and runtime count, keeping them immutable
through the job. Stock reloads the TX count during iteration. This is a declared
ownership restriction, not an assertion that stock takes one initial snapshot.
The bounded model supports at most 72 slots and the exact nine-byte bitmap;
reached out-of-range indexes fail before indexing. RX still requires its actual
four array entries independently of TX count. It follows represented count/bit
tests without inventing a bitmap-count repair.

The nullable-band binding is passed into PSM with its known status. An unknown
band becomes `AXD_PSM_BAND_STATE` only when an actual wait reaches its band
guard, after any prior policy/control/cache effects. Public-down, a SLEEP
decision or an already-awake physical sample can avoid that guard. Known-null
skips waiting; known-present runs the real delay/SHM protocol. This preserves
the [optional PSM binding contract](PSM_CONTROL_MODEL.md#optional-nullable-band-binding).

Begin snapshots all copied inputs before clearing its own job, including
own-context aliases. Every step invokes at most one synchronous user callback.
Error wins over simultaneous callback cancellation; successful cancelling
callbacks retain completed effects. Child partial contexts propagate before
checking the parent's terminal state. Whole-child completion flags are set only
when the child returns normally. Software stores that follow a child return or
barrier are separate source-ordered transitions, not early success claims.
Terminal jobs are silent: no invented unwind, restore, cleanup, retry or final
clock request occurs after failure, dependency or cancellation.

DMA return values and clock/AI poll completion retain their existing child
semantics. Poll exhaustion or a returned success value need not mean a physical
disabled/ready state was observed. `AXD_CORE_TRACE_COMPLETE` means that this
software trace returned; inspect child observations, freshness and timeout
diagnostics separately. It is not hardware-reset or radio readiness.

## Independent checks and remaining work

`tests/test_core_reset.c` constructs original literal typed event queues from
the retained contracts. Replies are supplied independently of writes. Checks
cover no-reset and normal paths, original-fast decisions, fresh SI predicates,
DMA queue/pair/null gates, suspended-FIFO wake, band discovery, shared state,
AC coreflags, main/extended cache timing, complete target PHY, PLL and lower
AI-wrapper boundaries. Callback failures, cancellation and both together check
retained partial effects, precedence, one callback per step and terminal silence.
Own-context alias and reached unknown/bounds regressions are included.

`tests/test_mctrl.c` separately covers unmasked values, overrides, mirror store
timing, reset clears and preserved depths. `tests/test_psm_override.c` adds lazy
nullable-band dependencies and known-null/present behavior without changing
legacy callers. The focused main-control and PSM suites passed strict C11
ASan/UBSan checks and their changed components compile freestanding for ARMv7.
The enclosing suite's final validation is recorded in [PROGRESS.md](../PROGRESS.md).

On 2026-10-15, all 20 unique selected higher/helper/lower Evidence IDs matched
actual retained structured records and expected subject digests. Both private
module inputs were rehashed against their pinned identities. This verifies
provenance, not complete execution equivalence. No new decompilation or device
execution was needed for this package.

Actual resource acquisition, OS adapters, synchronization, runtime metadata and
wake-policy acquisition, SB interconnect handling and reached MBSS/fatal recovery
remain dependencies. Hardware reset/upload/start proof, PHY/radio/channel and
calibration, DMA descriptors/interrupt/RX/TX, Linux WLAN integration and exact-board
OpenWrt validation remain incomplete. Host tests and portable builds do not
complete those milestones.
