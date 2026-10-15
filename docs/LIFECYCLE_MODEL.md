# Offline reset and startup implementations

Original C components, 2026-10-15. The [exact-artifact lifecycle audit](DRIVER_LIFECYCLE_AUDIT.md)
and its [Evidence summary](../findings/DRIVER_LIFECYCLE_EVIDENCE.json) establish
the recovered contracts. This document distinguishes the implemented boundaries
from the larger, unfinished device lifecycle.

## AI-wrapper reset

`driver/ai_reset.c` models the current HND D11 core-ID `0x812`, revision-130 AI
wrapper protocol for interconnect types 1, 3 and 4. It takes resolved `bits`,
`reset_bits` and secondary-wrapper presence explicitly. It does not discover the
SI interconnect, adjust caller reset flags, map wrappers or configure clocks.

The AI public types use `axd_ai_reset_*` and constants use `AXD_AI_*` so AI and
PHY reset headers can be included together. Function names remain
`axd_ai_reset_begin/step/cancel`; the namespace change preserves numeric event,
phase and error values and protocol behavior. The compile-only
`tests/check_driver_headers.c` includes every public driver header and checks
those numeric values, catching cross-component declaration collisions.

Events identify wrapper 0 (primary) or 1 (secondary) and a wrapper-relative byte
offset. When present, the secondary completes first. A future exclusive transport
owner must resolve those handles from actual per-core metadata; they are not
physical addresses or proof that direct mapping is supported.

The protocol preserves store/full barrier events, ordered control/readback
operations, the core-130 forced-clock workaround, and readback-derived clears.
Each busy wait has an initial read and at most 30 delay-10/read iterations.
Release has at most nine zero writes and ten reset-control reads. Final control
writes still execute when busy/reset values remain nonzero, as in stock.

`TRACE_COMPLETE` therefore means the modeled protocol ended, **not that reset
succeeded**. `busy_exhaustions` and `reset_nonzero_mask` expose observed problems;
`completed_wrappers` counts completed protocol traces. A later proven core-up
check and higher clock/PHY/DMA state are still needed. The model deliberately
omits no error by relabeling a stuck reset as ready.

## Main/extended startup

`driver/psm_start.c` models the core-130 startup-helper boundary with explicit
cached main/extended/mirror control and wake/mute/mirror/PSMx flags. The main
control writes use changed-cache semantics; mirrored updates preserve bits
outside `0xcfc72706`. For the two fixed startup values, wake and mute overrides
do not alter their already-requested bit state. Extended control has its own
cache and no main override logic.

Each selected bank receives its status clear, control writes and status reads
in recovered order. Polling uses an iteration budget: initial read, up to
100,000 delay-10/read iterations, and a separate final confirmation read. Each
read is followed by its own full barrier. Final confirmation can lose an earlier
acknowledgement or observe an acknowledgement after the last loop read.

The FIFO check uses logical SHM byte offset `0x422`. Its physical object sequence
is store barrier, write `0x10108` to `0x160`, read back `0x160`, full barrier,
16-bit read at high-halfword port `0x166`, full barrier. The selector readback is
discarded. The resulting 16-bit value must have bits 15:13 equal to 2.

`SUSPENDED` means the selected processors acknowledged startup suspension and
the FIFO-layout predicate matched. It does not prove normal MAC enable, valid
PHY calibration, RX/TX operation or radio readiness. Core-130 attach enables
PSMx; the API retains the recovered explicit conditional flag, and callers for
that attach state must supply it as enabled.

The model deliberately **fail-stops** when either bank lacks acknowledgement
and reports FIFO mismatch as failure. Stock main timeout returns failure;
stock extended timeout calls a fatal diagnostic and then continues to FIFO
verification. The outer stock BMAC init even ignores the helper return.
Diagnostic/fatal routines are unexpanded callouts, not silently implemented
hardware cleanup. These policy differences remain explicit rather than being
claimed as an identical stock failure trace.

## Shared execution and validation rules

- Each public step performs at most one synchronous callback or a bounded
  internal state transition. Polling/delay repetition occurs across steps.
- Delay events ask the backend to delay; host tests record them. These libraries
  do not sleep, access a device or execute ARM barriers on the host.
- Context is snapshotted at begin. Fields are single-threaded job state;
  callbacks may cancel but may not mutate fields or reenter begin/step.
- Counters distinguish entered and successful callbacks. A failing operation may
  already have produced backend effects; no rollback/retry is invented.
- Successful callback cancellation persists, including the final operation.
  Callback failure takes precedence over simultaneous cancellation. Terminal
  calls cause no additional events.

Independent tests cover literal event traces, optional/cached paths,
readback-derived values, boundary waits, timeout/failure paths, each callback's
error/cancellation precedence and terminal silence. ASan/UBSan and ARMv7 build
results are recorded in [PROGRESS.md](../PROGRESS.md). Tests model status reads;
writing all ones does not implement W1C status behavior in an arbitrary backend.

## Remaining D1 and integration work

These components are not an end-to-end bring-up coordinator. The higher WL
reset includes DMA, PHY, PLL, clock and auxiliary-core operations, and supplies
mode-dependent bits to HND. Actual SI mapping/resource state, those prerequisites,
complete wake-policy/MBSS/fatal handling and cleanup/recovery remain outstanding.
The [wake/suspend/enable model](PSM_CONTROL_MODEL.md) and
[DMA-reset model](DMA_RESET_MODEL.md) implement additional bounded subsets. Required
VASIP/SMC paths must be resolved rather than assumed absent. A subsequent model
must compose proven stages with explicit prerequisites before any hardware use.

Complete SHM ABI, physical init-object writes, PHY51/radio/channel/calibration,
DMA/interrupt/RX/TX, kernel integration and exact-board hardware tests remain
separate full-driver requirements. Compilation and a simulated acknowledgement
do not meet them.
