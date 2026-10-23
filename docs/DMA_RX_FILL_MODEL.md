# Current normal DMA receive fill

`driver/dma_rx_fill.[ch]` implements the complete current `dma_rxfill` normal
packet path as original portable C with typed resource and OS requests. Its
source is the pinned current 17.10.121.37 module, not the optional branches in
older GPL 17.10.86.5 material. The 944-byte current function is identified in
[the evidence metadata](../findings/DMA_RX_FILL_EVIDENCE.json). No router or
hardware operation was performed.

The caller supplies the canonical RX ring/list context, flags, buffer size,
post target, extra headroom, address translation offsets, allocator binding,
no-buffer counter and runtime ownership. These are coherent snapshots of actual
runtime facts. No physical queue/channel address, DMA aperture or allocator
implementation is silently invented.

## Actual normal path

A nonzero byte at the current DI's byte0x104 returns zero immediately, without
I/O. The split/header byte0xf4 and segmented-buffer byte0x106 select distinct
paths; this component stops with explicit dependencies for those branches.
They are not treated as successful normal fills.

For the supported normal branch, alignment is16 when flag0x10 is set and1
otherwise; flag0x1000 overrides it with8. The post target is the unsigned minimum
of ring count and the configured RX post count. Outstanding buffers are
`(out-in)&(count-1)`. If outstanding exceeds target, the function returns one
without I/O. Otherwise it allocates target minus outstanding buffers. Extra
headroom is used only when buffer size exceeds204. Each allocation request is
`buffer_size + extra_headroom + alignment - 1`.

The actual service and memory sequence is:

1. Call the runtime BPM allocator entry loaded from imported `gbpm_g+0x24`,
   passing the requested size. A genuine zero allocation result selects the
   failure path. A known binding and an actual OS backend are required.
2. Freshly load the packet's byte0x160 auxiliary pointer, then its byte0x1c
   flags. Store flags with bit0x20 cleared, then store zero through the auxiliary
   pointer. Call imported `osl_pkt_account` with the OS identity and the two
   observed argument-register values one/one.
3. If headroom is nonzero, call imported `skb_pull`. For alignment greater than
   one, freshly load DATA and calculate `(alignment-DATA)&(alignment-1)`; pull
   that amount if needed. Freshly read NONLINEAR and LENGTH, then round length
   down by alignment. A nonlinear packet calls real imported `___pskb_trim`.
   A linear packet freshly loads DATA and stores its new LENGTH and TAIL.
4. Store zero to the freshly selected first DATA word. Freshly reload packet
   DATA and call imported `osl_virt_to_phys`, using the returned ARM32 address.
5. Flag0x10000 chooses actual bulk tracking: clear new packet NEXT, link it
   through the previous tail or store the primary head, then store the tail.
   Otherwise store the packet label in the current packet slot. This happens
   before descriptor publication; a later failure retains the tracking prefix.
6. Apply the proven current `dma64_dd_upd` formatter transform, with SOF bit31
   and EOT bit28 on the last slot. Write descriptor low/high/control1/control2
   in that order. Flag bit0 requests four fresh descriptor reads in the same
   order; odd XOR parity adds a fifth control2 store using the computed control2,
   not a write echo. Advance only the job's local producer index per buffer.
7. Emit ST, write the channel-relative producer register at byte4, then commit
   the software `out` index. The old software index remains visible throughout
   producer I/O. No RX available-count or cached-completion update occurs here.

The source clamps the target to count, not count-minus-one. The model preserves
that arithmetic, including a complete-ring target whose final producer wraps
to the same index; it does not assert safe full/empty inference or hardware
support for that configuration. Likewise, count zero with zero cursors still
performs the unchanged producer ST/write/out sequence. This differs from other
DMA leaves' absent-direction behavior and is tested explicitly.

## Allocation failure and partial state

