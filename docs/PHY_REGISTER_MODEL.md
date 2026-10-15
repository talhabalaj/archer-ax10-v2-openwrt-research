# Core-130 / PHY-51 register, TSSI and reset primitives

`driver/phy_register.c` is original C implementing the retained direct PHY
read/modify, analog reset, inline reset pulse, and one/two-core idle-TSSI
snapshot/restore contracts. Identity is explicit: D11 core 130 minor 0/1,
PHY type `0x0b`, revision 51; runtime PHY core count is supplied, never inferred
from BCM63178. See the [resource ledger](DRIVER_RESOURCE_AUDIT.md) and its
[actual Evidence IDs](../findings/DRIVER_RESOURCE_EVIDENCE.json).

The caller must own a valid powered/clocks-enabled D11/PHY resource. This layer
does not acquire a map or lock, suspend/enable MAC, enter/exit PHY-register
nesting, evaluate wake policy or run the outer PHY-clock/core-flags/reset
coordinator. [Reason-2 wake behavior and the RSDB workaround](DRIVER_PHY_WAKE_ADDENDUM.md)
remain required higher dependencies. `TRACE_COMPLETE` establishes no PHY-ready
predicate, calibrated radio or working Linux driver.

## Exact operations and widths

The portable D11 event enum appends `WRITE16`; existing numeric event values
are preserved. `ST/SY` denote recorded store/full barriers. All offsets below
are bytes from the supplied current-D11 map, not physical addresses.

| Command | Trace or behavior |
| --- | --- |
| READ, selector below `0xc000` | `ST,W16(0x3fc,selector),R16(0x3fe),SY`. Software bookkeeping at PHY context `+0x104` clears after the successful address write, before the read. |
| MODIFY, selector other than `0xffff` | `ST,W16(0x3fc,selector),ST,R16(0x3fe),SY,W16(0x3fe,(old&~mask)|(value&mask))`. Bookkeeping clears after successful data write. There is no extra pre-data-write barrier or final readback. |
| MODIFY `0xffff` | Actual helper no-op, including unchanged bookkeeping. Other high selectors such as `0xc000` still use direct ports. |
| ANALOG_RESET | `ST,W16(0x3e6,0)` only. No delay or readiness read is added. |
| RESET_PULSE | `R16(0x7c0),SY` retains original A; `ST,W16(0x7c0,A|0x80)`; `ST,W16(0x3fc,0x1409)`; `R16(0x3fe),SY` obtains B; `ST,W16(0x3fe,B|0x8000)`; `ST,W16(0x7c0,A&~0x80)`; core-130 `delay(100)`. The last control write uses original A, and adds no confirmation read. |

API selectors, mask and value are explicitly u16, and successful read replies
are narrowed to u16. `last_read` exposes that sample. READ's full16 result is
committed after its successful final SY. Reset-pulse and analog direct writes
do not acquire the helper's unrelated software-bookkeeping effects.

READ selectors at least `0xc000` end `DEPENDENCY_TABLE_READ` before I/O. Their
actual table-29 contract has now been [recovered](DRIVER_AC_TABLE_READ_AUDIT.md),
including packed W32 header writes and MAC/PHY guards, but has not yet been
implemented/composed here. It is not a flat direct-port read or zero result.

## TSSI slot mapping and partial state

The AC callback's flag-one path snapshots low10 bits; flag zero restores them
with mask `0x3ff`. For one PHY core it accesses selector `0x645` and saved
slot zero. For two cores it accesses `0x845` into saved slot **one**, then
`0x645` into slot **zero**. Restore uses the same order and fresh read samples
to preserve the other six bits of each register. Saved upper bits are masked.

SNAPSHOT invalidates active slot-valid bits at its first ENTRY step. Each slot
commits only after the complete direct-read trace, including successful SY.
If cancellation happens in that successful SY callback, the committed slot
remains visible; callback error or cancellation before SY leaves it invalid.
Unvisited saved values can remain in memory with validity clear. Inactive slots
are unchanged. Partial snapshots never imply a completed valid save buffer.

