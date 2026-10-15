# D1 MAC/PSM lifecycle analysis checkpoint

## Status and inputs

This advances the reset/start/acknowledgement analysis for the complete-driver
goal. It does not complete D1 implementation or the replacement driver. Host
simulation, PHY/radio initialization, DMA/RX/TX, kernel integration, and eventual
hardware validation retain their separate acceptance requirements.

Two exact stock artifacts were inspected offline with REA 6.5.0/Ghidra 12.1.4:

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| `wl.ko` | 5,803,056 | `8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c` |
| `hnd.ko` | 366,060 | `d05ff061b3a636630955a13fdd20a953f3dfb07ee92904a987dd4c77b648b45a` |

The prior `wlc_bmac_init` record was reused. New private sessions separately
bound each artifact, exported evidence, and closed successfully. Both source
digests remained unchanged. No router call, driver loading, MMIO, or radio action
occurred. Raw output and proprietary bytes remain private. Assembly controls
ordering where pseudocode omits reads or recovers arguments incorrectly.

## Resolved core-130 register and SHM fields

Offsets are relative to the caller's mapped D11 register base, not physical
addresses or a complete hardware map.

| Role | Offset | Width |
| --- | --- | --- |
| Main MAC control | `0x120` | 32 |
| Main interrupt status | `0x128` | 32 |
| Object address / data | `0x160` / `0x164` | 32, with 16-bit data reads below |
| Extended MAC control | `0x1b4` | 32 |
| Extended interrupt status | `0x1c0` | 32 |
| Optional additional control mirror | `0x324` | 32 |
| Conditional reset-path extra fields | `0xa08`, `0xa06` | 16 |
| Wake-failure diagnostic fields | `0x154`, `0x6ca`, `0x7e8` | 32, 16, 16 |

Core-130 standard SHM descriptors resolve debug-state byte offset `0x40` and
FIFO-layout byte offset `0x422`. These are distinct from MMIO offsets.
`wlc_bmac_read_objmem16` performs:

1. `DSB ST`; write object address `selector | (byte_offset >> 2)`.
2. Read back that address register; `DSB SY`.
3. Read 16 bits from data port plus `(byte_offset & 2)`; `DSB SY`.

Wake/enable use selector `0x10000`. Thus debug state uses address value
`0x10010`, then a 16-bit read at data port `0x164`. Readback is discarded.
FIFO verification reaches `wlc_read_shm`/`wlc_bmac_read_shm`. A subsequent bounded
wrapper pass resolved this gap: at `0x001a627c` the wrapper sets selector
`0x10000`, then tail-branches to the retained SHM16 helper at `0x0019fa44`.
Consequently FIFO offset `0x422` uses `DSB ST`, write32 `0x160=0x10108`,
read32 `0x160`, `DSB SY`, read16 `0x166`, `DSB SY`. The readback value is discarded.
Wrapper assembly evidence:
`ev_265d43451a8c0ff884306cfd400bfb8be89b4b3b729788bff7f61463f1f74b50`.

## Control state and writes

Recovered hardware-object fields include main cached control `+0x1f0`, main
suspend depth `+0x1f4`, wake override `+0x1f8`, mute override `+0x1fc`, extended
cached control `+0x268`, extended suspend depth `+0x26c`, fatal reason `+0x248`,
and timeout flags `+0x580`.

`wlc_bmac_mctrl` computes `new_cache = (old_cache & ~mask) | value`. If unchanged,
it returns without writing. Otherwise it stores the new cache, forces bit
`0x04000000` when wake override is nonzero, and, when mute override is nonzero,
clears `0x00040000` and sets `0x00020000` in the effective write value. It emits
`DSB ST`, then writes main control. There is no immediate readback in this helper.

