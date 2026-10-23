# Ordered PHY51 initialization registry and dispatcher

`driver/phy_scheduler.c` implements the recovered initialization registry and
ordered dispatcher for core130 minor0/1, AC PHY51, chip63178. It composes actual
closed operations and stops at reached missing metadata or unimplemented
children. It does not complete PHY initialization or establish radio readiness.
No router, MMIO backend, module loading or RF operation runs here.

The current pinned module and selected static observations are indexed in
[PHY_SCHEDULER_EVIDENCE.json](../findings/PHY_SCHEDULER_EVIDENCE.json). Existing
attach/table/analog evidence was reused. The new bounded native session was
exported and closed. Raw instructions, payloads, board values and local paths
remain private.

## Complete recovered init vector

All14 distinct direct callers of `phy_init_add_init_fn` were identified; two
register two functions each. Incoming typed references corroborate the16 call
sites. Successful common-module attachment registers these16 init functions:

| Priority | Common registered function | Actual AC body / current implementation |
| --- | --- | --- |
| 1 | `phy_cache_init` | Conditional zero allocations and source-ordered cache stores. |
| 3 | `phy_ana_init` | Registered `phy_ac_ana_switch(on1)`: actual `ST,W16(3e6,0)`. |
| 4 | `phy_chanmgr_set_bw` | Different bandwidth reaches missing BMAC reset/bwinit; equal bandwidth runs real SI flags read/check. |
| 5 | `_phy_init` | Actual type dispatcher slot5 is null in this AC registration; returns0. |
| 6 | `phy_noise_reset` | Registered `phy_ac_noise_reset`: conditional shared interference-mode store. |
| 7 | `phy_radio_on` | Registered AC radio-on child, including its real explicit50e0 logen/PLL/RCAL/RCCAL switch, then the actual [register-table child](RADIO_REGTBL_MODEL.md). |
| 8 | `phy_tbl_init` | Real common/AC wrapper and complete implemented target51 table-init child. |
| 9 | `phy_tpc_init` | Registered `phy_ac_tpc_init`: source-ordered per-core target51 byte pairs. |
| 10 | `_phy_radar_init` | Non-5GHz guard returns0; 5GHz composes the real target51 radar/MHF child with explicit runtime enable/threshold facts. |
| 11 | `phy_antdiv_init` | Real clock/MAC guard and PSM children around actual AC setter, whose body is `bx lr`. |
| 12 | `phy_noise_init` | Real guards, mode selection/clamping and shared store, the actual [AC mode child](PHY_NOISE_MODEL.md), then a separate PI378 store after it returns. |
| 13 | `phy_chanmgr_init_chanspec` | Registered AC channel initialization: the actual [modules0-2](PHY_CHANSPEC_MODEL.md) execute; module2's radio50e0 `wlc_phy_chanspec_radio20704_setup` is the first unresolved required child. |
| 16 | `phy_papdcal_init_cb` | Actual `mov0;return` leaf. This does not implement PAPD calibration. |
| 19 | `phy_rxgcrs_init` | Registered AC conditional clear and channel-state chanspec store. |
| 20 | `phy_btcx_init` | Registered AC byte18=FF store. |
| 21 | `phy_hirssi_init` | Target51 half18/half1a=FFFF, then byte16/byte17=0. |

These are initialization callbacks, not the watchdog, down or complete
calibration registry. Attachment invokes other functions which are not init
callbacks. In particular, a named calibration attachment is not proof that its
calibration body runs at that position.

The actual registration helper sorts init priority bytes ascending and inserts
a new equal priority **before** existing equal entries. It shifts the entire
occupied suffix, including down entries. Its common attach allocatesE4 bytes
and sets shared init/down capacity23. Full capacity returns-22 without mutation.
The portable registry has the same capacity and retains separately supplied
init/total counts. It rejects malformed counts/order and priorities above255,
rather than reproducing stock's compare-u32/store-u8 discrepancy.

`axd_phy_registry_target51` reconstructs all16 init entries in actual successful
common attachment order, then applies that insertion algorithm. The caller
binds actual common-context labels, indexed by the final sorted priorities.
It reconstructs the init vector, not allocation or execution of the down vector.
Unknown labels stay unknown; they are required only when the invoked body uses
its context. The actual PAPD no-op does not dereference its unused context.
The ordinary registry API can represent smaller explicit scheduler inputs for
isolated tests; such a vector does not establish complete target initialization.

## Dispatcher state and return rules

Actual `phy_init_invoke_init_fns` calls each function/context pair in array order,
returns the first nonzero callback result unchanged, and reloads the init count
after each zero return. Only after every callback returns0 does it call
`phy_set_sliceupstate(...,1)`, then return0. The slice helper ORs one shifted bit
into its existing software word. The portable dispatcher separates callback
return, cursor advance, slice-up store and final return into internal steps.
It retains the observed ARM register-shift behavior: the low8 unit bits select
the shift; shifts32..255 contribute a zero mask.

The job snapshots an immutable registry/context. This deliberately narrows
stock's live count, pointer and field reloads; callbacks may cancel but may not
mutate/reenter a job. All presently closed target callback bodies return0.
The generic nonzero-return branch is retained, while unresolved functions stop
as dependencies rather than accepting an arbitrary supplied success/result.
Host I/O failure/cancellation is separate from an original integer callback
return. A failed or incomplete child never marks its parent callback returned.

