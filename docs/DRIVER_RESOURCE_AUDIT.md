# Core-130 clock, PLL, PHY reset, wake policy, and DMA reset ledger

This is an original behavioral account of a bounded static REA 6.5.0/Ghidra
12.1.4 examination. Target: `wl.ko`, 5,803,056 bytes, SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
The companion [Evidence summary](../findings/DRIVER_RESOURCE_EVIDENCE.json)
contains actual Evidence IDs. Raw disassembly,
pseudocode, descriptor bytes, and the exported bundle remain private. The
session was exported and closed; the input digest was checked again.

Follow-up: the [HND fallback/resource audit](DRIVER_CLOCK_FALLBACK_AUDIT.md)
and [PHY wake/workaround addendum](DRIVER_PHY_WAKE_ADDENDUM.md) now resolve
the bounded callees marked unexpanded below. Their implementation and runtime
resource acquisition remain open. The [D11 clock/PLL model](CLOCK_CONTROL_MODEL.md)
implements the direct trace subset; the original session scope below is retained.

This establishes stock operation contracts, not tested hardware behavior or a
complete driver. No router, target execution, or physical MMIO was used.
Runtime capability, SI mapping, band/PHY context, and queue pointers remain
inputs rather than values inferred from the firmware's chip name.

## Notation and resources

`ST` means ARM `DSB ST`; `SY` means `DSB SY`. `R16/R32` and `W16/W32`
describe access widths, relative to the supplied register mapping. Ordinary
software loads/stores are distinguished from device accesses. Delays are
arguments in microseconds to `osl_delay`, not measured elapsed deadlines.
The core-130 descriptor resolves these fields:

| Resource | Offset from D11 mapping | Width |
|---|---:|---:|
| Clock control/status | `0x1e0` | 32 |
| PHY address | `0x3fc` | 16 |
| PHY data | `0x3fe` | 16 |
| Analog reset | `0x3e6` | 16 |
| PHY reset control | `0x7c0` | 16 |

The existing attach evidence supplies D11 base at hardware context `+0x198`,
descriptor root at `+0x19c`, SI handle at `+0x18c`, revision at `+0x14c`, and
current band at `+0x1a8`. Descriptor fields here are nested offsets `+0x70`,
`+0xa8/+0xaa`, `+0x94`, and `+0xf6`, respectively. These are offsets, not
physical addresses. Retained HND evidence supplies the actual SI core-flags
primitive: `R32(ioctrl),SY`, compute `(old & ~mask) | value`, then
`ST,W32(ioctrl,new),R32(ioctrl),SY`.

## Clock request: `wlc_clkctl_clk(hw, mode)`

The first discriminator is SI capability word `+0x18`, bit `0x10000000`.
When clear, the function calls imported `si_clkctl_cc(si, mode)`. Its returned
low byte is stored at hardware context `+0x20d`; a nonzero return sets wake
override bit `0x10` at `+0x1f8`, while zero clears it. That HND fallback was
not expanded in this bounded session.

When the capability is set, hardware accesses occur only when hardware byte
`+0x20e` is nonzero. Mode zero performs `ST,R32(clock),SY,W32(old | 2)`,
delays 64, then `R32(clock),SY`. If status bit `0x20000` is clear it performs
up to 1,500 repetitions of `delay(10),R32(clock),SY`, stopping when that bit
is set. There is no additional confirmation read after this loop.

For nonzero mode, a preliminary wait occurs only when SI word `+0x20` is zero,
one `R32,SY` has any bit of `0x12` set, and a separate `R32,SY` has bit
`0x20000` clear. It uses the same 1,500-repeat wait. The function then performs
`ST,R32(clock),SY,W32(old & ~2)`.

For this capability branch, `+0x20d` becomes `mode == 0`, even if `+0x20e`
prevented all MMIO. The function is void and has no timeout failure result.
The software byte therefore cannot serve as clock-ready evidence. In these
RMW operations, `ST` precedes the read; there is no extra `ST` inserted between
the following `SY` and write.

## PHY PLL: `wlc_bmac_core_phypll_ctl(hw, enable)`

No work occurs if hardware byte `+0x20e` is zero, public byte `+0x1d` is
nonzero, or imported `si_iscoreup(si)` returns false. The core-130 branch uses
the 32-bit clock register above; the distinct core-30 branch is outside this
ledger. Callers use boolean enable values.

Disable performs `ST,R32,SY,W32(old & ~0x300)`, followed by another `R32,SY`.
Enable first checks the actual RSDB predicate; if true it performs
`ST,R32,SY,W32(old & ~0x20)`. It then obtains a distinct sample A with
`R32,SY`. If `(A & 0x300) == 0x300`, it skips the request write, ready polling,
and final read entirely. This is a consequential stock branch.