If byte `+0x50c` enables a mirror and the input mask intersects `0xcfc72706`, a
separate cached masked update can emit `DSB ST` plus a write to mirror `0x324`.
Assembly shows ordinary returns where pseudocode reported an indirect jump.
Attach initializes this mirror-enable byte to zero; later runtime state remains
an input. Extended `mctrlx` independently updates its cache and emits a changed
value with `DSB ST` to `0x1b4`, without the main helper's override logic.

## Startup acknowledgement ledger

The actual startup seed is `wlc_bmac_wowlucode_start`, called by the reused BMAC
initialization after uploads and optional VASIP/SMC work.

| Stage | Observed operations / condition |
| --- | --- |
| Main prepare | `DSB ST`, write `0xffffffff` to main status; main mctrl full-mask value `0x04020406`, then `0x04020402`. Each helper preserves its cache/override/mirror rules. |
| Main acknowledgement | Read status, then `DSB SY`; test bit `0x1`. If clear, repeatedly delay 10 microseconds, read status, `DSB SY`, up to 100,000 delays. Always perform a separate final read plus `DSB SY` and test bit `0x1` again. |
| Main timeout | Call `wlc_dump_ucode_fatal(...,2)` and return `-1`; extended startup and FIFO verification are skipped. |
| Extended prepare | On the common-public feature byte `+0x1a7`: `DSB ST`, write `0xffffffff` to extended status; if cached extended control differs from `0x402`, update cache and emit `DSB ST`, write `0x402` to extended control. |
| Extended acknowledgement | Same initial/loop/final reads and bit-`0x1` predicate, with its own 100,000-delay budget and extended status port. |
| Extended timeout | Call `wlc_dump_psmx_fatal(...,2)`, then proceed to FIFO verification; this branch does not itself return `-1`. |
| FIFO verification | For core 130, logical 16-bit SHM value at `0x422` must satisfy `((value >> 13) & 7) == 2`; otherwise return `-1`, else zero. |

The loop budget is a counter initialized to 1,000,009 and reduced by 10 per
delay, stopping at 9; nominal requested delay is at most 1,000,000 microseconds
per bank. It is not an elapsed-wall-clock deadline. Immediate acknowledgement
still requires the separate final confirmation read. The status bit is also
identified as `MI_MACSSPNDD` by suspend diagnostics: startup is waiting for a
startup-suspended acknowledgement, not proving RX/TX operation or every firmware
service ready.

Attach sets the extended-feature byte to `(core_revision > 127)`, so the core-130
path includes extended startup. A fake bus must separately model status-register
write side effects; observing writes of all ones does not by itself implement
write-one-to-clear behavior.

The reused `wlc_bmac_init` ignores this startup helper's return and continues with
`mctrl(mask=0xc000,value=0)`. A new implementation that stops on timeout or layout
failure is a deliberate stricter policy, not the observed caller behavior.

## Suspend, enable, and wake ledger

Both suspend entry points read control with a following `DSB SY` and do nothing
unless bit `0x2` is set. They increment their separate nesting depths; the first
level performs hardware work. Counter overflow/underflow is not validated by the
stock entry points and should not be silently converted into a supported API.

