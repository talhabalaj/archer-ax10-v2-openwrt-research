# Composed target PHY reset

`driver/phy_reset.c` composes the recovered reset path for the actual target:
AC band type `0xb`, band PHY revision 51, D11 core 130 minor 0/1 and chip
63178. It uses real idle-TSSI, PHY-clock, direct pulse/analog and beamforming
jobs. It has no successful placeholder child, MMIO backend, resource mapper,
kernel synchronization or radio-ready outcome. This is the PHY reset function,
not the enclosing complete device-reset/upload/start lifecycle.

The contract is the [exact reset/beamforming audit](DRIVER_PHY_RESET_BF_AUDIT.md),
[assembly-backed boundary correction](PHY_RESET_EXACT_PATH.md) and their retained
Evidence. Other band revisions/types are rejected before I/O;
their different preparation/pulse paths have not been silently replaced with
the target path. Public PHY revision remains an independent explicit field.

## Ordering and inputs

On the first ENTRY step, zero all eight bytes of the local four-slot TSSI buffer
and clear model observation-valid bits. This initialization is not an observed
snapshot. An immediate cancellation before ENTRY leaves the original copied
context unchanged. Then run these stages:

1. Test public revision. For 51, require the PHY/module/table binding and invoke
   the complete outer idle-TSSI snapshot. The callback null gate precedes MAC
   suspend. For a present callback, its context and first-word PHY binding are
   checked at the actual later callback point, AFTER MAC suspend and PHY enter.
   An absent registered callback is a real no-op and does not require context.
2. Execute RESET_PREPARE: bandwidth flags, delay 2 and the full PHY-clock enable,
   including its optional unit-1 workaround. This is after the outer snapshot.
3. Execute the target band-revision-51 pulse, then the core-130 delay 100.
   In the actual stock function the delay is at the common post-pulse join;
   the target's pulse is always reached within this explicitly accepted scope.
4. Require the PHY binding and record `chanspec & 0x3800` at the actual later
   bandwidth-state point. The model stores this word in `context.clock`.
   Stock's chip-63178 store dereferences the PHY context before the subsequent
   analog null guard. Analog callback absence cannot justify a missing PHY.
5. Evaluate the analog PHY/module/table/callback bindings. An absent callback
   skips the actual registered AC analog child. A present callback requires its
   nonnull context pointer whose first-word PHY pointer matches this reset's PHY
   instance. The actual callback uses that PHY's descriptor/base resources,
   then emits its store-barrier/16-bit-zero-write sequence. An unrelated callback
   context cannot silently reuse the current D11 resource.
6. Re-evaluate public revision and, for 51, invoke outer idle-TSSI restore with
   the same local buffer. Actual active-slot validity is required when the
   callback is present. Stable absence makes both wrappers no-ops and leaves
   the initialized buffer unobserved.
7. In order, test hardware flags bit 0, public BF byte, nonnull selected TXBF
   pointer and its enable byte. A false predicate skips later unavailable
   fields. A true branch invokes the [actual cleaner](BF_CLEAN_MODEL.md), after
   binding its TXBF/WLC/hardware identities to this caller and sharing the
   latest MAC/policy context. Main and conditional extended MAC work remains
   real even though the core-130 leaf helpers have no SHM I/O.

Availability is explicit for reached public/callback/pointer/BF facts. A
`known == 0` flag marks missing input and ends DEPENDENCY; malformed known flags fail.
Required known-null objects fail BINDING instead of emulating a stock crash.
Callback facts are normalized absent/present bindings for the actual registered
AC implementations, not a facility for replacing arbitrary unknown functions.
Public BF and TXBF enable bytes retain their full byte/nonzero semantics.

The caller owns acquisition, coherent pointer-to-object bindings, synchronization
and powered/mapped resources. Facts and external policy/BF objects remain stable
through the job; repeated revision/dispatch tests are performed at their reached
points against that binding, rather than assuming a different live object was
acquired. The metadata counter/last field describe model accesses, not an ARM
compiler-load trace or an acquisition implementation.

## Regions, progress and interruption

The single backend operation type distinguishes D11, current wrapper and unit-1
wrapper. This prevents two registers with the same offset from being conflated.
D11 barriers retain D11 region; delays use NO_REGION. No MMIO address, IRQ token,
map switch, clock-ready predicate or measured delay is invented by the adapter.

Every step invokes at most one user callback or performs a bounded transition.
Parent counters count actual user callbacks, not internal adapter invocations.
All child partial contexts and pending callouts propagate after every child
step, including failed/cancelled ones. The retained active child exposes its
specific owner/policy/metadata dependency and diagnostics.
Late idle-callback binding failure retains the entered PHY depth and suspended
MAC state, with the reached binding field in the idle child's diagnostics.

Completion flags indicate complete child wrappers, not RF readiness or observed
data. For example, an absent idle callback can complete a wrapper while no TSSI
valid bit is set. Conversely, cancellation on a successful direct-read barrier
can preserve a saved value before the wrapper's completion flag is set. A
successful final pulse-delay cancellation preserves its completed I/O without
running the later bandwidth store.

Cancellation reaches the active descendant. A successful cancelling callback
retains its effects; a simultaneous callback error wins. Missing backend callbacks
fail without counting an entered user callback. Terminal states are silent. No
failure creates an invented MAC enable, PHY exit, analog operation, saved-buffer
restore, retry or rollback. Callbacks may cancel but cannot reenter or mutate jobs.

## Validation boundary

Independent literal tests use strict supplied read replies and region-tagged
events. They check snapshot-before-preparation, distinct public/band revision
roles, the late bandwidth commit, analog absence and the ordered BF gates.
Every selected callback is swept for error, cancellation and both, with partial
buffer, clock/workaround, MAC and completion-state checks. Alias-safe restart,
missing/invalid reached inputs, binding failures and terminal silence are
included. The enclosing runner performs ASan/UBSan and ARMv7 compilation checks.

These are offline composition checks. Earlier actual-ARM harnesses independently
cover selected PHY/table/TSSI and pure wake-policy children; they do not execute
this entire reset coordinator. No modern-kernel linking, physical mapping,
device microcode execution, calibration, RF or router reset is established.