Otherwise, enable obtains sample B using `ST,R32,SY` and writes `B | 0x300`.
If sample A already had both status bits `0x03000000` set, it delays four
times by 16 before continuing. It reads status with `R32,SY`; while the two
status bits are not both set it performs at most 10,000 repetitions of
`delay(10),R32,SY`. This request-write path then always makes a separate
final `R32,SY`; that sample is not checked for failure. The function is void.

After either enabled/disabled body, the secondary-core helper receives
`enable ^ 1`. It returns immediately if RSDB is false. Otherwise it saves
the current core unit, disables host interrupts, switches the D11 mapping to
unit 1, and performs `ST,R32(clock),SY,W32(new)` on that mapping, clearing
bit `0x20` for helper argument zero and setting it for nonzero. It restores
the former core unit and interrupt state. Core enumeration, mapping, and
interrupt operations remain required SI/OS contracts.

RSDB is an actual runtime predicate, not established false merely because the
chip is 63178. It requires more than one D11 core and an exclusion flag clear.
One runtime path recalculates D11 count through `si_numcoreunits(si,0x812)`
and applies chip exclusions; the other uses previously established context.
The latter count and exclusion initialization are not proven here.

## PHY reset: type `0x0b`, revision 51, chip 63178

Bandwidth selector `chanspec & 0x3800` maps `0x1000 -> 0x40`,
`0x1800 -> 0x80`, `0x2000/0x3000 -> 0xc0`, `0x2800 -> 0x800`,
and other values to zero. The current band supplies PHY type/revision;
public PHY revision 51 also gates the idle-TSSI wrapper described below.
No PHY identity is inferred from chip ID alone.

For the stated type/revision, reset requests SI core flags with mask `0x8ce`
and value `bandwidth_flags | 0xe`, delays 2, then enables the PHY clock.
PHY-clock enable stores byte `hw+0x210`, calls core flags with mask `0x300`
and value `0x100`, then for this chip calls core flags with mask `0xe` and
value zero, delays 1, calls core flags with mask/value `0x4`, and delays 1.
Between those last operations a conditional RSDB workaround can run when
RSDB is true and hardware byte `+0x264` is zero, then set that byte to one.
That workaround is an explicit unresolved optional branch, not a no-op.
Other chip-specific PMU branches were excluded by the stated chip identity.

PHY-clock disable, if used, stores the byte and calls core flags with
mask `0x200a`, value `0xa`, delays 1, then mask `0xa`, value `0x8`, delay 1.

The revision-51 reset sequence then uses these exact device operations:

1. `R16(0x7c0),SY`, retaining sample A.
2. `ST,W16(0x7c0,A | 0x80)`.
3. `ST,W16(0x3fc,0x1409)`.
4. `R16(0x3fe),SY`, retaining sample B.
5. `ST,W16(0x3fe,B | 0x8000)`.
6. `ST,W16(0x7c0,A & ~0x80)` using the original A, with no final readback.

Core 130 then delays 100. For chip 63178 the bandwidth-state helper stores
`chanspec & 0x3800` at PHY context `+0x396`; that helper does no MMIO.
If a PHY context exists, analog-reset dispatch is attempted. The AC attach
registration binds the actual reset callback to `phy_ac_ana_reset`, whose
entire device trace is `ST,W16(0x3e6,0)`: no readiness read or delay follows.
An optional beamforming cache-clean branch remains dependent on actual
runtime flags and callbacks, and was not expanded.

The public-revision-51/type-AC path calls idle-TSSI with an 8-byte local save
buffer and flag 1 before reset, and with flag 0 after analog reset. Callback
registration resolves to `wlc_phy_ac_idletssi_force`. Flag 1 snapshots low
10 bits, and flag 0 restores those bits with mask `0x3ff`; the name does not
mean flag 1 forces a constant. For PHY revision 51 and one core the register
is `0x645`; for two cores the order is `0x845` then `0x645`. The active PHY
core count remains an input. Three/four-core paths include `0xffff` selectors
for this revision: reads route to the AC table helper, whereas modify on
`0xffff` returns immediately. Their table-read contract is not expanded.

The idle-TSSI wrapper returns if its optional callback is absent. Otherwise it
calls MAC suspend, PHY-register enter, the callback, PHY-register exit, and MAC
enable. Enter/exit maintain byte nesting at PHY `+0x10e` and call PHY-register
wake-override wrappers on the outer transitions. Their wake-override internals
remain a dependency. Retained MAC run-bit guards apply: initial corereset with
control inactive need not have running microcode merely because these helpers
are invoked. This is not an unconditional cycle requiring prior MAC enable.

