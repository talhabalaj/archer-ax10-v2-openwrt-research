# Current descriptor-only DMA transmit posting

`driver/dma_tx_post.c` implements the complete observed current `dma_txdesc`
leaf as a bounded offline job. It is descriptor-only posting, not the ordinary
packet TX path. No DMA mapping, cache operation, IRQ handler, packet transfer,
router/MMIO access or RF execution is supplied or implied.

The pinned current module and exact source observation are indexed in
[the selected evidence](../findings/DMA_TX_POST_EVIDENCE.json). The function is
324bytes with no calls or relocations. Its descriptor/ring layout is corroborated
by [the descriptor work](DMA_DESCRIPTOR_MODEL.md); older GPL17.10.86.5 remains
corroboration, not a substitute for current17.10.121.37 instructions.

## Exact order and resources

The function first computes `next=(out+1)&(count-1)`. When next equals in it
stores availability0, increments the no-descriptor counter modulo2^32, and
returns `FFFFFFFF`. There is no reached descriptor/channel resource or input
mutation on this branch.

Otherwise, a nonzero full burst-control byte causes an input control1 store
adding NOTPCIE bit18. An old out index equal to count-1 then causes another
input control1 store adding EOT bit28. The portable job represents these source
stores in its copied `request.descriptor`; it deliberately does not mutate the
caller's request storage. The independent internal steps preserve their order.

Four typed descriptor-memory callbacks store low address at slot+8, high address
at slot+12, control1 at slot+0 and control2 at slot+4. All raw word bits survive:
no count masking, address translation, automatic coherent flag or normal-packet
formatter is inserted. The normal `dma64_dd_upd` formatter has different inputs
and must not replace this raw descriptor-only path.

When control flag bit0 enables parity, four **fresh supplied descriptor-memory
reads** follow in low/high/control1/control2 order. These are RAM observations,
not an implicit write echo. Their XOR parity decides whether to perform a fifth
store of the original input control2 with bit18 set. The copied input control2
is unchanged. There are no descriptor-memory barriers in this observed leaf.

The software out index is stored next. With nonzero commit byte, an actual
store-barrier callback and channel-relative `W32(4,pointer_base+out*16)` follow.
Availability is recalculated only after that whole commit returns. Commit0
skips the channel resource entirely and then updates availability. Finally,
return0 is made valid. No return value claims hardware acceptance or successful
transmission.

Callbacks carry distinct descriptor-memory/channel resource tags, opaque
caller-supplied IDs and relative offsets. Owners must provide serialized live
resources and explicit read samples. The component acquires no mapping, discovers
no FIFO/register base, supplies no lock, and manages no packet/buffer lifetime.

## Bounded model policy and partial execution

Count/in/out are preflighted as power-of-two2..32768 and in-range indices.
These bounds fit the observed u16 indices without asserting actual hardware
capacity. Descriptor ownership/identity/capacity is checked only after the
full-ring branch and input software stores. Channel ownership/identity and
strict pointer-mask/base boundary checks occur only on commit, after the out
store. Unavailable reached ownership is an explicit dependency. Invalid
metadata is an explicit failure; no successful resource stub is used.

The current binary reloads metadata later in the function. The job snapshots
immutable metadata and narrows those live changes; callbacks may cancel but may
not mutate jobs or reenter begin/step. Caller request/context aliases are copied
before job clearing. A step performs at most one callback. Callback failure wins
over simultaneous cancellation, and successful cancelling callbacks retain their
memory/read telemetry. Software cursor, availability, input flags and return
validity have separate source-order boundaries. Terminal jobs perform no retry,
rollback, cleanup or producer write.

A failed parity read leaves out unchanged. A dependency/failure at channel
commit leaves the new software out stored and availability stale. Even a
successful cancelling producer write leaves availability uncommitted because
its later software store has not executed. Full-ring counter wrap and partial
cancellation between its availability/counter stores are independently tested.

## Validation

Original literal tests check four/five write order, all four independent parity
reads, non-echo replies, EOT/burst/raw-control bits, full/empty/wrap conditions,
noncommit unused-resource handling, aliases and every callback error/cancel/both
prefix in the odd/even parity commit paths. The suite passes strict C11 under
ASan/UBSan. The component compiles freestanding for ARMv7 with warnings as errors.

The optional [actual ARM comparison](../tests/check_dma_tx_post_arm.py) builds a
temporary host shim and executes only the closed pinned leaf with Unicorn2.1.4.
All1,408 synthetic cases match typed descriptor/channel traces, per-callback
software snapshots, final descriptor memory, mutated input words, cursor,
availability/counter and return value. This includes240 full-ring cases and
76,496 actual ARM instructions. Three strict context/read-queue rejection probes
pass. No external call, relocation or hardware operation occurs. This validates
the tested leaf, not DMA capabilities, packet mapping, the normal TX path,
transmission-status reconciliation, interrupts or Linux integration.
