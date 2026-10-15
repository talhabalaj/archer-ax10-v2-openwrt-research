# Core-130 wake, suspend and enable implementation

Original offline `driver/psm_control.c`, grounded in the retained
[lifecycle ledger](DRIVER_LIFECYCLE_AUDIT.md#suspend-enable-and-wake-ledger) and
[Evidence IDs](../findings/DRIVER_LIFECYCLE_EVIDENCE.json). This adds the normal
wake/suspend/enable register paths around the existing startup subset. It does
not supply hardware access, resource ownership, fatal recovery or Linux
integration. The actual pure wake predicate and action wrapper are now
implemented from the [closed policy audit](DRIVER_WAKE_POLICY_AUDIT.md), using
explicit externally acquired inputs.

## Commands and explicit context

The copied context carries core/minor identity, main/extended control caches,
separate suspend depths, wake-reason mask, mute/mirror state, band presence,
u16 wake budget, software hint/reason/timeout fields and the MBSS flag. Inputs
must come from the eventual driver/resource owner; zero is not proof that an
unknown optional feature or hardware state is absent.

| Command | Implemented normal trace and outcome |
| --- | --- |
| Wait for wake | No-op without a band. Otherwise delay 40 us, read 16-bit SHM state at byte offset `0x40`, then at most `ceil(budget/10)` delay-10/read iterations until state 2. Always a distinct final SHM read. A confirmed state 2 conditionally updates the software hint. |
| Suspend main | Read control/`SY`, require run mask `0x2`, increment main depth. Nested entry checks the original control sample. First entry sets wake reason 4 and, when needed, sets the cached wake bit and executes the wake wait. Check control/status for all ones, clear MAC enable mask `0x1` through mctrl, then poll suspended status. |
| Suspend extended | Independent depth/cache. Nested entry takes an extra control sample; first entry does the control/status checks without a main wake override, then clears extended enable and polls its own status. |
| Enable main | Read control/`SY`, honor already-enabled/no-depth no-op, decrement depth and act on zero. Changed-cache control update, status write 1, control and status readbacks, then SHM state reads while state is 3. State 2 is required. Clear wake reason 4, then expose any remaining wake-control/MBSS dependency. |
| Enable extended | Separate depth/cache, extra pre-write control read, changed-cache enable update, status write 1, control/status readbacks. No SHM state poll. |
| Override SET | Arbitrary full32 reason, including zero. If old reasons zero and cached wake clear, store reason, update cached wake and run the exact wake wait. Otherwise only OR reason. |
| Override CLEAR | Always store old reasons AND NOT reason. Nonzero result returns; zero invokes the actual wake-control wrapper, including when old mask was already zero. |
| Wake control | Explicit known public-up zero is a no-I/O return. Up true reads physical control/SY, evaluates policy, applies cached wake set/clear, and waits only when policy true and the original sample lacked the wake bit. |

The literal constants above are bit **masks**: run `0x2`, MAC enable `0x1`.
Each SHM debug read is `ST,W32(0x160,0x10010),R32(0x160),SY,R16(0x164),SY`.
The address-port readback is discarded; only the final data read supplies state.
Read16 results are narrowed before evaluating the state.

Suspend has initial status read, at most 8,300 delay-10/read iterations and a
separate final confirmation, followed on success by a final control/all-ones
check. Main enable allows 1,001 SHM reads and 1,000 delays while state remains 3;
it has no invented additional final SHM read. These are counter budgets, not
elapsed-clock deadlines. Every public step has at most one callback.

## Completion and deliberate policy differences

`DONE` has a separate result: no-op, nested-depth change, woken, suspended,
enabled, override-updated or wake-updated sequence. None proves RF, valid PHY
calibration or RX/TX.

Main enable, override CLEAR and standalone wake-control now execute the actual
wake action when reached public-up/policy state is supplied. Unknown public-up
stops before hardware I/O; known up with unknown reached policy stops after the
physical control/SY. MBSS request-queue work remains an unexecuted dependency.
`pending_callouts` records missing work, not a successful callback. Legacy begin
leaves policy unknown, preserving its previous dependency behavior.

`axd_psm_policy` supports explicit known policy results or an optional immutable
`inputs` pointer to the [actual pure predicate snapshot](WAKE_POLICY_MODEL.md).
The latter takes precedence. Storage must remain valid and stable outside all
jobs through terminal state; copied policy snapshots copy the pointer, not its
objects. At the decision point the evaluator receives current `wake_reasons`,
including preceding reason-2/4 clear. Initial override state must not be frozen
with the other inputs. Compact decision/reason/pass/slot/access telemetry is
retained. Missing reached facts become dependency; known-invalid reached
bindings fail `POLICY_STATE`. Public-up false bypasses predicate evaluation.

The physical wake sample is separate from later SHM/cache state. An unchanged
cached control can suppress a write while the original physical sample still
requires a wake wait. Conversely, a sample already carrying wake avoids that
wait even when the cache update writes. No policy value is a readiness result.

The original implementation deliberately:

- Rejects depth overflow/underflow instead of reproducing unchecked wrapping.
- Stops at invalid wake state **before** stock diagnostic reads at `0x154`,
  `0x6ca`, `0x7e8` or additional diagnostic/fatal helpers. These accesses/callouts
  are unimplemented; tests do not claim a complete stock failure trace.
- Stops on all-ones suspend checks before executing `wl_down`.
- Stops on missing suspended ACK, recording timeout flag `0x2000`; stock extended
  timeout can continue through a diagnostic to a final control check.
- Rejects every non-2 main-enable result, even when stock flag `0x2000` suppresses
  fatal and lets cleanup continue. That case has no pending fatal and retains
  wake reasons. Other invalid results record a required fatal callout/reason.

Software diagnostics in the copied job are model state, not proof that stock
fatal helpers ran or that a hardware recovery/cleanup took place. No callback
failure, timeout or cancellation invents rollback or retry.

## Shared I/O and restart behavior

The startup and control components now share `driver/d11_io.h`. Startup event
names/types moved from `AXD_START_*`/`axd_start_operation` to
`AXD_D11_*`/`axd_d11_operation`; primitive numeric values and startup trace are
unchanged. Phase/error names remain specific to each job. Delay/barrier events
are requests to a synchronous backend; host tests record them without sleeping
or issuing hardware barriers.

The three context-based begin functions for PSM control, startup and AI reset
now snapshot context before clearing the job. A caller can safely restart/chain
using `&job.context`. Previously that alias was zeroed before copy. Independent
regressions exercise a main suspend-to-enable chain and startup/reset restarts;
private mutants removing the fixes fail specifically at these regressions.

A mirror-cache timing bug was also fixed in startup and control: the primary
cache changes before its primary write, but the mirror cache must not advance
when merely queueing primary work. Its software store now happens immediately
before the mirror store barrier, after primary barrier/write success. Cancel or
error at either primary event retains old mirror cache, including successful
cancellation at the primary write. Failure at the mirror barrier sees the new
mirror cache, matching its preceding software-store position. Independent
prefix regressions and private premature-update mutants verify both modules.

Callbacks may request cancellation, never reenter begin/step or mutate job
fields. Successful callback cancellation persists, including final reads or
barriers; a simultaneous callback error wins. Terminal calls produce no I/O.

## Validation and outstanding work

Independent literal traces and source-derived long scripts cover the five original
commands and the new override/action APIs, optional/cache/mirror paths, no-band
and hint behavior, maximum u16
wake budget, exact suspend/enable limits, final-only/lost ACK, nested reads,
invalid depths, all-ones checks, dependency combinations and every callback's
failure/cancellation precedence. ASan/UBSan and freestanding ARMv7 checks pass.
The optional pinned-table/selector/upload comparisons remain separate.

Higher reset/SI composition, runtime policy-input acquisition, MBSS/fatal
recovery, physical init-object writes and full SHM ABI remain required.
[PHY nesting and outer TSSI](PHY_GUARD_IDLE_MODEL.md) now use these real override
and policy-action paths. The [roadmap](../ROADMAP.md) retains PHY/radio/channel/calibration,
DMA data path, RX/TX, Linux and exact-board hardware milestones.

## Optional nullable-band binding

Enclosing lifecycle jobs can set `band_binding_required=1` and explicitly supply
`band_presence_known` plus the normalized `band_present` predicate. A reached
wait-for-wake band guard with unknown presence stops as `DEPENDENCY` with
`AXD_PSM_BAND_STATE` (mask `0x10`). Earlier physical control reads, cache changes,
mirror updates and writes remain committed. Unknown bands are not rejected
when public-down, a SLEEP decision or an already-awake physical sample avoids
the wait. Known null skips the wait; known present uses the existing delay/SHM
protocol. Invalid enabled binding flags are rejected at begin. Legacy zero
`band_binding_required` retains the existing caller-known predicate behavior.

The focused override suite checks both placeholder values for unknown presence,
known-null/present behavior, lazy guards, alias restart and every callback fault,
cancellation and combined exit in the preceding/complete wake traces. This is
synthetic context validation with no new hardware operations.