For PHY selectors below `0xc000`, register read is
`ST,W16(0x3fc,selector),R16(0x3fe),SY`, with a software bookkeeping zero at
PHY `+0x104` between address write and data read. Modify first returns for
selector `0xffff`; otherwise it does
`ST,W16(addr,selector),ST,R16(data),SY,W16(data,(old & ~mask)|(value & mask))`
then clears that software bookkeeping field. No final barrier or readback is
present. This helper's barriers differ from the inline reset sequence above.

## Main-enable wake-control dependency

`wlc_set_wake_ctrl` returns if public byte `+0x1c` is zero; otherwise it enters
`_wlc_set_wake_ctrl`. That function obtains the original main-control value
using `R32(0x120),SY`, then evaluates `wlc_stay_awake(wlc)`. True requests
`wlc_mctrl(hw,0x04000000,0x04000000)`; if the original hardware sample did not
have that wake bit, it calls retained `wlc_bmac_wait_for_wake`. False requests
`wlc_mctrl(hw,0x04000000,0)`. Existing cache and wake/mute override semantics
of mctrl still apply. A true policy result alone does not imply a physical
write when cached state is unchanged.

The stay-awake decision includes numerous context flags, optional scan/RRM and
association state, wake overrides, and three passes across the BSS context
array. Clearing override reason `0x4` alone does not determine that result.
The decision function itself was recovered, but RRM/association helpers and
initialization of these policy inputs were not expanded. A simulator must
represent those inputs or expose the decision as an explicit external policy
contract; substituting success is not stock behavior.

## Actual DMA reset callees

These functions are defined in this exact WL ELF, rather than imported HND
operations. `di` denotes their runtime DMA context, and `T/R` are supplied
register-block pointers at `di+0x48/+0x4c`. Control is block `+0`; status is
block `+0x10`, state mask `0xf0000000`. These offsets do not establish the
physical queue mapping. The two queries were added solely to close the reset
callee ledger; descriptor allocation and queue data paths were not followed.

RX returns true without accesses if its 16-bit count at `di+0x80` is zero.
Otherwise: `ST,W32(R.control,0),R32(R.status),SY`. If the state mask is
nonzero, it performs at most 1,000 `delay(10),R32(status),SY` repetitions,
returning true on masked zero and false on expiry. There is no extra final
confirmation read or rollback.

TX returns true untouched if its 16-bit count at `di+0x5a` is zero. If the
indirect flag byte `di+0x35` is zero, it obtains `R32(T.status),SY`; masked
zero returns true immediately, without updating cached word `di+0x12c`.
Otherwise it performs `ST,W32(T.control,2),R32(T.status),SY`, waiting while
state is nonzero and `(status & 0xe0000000) != 0x20000000`. The wait is at
most 1,000 `delay(10),R32(status),SY` repetitions. Exhausting this suspend
wait does not abort the subsequent disable operation.

For a nonzero indirect flag, TX instead performs
`ST,W32(*(*(di+0x10c)+0xc),*(di+0x38))` and copies that selected index's low
byte into `*(di+0x10c)+4`. No status poll or selector cache shortcut precedes
this write. Both paths then perform `ST,W32(T.control,0)`.

The post-disable status poll runs for direct mode or when flag word `di+4`
has bit `0x100` set. It is an initial `R32(status),SY` followed, if needed,
by at most 1,000 `delay(10),R32(status),SY` repetitions, requiring masked
zero. On exhaustion TX delays 300, stores `0xffffffff` at `di+0x12c`, and
returns false. Other paths reaching disable store that same cache value on
return. Indirect mode without flag `0x100` skips the post-disable poll and
returns true. There is no write/read rollback on failure. No RX-like 300-us
delay exists, and TX's 300-us delay occurs only on post-disable failure.

## Remaining implementation and evidence boundaries

The resource ledger is suitable for explicit offline state/trace contracts.
It does not supply actual runtime values for capability `0x10000000`, clock
valid flags, RSDB count/exclusion state, SI mappings, PHY core count, BSS wake
policy, DMA context allocation/register windows, or optional callback state.
The imported clock fallback, optional RSDB workaround, PHY table-read and
wake-override transitions, and beamforming branch are identified dependencies.
Optional VASIP/SMC paths remain open. Main and extended PSM readiness continue
to use the separate retained startup contract; clock/PLL software state is not
their acknowledgment. PHY/radio/channel/calibration initialization, DMA data
paths, interrupts, RX/TX, Linux integration, and hardware validation are still
required for the full driver.
