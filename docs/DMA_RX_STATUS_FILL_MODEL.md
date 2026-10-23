# Current status receive fill and BMAC selection

`driver/dma_rx_status_fill.[ch]` implements the complete current normal target130
`dma_sts_rxfill` body as original portable C. `driver/bmac_rx_fill.[ch]` implements
its actual BMAC eligibility wrapper and composes this child or the existing
[normal packet fill](DMA_RX_FILL_MODEL.md). Their source is the pinned current
17.10.121.37 module. Selected provenance is in
[the evidence metadata](../findings/DMA_RX_STATUS_FILL_EVIDENCE.json).

The status body has no allocation callback. It removes already allocated nodes
from the actual status pool using fresh serialized memory observations. Runtime
bindings, capacities, pool ownership, data access and the genuine imported
address translation service must be supplied by a real backend. No hardware,
router or RF operation was performed.

## Source order

A nonzero DI byte 0x104 returns zero. Otherwise outstanding is
`(out-in)&(count-1)` and the target is `min(count,receive_posts)`. Outstanding
above target returns one without I/O. Every other completed path returns zero,
including successful fill, no work and pool exhaustion. The current body
explicitly discards the initial-failure idle child's Boolean result. Older
17.10.86.5 GPL return expressions do not override these current instructions.

For each requested node, the body:

1. Freshly reads pool HEAD, then node NEXT at byte 0x1c, and stores the pool's new
   HEAD. It clears NEXT with four separate byte stores, freshly reads pool
   AVAILABLE and stores that value minus one with ARM32 wrap semantics.
2. Freshly reads node DATA at byte0, clears the selected data word, then freshly
   reads DATA again. Imported `osl_virt_to_phys` is a legitimate typed OS service
   request for that second observed address. No generic DMA-map, cache or unmap
   call occurs in this body.
3. Stores the current index as a halfword at node byte 0x18, then emits another
   four separate NEXT byte clears. It links the node through the previous RX
   tail or commits the primary RX head, then commits RX tail. Prepared-node
   accounting precedes descriptor work.
4. Applies the proven current `dma64_dd_upd` transform and writes descriptor
   address-low, address-high, control1 and control2 in that order. SOF is bit 31;
   EOT is bit 28 for the last slot. Flag bit 0 requests four fresh descriptor
   reads in the same order. Odd XOR parity adds a fifth control2 write using
   the computed control2 and parity bit 18. Read replies never echo stores.
5. Advances the local index. Once the target is satisfied, it emits ST, writes
   the channel-relative producer at byte4, and only then commits software OUT.
   The original OUT remains visible throughout publication I/O. RX AVAILABLE
   and cached completion state are unchanged.

An empty pool increments the freshly read pool failure counter at byte8 once.
If no node was completed and count is nonzero, the actual idle child performs
R32(status byte 0x10), SY, R32(producer byte4), SY. Its result is discarded. The
body adds the missing-node count to the software no-buffer counter before the
normal publisher sequence. Exhaustion after a completed prefix skips idle.
Unsigned pool and no-buffer arithmetic retains ARM32 wrap semantics.

The source allows a target equal to count. The model preserves that arithmetic,
including a producer wrapping to its original index. Count zero with zero
cursors still publishes the unchanged producer. These behaviors do not establish
live ring safety. The model deliberately narrows unchecked stock behavior with
core 130.0/1, power-of-two ring, cursor, descriptor-capacity and nonnull binding
checks. Ownership dependencies occur only when the corresponding resource is
reached; unused pool/node/OS resources do not block no-work or disabled paths.

## Actual BMAC wrapper

The wrapper always observes the hardware-to-WLC-to-public pointer chain and
freshly reads public byte 0x1c0 before comparing FIFO. Only FIFO 3 with sample bit 0
set chooses status fill. All other cases compose actual normal fill, including
FIFO 3 with sample0x80. The source selects DI word 6+FIFO; status FIFO 3 therefore
uses word 9. The API carries explicit hardware/WLC/public facts, selected FIFO,
selected DI and each child binding. A reached child must match that DI; another
FIFO's context cannot silently substitute. FIFO below 72 is a host bounds policy,
not a recovered hardware queue capacity.

Child contexts are canonical before every external callback and after each
transition. The wrapper exposes the child's scalar result for trace inspection;
the native wrapper is void and does not imply readiness or successful RX.
Higher receive orchestration can reuse this wrapper for both status FIFO 3 and
the originally requested FIFO.

## Completion and verification

Both jobs snapshot aliased begin inputs, emit at most one callback per step,
retain successful callback effects and stop later stages on cancellation.
Callback errors take precedence over simultaneous cancellation. Partial pool
removal, byte clears, node links, descriptor writes and a completed producer
write survive interruption. Software OUT commits in its later source stage.
Terminal calls are silent; no invented rollback or cleanup is performed.

The independent literal C suites cover full positive node preparation, empty
and partial pools, empty/nonempty RX tracking, full-target wrapping, parity on
and off, fresh parity samples, exact byte/halfword widths, publication and every
principal callback error, cancellation and combined prefix. Wrapper tests cover
all 256 public-byte values on FIFO 0/2/3/71, actual positive status composition,
normal no-work composition, binding mismatches, aliases and terminals. Both
pass strict C11, AddressSanitizer/UndefinedBehaviorSanitizer and freestanding
ARMv7 compilation. Fourteen compiling source mutants are rejected. An independent source review
of both production components and a separate checker review plus expanded ARM
rerun were clean.

`tests/check_status_fill_arm.py MODULE` accepts the caller's immutable pinned
module and builds its own temporary C comparison shim. It validates module and
function hashes, exact relocation sets and bound CALL/JUMP symbol targets. The
default closed matrix executes four actual ARM bodies: the BMAC wrapper,
disabled normal fill, status fill and idle. Its 1,171 cases execute 41,957
instructions and 72 actual idle calls. It compares typed event traces,
per-event context/counter snapshots, final pool/channel bytes, final contexts
and scalar results. Coverage includes every eligibility byte, disabled branches,
outstanding-above-target, no-work/count-zero and initial empty pools, mask
variations and ARM32 counter wrapping.

With `--hnd HND_MODULE`, the checker additionally validates the pinned HND module
and its actual eight-byte `osl_virt_to_phys` implementation. This current helper
adds `0x81000000` with ARM32 wrap and returns; it has no memory access, calls or
relocations. That shipped transform is a synthetic comparison fact, not a claim
that arbitrary runtime virtual addresses can safely be mapped. The checker
binds the actual imported call to these actual HND instructions and the actual
WL descriptor call to its closed updater. No OS success stub is used.

The expanded matrix passes 1,603 cases and 153,317 actual instructions across
six actual bodies, including 432 positive-pool cases. It covers one/two-node
successful and partial fills, empty/existing RX tracking, a complete-ring target,
producer wrap, parity disabled and fresh odd/even parity, address offset overflow,
upper descriptor offsets and ARM32 pool/no-buffer counter wrapping. It also
compares final pool, node, data, descriptor and channel bytes. All 72 idle calls
remain actual child executions. Five negative probes reject unbound tails,
unknown public memory, missing or unused fresh reads, and an explicitly unbound
address-translation call. No external function is stubbed in either mode.

The real OS backend still owns address policy and coherent resources. Concurrent
ownership, IRQ handling, Linux packet delivery and physical receive behavior
remain outside these offline checks. Private analysis was exported and closed;
no private source bytes or local paths are published.
