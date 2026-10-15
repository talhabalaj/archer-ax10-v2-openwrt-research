# Actual radio control and the remaining power-on boundary

`driver/radio_control.c` implements the actual AC switch-off sequence for
core130 minor0/1, AC PHY51 and chip63178. It also implements the real radio
register read/write/modify leaves and the recovered identity50e0 power-on
prefix. Power-on always stops at an explicit remaining child dependency; it
does not report a completed radio-on operation or a ready radio.

The source is the pinned stock `wl.ko`, SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
[Selected Evidence metadata](../findings/RADIO_CONTROL_EVIDENCE.json) records the
exact assembly, descriptor and reused registration Evidence IDs. Full analysis
records and private register tables remain outside this repository. This work
does not establish the actual radio model or subrevision of the router.

## Three distinct registered paths

The recovered AC registration sets slot0 to `phy_ac_radio_switch`, slot1 to
`phy_ac_radio_on`, slot4 to the already implemented identity thunk, and slot8
to the AC callback context. It zeroes the nine-word block first; slot3 stays
null. Actual common registration copies that block.

`phy_radio_init`, called on the BMAC graph, loads optional slot3. The known-null
case returns without I/O or radio-on work. `AXD_RADIO_CTL_COMMON_INIT` models
that real case, requiring the reached PI, common-radio and dispatch pointers.
A reached unknown slot is a dependency; a nonzero slot is an unimplemented
common-callback dependency. No arbitrary callback can report success for it.

The separately scheduled `phy_radio_on` is registered at priority7. It calls
slot1 with the slot8 AC context and returns zero only after that child returns.
The actual AC child loads the channel-manager data pointer, stores zero to its
byte10, clears PI byte47e, calls the actual radio switch with on=1, and only
then calls `wlc_phy_set_regtbl_on_pwron_acphy`. The registered-on command models
the real pointer chain and early stores, including preserving the channel data
store if a later PI binding is unavailable. The current ON child stops at a
required continuation, so the post-switch register-table tail is not executed
or treated as successful. That tail itself has substantial PHY/band dispatch.

The slot0 AC switch thunk loads PI from its callback context, clears PI47e,
then calls the actual switch body with the original on argument. The OFF body
performs another PI47e zero store. The two switch commands model these AC
thunks; their context-to-PI binding must match the supplied target PI.

The distinct common `phy_radio_switch` wrapper reads maincontrol with SY,
calls `wlapi_update_bt_chanspec`, then calls slot0. The BT thunk reaches
`wlc_bmac_update_bt_chanspec`, whose capability predicates can reach actual
BT/GCI/ECI notification children. That entire common wrapper is audited here,
but is not implemented by the AC switch commands. Its read or BT work must not
be skipped when an enclosing caller actually reaches that wrapper.

## Completed target OFF sequence

After the two AC-thunk/body PI47e stores, the core130 switch body performs:

1. Direct packed PHY writes to473e=0,4739=0,473a=0,4725=1fff,
   4729=0,4721=ffff,4728=0,4720=3ff.
2. Actual PHY modify408/mask2/value0.
3. Read public radio ID. Only identity2069 needs the public revision byte;
   revision-major2 performs one real radio modify per PHY core, selector
   `0xc | (core_index << 9)`, mask80/value0. Other identities and revisions skip
   this actual conditional loop. No radio identity is inferred from PHY51.
4. Direct packed PHY writes417=0,416=1.
5. Read chip ID. For chip63178, the actual stock branch returns here.

The child packed writer preserves the actual shared bus1 halfword counter and
threshold/version flush protocol. The PHY modify uses its actual register
read/modify/write child. The optional radio modifications use the real radio
ports and their own counter protocol below. An unknown radio ID therefore
stops after the eight writes and PHY modify; unknown chip ID stops after both
tail writes. Unknown revision is irrelevant for a non2069 identity. Other-chip
tails stop as explicit dependencies rather than borrowing the target return.

## Executed ON prefix and private span

For explicit identity50e0, the recovered core130 branch writes PHY4728=0,
4720=3ff,415=0,40e=0,40c=2000,408=0,417=0,416=d,408=7, delays1000us,
then writes408=6. These are direct writes, not modify operations with invented
reads. The actual revision check follows those effects. Revision<=3 selects
`prefregs_20704_rev0`; higher revisions select a null pointer in the stock
body. The model fails that reached null binding after preserving the prefix.

