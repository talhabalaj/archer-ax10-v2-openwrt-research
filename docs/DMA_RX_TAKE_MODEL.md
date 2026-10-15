# Current DMA receive packet-slot leaf

`driver/dma_rx_take.c` implements the complete observed current
`dma_getnextrxp.part.1` leaf as a bounded offline job. The returned value is an
opaque ARM32 packet label. It is not a parsed packet, DMA ownership transfer,
cache-coherency proof, successful reception or permission to reuse a buffer.
No router, hardware/MMIO or RF operation was performed.

The pinned current `wl`17.10.121.37 module, exact296byte leaf and selected
observation are indexed in [the evidence](../findings/DMA_RX_TAKE_EVIDENCE.json).
The function has no calls or relocations. Older GPL17.10.86.5 remains supporting
source material, not a substitute for these current instructions.

## Current source order

The leaf first compares software in and out. Equality returns0 without any
reached slot/status resource or availability update. Otherwise, cached current
is compared with in. A different cached value permits a direct slot access.
Equality requires an actual channel-relative status0 read at offset`0x10`, then
a full barrier. The pointer is converted using the supplied mask and base:
`(((sample & mask)-pointer_base)&mask)>>4`. The current binary extracts a u16
index and stores it in the cached-current halfword.

If the new current index equals in and force is0, return0 follows without a
slot access. Any nonzero unsigned force bypasses that equality return; it does
not suppress the status read or barrier. The force argument remains active
through skipped-sentinel loops.

A taken slot is read as an ARM32 label and then cleared to0 with an actual
slot-memory store. Only afterwards does the leaf advance in with count-1
wrapping and recompute availability as count-used-1. These software stores
remain separate portable steps. Successful clear cancellation therefore retains
the clear effect while leaving the later in/availability stores unexecuted.

Normally the sampled label is returned, including label0. Label`80000000` is
special only when either of the two full mode bytes is nonzero. In that case,
the leaf returns0 if in has reached out, or repeats the cached-current check
otherwise. The portable job records a skipped-sentinel telemetry count after
the index/availability stores; that counter is not a stock software-field store.
With both mode bytes0, `80000000` is returned as an ordinary label after its
slot has been cleared and in/availability advanced.

There are no packet frees, DMA unmaps, cache operations, refill calls, allocation
or interrupt acknowledgments in this closed leaf. Those operations belong to
other actual wrappers/callers and must not be inserted as invented cleanup.

## API and bounded policy

The context supplies core130.0/1 identity, serialized ring metadata/cursors,
cached current, both mode bytes, slot capacity, explicit resource ownership and
opaque status/slot IDs. IDs are labels for an adapter; no physical address or
mapping is inferred, and zero is not an implicit missing-resource sentinel.
Unowned reached status or slot resources return dependency. A cached shortcut
can use slots without reaching the status resource. Empty rings reach neither.

The existing strict ring policy validates count, index, mask, alignment and
boundary before I/O. Reached slot capacity is checked before access. A newly
sampled unaligned/outside-ring hardware pointer fails rather than reproducing
unchecked stock truncation/indexing. Cached current and other facts are supplied
snapshots; they do not establish a live hardware observation. The binary reloads
fields during its loop, whereas this model requires immutable metadata and
serialized external resources. Actual attach probes, acquisition, locks and
cache validity remain external dependencies.

Begin snapshots own-context aliases before clearing the job. Every step makes
at most one synchronous callback. Callbacks may cancel but cannot reenter or
mutate jobs. Successful samples and clears survive cancellation; callback error
wins simultaneous cancel. Terminal states emit no cleanup, retry or rollback.
`removed_slots` counts successful clear callbacks. `skipped_sentinels` commits
only in the later value decision after software index/availability stores.
Return validity describes the complete leaf return, not the physical state of a
buffer represented by that label.

## Validation and remaining receive work

Independent original literal tests cover status refresh and cached shortcuts,
force0/1/FFFFFFFF, distinct non-echo read queues, mode full-byte predicates,
ordinary/special sentinel labels, null-slot removal, multi-sentinel wrap loops
with subsequent status refresh, empty/unowned resources, strict sample/slot
bounds, aliases and terminal silence. Every callback in selected normal and
multi-sentinel paths covers error, cancellation and both. ASan/UBSan passes.

The optional [actual ARM comparison](../tests/check_dma_rx_take_arm.py) verifies
the pinned closed leaf with Unicorn2.1.4 and synthetic contexts/read queues.
All2,304 cases, including864 empty-ring cases, match typed status/barrier and
slot read/clear traces, per-callback in/availability/cached-current snapshots,
final return/software state and clear offsets. This executes379,379 actual ARM
instructions with no calls or relocations. Three strict context/read-queue
rejection probes pass. It executes no device operation or successful external
stub.

This closes only the packet-slot leaf. The complete `dma_rx`/BMAC receive path,
frame length and status parsing, split/status-FIFO variants, packet/cache/DMA
ownership, buffer-pool lifetime, interrupts/deferred work, error recovery and
Linux delivery remain separate work. Passing synthetic traces or returning a
nonzero label does not complete those requirements.
