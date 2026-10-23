# Registered PHY51 channel initialization: modules 0-3

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
and 50e0 runs the real resetcca then
`wlc_phy_chanspec_radio20704_setup(PI, chanspec, AC flags bits1/2)`. Its5GHz body,
`wlc_phy_radio20704_pll_tune` and VCO calibration are implemented, completing
module2. Module3 `chanspec_setup_phy` is implemented for rev51: the hi-RSSI,
LESI and smoothing block runs only when the revision is not0x33, so PHY51
executes the PHY70 bit12 pulse around a 10us delay and the actual
`phy_utils_write_phyreg(107d, chanspec low byte)`. Paths where module2 returns
early (other radio IDs, no table or row) continue into module3, as in the
actual code. Module4 `chanspec_setup_cmn` has begun: with AC flag bit0 set (the
initialization path) it binds the STF data and antenna-diversity objects and
runs `phy_ac_antdiv_chanspec` (5GHz: the board configuration byte; 2.4GHz: 0
when disabled, else bit channel-1 of the per-channel mask; all explicit runtime
inputs). The next required child, `wlc_phy_set_reg_on_reset_acphy`, is the
reported gate. Nothing
after it is claimed; no radio ID or revision is inferred from the board.

## radio20704 5GHz body

The channel row comes from the actual tune-table selector: radio revision
(public+0xc) below3 uses one table for every band, revision3 one table per band,
and the SI package word (sih+0x64: 0x84/0x85/0x86000000) picks the 63178 or 47622
slice variant; other values or revisions select no table, so the setup returns
and module3 is the gate. The tables are PRIVATE vendor data: callers bind a
borrowed span whose identity and native row count (0x56, 0x7f, 0x48 or 0xe) must
match the selection, and the job linearly searches 16-byte rows for the channel.
Tests use an original synthetic 86-row table, never the vendor rows.

With argument1 the real LOGEN pulse (PHY728 bit8, 1us) runs. For5/6GHz chanspecs
eleven radio writes take per-channel bytes4..14 with their masks/shifts, then
88a bit7 and 9f6 bit0. Frequencies at or below3000MHz and above6289MHz are
explicit2.4GHz/6GHz gates. The5GHz branch sets or clears 9f6 bit13 by the
actual package/frequency predicate, runs a25-write per-core block (core1 at
+0x200, other cores the actual ffff selector) ending with the 1f3/3f3 loop tail,
four tail writes, and per-core 2d bit4 (set at or below5999MHz).

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

## PLL tuning and VCO calibration

[radio20704_pll.c](../driver/radio20704_pll.c) computes the PLL value block of
`wlc_phy_radio20704_pll_tune` from the channel frequency and a runtime
configuration (reference word, reference shift, mode byte) reached through the
AC radio object. Only the offsets the actual code stores are updated in the
caller-owned u16 block; the rest keep their values. The chain keeps the actual
64-bit/32-bit boundaries, including two deliberate low-word truncations (the
doubled loop constant and the 10^9 product) and 64-bit signed clamped
differences. A mode byte above2 is the actual early return (no stores, no
writes). Where a divisor would be0 or1 the vendor divide leaves its output
untouched, so the actual code would read an uninitialized stack slot; such
inputs are refused as a binding failure instead of inventing a value. The
channel job then writes all73 register fields through caller-bound selector,
mask and shift descriptors (ARM shift semantics), sets 9f6 bits14 and15, and
runs the VCO calibration sequence (eleven radio writes, two 10us delays).

Verification limits: the HND helpers are matched to the pinned instructions,
but this routine is checked by a double-entry comparison, not by executing its
instructions. [check_radio20704_pll.py](../tests/check_radio20704_pll.py)
holds a second, separately written Python transcription (with its own helper
implementations); it agrees with the C on 16896 cases (7875 full computations
at crystal-scale references). Both derive from the same recovered order, so a
shared misreading would not be caught. The 0x6c saturation at127 is not
reached by realistic inputs and is untested. The channel tests write the
fields from Python-generated value tables, not from the C.

## HND fixed-point helpers for PLL tuning

`pll_tune` derives its PLL fields with HND `math_fp_*` helpers from `hnd.ko`.
[hnd_math_fp.c](../driver/hnd_math_fp.c) reimplements the reached closure:
head room, 64-bit multiply/divide with Q-format shifts, 32/64-bit round, floor
and ceiling, the local 64-bit shift routines and `math_uint64_divide`. ARM
register-shift edge cases (byte-truncated counts, zero at32 or more) are kept.
The vendor divide is not exact: its carry term `(0xffffffff%d+1)%d` drops one
unit when the sum equals d (2^32/2 yields 0x7fffffff); this is reproduced on
purpose because PLL values depend on it.

[check_hnd_math_arm.py](../tests/check_hnd_math_arm.py) executes the actual pinned
helper region, including its unnamed shift routines, with internal calls and its
bit-length table resolved from the module's own relocations. The only external
calls, the kernel's `__aeabi_uidiv`/`__aeabi_uidivmod`, use their ABI-defined
unsigned semantics and are counted; any other call or relocation fails. 37246
cases (about2.6 million executed instructions) match the C exactly, and three
compiling mutants (shift-count width, divide carry, normalization limit) are
rejected. `pll_tune` itself is not implemented yet.

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
joined independent trace (28990 events); its reported terminal child is now
`wlc_phy_radio20704_pll_tune`. The radio20704 tests cover table identity/count
binding, missing channel, unselected package and revision (including that
revisions above3 never read the package word), the revision3 per-band table,
the47622 package, both bit13 outcomes, the2d frequency threshold, and the2.4/6GHz
gates; thirteen further compiling mutants of this section are rejected. Completed callbacks remain eleven:
callback13 returns only after all fourteen modules and its epilogue exist.
