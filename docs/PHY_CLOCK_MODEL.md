# PHY clock and AI core-flags trace model

`driver/phy_clock.c` is original portable C for the retained PHY-clock subset of
[the resource ledger](DRIVER_RESOURCE_AUDIT.md), [RSDB workaround](DRIVER_PHY_WAKE_ADDENDUM.md)
and exact HND AI core-flags assembly. It models D11 core 130 minor 0/1,
PHY type `0xb` revision 51, chip identity **63178 (`0xf6ca`)**, and AI
interconnect 1/3/4. The chip value is a supplied SI identity, not inferred from
a PCI device-family match. Other identities and invalid boolean flags are
rejected before callbacks.

## API and resource contract

Commands are `ENABLE`, `DISABLE`, `RESET_PREPARE` and `RECORD_BANDWIDTH`.
`begin` snapshots context before clearing the job, including an aliased
`&job.context`. The context carries chanspec, existing bandwidth-state word,
software PHY-clock byte, RSDB predicate, workaround-done byte and two explicit
owner flags. No board values are supplied by default.

Current and D11-unit-1 **wrapper** resources are distinct typed regions. Their
register `+0x408` is not D11 MMIO `+0x408` or clock control `+0x1e0`. An owner
must provide stable, exclusive access and preserve the caller's map/window
state; physical addresses, runtime enumeration and save/restore mechanisms
remain external prerequisites. The model emits no invented mapping, selected
index, saved unit or interrupt token. In particular the workaround has no
fabricated IRQ-disable/restore trace; the retained WL wrapper lacks the PLL
secondary helper's IRQ wrapping.

Missing current ownership ends `DEPENDENCY_CURRENT_OWNER` at the reached
core-flags boundary. Enable/disable first update the software PHY-clock byte,
as stock stores `hw+0x210` before its first core-flags call. Preparation reaches
its initial flags before that software-clock update, so missing current owner
there preserves the old byte. Missing unit-1 ownership ends
`DEPENDENCY_UNIT1_OWNER` after already-completed current-wrapper operations.
There is no rollback or assumed successful switch.

## Exact operations and state placement

Each nonzero-mask AI core-flags update emits:

`R32(wrapper+0x408),SY,ST,W32((first_read & ~mask) | value),R32(same),SY`.

Upper bits come from the **first** read, while the final read can differ from
the write. There is no changed-cache shortcut: every reached update writes.
The final read is an observation, never a readiness predicate. This component
uses only the nonzero masks below; it does not expose a generic zero-mask query
or all chip/core exclusion branches of HND's public flags API. `ST/SY` and
delays are trace events, not host barriers or elapsed-time assertions.

| Command/stage | Recovered contract |
| --- | --- |
| Enable entry | Set software PHY-clock byte to 1. Current-wrapper flags mask `0x300`, value `0x100`; then mask `0xe`, value 0; delay 1 us. |
| Optional workaround | Only if RSDB is true and workaround-done is false: unit-1-wrapper flags mask `0x1f`, value `0xf`; then mark software workaround-done 1. |
| Enable tail | Current-wrapper flags mask/value `0x4`; delay 1 us. |
| Disable | Set software PHY-clock byte to 0; current-wrapper flags mask `0x200a`, value `0xa`; delay 1 us; flags mask `0xa`, value `0x8`; delay 1 us. No RSDB workaround. |
| Reset prepare | Current-wrapper flags mask `0x8ce`, value `bandwidth_flags | 0xe`; delay 2 us; then the enable sequence. |
| Record bandwidth | Software-only `bandwidth_state = chanspec & 0x3800`; zero callbacks and no owner dependency. It does not enable clocks or run any reset pulse. |

Preparation maps `chanspec & 0x3800`: `0x1000 -> 0x40`, `0x1800 -> 0x80`,
`0x2000/0x3000 -> 0xc0`, `0x2800 -> 0x800`, other encodings -> zero.
These are recovered selector rules, not a channel-validity or regulatory check.

The [exact reset order](DRIVER_PHY_RESET_BF_AUDIT.md) places the outer TSSI
snapshot **before** `RESET_PREPARE`; the restore follows pulse, bandwidth and
analog reset. The [target coordinator](PHY_RESET_MODEL.md) runs the pulse and its core-130 100-us delay before
invoking `RECORD_BANDWIDTH` at the actual later software-state location. Keeping
that command separate prevents publishing the new bandwidth-state word early.
No pulse, TSSI snapshot/restore, table-29 sequence or beamforming callout is
duplicated here. RSDB input acquisition and higher clock/reset composition
remain outside this component.

## Progress, observations and interruption

Every step performs at most one synchronous callback or a bounded internal
transition. `attempted_ops` counts entered callbacks, `completed_ops` counts
successful returns; neither proves hardware commitment. Each wrapper has its
own last sample, observed flag and writes-since-sample flag. A successful write
marks a newer write; its successful readback refreshes the sample. Unit-1 reads
do not overwrite the current-wrapper observation. `last_read` can refer to either
region and is not a readiness result.

`current_updates`/`unit1_updates` count core-flags calls whose final read **and
full barrier** both succeeded. Workaround-done commits with the successful
unit-1 final barrier, including when that callback requests cancellation.
This is a modeled register-call completion, not proof that an external map
restoration occurred. Its owner contract still applies. Erroring callbacks
count no success or software commit, even if the backend had partial effects.
Error wins over simultaneous cancellation; successful callback observations and
completion effects survive cancellation. Terminal states emit no further I/O.
Callbacks may cancel but cannot reenter begin/step or mutate job fields.

`TRACE_COMPLETE` means this limited software trace ended. The software clock
byte can be changed before any device operation, and the final flags read has
no clock-ready test. Neither the byte, update count nor final phase establishes
clock lock, PHY/radio readiness, firmware execution or safe DMA/RF operation.

## Validation

`tests/test_phy_clock.c` uses literal independent event/reply fixtures from the
retained behavioral ledger. Non-echo final samples verify preservation of first-
read upper bits and separate current/unit-1 observations. Enable, disable,
workaround skipped/already-done, actual workaround and prepare-plus-enable have
exact flag/delay ordering. All eight bandwidth encodings and the separate
software-only recording command are checked, alongside dependencies before
current flags and after two current updates plus delay when unit 1 is absent.

Every callback in selected traces is swept for error, cancellation and both.
An independent successful-prefix oracle checks samples, write freshness,
completed update counters and workaround-state commits, including the last
barrier and final delay. Tests also cover alias restart, all accepted
interconnects, identity/boolean rejection, absent callbacks, cancellation before
I/O, bounded progress and terminal silence. Strict ASan/UBSan execution and
freestanding ARMv7 compilation pass. No Linux binding, device test, physical
mapping, measured timing or RF operation was performed. The shared runner and
checkpoint include these tests; the target reset composes this job as a real child.
