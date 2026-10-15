/* SPDX-License-Identifier: MIT */
#ifndef AXD_DMA_TX_POST_H
#define AXD_DMA_TX_POST_H
#include "dma_descriptor.h"
struct axd_dma_tx_post_request {
    struct axd_dma64_descriptor descriptor;
    uint8_t commit;
};
struct axd_dma_tx_post_context {
    struct axd_dma64_ring ring;
    uint32_t control_flags, available, no_descriptor_count;
    uint32_t descriptor_identity, descriptor_bytes, channel_identity;
    uint8_t burstsize_control, descriptor_owned, channel_owned;
};
enum axd_dma_tx_post_kind {
    AXD_DTP_MEMORY_WRITE32=1, AXD_DTP_MEMORY_READ32,
    AXD_DTP_STORE_BARRIER, AXD_DTP_CHANNEL_WRITE32
};
enum axd_dma_tx_post_resource { AXD_DTP_DESCRIPTOR=1, AXD_DTP_CHANNEL };
struct axd_dma_tx_post_operation {
    enum axd_dma_tx_post_kind kind;
    enum axd_dma_tx_post_resource resource;
    uint32_t identity, offset, value;
};
typedef int (*axd_dma_tx_post_io_fn)(void *, const struct axd_dma_tx_post_operation *, uint32_t *);
enum axd_dma_tx_post_phase {
    AXD_DTP_INVALID=0, AXD_DTP_RUNNING, AXD_DTP_DONE, AXD_DTP_DEPENDENCY,
    AXD_DTP_FAILED, AXD_DTP_CANCELLED
};
enum axd_dma_tx_post_error {
    AXD_DTP_NO_ERROR=0, AXD_DTP_BAD_ARGUMENT, AXD_DTP_BOUNDS,
    AXD_DTP_BINDING, AXD_DTP_IO_ERROR
};
struct axd_dma_tx_post {
    enum axd_dma_tx_post_phase phase;
    enum axd_dma_tx_post_error error;
    struct axd_dma_tx_post_request request;
    struct axd_dma_tx_post_context context;
    uint32_t slot, next, parity_sample, last_read, producer, return_value;
    unsigned stage, cursor, descriptor_writes, parity_reads;
    unsigned cursor_committed, availability_committed, producer_written, return_valid;
    uint64_t attempted_ops, completed_ops;
};
/* Exact current descriptor-only dma_txdesc, not packet-mapping TX. Raw words
 * retain all bits: no count masking, address translation or coherent-bit default.
 * The stock input pointer's control1 stores are represented by the COPIED
 * job.request.descriptor in source order, never by mutating caller storage.
 * Request/context may alias previous job snapshots. Mutable descriptor-memory
 * and channel bindings are typed opaque IDs owned/serialized by the caller.
 * Full ring returns FFFFFFFF after available0/counter++ with no reached resource.
 * Count/in/out preflight is stricter than stock. Other metadata is checked only
 * when reached: descriptor capacity before stores; pointer mask/base and channel
 * ownership only for commit, AFTER the new software out index has been stored.
 */
int axd_dma_tx_post_begin(struct axd_dma_tx_post *,
    const struct axd_dma_tx_post_request *, const struct axd_dma_tx_post_context *);
/* At most one synchronous callback per step. Descriptor writes are actual
 * low/high/control1/control2 stores, followed by FOUR fresh supplied RAM reads
 * when parity is enabled; no implicit read/write echo. Optional parity rewrite
 * uses original input control2|bit18. Cursor advances before optional ST/channel
 * offset4 write; availability updates only after that whole commit returns.
 * Successful cancelling callbacks retain their effects, errors win concurrent
 * cancellation, terminals remain silent. No cleanup/retry/rollback, DMA mapping,
 * cache maintenance, IRQ or Linux backend exists here. DONE/return0 means this
 * trace returned, never packet ownership, successful transmission or readiness.
 */
enum axd_dma_tx_post_phase axd_dma_tx_post_step(struct axd_dma_tx_post *, axd_dma_tx_post_io_fn, void *);
enum axd_dma_tx_post_phase axd_dma_tx_post_cancel(struct axd_dma_tx_post *);
#endif
