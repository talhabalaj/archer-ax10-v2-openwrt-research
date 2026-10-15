# Physical object-memory writes and initialization lowering

`driver/object_memory.c` implements the actual current object-port copy, auto-
increment copy/fill and halfword-write helpers. `driver/init_io.c` composes it
with the existing effective initialization consumer, so an object-copy record
now runs its physical-port protocol. These are original portable components,
with caller-bound resources and no MMIO backend. No device operation occurred.

The exact current `wl.ko` identity and selected actual assembly IDs are in
[OBJECT_MEMORY_EVIDENCE.json](../findings/OBJECT_MEMORY_EVIDENCE.json). The
bounded session was exported and closed. Source records remain private. The
[effective consumer](EFFECTIVE_INIT_AUDIT.md) supplies initialization semantics;
the [earlier source notes](D2_CONTEXT_SOURCE_NOTES.md) remain corroboration rather
than a replacement for current writer assembly.

## Actual protocols

Addresses are full 32-bit byte addresses. Each programmed object address is
`selector | (address >> 2)`. Object address/data ports are D11-relative `160/164`.
An address operation is always `ST,W32(160),R32(160),SY`; the actual discarded
read is essential even where decompiled pseudocode omits the volatile result.
Replies are caller-supplied observations, never invented write echoes.

- WRITE16 adds `ST,W16(164 + (address & 2), low16(value))`. Address bit1 selects
  the low/high halfword lane. The ordinary and extended SHM wrappers use
  selectors `10000` and `90000` respectively; these are distinct spaces.
- COPY returns without I/O for a signed nonpositive byte count. When
  `(selector & 50000) == 10000` or `(selector & 70000) == 20000`, it programs
  an address and writes one halfword per iteration. Otherwise it programs an
  address and writes one little-endian word per four bytes, then a halfword
  tail when present. Every element repeats its real selector readback protocol.
- COPY32 and SET32 program one address with `selector | 03000000`, including
  when the signed byte count is zero or negative. Positive counts then perform
  `ST,W32(164)` per word, without reprogramming or rereading the address. COPY32
  assembles little-endian bytes; SET32 repeats the supplied full word.

No final readback, barrier, ready predicate or cleanup is added. Successful data
callbacks advance `completed_bytes`, including callbacks that request cancel.
`address_writes` and `last_read` retain successful selector writes and samples.
These counters report callback completion, not physical memory commitment.

## Bounded policy and composition

Positive COPY lengths must be even; positive COPY32/SET32 lengths must be
multiples of four. The implementation checks source bounds, actual lane
alignment and nonwrapping address ranges before any I/O. Stock can read beyond
an odd/partial source and wrap unchecked addresses; those cases are rejected.
Selector values do not establish object geometry or valid device ranges. The
owner supplies immutable source bytes, mapped and serialized resources, clocks
and actual capacity validation. Missing reached ownership ends as a dependency.
A nonpositive COPY needs no unused owner; auto-increment commands still do.

`axd_init_io_begin` retains the existing whole-table preflight, runtime addend,
width truncation, skipped-width and sentinel behavior. Direct MMIO barriers and
writes remain direct events. Object records use the real COPY child with the
consumer's computed byte address, selector30000 and low16 value. The entire
object helper must return before that physical record is counted complete or
the following table operation runs. A successful final data write followed by
cancellation retains partial bytes but does not claim whole-helper return.
The nested effective consumer's counters describe semantic dispatch only;
outer counters count actual user callbacks and physically completed records.

All begins snapshot inputs before clearing their own job, including aliased
requests/contexts and the initialization addend. The previous effective-init
begin also now snapshots an aliased `&job.addend`; a regression checks that it
does not silently become zero. Table/source storage stays immutable outside
its active job. Each step invokes at most one synchronous user callback.
Callback error wins simultaneous cancellation. Terminals are silent, with no
rollback, retry or invented unwind.

## Validation and limits

Independent literal tests cover all selector-route bit combinations, both
halfword lanes, distinct readbacks, full byte addresses, mixed words/tails,
auto-increment headers for nonpositive lengths, exact buffer/range boundaries,
ownership, aliased restart, every callback fault/cancel/both and terminal silence.
Initialization integration tests independently specify mixed direct/object
traces, whole-table rejection, record commitment and successful-cancel effects.
The optional instruction comparisons and final sanitizer/ARMv7 build results
are recorded in [PROGRESS.md](../PROGRESS.md).

This closes physical writer lowering, not full D2 or the driver. Runtime addend
acquisition, complete SHM/register ABI, actual mappings/ownership, surrounding
initialization, PHY/radio/calibration, DMA/IRQ/RX/TX and Linux/device validation
remain separate requirements.