The actual AC type registration zeros its nine-word function block and leaves
slot5 null. The actual type dispatcher returns0 for that slot. The PAPD init
leaf and AC antdiv setter likewise have recovered empty bodies; these are
source-backed cases, not success placeholders for radio or calibration work.

## Real prefix and software effects

Cache initialization first conditionally allocates the requested u16 reuse
buffer when its signed state byte isFF and size is nonzero. The real OS service
is zero allocation. After it returns, state-byte0 and buffer-pointer stores are
separate. If the shared table pointer is null, the callback stores
`u16(entry_count)*4` as table bytes, allocates that size, then separately stores
used-count0 and the returned pointer. Existing allocated tables need no unused
size/allocator input. A zero allocation reply reaches the unresolved allocation-
failure diagnostic/fatal chain; no fatal success or later pointer store is
invented. Successful allocation labels remain caller-owned after cancellation;
the component issues no free or guessed cleanup.

Analog-on composes the already verified PHY primitive with exactly the actual
two-event protocol. The bandwidth callback compares the actual PHY bandwidth
state with PHY chanspec bits3800. A difference reaches `wlc_bmac_bw_set`, whose
reset/bandwidth-init chain is not implemented here. Equal state still executes
the actual `wlc_bmac_bw_check`: real current SI coreflags read/SY and comparison
against the separate BMAC hardware chanspec encoding. Its possible-20 result
is retained diagnostically and ignored by the common callback, as in stock.
The PHY and hardware chanspecs are distinct inputs; they are not assumed equal.

Noise-reset skips only when actual PI flags3b0 contain802. Otherwise it selects
the actual 2GHz/other-band shared default and stores it to current interference
mode. Later noise-init has its own same flags guard, forced/default selection,
and forced5GHz greater-than1 clamp. Its shared mode store precedes the reached
unimplemented `phy_ac_noise_set_mode`; the later cached PHY mode store is not
performed without that child returning.

Target51 TPC initializes min/max bytes to32 hexadecimal for each actual PHY
core, with separate stores and cursor advances. A reached count above4 is
rejected before indexing the bounded host arrays; zero follows the actual
empty loop. RXGCRS clears its AC byte before requiring the channel-state pointer
and storing chanspec, preserving partial state when that pointer is missing.
HIRSSI's four stores and BTCX's single byte store retain their actual order.

Antdiv with clock byte0 records the helper's-11 and returns0 from the enclosing
init callback. With clocks present it samples maincontrol/SY. Bit0 invokes the
real PSM suspend/enable children around the actual empty AC setter. These
children retain their own reads, nesting, policy, fatal and dependency rules;
there is no assumed successful MAC suspension.

## Canonical children, tests and remaining gates

`context.table.hardware` and `context.table.control` are canonical PHY/PSM and
SI state. Radio hardware is bound from that canonical snapshot at dispatch.
Every child publishes partial state before user callbacks and after each step,
including callback-triggered cancellation. Table control freshness propagates
with the existing child, including IRQ invalidation. Unknown reached facts are
dependencies; known required nulls fail. Source/registry/context aliases are
snapshotted before clearing a job. Each step invokes at most one user callback,
error wins simultaneous cancellation, and terminals are silent.

The independent suite checks literal sorted labels, equal-priority insertion,
occupied down-tail movement, capacity, aliasing, the six-event real full-target
prefix, the existing independent412-event real table oracle, a six-event nested
antdiv/PSM trace and a21-event real radio prefix with pre-I/O bookkeeping.
All1,389 selected callback failure/cancellation/coincident-error boundaries are
checked, plus software-only cancellation between allocation/state/pointer,
per-core TPC, HIRSSI and slice-up/return stores. It tests distinct bandwidth
inputs/ignored failure, missing/unused facts, null allocation, partial noise/
RXGCRS state, canonical child snapshots and terminal silence. Strict C11
ASan/UBSan tests and freestanding ARMv7 compilation pass. Seven private semantic
mutants are rejected. This scheduler has not been executed as stock ARM code;
the existing selected leaf instruction checks remain separate evidence.

The full target vector currently completes eleven callbacks (priorities1-12)
and stops at priority13, provided the actual allocations and matching bandwidth
inputs permit that prefix. Radio-on with its cold register-table child, table,
TPC, the real [radar child](PHY_CHANNEL_INIT_MODEL.md), antdiv and the noise mode
child now execute. An earlier differing-bandwidth branch and the noise mode-bit0
clear desense branch remain explicitly unimplemented. Callback13 executes its real module0
(core-to-core sync, resetcca, phycorestate, BMAC bandwidth) and then stops inside
module2 at `wlc_phy_chanspec_radio20704_setup`. Remaining channel set/tuning and the later callbacks stay required.
The scheduler does not acquire actual runtime registry/module pointers, OTP,
NVRAM, radio ID/subrevision, private payloads or OS resource ownership. Those
bindings, remaining children, physical timing/calibration, DMA/IRQ/data path and
Linux WLAN integration remain gates before a replacement driver can run.
