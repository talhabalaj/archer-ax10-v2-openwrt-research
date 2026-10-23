# Actual registered radio-on register-table tail

[radio_regtbl.c](../driver/radio_regtbl.c) implements the current complete
`wlc_phy_set_regtbl_on_pwron_acphy` body for core130.0/1 AC PHY51.
[radio_power.c](../driver/radio_power.c) now executes this real mandatory child
after the explicit50e0 switch returns. With all reached inputs bound, the
registered priority7 radio callback can return. That closes one required
initialization callback, not the complete PHY vector, BMAC initialization,
calibration, kernel integration or working wireless operation.

The implementation requires actual runtime chanspec, TSSI/PAPD state, initial
table-load flag, owned cached BMAC state and private descriptor provenance.
No model, board identity, initial flag or payload default is inferred. The
selected source index is [RADIO_REGTBL_EVIDENCE.json](../findings/RADIO_REGTBL_EVIDENCE.json).
Raw records, native output and all proprietary table bytes remain private.
No router, hardware, RF, module load or device backend was used.

## Source order and actual children

The TSSI predicate first selects the real2g/5g byte getter according to chanspec.
The native getters traverse TPC context/data/shared state before reading bytes21
or22. Only a nonzero returned byte reaches flags470/471. Short-circuit ordering
is preserved: a true flag470 in2g does not read flag471. The ignored
`wlapi_si_coreunit` call resolves to the local `wlc_bmac_coreunit` leaf. It loads
the actual input+18 object pointer and tests that object's full word150:
zero returns1, nonzero returns2. The original C child performs that exact test;
no supplied getter return or arbitrary successful vendor callback substitutes
for it. This native return is ignored by the enclosing register-table body.

The body executes the actual initial packed PHY writes, PHY408 RMW and per-core
747/947/b47/d47 modifications. Core130's recovered selector bank is4000.
It then writes4729/4721, reads the original19e guard, and performs a separate
fresh RMW to set bit1. A nonzero PI398 flag executes exactly nine private EXT
table descriptors followed by eight zero-table descriptors. Zero descriptors
issue individual actual wrapped writes at offsets0..count-1; their descriptor
offset and payload are unused. Count0 skips the loop.

The new `axd_phy_table_write_ext_begin` entry reuses the existing real generic
writer with the exact proven no-op target HWACI leaves. It adds no outer
maincontrol read, MAC suspension or19e guard. Normal table writes retain all
those real wrapper operations, including each independent maincontrol/guard
observation and any reached real PSM child. Both entries preserve actual widths,
word order, counters and bus-dependent flushes.

After the static tables, a fresh19e RMW restores the originally sampled bit1.
The TSSI PHY writes follow. Flags470/471 then independently guard two128-entry
width8 clear loops, table5d then7d at each index. If those guards do not select
the loops, the actual PAPD epacal child tests context byte0, then byte4 only
when byte0 is zero. A nonzero byte also selects the same loops.

The final packed/PHY sequence precedes the mandatory table3f clear loop:
228 individual width32 wrapped writes, with original offset order0..227.
Seven width48 wrapped writes to table14 follow. Their six-byte records are
constructed from actual instruction immediates and persistent stack halfwords,
including the distinct retained middle words and final all-zero row. No vendor
array supplies these seven records. Only after the last real child returns
does the enclosing job complete.

## Private provenance and ownership

[radio_regtbl_inventory.py](../scripts/radio_regtbl_inventory.py) first pins the
exact stock module, then checks the function's MOVW/MOVT relocation pairs to
both descriptor arrays and both count objects. Counts are exactly9/8 and
descriptor spans180/160 bytes. Each write descriptor's ABS32 relocation resolves
the original immutable payload, validating its width, length, bounds and absence
of further payload relocations. Default output contains only sizes, hashes,
symbol/file ranges and source bindings. Optional extraction writes normalized
descriptors and the nine private payloads only into a new external directory;
it rejects another module, an existing destination and a checkout destination.

Callers resolve the private descriptors into borrowed typed requests. Payloads
and descriptor arrays remain immutable and owned outside jobs through terminal
state. Runtime snapshots are coherent and serialized. Unknown reached facts
stop explicitly; malformed known values, required nulls and short descriptor
arrays fail. Unused fields do not block a branch. BMAC object bindings and the actual word150
are required at the getter reach point.

## Failure behavior and validation

Each step performs at most one synchronous callback. Canonical hardware state
is published before callbacks and after every child transition, including
terminal ones. Successful cancelling operations retain observations, effects,
pre-I/O counters and successful child-step commits. Later stages do not run;
error wins simultaneous cancellation. There is no invented guard cleanup,
resource release, retry, rollback or readiness test. Own-context begins snapshot
aliases before clearing the job.

[Independent literal tests](../tests/test_radio_regtbl.c) check complete cold and
warm traces using nine/eight-row descriptor spans whose ids, counts, widths and
payloads are all original synthetic values (the stock rows stay private),
all mandatory228/7 writes, both optional TSSI-table clear predicates, one through
four cores, original guard0/1 restoration and actual BMAC cached-field coreunit computation.
The warm/cold traces contain3442/25622 callbacks. All10326 error,
cancellation and coincident-error boundaries in the warm trace pass, plus a
sampled cold sweep across the EXT writes and zero loops. Row counts must equal
the pinned9/8 globals; a longer supplied span fails rather than being truncated. Other checks cover pre-I/O canonical counters on failed ST, reached
unknown/null inputs, short-circuit PAPD fields, aliases and terminal silence.
The composed registered radio-on test executes the full actual switch and
cold register-table child without replacing any vendor helper with success.
Strict C11 ASan/UBSan and freestanding ARMv7 compilation pass. Eight meaningful
compiling semantic mutants are rejected, including descriptor EXT-wrapper
selection, original guard restoration,228-count bound,48-bit middle word,
TSSI order and the actual cached-field coreunit branch. This package
does not claim executed stock ARM comparison of the register-table body.

The full-vector functional probe remains the completion gate for the16 required
callbacks. Its joined cold radio, table/PMU/GPIO, enabled5GHz radar, antdiv and
active noise-mode trace consumes28039 explicit I/O events and completes11
required callbacks. Its original synthetic prefregs preserve all54 writes plus
the terminating55th record, rather than reusing an isolated empty-loop fixture.
It then reports the actual required channel13 child, with
no final return or slice-up store. All16 registry entries remain present. Isolated radio success does not permit skipping table, radar,
noise-mode, channel setup or any later required callback. Physical PLL/timing,
calibration validity, private board inputs and OS/DMA/IRQ/WLAN ownership remain
separate requirements before a replacement driver can run.