| Operation | Predicate, order, and failure |
| --- | --- |
| Main suspend | Set wake-override reason `0x4`. If it was the first override and the main cached wake bit was clear, force `0x04000000` through mctrl and call wait-for-wake. Read control/status with `DSB SY`; either all-ones value invokes `wl_down`. Clear control bit `0x1` via mctrl. Poll status bit `0x1` using initial read, up to 8,300 delay-10/read iterations, and a separate final confirmation. On timeout, dump fatal except the explicit chip-`0xa8d1` exception, set timeout flag `0x2000`, and return. On success read control again; an all-ones value invokes `wl_down`. |
| Extended suspend | Separate depth and extended cache. Same all-ones checks, clear cached control bit `0x1` with a changed-value `DSB ST` write, and the same 83,000-microsecond nominal poll budget. Timeout calls extended fatal and sets `0x2000`, then still performs the final control/all-ones check. It does not call the main wake-override helper. |
| Nested suspend | No repeat hardware suspend when depth is already nested. If control bit `0x1` remains set, emit diagnostics and the bank-specific fatal callback. |
| Main enable | Require control bit `0x2`. If bit `0x1` is already set and depth is zero, return. Otherwise decrement depth and perform hardware work only when it reaches zero. Set main control mask/value `0x1`; `DSB ST`, write status `0x1`; read main control/`DSB SY`, then main status/`DSB SY`. Poll logical SHM state `0x40`: while it is 3, decrement a 1,001-read counter and delay 10 microseconds unless exhausted. Success requires state 2. Other values or exhaustion invoke fatal reason `0x34` if not already set, unless timeout flag `0x2000` suppresses fatal. Afterwards clear wake-override reason `0x4`, possibly call `wlc_set_wake_ctrl`, and optionally reset the MBSS request queue. |
| Extended enable | Require extended control bit `0x2`; decrement extended depth and act only on reaching zero. Read control again with `DSB SY`; set cached bit `0x1`, writing only if changed; `DSB ST`, write extended status `0x1`; read extended control/`DSB SY`, then extended status/`DSB SY`. There is no SHM state poll in this function. |
| Wait for wake | No-op when the band pointer is null. Otherwise delay 40 microseconds, then read SHM state `0x40` until 2 or the budget expires. Budget is unsigned-16-bit field `+0x212`, plus 9 for 10-microsecond rounding. Maximum further delays are `ceil(budget/10)`, each followed by another read. Always reread state after leaving the loop. Final state 2 can set software byte `+0x4cf`; otherwise read diagnostic registers, set reason 3 if no reason exists, and call `wlc_fatal_error`. |

Main-enable polling has at most 1,001 SHM reads and 1,000 delays: 10,000
microseconds nominal requested delay. Wait-for-wake's budget comes from earlier
fast-power-up/synthesizer delay calculations in BMAC initialization, not a fixed
constant established here. Fatal/down helpers are callouts with effects not
expanded in this pass. Some stock callers continue if these callouts return.

## Reset across the exact HND dependency

`wl.ko` binds its reset callback to imported `si_core_reset` on core unit zero.
The supplied exact `hnd.ko` provides that implementation. Interconnect types
1, 3, and 4 select the AI implementation; type zero selects an SB path not
expanded here. Actual selected interconnect and wrapper mappings remain runtime
context, not values inferred solely from the chip number.

`wlc_bmac_core_reset` adds `0x20` to reset bits for core 130. It adds that bit to
the normal bits only in mode 2; other core-130 modes clear it from normal bits.
Its higher corereset caller adds `0x4` and supplies zero initial reset bits on
the observed reset branch. It also handles DMA resets, clocks, PHY state/reset,
core flags, and PLL control. Those resource operations cannot be omitted from a
claim of complete hardware reset merely because the wrapper protocol is known.

The AI reset operates on a selected wrapper base with 32-bit IO control at
`+0x408`, reset control at `+0x800`, and reset status at `+0x804`.
For D11 core ID `0x812` and revision 130, its current-wrapper protocol is:

1. Read reset status plus `DSB SY`; if nonzero, delay/read in 10-microsecond
   increments, at most 30 delays (300 microseconds). Continue even if still busy.
2. `DSB ST`, write reset control 1; delay 10 microseconds; repeat the bounded
   reset-status wait.
3. `DSB ST`, write IO control `bits | reset_bits | 3`; read it back, `DSB SY`.
   Delay 1 microsecond; `DSB ST`, write the readback value with bit `0x2` cleared.
4. Repeat the bounded reset-status wait. Initialize a release counter to 10.
   Read reset control plus `DSB SY`. While nonzero and a predecremented counter
   remains nonzero, wait for reset status, `DSB ST`, write reset control zero,
   wait for reset status, and reread reset control. This allows at most nine
   release writes and ten reset-control reads, rather than ten release writes.
