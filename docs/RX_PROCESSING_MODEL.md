# Current packet and status receive processing

`driver/rx_packets.c` implements the recovered current `dma_rx` body with real
bulk-list and status children. `driver/rx_status.c` implements `_dma_sts_rx`,
including its actual timeout unlink and inlined pool-return stores. These are
original bounded portable jobs, with typed external memory and OS operations;
they have no native device backend or Linux packet-delivery integration.
The [selected assembly index](../findings/RX_PROCESSING_EVIDENCE.json) pins the
current source and its four functions. Raw code, packet buffers, proprietary
objects and analysis records remain private. No hardware/RF operation occurred.

## Packet body and real children

The initial bulk call uses the caller's unsigned budget and force-all0. Later
continuation acquisition uses budget1/force-all0. Both execute the real
[closed bulk helper](DMA_RX_LIST_MODEL.md); this build does not call the separate
slot-removal leaf, nor does it perform an unmap/cache call absent from the
current instructions. The primary list cursor/state propagates before and after
child callbacks. The higher processor reads the configured receive offset after
the initial bulk child, including an empty result.

For each returned packet, the body loads its data pointer and repeatedly reads
its first u16 length. Zero permits at most200 one-microsecond OS delays, with
200 reads and200 delays on exhaustion. There is no final read after the last
delay. A nonzero sample computes the first trim as the smaller of buffer size
and receive offset plus frame length. A linear packet gets actual length and
tail stores, using a fresh data-pointer load. A nonlinear packet reaches the
actual imported trim service. These are external owner operations, not vendor
helpers replaced with successful callbacks.

When bytes remain beyond the first buffer, flag0x40 takes the drop branch.
Otherwise a fresh header word supplies its bits24..27 continuation count. Each
continuation comes from the current packet NEXT or another real budget1 bulk
call. FRAGMENT is linked before trimming the next buffer. Its predecessor NEXT
is then cleared, followed by remaining-byte/count updates. The last continuation
NEXT is moved to the original packet before being cleared. Flag4 controls the
subsequent retained-packet branch. The implementation preserves the observed
missing-continuation/flag behavior; retained labels and counters do not prove
that a complete valid frame was received or decoded.

A dropped packet is spliced out of the accepted list, its NEXT cleared, and
the local head/current updated before the actual imported packet-free request.
The error counter increments only after that service returns. It is not
invented rollback for a failed child. Accepted-tail and caller count updates
precede the final NEXT load for a retained packet. Both counters wrap as u32.

At the tail, a known nonnull status context plus nonnull caller status output
composes the actual status job below, with an explicitly matching binding and
known output snapshot. Main output head/tail stores wait for that entire child
return. A missing reached binding/child stops after retaining earlier packet
effects and count updates; it cannot silently stand in for empty status.

## Status body and pool boundary

The status job composes the actual closed status-list helper with force-all0.
Each node supplies a fresh data pointer and the same200-read/200-delay length
loop. A positive length updates the retained tail/count and reads the next
node. Timeout unlinks the node, updates the local head when needed, then clears
NEXT as **four separate byte writes**, not an invented single word write.

The actual inlined pool return reads the registered callback. Only a nonnull
callback requires the node's signed16 index. If that index is not-1, the actual
callback argument is read and the job stops at
`AXD_RS_REGISTERED_POOL_CALLBACK`. Its implementation remains unrecovered; no
arbitrary success callback is accepted. Null callback or index-1 executes the
real remaining pool operations. The index is not overwritten merely because
an unexecuted callback would have done so after returning.

The pool count is freshly loaded, incremented and stored. NEXT traversal is
fresh and bounded. After its end, high-water and head are read in source order;
the maximum comparison is signed, including count wrap. The selected high-water,
last-node NEXT and pool-head stores follow in order. Only then does current
advance and the status error counter increment. Final status head/tail stores
follow the entire node loop. Pool effects are retained across failure/cancel;
no pool callback, cache action, allocation or free is invented.

## Interfaces, policy and partial state

Packet/status labels are synthetic or owner-mapped ARM32 values. Packet field
names and status/pool offsets describe this current semantic contract; they are
not portable skb/kernel ABI offsets. All packet, status, buffer and pool reads
are explicit fresh observations. The adapter must validate actual capacities,
object lifetime, serialization and access width before serving a request.
Writes are real typed requests. The model supplies neither implicit zeros nor
invented write echoes. Imported trim/free/delay requests require an actual OS
adapter; their host test recording does not implement those services.

Metadata is a coherent immutable snapshot except actual child state and backend
effects, narrowing stock's live reloads. Owners are required only when reached.
Required null labels fail, and unknown reached facts are dependencies. Explicit
work limits1..65535 bound hostile packet/pool cycles. Receive offset must not
exceed the supplied u16 buffer size; negative continuation trims and wrapping
tail-pointer arithmetic fail instead of reproducing unchecked corruption.
Earlier writes remain visible when a later bound fails.

Begins snapshot their own aliased requests/contexts/outputs before clearing the
job. Every step enters at most one user callback. A successful cancelling read
or write keeps its observation/effect; later software stores do not run. Callback
error wins coincident cancellation. Child partial contexts propagate before
callbacks and after returns. Terminals emit no cleanup, retry or rollback.
Final output head/tail are separate portable stores; intermediate count updates
remain observable even when those final stores were never reached.

## Verification and remaining work

Independent literal tests cover linear/nonlinear trims, two delayed reads,
the exact200-delay timeout, fragment acquisition through another real bulk
child, leading/middle drop splicing, error-count wrap, actual positive/empty
status children, required/unused bindings and source-ordered output commitment.
The status tests exercise byte detach, callback-null/index-1 versus unknown
registered callback, fresh pool reads and signed high-water edge cases. Every
callback in selected full/timeout paths covers error, cancel and both together.
Strict ASan/UBSan runs pass; twelve meaningful compiling private mutants are
rejected. The shared runner/build and actual ARM results are recorded in
[PROGRESS.md](../PROGRESS.md). The
[four-function ARM comparison](RX_PROCESSING_ARM_CHECKS.md) separately executes
the actual packet/status bodies and list children on positive-length linear
fixtures; it rejects reached imported OS calls rather than emulating success.

This closes the named packet/status bodies within these explicit dependencies.
RX-fill allocation/mapping/coherency, status-pool callback registration, BMAC
header/status merging, ISR/deferred-work handling, RSSI/security/frame decoding,
Linux delivery and device proof remain required. A completed software job does
not establish DMA ownership, a valid frame, physical reception or a working
replacement driver.
