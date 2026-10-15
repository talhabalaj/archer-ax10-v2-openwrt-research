# Composed AC table-29 read model

`driver/phy_table.c` implements the exact table-ID `0x29`, count-one, width-32
wrapper recovered in [the table-read audit](DRIVER_AC_TABLE_READ_AUDIT.md).
It composes the existing direct PHY and PSM-control jobs rather than treating
suspend, enable or PHY modifications as successful external stubs. Identity is
core 130 minor 0/1, AC-family PHY 51; resource ownership and powered/clocks-enabled
access remain caller prerequisites. This is an offline operation model with no
kernel, lock/mapping backend, actual PHY-ready predicate or radio operation.

## Wrapper and children

1. Read main control at D11 `0x120`, then full barrier. Only sampled mask `1`
   selects MAC suspend/enable. Mask `2` alone does not select that branch.
2. When selected, run the actual `AXD_PSM_SUSPEND_MAIN` child. Its runtime MAC
   context must be present. The established nesting, wake, polling and stricter
   failure policies apply; no acknowledgement is fabricated.
3. Read logical PHY `0x19e` through the direct PHY child. If sampled bit `2`
   is clear, set it through a **fresh** masked modify (`mask=2,value=2`).
4. Program table ID and index using the packed-write traces below; then read
   low and high halves through PHY selectors `0x0f` and `0x10`, respectively.
5. If the guard-set data write succeeded, clear bit `2` through another fresh
   masked modify. This preserves the new sample's other bits; it does not
   restore the entire original guard word.
6. When the original main-control mask `1` was set, run the actual
   `AXD_PSM_ENABLE_MAIN` child. Its wake-control/MBSS dependencies propagate.

The proven PHY-51 HWACI helpers have no device/software effects in this path;
no fake callback result replaces them. PHY-register enter/exit and host locking
are not performed by the stock table wrapper, and are not added here. TSSI's
[outer caller](PHY_GUARD_IDLE_MODEL.md) implements separate wake/nesting prerequisites.

MAC context can be absent while the sampled branch does not require it. Missing
context on a reached suspend becomes `DEPENDENCY_MAC_CONTEXT`. Supplied MAC
context is prevalidated for matching core/minor and valid PSM flags, even when
that branch may later be skipped. A PSM dependency becomes
`DEPENDENCY_MAC_CALLOUT`, with its actual `pending_callouts` and updated context
retained. Semantic child failures retain child diagnostics; callback error or
missing backend is exposed by the parent. No failed child is retried or silently
treated as successful. The inherited fail-stop policies differ from some stock
diagnostic/cleanup continuations; see [PSM policies](PSM_CONTROL_MODEL.md).

## Packed writes and software counter

Index is the caller's original u32. The physical index write narrows to low16;
there is no subtraction of `0xc000` or conversion to a byte address. With `ST`
denoting a store barrier, the header transfers are:

- `ST,W32(D11+0x3fc,0x0029000d)` for table ID.
- `ST,W32(D11+0x3fc,((index&0xffff)<<16)|0x000e)` for index.

These are packed 32-bit writes, not normalized pairs of 16-bit accesses. The
explicit shared-context bus word and u16 write threshold control the prelude.
When that word equals one, increment the PHY bookkeeping counter modulo 65536.
If the incremented counter meets/exceeds threshold, reset it and emit
`R16(D11+0x3e0),SY` before the packed write. The version sample is discarded.
For other bus values the prelude does neither counter mutation nor flush read.

The mandatory earlier PHY guard read clears the counter. Thus thresholds zero
or one flush before both header writes; threshold two flushes only before the
index write; thresholds at least three flush neither. The later direct data
reads clear the counter again. This component implements the full count-one
wrapper, not a generic standalone packed-write helper with arbitrary initial
counter. Runtime bus/threshold acquisition remains outside the model.

## Partial state and interruption

`low_valid` and `low_half` commit after the low read's successful final SY.
`word_valid`, `high_half` and full32 `result` commit after the high read's final
SY, including cancellation in that successful callback. Both halves are always
read even when a caller ultimately consumes only low16. A valid word does not
mean guard clear, MAC enable or the wrapper completed. An all-zero word is valid
when both actual supplied reads completed; zero is never an implicit reply.

The direct PHY job now exposes `result_valid`, eliminating inference from a
possibly zero result or private child stage. Child contexts are copied back
after every step, including failure/cancellation, so software nesting/cache and
bookkeeping changes remain explicit. `changed_guard` records a successful SET
data write and stays diagnostic after normal clear. `flush_reads` counts successful
version-read callbacks before their SY. `last_read` tracks the latest successful
read across parent and children; initial main control is retained separately.

At most one user callback occurs per parent step. Parent attempted/completed
counts include user callbacks from all children. A missing backend causes no
user attempt; child adapter diagnostics may still show that attempted adapter.
Software counter increments/reset happen before I/O and remain visible even if
the first queued callback fails. Successful cancelling callbacks keep their
specified effects; simultaneous error wins. Terminal states are silent, with no
invented rollback, guard cleanup or MAC enable on a failed/cancelled path.

## Validation

`tests/test_phy_table.c` derives literal expectations independently from retained
assembly. Forty combinations cover four bus words, thresholds 0/1/2/3/65535 and
existing/temporarily set guards. It verifies packed words and index truncation,
non-echo fresh RMW samples, pre-I/O counters, discarded version reads, both data
halves and partial-valid state. Selected active-MAC traces use the real PSM
children, including wake-policy dependency and semantic nesting failures.

Every event in the selected traces covers error, cancellation and coincident
error/cancellation. Per-step checks include copied MAC state, helper validity,
successful guard-set timing, callback accounting, missing context/backend,
invalid inputs, aliased context restart and terminal silence. The [composed ARM harness](PHY_COMPOSED_ARM_CHECKS.md) additionally compares
actual selected stock instruction paths under inactive-MAC synthetic contexts.
These checks prove modeled trace behavior under supplied inputs; no physical MMIO, bus timing,
Linux linkage, peripheral table state, calibration or RF operation is validated.

The shared context carries optional immutable actual wake-policy input bindings.
PSM children evaluate them using the current reason word after reason-4 clear;
reached unknown/invalid fields remain explicit. The table API itself does not
acquire those inputs or invoke outer PHY enter/exit.
