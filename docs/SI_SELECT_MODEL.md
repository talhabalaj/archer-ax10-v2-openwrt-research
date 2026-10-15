# Bounded SI core selection

`driver/si_select.c` implements original offline `si_setcore(id,unit)` and
`si_setcoreidx(index)` behavior for the primary-wrapper AI path described in
the [exact HND fallback audit](DRIVER_CLOCK_FALLBACK_AUDIT.md#actual-selection-helpers-and-resource-inputs).
The retained input is `hnd.ko`, 366,060 bytes, SHA-256
`d05ff061b3a636630955a13fdd20a953f3dfb07ee92904a987dd4c77b648b45a`,
analyzed with REA 6.5.0/Ghidra 12.1.4. Actual operation names and IDs are in the
[dependency Evidence summary](../findings/DRIVER_RESOURCE_DEPENDENCY_EVIDENCE.json).
No vendor implementation or binary payload is embedded.

## Explicit runtime state

The owner supplies a stable catalog of core IDs and physical core/primary/
secondary-wrapper addresses, plus the actual runtime core count. The selector
copies current virtual map/index/primary-wrapper state and all 32 cache entries
before starting. Catalog metadata remains immutable external storage; updates
affect only `job.state`. Both input context and state may alias their own prior
job snapshots safely. Actual identity, enumeration, mapping ownership and
serialization are acquisition prerequisites, not inferred from model names.

The explicit scope limit is 32 runtime entries. Larger counts fail before I/O.
This is a stricter model boundary than the stock ID search; the stock AI body
itself limits valid indexes to `min(count,32)`. Catalog length is checked at each
reached search/selection access. Missing reached catalog storage is a dependency;
short storage fails bounds checks. Raw catalog values are complete supplied
facts, so a zero physical address does not mean missing input or a board default.

ID selection scans in order and counts matching IDs until the requested unit.
One bounded search transition visits one entry. No match returns zero without
changing state. Index selection does not search. AI interconnects 1, 3 and 4 run
the recovered body; index outside the accepted count returns zero unchanged.
Interconnect 0 exposes the unexpanded SB selector as `DEPENDENCY`. Other
interconnects preserve the actual zero-return skip. A missing ID returns before
interconnect dispatch, including on SB. These outcomes remain distinct results.

## Bus-specific operations and stores

For bus zero, nonzero physical address and zero cache request a logical `MAP`.
The core request has size `0x1000`, except core ID `0x50b` uses `0xf000`.
Both wrappers use `0x1000`; mapping type is zero. Core mapping precedes primary,
which precedes secondary, even though this API selects the primary wrapper.
Nonzero cached maps are reused even when their physical address is zero.

A successful callback returns the actual ARM32 mapping. It enters that cache
immediately, including when the callback requests cancellation. Zero returns
also enter the cache and continue: the actual HND body adds no success check.
`zero_map_results` and resource-bit `zero_map_mask` expose those observations
without turning them into fabricated readiness or rejecting them as OS errors.
A later new selection can request mapping again when that cache remains zero.

The first current-map store follows the core cache stage, before either wrapper
mapping. After both wrapper stages, the primary-wrapper SI field is stored.
Finally, the core map is stored again and the index is stored. These are
separate bounded internal transitions; they do not run after cancellation.
`current_map_stores`, `wrapper_stores` and `index_stores` count actual model
stores, including unchanged/zero assignments. Normal bus-zero counts are 2/1/1.

For bus one, the selector requests PCI write32 at `0x80` with the core physical
address, then at `0x70` for bus-core ID `0x83c`, otherwise `0xac`, with the primary
wrapper physical address. Zero addresses are still written. Each request has
size 4; PCI callback replies are ignored. There are no invented ARM barriers or
delays. PCI ownership is required only when those requests are reached. Current
virtual map is retained, primary-wrapper SI field and all caches stay unchanged,
then current map/index are stored. Counts are 1/0/1. Other bus words select
current map zero and update index while retaining wrapper/caches, without I/O.

`TRACE_COMPLETE`/`SELECTED` means the in-range selector path ended, possibly
returning zero. It does not establish a usable resource. No-match, out-of-range,
unsupported-interconnect skip and unresolved dependency remain separate from
that completion. The surrounding ChipCommon/D11 owner must validate and bind
the selected resource before physical accesses, preserving its actual selection,
interrupt-callback and restoration order.

## Backend, interruption and tests

MAP and PCI writes are typed logical requests to a caller-supplied OS adapter.
This library performs no kernel mapping, PCI access, resource discovery or IRQ
operation. The owner supplies actual backend semantics and exclusive window/map
lifetime; the model does not invent a physical address or successful OS call.
At most one callback occurs per step. A callback error records no successful
cache update, even if the external adapter had partial effects. Error wins over
simultaneous cancellation. Successful effects remain on cancellation, and
terminal calls are silent. No automatic restoration, unmap, rollback or retry
is synthesized. Callbacks may cancel, but never reenter or mutate job fields.

Independent synthetic tests specify literal MAP/PCI tuples and inspect state at
each callback. They cover all 64 cache/physical-presence combinations, eight
zero-map-result patterns, special core size, zero PCI values, bus and interconnect
dispatch, duplicate-ID units, bounds/no-match, owners, alias restart, every
callback error/cancel combination and cancellation between internal stores.
Strict ASan/UBSan checks and freestanding ARMv7 compilation pass. No actual-ARM
differential of this selector, kernel integration or device execution is claimed.
