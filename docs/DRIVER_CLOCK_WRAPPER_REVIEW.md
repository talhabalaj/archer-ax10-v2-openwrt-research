# Reviewed HND clock resource wrapper and WL fallback

Original sanitized re-review of retained actual assembly only; no new REA
queries, session, source changes, router access or target execution. Both exact
artifact digests were rechecked. Existing sessions are already exported/closed.
Companion selected Evidence JSON supplies actual IDs.

## Signed revision and return

The public si_clkctl_cc wrapper reads signed 32-bit CC revision SI+0x14.
Revision <=5 returns exactly zero before any resource selection, IRQ callback,
MMIO, delay or body entry. Otherwise it preserves the full mode argument and
tail-calls the body. The body rechecks signed revision before either fast or
ordinary work. For revision >5, every ordinary exit returns full32(mode==0),
including skipped clock work, fast-map absence, and exhausted status polling.
The return is not readiness. Thus mode 0/revision <=5 is false, not true.

## Fast mapping

Fast selection requires bus word SI+4 ==1, and bus-core ID SI+8 either 0x83c,
0x820, or 0x804 with unsigned bus-core revision SI+0xc >12. It first rejects
original current map SI+0xa0 ==0, then computes u32(map+0x3000), and rejects
only a zero result. Arithmetic carry/wrap itself is not rejected: a nonzero
wrapped result is accepted. A rejected map returns(mode==0) before clock gates.
Fast mapping never saves/switches/restores core index or invokes either IRQ
callback, even when capability/mode subsequently skips clock work. The derived
CC address is a local value; no map/index store is performed by this body.

## Ordinary IRQ/selection sequence

1. Load disable callback SI+0x84, then initial current index SI+0xa4.
2. If callback is null, token is 0 and no metadata/core-ID guard loads occur.
   Otherwise load metadata pointer SI+0xb4, core ID at metadata+0x80+4*index,
   and configured IRQ core ID SI+0x7c. A mismatch sets token 0 without calling.
3. On a match, call disable with freshly loaded context SI+0x80. Save its full
   32-bit return token. RELOAD SI+0xa4 after callback; this post-callback index
   is the eventual restoration request. If callback did not run, use initial
   index. The stock wrapper does not require disable to preserve current core.
4. Call si_setcore(SI,0x800,0) unconditionally before capability/mode skip tests.
   Keep its actual returned core mapping. There is no generic zero-map guard;
   a reached clock access can dereference an absent mapping. A backend failure
   here is explicit host-model safety, not a stock error-handling branch.
5. After clock work OR capability/mode skip, call si_setcoreidx(saved_index).
   Its return is ignored. Do not assume it changed current index: the actual
   selector may reject a request before its stores.
6. Only after that call, reload restore callback SI+0x88. If null, finish without
   guard metadata loads. Otherwise reload current index SI+0xa4, metadata
   SI+0xb4, configured IRQ core ID SI+0x7c and current enumerated core ID. On
   equality, call restore(freshly loaded SI+0x80 context, saved_token).

The restore guard is independent of whether disable ran. Restore can run with
zero token when disable was absent or skipped. Expected ID, metadata pointer,
current index, restore callback and context are re-evaluated after selection
restoration, not cached from the disable guard. They are host software fields;
there is no global IRQ-state Boolean, token-valid flag, hidden restore-when-
disabled rule, or automatic unwind. The wrapper adds no lock/barrier around
these callbacks. Synthetic immutable contexts may narrow mutation, but must
label that as their precondition rather than actual stock load elision.

## Gate/mode skips and actual child effects

Operation gate: capability SI+0x18 bit 0x40000, OR signed CC revision >19.
When false, ordinary selection/restoration/IRQ behavior above still occurs.
Only mode 0 and mode 2 run clock MMIO and settle/poll behavior. Other modes skip
all clock MMIO and delays, then ordinary restoration still occurs, returning0.
If capability 0x10000000 is clear, an entered mode 0/2 body delays150 exactly
once, with no status read. This is WL's fallback route. The retained audit
records all older-revision RMW/crystal and capability-present poll operations.

The wrapper itself does not directly store SI software fields. Actual selector
calls may change mapping/index: in-range AI selection stores SI+0xa0 and SI+0xa4;
bus0 can also store SI+0xac and metadata map caches. An out-of-range/unsupported
selection can leave these fields unchanged. Therefore the final IRQ guard must
use selector output state, not merely the requested saved index. Ownership,
acquisition, MMIO/PCI backend and live callback implementations remain explicit
integration resources. SB interconnect is outside retained AI closure.

## WL fallback: cache then reason mask, no PSM helper

wlc_clkctl_clk tests SI capability 0x10000000 first. When clear, it calls
si_clkctl_cc with the same SI/mode; it does not first consult HW+0x20e.
On return it loads the existing override word HW+0x1f8, compares the full32
return to 0, stores the low8 return at HW+0x20d, then sets or clears override
mask 0x10 according to the full return and stores HW+0x1f8. Its cache store is
before its override-word store. For actual si_clkctl_cc returns these values
are 0/1; a truncated byte alone is not the branch condition.

This fallback branch has NO wake-override setter/clearer call, mctrl write,
wait_for_wake, wake-policy evaluation, or PSM callback. Updating reason 0x10 is
an inline host-word update, distinct from the PHY-register reason 2 wrappers.
A model must not invent those PSM operations here. No readiness is inferred
from the mode predicate, cache byte or resulting override word.

Implementation and independent tests: [SI selector](SI_SELECT_MODEL.md),
[HND wrapper](SI_CLOCK_MODEL.md) and [WL caller](WL_CLOCK_MODEL.md). The
[selected Evidence summary](../findings/DRIVER_CLOCK_WRAPPER_REVIEW_EVIDENCE.json)
contains the reused identities, not raw disassembly.
