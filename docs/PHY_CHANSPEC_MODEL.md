# Registered PHY51 channel initialization: modules 0-2

[phy_chanspec.c](../driver/phy_chanspec.c) implements the first part of the
priority13 callback `phy_ac_chanmgr_init_chanspec` for core130.0/1 AC PHY51,
chip63178. The [scheduler](PHY_SCHEDULER_MODEL.md) now composes it as the real
callback13 child. The callback cannot return yet: it reports the next required
module as an explicit dependency. This is offline source closure, not radio
readiness, channel tuning or a working driver.

The selected source index is [PHY_CHANSPEC_EVIDENCE.json](../findings/PHY_CHANSPEC_EVIDENCE.json).
Raw decompiler/assembly records stay private. No router, hardware, RF, module
load or device backend was used.

## Structure of the callback

The callback stores a channel-manager data byte, sets AC flag bit0, optionally
sets the radar first-indicator byte, then calls `wlc_phy_chanspec_set_acphy`.
That function stores the previous chanspec halfword in the common object and
writes the argument back to PI38c, then walks a fourteen-entry, null-terminated
module table in `.data`: setup, setup_tpc, setup_radio, setup_phy, setup_cmn,
noise, setup_rxgcrs, tune_radio, tune_phy, tune_txpath, tune_rxpath, fw_enab,
cleanup and btcx. Idle-TSSI measurement and PHY51 ADC/AFE/core-to-core branches
follow. Module0 `chanspec_setup` is implemented. Module1 `chanspec_setup_tpc` is
its actual `rev>46` return for PHY51. Module2 `chanspec_setup_radio` dispatches on
the explicit radio ID at public+0xa: 2069 is an explicit gate, other IDs return,
and 50e0 runs the real resetcca then reaches
`wlc_phy_chanspec_radio20704_setup(PI, chanspec, AC flags bits1/2)`, the reported
gate. Nothing after it is claimed; no radio ID is inferred from the board.

## Module0 order

1. **Core-to-core sync, argument0.** A fresh MAC-control sample (D11 word120 plus
   full barrier) selects the real PSM suspend/enable children around the body.
   Per-core PHY 725/601 clears, the 173 bit sequence, per-core 601 sets, 173/46a,
   then a sampled 19e guard around `wlc_phy_resetcca_acphy`, restored from the
   original bit1. PHY51 then clears six radio register bits per core (88/288;
   other core indexes use the actual ffff selector), then PHY16b.
2. **resetcca.** Fresh u16 sample of D11 7c0 (core130 offset-table entry f6) with
   full barrier; a 1us delay only when sample bit1 is clear; real
   `wlc_bmac_phyclk_fgc` force/release through `si_core_cflags(2,…)` for band phy
   types4/11 only; the 6/2 writes, PHY1 bit14 toggle, and the original sample
   written back before PHY400 bit1 clear.
3. **19e sample.** Bit0 is stored to the channel manager, then 19e bit0 is set.
4. **phycorestate.** The PHY core count is recomputed from a fresh PHY0b read
   (`&7`, shifted right by one for a nonzero phymode&fff7), then the core bitmap
   and, when the temperature object exists, its tempsense byte. The count is
   therefore a runtime observation, not a fixed input. `phy_get_other_pi` is an
   identity leaf in this build, so phymode7 repeats the same-PI stores.
5. **Bandwidth.** Band byte, AC flag bit2 and bandwidth word are stored, then the
   real BMAC bandwidth set: the band's PHY pointer must alias this PI, the
   bandwidth state is stored, and AC phy type11 performs the actual
   `si_core_cflags(8c0, …)` bandwidth reset from the BMAC chanspec. Other phy
   types reach `wlc_bmac_phy_reset`, an explicit dependency. The registered
   chanmgr bwinit slot must be the identified AC leaf, which returns -EINVAL
   (ignored). A null slot would fall back to a re-entrant `phy_init`; that and
   any unidentified slot are dependencies, never assumed success.
6. **Tail.** The chip4335/80MHz branch is a gate (not reached for63178), then a
   2us delay, AC flag bit1 and the band-dependent PHY3 bit8 write.

The prologue's AC object and `chanspec_setup`'s `*(PI+10)` are the same
registered object (`phy_ac_attach` passes it as the chanmgr context word1), so
one canonical binding is used. Unknown reached inputs stop as dependencies;
required nulls and alias mismatches fail.

## Failure behavior and validation

Each step performs at most one synchronous callback through a combined D11/SI
operation. Child partial context is published before callbacks and after every
step. Error wins coincident cancellation; successful cancelling effects remain;
terminal states are silent with no invented cleanup or guard restoration.

[Independent literal tests](../tests/test_phy_chanspec.c) check the 335-event
module0 trace from recovered immediates, every software result, all error,
cancel and coincident-error prefixes, the optional delay, phymode/core-count
recomputation, optional radar and temperature objects, MAC-enabled PSM reach,
bandwidth and SI gates, null/alias failures and identity rejection. Twelve
compiling semantic mutants (bit order, offsets, delays, masks, guard source,
band predicates) are all rejected. Strict C11 ASan/UBSan and freestanding
ARMv7 compilation pass. This package does not claim executed stock ARM
comparison of these bodies.

Coverage limits, stated plainly. The fixture was written from the recovered
assembly by the same author as the C; an independent reviewer re-derived every
immediate on this path from the pinned assembly, but the fixture is not an
independently authored oracle. The MAC-enabled branch is only tested up to
reaching the real PSM suspend (with no MAC context it stops explicitly); its
suspend/body/enable success trace is not yet exercised. Three or more cores
(radio selector ffff) are untested. The scheduler requires the selected SI
mapping before starting callback13, earlier than the first actual cflags use;
this stops conservatively with fewer partial effects. The two phycorestate
count stores are modeled as one software store. The BMAC chanspec word and PI396
bandwidth state are rebound from and published to the scheduler's canonical
values, so callback4's bandwidth check and callback13's set share one state.

The full-vector probe executes modules0-2 inside callback13 and consumes its
joined independent trace (28417 events); its reported terminal child moved from
the opaque callback name to `wlc_phy_chanspec_radio20704_setup`. Completed callbacks remain eleven:
callback13 returns only after all fourteen modules and its epilogue exist.