The first allocation failure calls the real closed `dma_rxidle.part.0` child:
R32(channel+0x10), SY, R32(channel+4), SY. Idle is
`((status ^ producer) & pointer_mask) == 0`; it is not a decoded channel-state
field. Fill's result is the inverse of idle. After any already-completed buffer,
an allocation failure skips this idle check and returns one. Either failure
adds missing buffers to the no-buffer counter and publishes the producer.
A zero-new-buffer plan also publishes and returns one without allocating.

The job separates reached software stores and physical operations in source
order. Steps invoke at most one callback. Errors win simultaneous cancellation;
successful cancelling reads/stores retain their sample/effect while later
stages stop. Terminals are silent and begin snapshots aliases before clearing.
An allocation can therefore be retained without packet initialization, tracking
can survive a descriptor failure, and a completed producer write can survive
cancellation before the software out store. No invented cleanup or retry runs.

## Ownership and narrower policy

ALLOCATE, ACCOUNT, PULL, TRIM and TRANSLATE are actual legitimate OS service
boundaries, not successful callbacks standing in for unknown driver helpers.
The adapter must implement the real allocator, accounting, packet mutation and
address conversion. Packet, auxiliary, data, descriptor, slot and channel
resources have separate reached ownership checks. Explicit null packet/data/
auxiliary bindings fail; unknown allocator binding stops as a dependency.
The runtime owns capacities, allocation lifetime and serialization.

The existing strict ring policy accepts power-of-two counts2..32768 with bounded
indices, a contiguous explicit mask and a noncrossing ring. The zero-count branch
requires zero cursors. Core130 revisions0/1 are supported. Descriptor/slot
capacity is checked only when reached. Allocation-size or linear-tail overflow
fails at its reached stage rather than reproducing unchecked stock arithmetic.
Metadata stays coherent and immutable outside callback effects; stock live
metadata reloads are not independent mutable configuration inputs.

No imported cache-flush, cache-invalidate or generic DMA-map call exists in the
retained normal path. Older public source describes optional BPM/coherent cache
policies, but that does not prove the current allocator's cache guarantees.
The backend must establish the real pool/coherency/address contract. This
component adds no cache success stub and makes no claim of Linux DMA safety,
packet ownership transfer, IRQ delivery or physical reception.

## BMAC eligibility wrapper

The current52-byte `wlc_bmac_dma_rxfill` unconditionally reads the eligibility
byte at byte0x1c0 through its parent objects. FIFO3 with that byte's low bit set
tails to `dma_sts_rxfill` using DI slot9. Otherwise it tails to `dma_rxfill` using
DI slot `fifo+6`. Other byte bits do not select status fill. The reusable
wrapper and separate complete status-fill model are now implemented in
`driver/bmac_rx_fill.[ch]` and `driver/dma_rx_status_fill.[ch]`; see
[their source order and verification boundaries](DMA_RX_STATUS_FILL_MODEL.md).

## Validation

`tests/test_dma_rx_fill.c` uses independent literal non-echo fixtures for bulk
and slot tracking, empty/existing bulk heads, one/two buffer wrap, full target,
alignment1/8/16 and override priority, extra-headroom threshold, real pull/trim
boundaries, address extension, four fresh parity observations, optional parity
store, first/partial allocation failure, idle comparison, no-work/count0
publication, reached owners/bindings/capacities and alias snapshots. Every
callback position in the principal branches is exercised with error, cancel
and both, checking retained partial state. Strict C11, ASan/UBSan and
freestanding ARMv7 compilation pass. Twelve private source mutations were
rejected by the literal suite.

`tests/check_dma_rx_fill_idle_arm.py MODULE` executes only the actual84-byte
zero-call idle child, with strict synthetic context/stack memory and fresh
read queues. It compares observations and the actual idle result against the
portable fill first-allocation-failure path. All144 cases passed, executing
3,024 actual instructions; four rejection probes reject unknown context,
missing/unused reads and changed selected bytes. The full fill's imported OS
implementations are not replaced with ARM success stubs. Whole-fill ARM
execution, actual OS allocation/cache behavior, mapping and hardware remain
outside this check. The formatter's separate closed ARM comparison is described
in [the descriptor model](DMA_DESCRIPTOR_MODEL.md).
