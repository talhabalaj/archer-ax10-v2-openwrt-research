# Actual 50e0 radio-on continuation

`driver/radio_power.c` composes the existing actual AC radio-on prefix with
the required logen, PLL, RCAL and RCCAL continuation. For an explicit50e0
identity, core130.0/1 AC PHY51, chip63178 and zero channel-manager data10 flag,
the switch command now completes the actual switch body. It does not establish
radio readiness or the board's actual radio identity. The registered radio-on
command still stops at its mandatory post-switch register-table child.

This is an addendum to [RADIO_CONTROL_MODEL.md](RADIO_CONTROL_MODEL.md), whose
older prefix API remains available. The pinned current `wl.ko` digest and exact
retained assembly records are indexed in
[RADIO_POWER_EVIDENCE.json](../findings/RADIO_POWER_EVIDENCE.json). The already
exported/closed source session was reused; no new native analysis session or
hardware/RF operation ran for this package.

## Real mode child and private provenance

`AXD_RPOWER_LOGEN` implements the actual22 radio modify calls in
`wlc_phy_radio20704_sel_logen_mode`. All calls execute the existing real radio
READ and WRITE leaves, including address OR, barriers, u16 bookkeeping and
bus1/bus2 flushes. The final modification remains a real tail call. Whole-ON
entries use mode0 exactly. Standalone modes0..2 select the corresponding byte;
larger modes are rejected rather than reading outside initialized stock scratch.

The mode helper constructs scratch rows from18 private bytes with their original
padding/layout: a three-byte row at0, six-byte rows at4 and12. Its actual
MOVW/MOVT relocation pair binds the selected anonymous rodata anchor. The
separate RCCAL setup uses eight private bytes, loading u16 at0 and u32 at4.
Its actual switch-body ABS32 relocation identifies that span. Neither span's
values are embedded in the public implementation or fixtures.

`scripts/radio_power_inventory.py MODULE` validates the pinned module, both
relocation paths, selected sections/ranges, span hashes and absence of span
relocations. Default output is metadata only. Optional `--private-output`
requires a new directory outside this checkout; raw18/8-byte spans remain
private. The published Evidence file records only offsets, lengths and hashes.
The C job borrows immutable external spans until terminal; its caller must pin
their provenance. Original synthetic test spans use unrelated values, including
different masked bits between the rows to detect row-selection mutations.

## Per-core setup and exact polls

After the prefix's actual private prefregs loop and radio88b modification,
the mode child runs. Per-core setup follows in source order. Cores0/1 use
their distinct1f2/3f2 and a3/2a3 selectors; later bounded core indices use the
actualffff sentinel calls. Those calls preserve reached radio-write counter
work. They are not silently replaced by ordinary writes.

The following actual packed PHY writes, delays and radio modifications start
PLL calibration. PLL882 bit40 is sampled at most101 times; every unsuccessful
sample is followed by its100us delay, including the last. The real per-core
56/256 and6a/26a modifications run afterward. Only a successful lock performs
a fresh882 read and copies its low6bits through radio87a mask3f.

The RCAL87c loop delays1000us before each of at most101 reads and checks bit1.
Success performs another fresh read; bits2..7 are accepted only for values
2..60, otherwise the source fallback50 is used. Exhaustion also uses50.
Subsequent83d/815/83c writes and cleanup modifications retain their exact order.
Exhaustion does not become a fabricated successful lock observation.

## RCCAL software state and arithmetic

Before RCCAL, the reached AC-radio/data bindings become required. The actual
software stores are separated: multiplier1000 hexadecimal, saved halfword8=10a4,
then saved halfwordA=low16(multiplier). Private RCCAL data is required afterward.
Partial bindings/spans therefore preserve all preceding physical/software work.

Two real RCCAL passes program2ee/2ef/2f0 with the private parameters, reset and
start the measurement, then delay100us before each2f3 read. Each pass permits
exactly10000 samples, checking bit0. Both success and exhaustion then perform
the actual2ee/mask1/value0 modification before fresh2f4 and2f2 reads. The zero
value follows the actual ARM poll-exit register flow; the decompiler omits that
fourth argument, so its abbreviated call alone is insufficient evidence.

Both samples are sign-extended from13bits. Their difference is multiplied with
u32 wrap, then divided **unsigned** by actual PI clock-word3e8 shifted right12.
Pass0 uses12430 hexadecimal and stores the quotient's low16 first to saved6,
then saved8. Pass1 uses12f89, divides1000000 hexadecimal by that first quotient,
and stores the full u32 multiplier. Zero done flags use the actual saved8 and
multiplier defaults without reading unused clock metadata. A reached zero
divisor reports the explicit unresolved ABI division-by-zero dependency.

After each pass, real2f0 cleanup precedes the next pass or final per-core
85/49/4a and285/249/24a full-mask modifications. Full-mask modifications still
perform their real reads. Saved halfwordA is updated from the final multiplier
after9f9 cleanup, then the actual18 final radio modifications run.

## Switch return and registered callback boundary

The actual switch tail resolves channel-manager data. A nonzero data10 flag
requires `wlc_phy_set_regtbl_on_pwron_acphy` and channel setup, and stops before
either is invented. A zero flag stores PI47e=1, then checks the actual chip.
Chip63178 follows the recovered return; other-chip tails remain dependencies.
The registered AC-on prefix clears data10 itself, preserving that actual path.

After the switch returns, the registered AC-on wrapper unconditionally calls
`wlc_phy_set_regtbl_on_pwron_acphy`. `AXD_RPOWER_ON_REGISTERED` therefore reports
`AXD_RPOWER_REGTBL_ON_PWRON` with switch-completed1 and PI47e=1, while its overall
callback remains incomplete. It never converts this remaining child into
success. The scheduler's priority7 child now composes this stronger job; legacy
`context.radio` stays synchronized, while private spans and calibrated fields
live in `context.radio_power`. Canonical PHY/PSM state remains table.hardware.

## Validation and limits

The independent literal suite checks all1157 events of a synthetic two-core
switch trace, all3471 failure/cancel/coincident-error callback boundaries,
the registered post-switch dependency,3/4-core sentinel cases and full
101/101/10000/10000 exhaustion. Calibration edge checks include fresh RCAL
values1/2/60/61, both signs of13bit values, a negative difference with u32
multiplication wrap, zero primary divisor and zero second quotient. It checks
mode0/1/2 row selection, missing/short private spans, partial calibration stores,
own-context aliases and canonical packed-child state before a failed first ST.
Real scheduler integration checks every pre-callback canonical snapshot and
selected calibration-store error/cancel boundaries. Strict ASan/UBSan passes;
eight compiling private semantic mutants are rejected.

`tests/check_radio_power_arm.py MODULE` independently executes the actual
mode/read/write/modify stock instructions, resolving the exact reviewed mode
branch/tail bindings and private MOVW/MOVT pair. All146 complete mode cases
match the original C event sequence, counter state and operation accounting,
with actual pinned private mode bytes and strict supplied reads. Typed context,
private and initialized-stack accesses are bounded. No vendor helper success
stub or external OS service is supplied.

That ARM comparison covers the mode child, not the full switch's delay/poll/
calibration body or registered wrapper. Those are checked with retained exact
assembly and literal C protocols. The component does not acquire actual radio
identity, clocks, resources, registry state, OTP/NVRAM or private data. Physical
timing, PLL/calibration validity, the remaining register-table/channel children,
OS ownership, Linux WLAN integration and real reception/transmission remain
gates before a replacement driver can run.
