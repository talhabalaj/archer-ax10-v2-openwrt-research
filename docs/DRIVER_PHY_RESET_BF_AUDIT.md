# Exact PHY-reset order and core-130 beamforming branch

Original sanitized account from retained reset assembly and a bounded new
REA 6.5.0/Ghidra 12.1.4 session on exact `wl.ko`, 5,803,056 bytes,
SHA-256 `8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
New queries expanded only missing beamforming callees and two tiny main-MAC
wrappers. Separately requested wake-policy queries share this session but are
reported separately. Raw records remain private. The session was exported and
closed; source identity was verified again. No device or target execution.

Actual Evidence IDs are in [selected Evidence summary](../findings/DRIVER_PHY_RESET_BF_EVIDENCE.json). This establishes
the core-130 control flow and dependencies, not hardware readiness or complete
reset resource acquisition. Host offsets below are not MMIO addresses.

## Reset ordering: snapshot precedes preparation

Let `HW` be the reset argument, `W=*(HW+0)`, current band `B=*(HW+0x1a8)`,
and public PHY information `Q=*(W+0x30)`. For type AC and band revision 51,
with the public PHY revision supplied independently, retained assembly at
`0x1adec8` establishes this sequence:

1. Initialize the 8-byte local TSSI save buffer to zero and derive bandwidth
   flags from the chanspec selector. Neither is the callback snapshot.
2. If band type `u16(B+0x1c)==0x0b` and public PHY revision
   `u16(Q+0x0a)==51`, call the actual outer
   `wlc_idletssi_force(phy,save_buffer,1)` at `0x1ae190`.
3. The ordinary AC band-revision-51 path then calls SI core flags with mask
   `0x8ce`, value `bandwidth_flags | 0xe` at `0x1ae144`; delay 2;
   call `wlc_bmac_core_phy_clk(HW,1)` at `0x1adf44`.
4. Execute the retained inline PHY reset pulse for band revision 51. The
   common join after the pulse decision separately delays 100 for core
   revisions above 64. Core 130 takes that delay independently of the pulse
   revision predicate.
5. For chip 63178, store the raw bandwidth selector in the PHY context,
   without a preceding PHY-context null check. The later analog call alone
   has an explicit PHY-context null guard.
6. Re-evaluate current band type and public PHY-revision conditions and call
   outer `wlc_idletssi_force(phy,save_buffer,0)` at `0x1ae17c` when reached.
7. Evaluate the optional beamforming branch described below, then return.

The software component called `RESET_PREPARE` currently represents step 3.
The exact callback snapshot must precede that component, not follow it.
Restore is after pulse, delay, bandwidth and analog operations. Snapshot and
restore use the **public PHY-revision field**; the pulse has a separate test
of **band revision**. Coherent attached PHY-51 context satisfies both, but the
binary does not merge those predicates. The special preparation branch tests
**band revision 47 and band minor revision 2**, not the public PHY revision.
It is not substituted for band revision 51. A narrowed model can require band
revision 51 while accepting any explicit 16-bit public PHY revision; public
revision 51 enables both TSSI gates and another value skips them. It must
retain these independent inputs rather than infer one from the other.

The local buffer is two zeroed 32-bit words, representing four initialized
16-bit slots. The stock function has no saved-value validity mask. Zero
initialization establishes defined bytes, not observed TSSI measurements.
When the idle-TSSI callback is absent, its outer wrapper returns before MAC,
PHY-register or buffer operations. With a stable absent binding, both save
and restore are no-ops and the buffer stays zero. A model may mark all slots
available because initialized; it must not label them observed snapshots.

The bandwidth selector and analog module/table distinction are recorded in
[the exact reset-path addendum](PHY_RESET_EXACT_PATH.md).

The outer idle-TSSI call is not merely its AC callback: it retains the actual
MAC suspend, PHY-register enter/wake, callback, PHY exit/wake and MAC enable
dependencies already recovered. Inactive-run guards still apply; invoking
these names does not itself establish an already-running PSM requirement.
The snapshot gate assumes a valid PHY context and does not add a null check.

## Caller gate for beamforming cache clean

After the optional TSSI restore, reset tests these conditions in order:

- Word `HW+0x578` has bit `0x1` set.
- Public byte at `*(W+0)+0x10e` is nonzero.
- `wlc_txbf_get_txbf_tx(*(W+0x69c))` returns nonzero.

The actual getter at `0x2741a0` only loads byte `txbf+0x12`; it has no
null guard, callbacks, state stores or MMIO. If true, reset reloads the TXBF
pointer from `W+0x69c` and calls `wlc_txbf_sounding_clean_cache` at
`0x2730a4`. These runtime flag/pointer values were not acquired from hardware
by this pass. Any failed caller predicate skips the entire clean operation.

## Actual clean-cache resource traversal

Let `T` be the TXBF context passed to the cleaner and `WT=*(T+0)` its stored
WLC context. This binding is used by the actual callee; a valid model must
establish it, rather than silently treating unrelated pointers as identical.

The cleaner calls `wlc_suspend_mac_and_wait(WT)`. The newly verified wrapper
loads `WT+0x18` and tail-calls retained `wlc_bmac_suspend_mac_and_wait`.
It then scans the primary slots starting at index zero. For each index `i`:

- Read bitmap byte `T+0x14+(i>>3)` and test bit `i&7`.
- Only if set, read slot pointer `*(T+0x7c+4*i)`.
- Only if that pointer is nonzero, read the peer-context pointer at
  `slot_pointer + *(T+0x0c)`.
- Only if this peer pointer is nonzero, call the index-invalidation helper
  with `(T,peer,(u16)i)`, then call the enable-bit helper with its returned
  value as the third argument.

The primary bound is unsigned word `T+0x44`, read again after every iteration.
Iteration continues while the incremented index is at most that value, so
the range is inclusive and bound zero still visits index zero. There is no
internal allocation-length validation or fixed iteration cap. A value of
`0xffffffff` can keep this unsigned loop running through wraparound; a bounded
model needs supplied valid arrays/bitmap/count or an explicit stricter policy.

It calls `wlc_enable_mac(WT)` after the primary scan. That tiny wrapper loads
`WT+0x18` and tail-calls retained `wlc_bmac_enable_mac`. Only then does it
sample public byte `*(WT+0)+0x1a7`. When nonzero, it calls retained extended
MAC suspend, scans the secondary list, and calls extended MAC enable.
Thus main enable precedes extended suspend; they are not one combined hold.

The secondary count is byte `T+0xc0`, bitmap starts at `T+0xc2`, and slot
pointer array at `T+0xc8`. This range is exclusive: indexes zero through
count minus one; count zero performs no slot scan but still reaches the
surrounding extended suspend/enable calls. Slot-pointer/cubby resolution and
two child calls match the primary logic. The count is initially sampled and
is reloaded after a valid peer entry's two helper calls; skipped entries use
the previous sample. The cleaner itself does not mutate these lists or bounds.
Stable ownership/lifetimes are an input; no host lock or interrupt guard is
established by this function.

## Core-130 children have no SHM I/O

Actual attach evidence writes hardware core revision into the public word
at `+0x14` and sets public extended feature `+0x1a7` for core revisions above
127. With coherent core-130 context, both index-update helpers below skip
their older-core SHM paths. This does **not** make the cleaner itself a no-op.

`wlc_txbf_invalidate_bfridx` first reads byte `WT+0x21`. Zero returns zero.
If nonzero, public `u16(*(WT+0)+0x14)` at least 128 returns that original
byte without SHM access, peer-field access, table writes or state stores.
Older-core SHM operations require revision below 128 and therefore cannot be
inferred as core-130 effects. The cleaner still passes the returned value
to the next helper.

`wlc_txbf_bfridx_set_en_bit` requires all of: byte `WT+0x21` nonzero,
public byte `+0x1a7` nonzero, and public 16-bit core revision below 128.
Core 130 therefore returns without SHM access or peer-field mutation,
regardless of the previous helper's return value. These no-I/O paths are
proven shipped code, not substitute callbacks returning success.

The older-core branches identify main/extended SHM calls, but their physical
lowering is outside this core-130 closure. No such target130 SHM operation is
left opaque inside the cleaner. The core-130 cleaner's externally visible
operations are the retained main and conditional extended MAC contracts;
its own remaining accesses are host metadata reads. It has no direct MMIO,
new delay, status/readiness poll, cleanup result, or rollback mechanism.

## State and implementation boundary

A composed reset model must carry the caller BF flags, public bytes/core
revision, TXBF enable byte, TXBF/WLC binding, primary inclusive bound, optional
secondary count/bitmap, slot pointers and cubby lookup validity. It must still
run MAC helpers on an enabled BF branch even with empty/skipped peer entries.
The cleaner does not test helper success returns before continuing. Existing
MAC fatal/dependency behavior and any model fail-stop policy remain explicit.
Runtime metadata initialization and kernel ownership are not supplied here.

The complete retained reset body has no VASIP or SMC call. Its return is void;
it adds no status result, readiness poll or rollback around these children.
VASIP/SMC elsewhere in higher initialization, full-driver resource integration
and later PHY/radio/calibration/data paths remain separate open work. No
production source files were edited in this analysis pass.