5. `DSB ST`, write IO control `bits | 3`; read it back, `DSB SY`; delay 1
   microsecond; `DSB ST`, write readback with bit `0x2` cleared; delay 1 microsecond.

Each busy wait checks for an exact zero reset-status value and can exhaust its
300-microsecond nominal delay budget without producing an error return. The
function still completes final IO-control writes after release retries exhaust.
It returns void and does not attest that reset deasserted. Readback-derived
values must be supplied by the fake bus, rather than assumed to echo writes.

`ai_core_reset` performs this protocol on a secondary wrapper first when its
per-core secondary entry exists, restores/selects the primary wrapper, then
performs it again on the primary. Wrapper selection depends on runtime core
metadata and direct-map versus PCI-window transport. No physical wrapper base
or unconditional single-wrapper assumption is established by these binaries.

`ai_iscoreup` reads IO control/`DSB SY`, requires `(value & 3) == 1`, then reads
reset control/`DSB SY` and requires bit `0x1` clear. AI core flags use IO control
`+0x408`; status flags use `+0x500`. Updating either performs read/`DSB SY`,
masked update `(old & ~mask) | value`, `DSB ST`, write, then read/`DSB SY`.
Core- and chip-specific exclusion branches also exist.

After low-level reset, the WL wrapper can access auxiliary core `0x849`, restore
the prior core, and conditionally clear 16-bit D11 fields `0xa08`/`0xa06`.
Higher corereset resets cached controls, sets extended cached control `0x400`,
updates PHY/clock/PLL state, and can invoke PHY reset. These are explicit
dependencies for subsequent implementation, not simulated success by default.

## Evidence and exact next questions

The companion [Evidence summary](../findings/DRIVER_LIFECYCLE_EVIDENCE.json) records selected operation names,
artifact identities, and actual Evidence IDs, including assembly and descriptor
reads. Private exported bundles retain full records. The initial WL/HND passes
ended at the checkpoint; a later separately requested FIFO-wrapper pass resolved
the read-trace gap described above.

Next questions and work, preserving the full-driver scope:

1. Implement and test the proven lifecycle ledger with independent exact traces,
   count boundaries, final confirmation reads, callback failure, cancellation,
   and cache/depth context. Distinguish stricter fail-stop policy from stock
   callouts and ignored return values.
2. Establish selected SI interconnect, primary/secondary wrapper metadata, bus
   mapping/window, mode, reset flags, clocks and power prerequisites; connect
   reset to explicit DMA and PHY resource states. Expand relevant clock/PLL and
   PHY-reset paths rather than replacing them with successful stubs.
3. Complete the remaining SHM ABI, effective initialization-table fixups, FIFO
   layout context, and later MAC enable/suspend callers and state initialization.
   The `wlc_bmac_read_shm` FIFO-read wrapper and its raw read trace are resolved.
4. Recover PHY-51/radio/channel/calibration initialization, then DMA/interrupt/RX,
   TX and Linux WLAN integration, HE/features, stability, and hardware validation
   according to the full roadmap. No driver milestone should be marked complete
   solely because an offline subset was recorded.

## Offline AI-reset model review

A later read-only comparison of `ai_reset.c`/`ai_reset.h` against retained HND
assembly found no material discrepancy in the supported core-130 normal trace:
initial plus bounded busy reads, nine release writes/ten reset-control reads,
readback-derived bit clearing, secondary-before-primary order, and continuation
after busy/stuck-reset exhaustion match the inspected special path. No new
binary query or implementation test was performed for this comparison.

The initial review identified that `release_writes` counted queued batches. The
implementation was then refined to increment only after a successful zero-write
callback. Independent per-callback error/cancellation tests verify that accounting,
including a successful write that cancels and a failed write with possible backend
effects. These are successful **callback** counts, not hardware-commit proof.
Callback failure and validation remain explicit offline-model policies; the stock
routine returns void. See [implemented boundaries/tests](LIFECYCLE_MODEL.md).