The selected private object has220bytes and55 little-endian u16/u16 records,
including one terminating selectorffff;54 actual register pairs precede it.
Its hash and ELF section/file offsets are selected metadata in the Evidence
file. The job borrows a caller-supplied immutable span with independently
verified provenance; it embeds no vendor register array. Synthetic test spans
are deliberately unrelated values. Bounds checks reject a partial record or
missing bounded terminator when reached. A completed record is counted only
after its whole actual write child returns; a partial child does not count.

After the private loop, the job performs real radio modify88b/mask3f/value1.
It then reports `AXD_RADIO_CTL_LOGEN_PLL_CONTINUATION` before the actual
`wlc_phy_radio20704_sel_logen_mode(PI,0)` call. That child loads private mode
data and performs22 radio modifications. Following it, the enclosing body
continues with per-core modifications, further PHY writes/delays, PLL/RCAL
polling and private object/state operations. Those operations are not replaced
with successful callbacks. Other identity branches, including2069, stop before
their first required unimplemented child. Consequently every ON command in
this package remains incomplete, including the priority7 scheduler path.

## Actual radio register protocol

For selector other thanffff, READ first ORs the selector with public halfwordE,
then emits ST,W16(3d8,address),R16(3da),SY. It commits the narrowed sample and
zeros the shared PHY bookkeeping halfword after the successful full barrier.
Selectorffff returnsffff without dereferencing unused PI/public metadata or
changing that counter. Selector1 follows the AC PHYtypeB path.

WRITE reads the actual shared bus word first. On bus1 it increments the u16
bookkeeping value with wrap; reaching the actual threshold performs
R32(maincontrol120),SY and then zeros that counter. This differs from the
packed PHY writer's version-read flush. The counter work precedes the
selectorffff check, so WRITEffff can flush even though it emits no radio write.
For other selectors it emits ST,W16(3d8,selector),ST,W16(3da,value). Bus2 with
actual shared revision<=3 then performs R16(version3e0),SY. The newly retained
bounded core130 descriptor read confirms that last port.

MODIFY composes the actual READ and WRITE leaves with
`(sample & ~mask) | (value & mask)`, narrowed to16bits. It therefore preserves
the ffff no-op behavior and any reached bus1 counter/flush work. It does not
substitute a PHY-port operation or an extra address readback. Unknown reached
read-address or bus-revision metadata stops at the actual point it is needed.

## State, checks and execution boundary

Begin snapshots its context before clearing the job, including its own previous
context. Private spans must remain immutable outside every job until terminal.
Each step invokes at most one synchronous callback. Canonical nested child
state is published before callbacks, including packed-write counter changes.
Successful effects and pre-I/O software stores survive cancellation/failure;
an error wins coincident cancellation. Terminals are silent. There is no
automatic retry, rollback, cleanup, native MMIO backend or readiness claim.

`tests/test_radio_control.c` checks literal full-width/order traces, both
target OFF paths, the ON prefix/private loop, registered early stores,
known-null common initialization, source-order metadata dependencies, malformed
spans, ffff semantics, address OR, bus1 wrap/threshold and bus2 flush. It injects
failure, cancellation and coincident error/cancellation at every callback of
the exercised registered ON and target OFF traces, direct READ and both bus
flush traces. It also observes canonical packed-child counter state before a
failed first ST. Six semantic
mutants for selectors, masks, address OR, flush boundaries and false ON success
are rejected. Strict ASan/UBSan host execution, freestanding ARMv7 compilation
and combined compilation of all45 then-current public driver headers pass.

`tests/check_radio_control_arm.py` independently executes the three exact
selected stock radio register leaves in pinned Unicorn2.1.4. Its200 edge/random
cases compare every I/O width/offset/value/barrier and final bookkeeping state
against the original C job, with strict literal supplied read queues. READ and
WRITE have no function relocations; MODIFY binds exactly its actual two
read/write branch relocations to those selected leaves. Every context/stack/
MMIO access is constrained. The check does not emulate the power sequences,
private table, complete attach/scheduler, actual timing or a physical radio.
The bounded native analysis session was exported and closed successfully.

A separate caller-supplied private-input check validates the220-byte object's
size, ELF positions, hash and55-record termination, then compares all54 pairs
through the real C registered-ON job. An independent expected trace matches245
operations, including the literal PHY/delay prefix and final88b modification.
The read sample is synthetic and the final job remains the required logen/PLL
dependency. No private table values are printed or written into this checkout;
this check does not execute the stock power sequence or establish a board ID.