RESTORE preflight requires all active slots valid before any callback. That is
an explicit stricter policy than the stock raw buffer. Restart from
`&job.context` snapshots the input before clearing the job. The current two-slot
API exposes table dependency for count three/four before I/O or context mutation.
The [resolved stock behavior](DRIVER_AC_TABLE_READ_AUDIT.md) uses repeated
`0xffff` table snapshot reads for those extra slots, but no-op modifies during
restore; future implementation must preserve this asymmetry.

Each step issues at most one callback or a bounded internal transition. Errors
and cancellation are terminal, with no invented retry, cleanup or rollback.
Successful callbacks count and update the specified software shadows even when
they cancel the job. Simultaneous callback error wins; the failed event is not
counted as completed and makes no successful shadow update. Hardware effects
on a failing transport would remain uncertain. Callbacks may cancel, but must
not reenter begin/step or modify job fields.

## Validation against independent fixtures and stock instructions

`tests/test_phy_register.c` uses independently derived literal read/modify,
analog, pulse and one/two-core TSSI fixtures. It checks masks, width truncation,
non-echo readbacks, original-A reuse, slot order, per-callback bookkeeping and
partial validity/result state. Every selected callback covers failure,
cancellation and coincident error/cancellation, including final barriers. High
selector asymmetry, missing saved slots, invalid identity/arguments, alias-safe
snapshot-to-restore and terminal silence are covered under ASan/UBSan.

`tests/check_phy_arm.py` adds a different oracle: **the exact selected stock
ARM function instructions execute under Unicorn 2.1.4**, using caller-supplied
private `wl.ko` and synthetic RAM/MMIO contexts. It compares C versus ARM traces
for `phy_utils_read_phyreg`, `phy_utils_mod_phyreg` and `phy_ac_ana_reset`.
264 deterministic boundary/random cases match event width/order/value, DSB
barriers, final bookkeeping and READ return. All input read replies are explicit;
missing/extra replies and reads of unsupplied context or stack bytes fail.

The harness verifies the pinned whole-module SHA-256, resolves selected ELF
function bounds/relocation sites, and rejects executed unresolved relocations,
code outside the selected function, unexpected accesses and instruction-bound
exhaustion. It does not load the module into a kernel or export its bytes. Its
C bridge and temporary shared library are built outside the checkout. Independent
private mutation checks reject six altered C models (missing/extra barriers,
wrong masking/bookkeeping and wrong access widths); eight tiny original ARM
probes reject missing/surplus replies, unsupplied context/stack, wrong width,
out-of-function code, executed relocation and an instruction-bound loop.

This implements part of the execution-path discipline suggested by the
[Quarkslab research](EXISTING_REVERSE_ENGINEERING.md), using our actual ARM
artifact and contracts. Unicorn provides the CPU/memory-hook mechanism; its
emulation supplies no physical device timing or radio state.
[Official API tutorial](https://www.unicorn-engine.org/docs/tutorial.html).

Run the public synthetic checks normally:

```sh
sh scripts/test_driver.sh
```

For the optional exact-stock instruction comparison, use a separate local
Python environment and caller-supplied private input. The dependency is pinned
in [requirements-emulation.txt](../tests/requirements-emulation.txt):

```sh
python3 -m venv /private/path/to/axd-emulation-env
/private/path/to/axd-emulation-env/bin/python -m pip install -r tests/requirements-emulation.txt
/private/path/to/axd-emulation-env/bin/python tests/check_phy_arm.py /private/path/to/wl.ko
```

This comparison covers three primitives under synthetic contexts. It does not
execute TSSI, the inline reset pulse or outer lifecycle in ARM emulation, and
does not validate bus timing, mapping ownership, kernel linkage, physical
reset, calibration or RF behavior. Those require separate evidence and tests.
