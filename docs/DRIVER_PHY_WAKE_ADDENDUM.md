# PHY-register wake override and RSDB PHY-clock workaround

Original behavioral addendum to [the resource ledger](DRIVER_RESOURCE_AUDIT.md). A new private
REA 6.5.0/Ghidra 12.1.4 session examined exact `wl.ko`, 5,803,056 bytes,
SHA-256 `8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
Necessary SI mapping helpers came from a separate exact-HND session described
in [the fallback audit](DRIVER_CLOCK_FALLBACK_AUDIT.md). Both sessions were exported and closed,
with artifact digests verified again. No target execution or router use.
Actual IDs and reused record provenance appear in the companion
[dependency Evidence summary](../findings/DRIVER_RESOURCE_DEPENDENCY_EVIDENCE.json); raw output stays private.

## PHY-register enter/exit and reason bit

Retained enter increments the byte at PHY `+0x10e` and calls its set wrapper
when the resulting unsigned byte is at most 1. Exit decrements that byte and
calls its clear wrapper only when the result is zero. Stock byte arithmetic
can wrap; neither helper establishes an overflow/underflow validation policy.
A model rejecting invalid nesting should document that stricter policy.

The newly expanded WL shim wrappers at `0x210814` and `0x210820` load the
hardware context from their argument's first word and tail-call generic
`wlc_ucode_wake_override_set/clear` with reason `0x2`. This resolves the
previously opaque callback wrappers; it is not a second register protocol.

The set helper is reused from existing lifecycle assembly, without another
query. If hardware override word `+0x1f8` is zero and cached main control
`+0x1f0` lacks bit `0x04000000`, it stores reason `0x2`, calls
`wlc_bmac_mctrl(hw,0x04000000,0x04000000)`, then calls the retained
`wlc_bmac_wait_for_wake`. Otherwise it only stores `old_override | 0x2`.
There is no unconditional physical control write or wait on nested entry.
Retained mctrl cache/override/mirror semantics and actual wake-poll failures
continue to apply; they are not replaced by a success stub.

The newly expanded generic clear at `0x1a3570` always stores
`old_override & ~reason`. A nonzero result returns without invoking wake
policy. A zero result calls `wlc_set_wake_ctrl(*hw)`, including when reason
`0x2` was already absent and the old override was zero. There is no explicit
barrier on the software mask store. The retained public-up guard, composite
stay-awake policy, cached mctrl update, and conditional wake wait then apply.
Clearing reason 2 therefore does not necessarily clear the hardware wake bit.

The earlier idle-TSSI sequence invokes MAC suspend/enable around these PHY
register helpers. Their retained inactive-run guards still apply: a PHY reset
with inactive MAC control does not require an already-running PSM merely
because the helper names include suspend and enable.

## Actual RSDB workaround at `0x1a0968`

The earlier PHY-clock assembly calls `wlc_bmac_4349_btcx_prisel_war(hw)` only
when the actual RSDB predicate is true and hardware byte `+0x264` is zero.
The caller sets that byte to 1 after the wrapper returns. The chip-name-like
function name is not an additional chip guard or justification to omit it.

The wrapper's complete sequence is:

1. Obtain current SI core unit with `si_coreunit(hw->si)`.
2. Call `si_d11_switch_addrbase(si,1)`.
3. Call `si_core_cflags(si,0x1f,0xf)`.
4. Restore the saved unit with `si_d11_switch_addrbase`.

It contains no host interrupt-off/restore, explicit delay, standalone PHY
register access, or readiness predicate. In particular it differs from the
PLL secondary-clock-request helper, which does disable/restore interrupts.
Mapping-switch return values are not tested by the WL wrapper.

For the retained AI core-flags path, the actual register trace after unit-1
selection is `R32(selected_wrapper+0x408),DSB SY`, then
`DSB ST,W32((old & ~0x1f) | 0xf),R32(same),DSB SY`. It preserves the upper
bits from the first read, and the final read is not a readiness predicate.
The wrapper does not program `D11+0x1e0` or PHY address/data ports.

Exact HND selection evidence resolves each D11 switch to enumeration of core
ID `0x812` and requested unit. AI bus-0 resource selection can map runtime
core/primary/secondary wrapper physical addresses; bus 1 instead programs
PCI configuration windows. See the fallback audit's resource table. Actual
enumeration, mapping resources, bus/interconnect and ownership remain runtime
inputs, rather than hard-coded addresses inferred from this workaround.

## Remaining boundaries

Reason-2 wrapper internals and the RSDB workaround are now resolved at their
actual WL/HND call boundaries. Complete wake-policy input acquisition,
runtime RSDB count/exclusion state, SI metadata acquisition and kernel mapping
backends still require implementation/evidence. The AC table-read branches,
beamforming branch, optional VASIP/SMC, and later PHY/radio/calibration/DMA
data paths remain distinct open work. These findings do not establish radio
readiness or complete the replacement driver.
