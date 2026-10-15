# Current receive path and remaining dependencies

This is an original static call-path summary of the pinned current ARM driver,
not a receive implementation or packet capture. The module identity and five
selected observations are in [the evidence index](../findings/DRIVER_RX_PATH_EVIDENCE.json).
REA6.5.0/Ghidra12.1.4 records were exported and the session closed. Raw code,
packets, firmware and device identifiers remain private. No hardware was used.

## Three distinct layers

The actual `dma_getnextrxp.part.1` leaf removes software packet labels from the
RX ring. Its bounded portable implementation and instruction comparison are
described in [DMA_RX_TAKE_MODEL.md](DMA_RX_TAKE_MODEL.md). It may consult a fresh
hardware-current sample, clears each selected tracking slot, advances the
software consumer and updates available capacity. It does not parse a received
frame, unmap DMA, synchronize CPU caches, refill the ring or deliver a packet.
An ordinary null label can still consume a slot. Sentinel skipping depends on
the actual full-byte mode fields, not a universal special-pointer assumption.

The higher `dma_rx` obtains a list through `_dma_getnext_rxlist`. For each
packet it reads the first u16 length, with at most200 one-microsecond delays
while that length stays zero. It bounds the initial trim by the configured
receive-buffer size. An oversized frame with flag0x40 takes the drop/free and
error-count path. Otherwise the first header's bits24..27 supply a continuation
count; flag4 gates acceptance after continuation handling. Subsequent list
acquisition, trimming, packet-link changes, malformed
or incomplete-frame disposal and error-count updates are real operations.
An optional status object invokes `_dma_sts_rx` separately. These branches need
actual packet, allocation, DMA/cache and list contracts; a successful slot-take
job cannot replace them. The selected current function occupies532 ELF bytes.

The enclosing `wlc_bmac_recv` has an optional CFP fast path. Its ordinary path
samples a descriptor-resolved D11 register with R32/SY, invokes `dma_rx`, and
conditionally refills FIFO3/processes separate receive status. It adjusts
per-packet metadata, invokes the actual twelve-byte packet-headroom operation,
and has optional chainability handling. It refills the requested FIFO before
either chain delivery or per-packet `wlc_recv`, with RSSI processing conditional
on the actual revision/header flags. Finally it
flushes AWL delivery and updates the caller's count/budget result. This896-byte
wrapper is not a complete parser; `wlc_recv` and its callees add substantial
protocol/security/station work. No single header layout or unchecked skb offset
is adopted as a portable interface from these observations.

## Next implementation work

Recover `_dma_getnext_rxlist` through the actual allocation/cache/unmap paths,
then implement bounded packet/list ownership and the current `dma_rx` trimming,
continuation and error rules. Separately recover RX-fill resources and the MAC
ISR/deferred-work acknowledgement/masking contract. Keep descriptor completion,
packet DMA ownership, decoded frame metadata and Linux delivery distinct.
Device receive validation and any RF work remain outside this offline change.
The older [GPL DMA source map](DMA64_SOURCE_AUDIT.md) is corroboration only;
its different driver version must not supply silent current defaults.
