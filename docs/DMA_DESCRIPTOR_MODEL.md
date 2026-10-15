# Current DMA64 descriptor and ring primitives

This component implements an offline value formatter and strict software ring
arithmetic recovered from the pinned current `wl` 17.10.121.37. It does not map
memory, publish descriptors, manage packets or implement a DMA data path. No
router, MMIO, interrupt or RF operation was performed.

The input module is 5,803,056 bytes, SHA-256
`8f35f1afa1f228a3a74cab2b8cc6315a17489e76547396d8e2faf66045bb332c`.
REA 6.5.0 / Ghidra 12.1.4 assembly observations and selected facts are indexed in
[the evidence metadata](../findings/DMA_DESCRIPTOR_EVIDENCE.json). Raw assembly,
module bytes, analysis records and the exported/closed REA session remain private.

## Current observations

`dma64_dd_upd` calculates descriptor addresses with `index << 4`. Four 32-bit
words are control1 at byte0, control2 at4, address low at8 and address high at12.
The current little-endian ARM stores establish the codec's byte order. The
updater writes low, high, control1, control2; parity can add another control2
write. The public codec is a pure byte conversion, not an emulation of that
publication protocol or a backend with memory-order guarantees.

The exact current value transformation is:

- Keep caller control1 unchanged and truncate byte count to its low15bits.
- When the configured low translation offset is nonzero and the input mapped
  address has either top bit set, move those top2bits into control2 bits16–17
  and clear them from the address before adding the low translation offset.
- Otherwise add the low translation offset to the input address unchanged.
  The addition wraps modulo2^32. The high word is the separately supplied high
  translation offset; this current ARM32 function does not add an input address
  high word or propagate low-word carry into it.
- If current control flag bit0 enables parity, XOR all four words and set
  control2 bit18 when necessary to produce even parity.

These are formatting facts, not evidence that any address/window or buffer
length is legal. In particular, the low15bit count field does not authorize a
32KiB transfer. External mapping, address-width/extension configuration, segment
size limits, coherency and board resources still require their actual owners.

`dma_txdesc` corroborates the16byte stride, next-free producer position and
one-slot-reserved capacity. It computes `next=(out+1)&(count-1)` and refuses a
post when next equals in. On the last ring slot it adds EOT bit28. Its current
burst predicate adds NOTPCIE bit18. Optional parity follows the same XOR fold.
Commit stores the new software out index, then emits a store barrier and writes
`pointer_base + out*16` to channel-relative offset4. No COHERENT-bit addition is
present in this observed current function; caller-supplied bits are preserved.

Current `dma_getnexttxp` distinguishes software ALL range1, current-descriptor
range2, and transferred range3. The current descriptor is decoded as
`(((status0 & mask)-pointer_base)&mask)>>4`. The transferred range also decodes
status1 using its separately loaded mask; if current differs from active,
its limit becomes previous(active), wrapped by count-1. Current and active masks
are not assumed identical. The function can reuse a cached completion cursor
instead of reading status0; the pure API takes explicit samples and implements
no hidden cache or hardware read.

Current RX reclaim corroborates the same masked/subtracted byte-pointer
conversion and separates software packet tracking from descriptors. RX fill
calls the updater, maintains packet or chain tracking, and eventually performs
`ST,W32(channel+4,pointer_base+out*16)`. Its buffer allocation, address/cache
helpers, multi-descriptor and special-buffer branches are outside this codec.
The observed completion/reclaim functions do not obtain completion by reading a
hardware-updated descriptor ownership bit.

## API and explicit model policy

[The header](../driver/dma_descriptor.h) exposes the current formatter, raw16byte
encode/decode, explicit ring validation/counts, producer/status conversion,
bounded post/reclaim accounting and range-limit arithmetic. Encode/decode
preserve all unknown/reserved bits. Formatter and codec inputs are snapshotted
before output writes, allowing overlap. Rejected calls preserve outputs/cursors.
There are no callbacks, hardware registers or always-success dependencies.

Ring metadata and cursors must be acquired by the caller and serialized.
Counts are power-of-two2..32768, a bounded policy compatible with the observed
u16 indices. This does not assert actual hardware support for those sizes.
The mask must be contiguous low bits, include the descriptor's low4bits, and
contain the entire ring without a boundary crossing. No nominal8KiB/64KiB mask
or board alignment is silently supplied. Status conversion rejects misaligned
and outside-ring values; range selection additionally rejects a limit beyond
posted descriptors. These are stricter host checks than the unchecked stock
arithmetic. ALL uses software out without requiring status samples and is not
hardware completion evidence. Even the stock name TRANSMITTED does not prove
over-the-air transmission or authorize buffer reuse.

`post` and `reclaim` only advance software cursors within available/used bounds.
A caller must independently establish descriptor publication ordering, DMA
quiescence/completion, packet lifetime, cache synchronization and unmapping.
There is no Linux DMA API integration, scatter/gather mapper, IRQ handler,
TX status reconciliation or RX parser in this component.

## Validation and remaining work

Original synthetic tests check literal non-echo descriptor words/bytes, all
single-bit parity positions, truncation and low-add overflow without high carry,
codec overlap and boundaries, ring wrap/full/empty/reserved-slot behavior,
explicit nonzero pointer bases and differing status masks, invalid masks/counts,
and all three range selections. The standalone test passes strict C11 with
ASan/UBSan. The component compiles freestanding for ARMv7 with warnings as errors.

The optional [differential harness](../tests/check_dma_descriptor_arm.py) executes the actual closed current
`dma64_dd_upd` ARM function with Unicorn2.1.4 and synthetic contexts/rings. All864
cases match the portable C descriptor bytes. Strict memory/instruction allowlists
reject unknown reads/writes/execution; this200byte leaf has no calls or relocation
operands. The observed write orders are `[8,12,0,4]` in648cases and
`[8,12,0,4,4]` in216cases. This proves the tested formatter transformation;
it does not execute ring publication, allocation/cache helpers, DMA hardware,
interrupts or a complete driver.

The older GPL17.10.86.5 material in [the source audit](DMA64_SOURCE_AUDIT.md)
corroborates common layouts and arithmetic. It is not interchangeable with the
current build. Its broader64bit input-address and optional coherency/barrier
branches must not be imported as current observations. Current runtime attach
probes, pointer-mask/alignment results, physical FIFO/channel addresses,
queue-selector locking, actual mapping/cache ownership, descriptor-only/AQM
payload ABI and packet/RX/TX-status lifetime remain unresolved. A bounded search
of current procedure names for DMA error/active/pending helpers found a TX-active
helper but did not establish MAC interrupt routing/masking/acknowledgment.
That requires following the actual MAC ISR and deferred-work caller chain.

Run the optional comparison with an existing Unicorn2.1.4 Python environment:

```sh
python3 tests/check_dma_descriptor_arm.py /path/to/pinned/wl.ko
```

The script builds its temporary host library, verifies module/function hashes,
and emits only portable counts/hashes. It does not extract or print raw payloads.
