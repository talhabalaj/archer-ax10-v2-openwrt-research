# Exact core-130 PHY reset boundary

This corrects and narrows the [reset/BF audit](DRIVER_PHY_RESET_BF_AUDIT.md).
It describes static control flow in the same exact `wl.ko`, SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
Host object offsets are not register addresses. No target was accessed.

## Independent gates and ordering

The target model requires explicit AC band type (`0x0b`) and **band revision
51**, with the already established BCM63178/core-130 clock identity. Other
band revisions are outside this model's supported boundary and must be
rejected before I/O. This is a deliberate narrowing: the stock function also
contains a special band-revision-47/minor-2 preparation path, non-AC handling,
and pulse branches for band revisions 47 and 129.

The 16-bit **public PHY revision** remains an independent input. Its value
51 enables the outer TSSI save before reset preparation and restore after
bandwidth/analog work; another explicit value skips both. It does not select
the ordinary AC preparation or the inline pulse. Band minor revision does
not affect the accepted band-51 path. Model facts must not silently equate
public and band revisions.

The accepted path is: initialize eight save-buffer bytes, optional outer
TSSI save, SI flags preparation, delay 2, PHY clock on, inline pulse, common
delay 100, bandwidth store, analog dispatch, optional outer TSSI restore,
optional BF cleaner, return. The 100 delay is guarded only by hardware core
revision greater than 64 at the common post-pulse join. It is therefore taken
for core 130 even when considering stock branches that skip the pulse.

`chanspec & 0x3800` selects preparation flags as follows; all masked patterns
have a defined stock branch:

| Selector | Flags |
| --- | --- |
| `0x1000` | `0x40` |
| `0x1800` | `0x80` |
| `0x2000`, `0x3000` | `0xc0` |
| `0x2800` | `0x800` |
| `0`, `0x0800`, `0x3800` | `0` |

Ordinary AC preparation uses mask `0x8ce` and value `flags | 0xe`. The later
bandwidth store uses the raw masked selector, not these flags.

## Bandwidth and analog ownership

For BCM63178 the reset caller reaches `phy_utils_set_bwstate` before its
later PHY-context null guard. The helper stores the argument's low 16 bits
at **PI+0x396** and returns. Thus PI is required on this path even if analog
reset might otherwise be described as optional. The stock exclusion tests
SI chip value `0xa8d1`; it does not exclude BCM63178. Assembly Evidence ID:
`ev_75077cfc6247e6509fca8a700c5ce699319ca8b96a46efd8a7b10921f0779acd`.

The later analog call explicitly tests PI for null. Once the generic helper
is entered, its chain is `module=*(PI+0x34)`, `table=*(module+4)`,
`callback=*(table+4)`. Module and table have no null guard. A null callback
returns without loading the context. Otherwise it loads `*(table+8)` as the
callback argument and tail-calls that callback. Module, implementation table,
callback and callback context are distinct resources. Callback absence does
not justify absent module/table storage. Assembly Evidence ID:
`ev_e834c3d006f9403ae118ad3ecb0ff922e9b62789c13d3056996352583efb7b16`.

The retained AC registration binds the actual AC reset callback; that callback
loads its PI from the first word of its supplied context, executes a store
barrier and a 16-bit zero write to the resolved core-130 PHY-control port
(`D11+0x3e6`), then returns. It has no status/readback loop. A model requires
either explicit callback absence or this proven binding plus a valid context;
AC band type alone does not validate an arbitrary runtime function pointer.

## Initialized bytes and actual snapshots

The stock reset zeroes two 32-bit local words: four defined 16-bit save slots.
It has no validity mask. An initialization/availability mask of `0xf` can
represent those defined bytes; it cannot mean four observed TSSI readings.
Observed-value tracking begins empty and changes only when the actual save
callback completes the relevant reads. The idle-TSSI wrapper checks its
callback before any MAC, PHY-register or buffer operations. Stable absence
makes both save and restore no-ops and leaves the zeroed bytes unchanged.

Required inputs when reached include valid W/HW/band/public/PI object
bindings, chanspec, SI and clock resources, actual descriptor/register mapping,
idle-TSSI module/table/callback binding and count/save-buffer ownership,
PHY-register/wake/PSM contexts, analog module/table/binding/context, and the
BF gate fields and traversal resources described in the companion audit.
Presence and revision facts are explicit; missing facts are dependencies.
Stable immutable object bindings are a model precondition, not evidence that
the stock code snapshots every pointer or forbids callback mutation.

## Exit and excluded work

The complete retained `wlc_bmac_phy_reset` body returns void and introduces no
readiness test, result-code check, rollback, VASIP call or SMC call. Existing
child fatal/dependency semantics still matter; a host model's fail-stop policy
must remain labeled as policy. VASIP/SMC in higher initialization and later
PHY/radio/calibration/data paths remain outside this closure.

Primary reset assembly Evidence ID:
`ev_88ab7f82543911a0b6e796994a952e2664124975ae6a818227a16cdc0611ab2b`.
The two wrapper bodies were separately captured with REA 6.5.0/Ghidra 12.1.4;
the bounded session was exported and closed, and the source digest rechecked.
Raw disassembly and proprietary bytes remain private. See the
[selected Evidence summary](../findings/DRIVER_PHY_RESET_EXACT_EVIDENCE.json)
for the new and reused records, including the actual AC callback assembly.
