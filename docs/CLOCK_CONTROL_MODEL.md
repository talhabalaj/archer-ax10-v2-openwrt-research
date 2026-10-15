# Core-130 clock and PLL trace model

`driver/clock_control.c` is original portable C implementing the bounded D11
clock-selection, PLL and secondary-core portions of the exact stock contracts
in the [resource ledger](DRIVER_RESOURCE_AUDIT.md) and its
[selected Evidence](../findings/DRIVER_RESOURCE_EVIDENCE.json). It supports
core 130 minor 0/1. It has no MMIO backend, SI resource mapper, interrupt manager
or kernel binding. `TRACE_COMPLETE` means the modeled software trace ended;
the stock helpers can finish without establishing physical readiness.

## Context and ownership

The caller explicitly supplies the SI capability word, SI word at offset
`0x20`, core-clock-valid flag, PLL-inhibit flag, core-up result, RSDB predicate
and existing fast-clock/wake-reason state. These are runtime values, not board
defaults inferred from BCM63178. `begin` snapshots the context before clearing
the job, including when restarting from `&job.context`.

Current D11 and D11 unit 1 are distinct typed regions. A supplied unit-1 backend
must own exclusive IRQ-safe access and preserve the caller's mapping and
interrupt state. The model does not execute the SI switch or manufacture a
physical address. RSDB work without that owner ends `DEPENDENCY_SECONDARY_OWNER`.

Clock selection with capability bit `0x10000000` clear ends
`DEPENDENCY_HND_FALLBACK` before I/O. The actual imported HND fallback has now
been [recovered](DRIVER_CLOCK_FALLBACK_AUDIT.md), and its [operation body is now implemented separately](CHIPCOMMON_CLOCK_MODEL.md). It
uses ChipCommon, whose register `0x1e0` is a different resource from D11 `0x1e0`.
Do not route this dependency through the D11 job or treat it as a successful
stub. The caller's wake reasons remain unchanged by the implemented capability
branch, as in the stock helper.

## Implemented operations

`ST` and `SY` denote recorded store/full barriers; delays are requested
microseconds, not measured host elapsed time. Every public step invokes at most
one synchronous callback or makes a bounded internal transition.

- **Clock selection:** mode zero requests fast clock with
  `ST,R32(0x1e0),SY,W32(old|2)`, delays 64, then reads status. Up to 1,500
  repetitions of `delay(10),R32,SY` wait for bit `0x20000`; there is no extra
  final read. Nonzero modes follow the stock dynamic branch: SI word `0x20`
  and separate samples determine whether that same wait precedes
  `ST,R32,SY,W32(old&~2)`. The software fast-clock flag becomes `mode==0`,
  even when the core-clock-valid guard prevents all I/O.
- **PLL disable:** subject to the three runtime guards,
  `ST,R32,SY,W32(old&~0x300)`, then a separate `R32,SY`, then any RSDB tail.
- **PLL enable:** RSDB first clears bit `0x20` on the current D11 mapping.
  Separate sample A then decides the branch. If both request bits `0x300`
  are already set, request write, polling and final read are all skipped.
  Otherwise a fresh RMW sample B supplies the write `B|0x300`. Both ready
  bits `0x03000000` in A cause four separate 16-us delays; bits in B do not.
  A new status sample starts a maximum 10,000-delay loop requiring both ready
  bits. A separate final `R32,SY` always follows this loop, even if immediately
  ready or exhausted. Stock does not check that final sample for failure.
- **Secondary control:** when RSDB is true, the owned unit-1 mapping receives
  `ST,R32(0x1e0),SY,W32(new)`. Standalone argument zero clears bit `0x20`;
  argument one sets it. PLL passes its enable argument inverted: enabling PLL
  clears the secondary bit, disabling sets it. RSDB false emits no unit-1 I/O.

RMW barriers precede the read. There is no invented store barrier between its
`SY` and write. In particular, neither mode flag updates nor a void stock
return is a ready predicate.

## Observations, errors and limits

`last_status` records the last successful **current-D11** clock read.
`status_observed` says such a read happened; `writes_since_status` distinguishes
a later successful write from a post-write observation. Unit-1 samples update
`last_read` but never replace the current-core observation. A pre-write RMW
sample alone therefore cannot establish a requested postcondition.

`sample_a`, `request_skipped` and `poll_exhausted` preserve distinct stock
branches. Exhaustion is diagnostic, not a replacement return code; a final
PLL sample can lose readiness or become ready only after exhaustion. A future
coordinator must evaluate the relevant actual postcondition and ownership.

Failure/cancellation is terminal and does no invented rollback. A successful
callback still counts and updates observations if it requests cancellation;
simultaneous callback error wins and records no successful operation. Callbacks
may cancel, but may not reenter begin/step or mutate job fields. Boolean
arguments/flags and supported revisions are validated before callbacks.

## Independent checks

`tests/test_clock_control.c` uses literal synthetic operation/read-result
fixtures independently specified from retained assembly. It checks non-echo
RMW replies, separate A/B samples, both secondary directions, missing owners,
no-I/O guards, alias-safe restart and current-core observation freshness after
every successful callback. Independent loop expectations cover the 1,500 and
10,000 bounds, absent versus mandatory final reads, partial ready masks,
final-only ready and final loss. Selected traces inject error, cancellation
and coincident error/cancellation at every callback and assert terminal silence.

Run these with `sh scripts/test_driver.sh`. Passing host checks and strict
freestanding ARMv7 compilation do not prove Linux linkage, MMIO behavior,
elapsed timing, clock lock, PHY initialization or any router execution.

## Next work

Compose the implemented ChipCommon body behind its actual SI/PCI/mapping
contracts, applying the WL cached-mode/wake result separately; keep its
capability/revision/mode behavior distinct. Use the implemented
[PHY primitives](PHY_REGISTER_MODEL.md) and complete their table-read,
wake/nesting, PHY-clock and optional dependencies. Compose with the existing reset,
PSM and DMA jobs only when the required resources and caller state are explicit.
Use the [new external research](EXISTING_REVERSE_ENGINEERING.md) to build an
original bank-aware differential harness; it supplies methodology, not
core-130 register or timing evidence.
