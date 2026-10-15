# Core-130 wake, suspend and enable implementation

Original offline `driver/psm_control.c`, grounded in the retained
[lifecycle ledger](DRIVER_LIFECYCLE_AUDIT.md#suspend-enable-and-wake-ledger) and
[Evidence IDs](../findings/DRIVER_LIFECYCLE_EVIDENCE.json). This adds the normal
wake/suspend/enable register paths around the existing startup subset. It does
not supply hardware access, resource ownership, wake policy, fatal recovery or
Linux integration.

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

`DONE` has a separate result: no-op, nested-depth change, woken, suspended or
enabled register sequence. None proves RF, valid PHY calibration or RX/TX.

Main enable returns `DEPENDENCY`, with no successful result, when it requires
`wlc_set_wake_ctrl` or MBSS request-queue work. `pending_callouts` identifies the
missing work; it is not an executed or successful callback. The actual wake
policy is mapped in [the resource audit](DRIVER_RESOURCE_AUDIT.md#main-enable-wake-control-dependency).
It includes BSS/scan/association state and cannot be inferred by clearing reason
4. No placeholder reports that policy or MBSS initialization as complete.

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

Callbacks may request cancellation, never reenter begin/step or mutate job
fields. Successful callback cancellation persists, including final reads or
barriers; a simultaneous callback error wins. Terminal calls produce no I/O.

## Validation and outstanding work

Independent literal traces and source-derived long scripts cover all five
commands, optional/cache/mirror paths, no-band and hint behavior, maximum u16
wake budget, exact suspend/enable limits, final-only/lost ACK, nested reads,
invalid depths, all-ones checks, dependency combinations and every callback's
failure/cancellation precedence. ASan/UBSan and freestanding ARMv7 checks pass.
The optional pinned-table/selector/upload comparisons remain separate.

Clock/PLL and higher reset composition, PHY-register/TSSI wake overrides,
complete wake policy, MBSS, physical init-object writes and full SHM ABI remain
required. The [roadmap](../ROADMAP.md) retains PHY/radio/channel/calibration,
DMA data path, RX/TX, Linux and exact-board hardware milestones.
