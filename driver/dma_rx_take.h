/* SPDX-License-Identifier: MIT */
#ifndef AXD_DMA_RX_TAKE_H
#define AXD_DMA_RX_TAKE_H
#include "dma_descriptor.h"
struct axd_rx_take_context {
    struct axd_dma64_ring ring;
    uint16_t core, minor, cached_current;
    uint8_t sentinel_mode_a, sentinel_mode_b, status_owned, slots_owned;
    uint32_t status_identity, slots_identity, available;
    size_t slot_count;
};
enum axd_rx_take_kind {
    AXD_RX_TAKE_STATUS_READ32 = 1, AXD_RX_TAKE_FULL_BARRIER,
    AXD_RX_TAKE_SLOT_READ32, AXD_RX_TAKE_SLOT_WRITE32
};
struct axd_rx_take_operation {
    enum axd_rx_take_kind kind;
    uint32_t identity, offset, value;
};
typedef int (*axd_rx_take_io_fn)(void *, const struct axd_rx_take_operation *, uint32_t *);
enum axd_rx_take_phase {
    AXD_RX_TAKE_INVALID = 0, AXD_RX_TAKE_RUNNING, AXD_RX_TAKE_DONE,
    AXD_RX_TAKE_DEPENDENCY, AXD_RX_TAKE_FAILED, AXD_RX_TAKE_CANCELLED
};
enum axd_rx_take_error {
    AXD_RX_TAKE_NO_ERROR = 0, AXD_RX_TAKE_BAD_ARGUMENT, AXD_RX_TAKE_UNSUPPORTED,
    AXD_RX_TAKE_BOUNDS, AXD_RX_TAKE_IO_ERROR
};
struct axd_dma_rx_take {
    enum axd_rx_take_phase phase;
    enum axd_rx_take_error error;
    struct axd_rx_take_context context;
    uint32_t force, status_sample, slot_sample, return_value;
    unsigned stage, return_valid, removed_slots, skipped_sentinels;
    uint64_t attempted_ops, completed_ops;
};
/* Actual current dma_getnextrxp.part.1 leaf. Empty software ring returns0
 * without accessing unused resources. A stale cached-current match triggers
 * actual RX status R32(+10)/SY; force nonzero bypasses completion equality,
 * never the actual status read. Slot handle is sampled then cleared, followed
 * by separate source-ordered in/available software stores. Sentinel80000000
 * is skipped only with either full-byte sentinel mode nonzero.
 * Ring/count/base/mask and post-call context are explicit immutable metadata;
 * stock reloads fields. Existing strict ring/mask/alignment policy applies,
 * including rejecting invalid hardware pointer samples instead of wrapping
 * unchecked into an out-of-ring or truncated index. Slot capacity is checked
 * at the reached access. Begin snapshots own-context aliases before clearing.
 * The backend owns serialized slot/status resources, actual read values and
 * clear effects. Return is a packet label, not DMA ownership, cache coherence,
 * successful reception, parsed packet or permission to reuse a buffer.
 */
int axd_dma_rx_take_begin(struct axd_dma_rx_take *, uint32_t force,
                            const struct axd_rx_take_context *);
/* At most one user callback per step. Successful samples/slot-clears survive
 * cancellation; later software stores do not run. Error wins simultaneous
 * cancel; terminals are silent without invented refill/reclaim/unmap/retry.
 */
enum axd_rx_take_phase axd_dma_rx_take_step(struct axd_dma_rx_take *, axd_rx_take_io_fn, void *);
enum axd_rx_take_phase axd_dma_rx_take_cancel(struct axd_dma_rx_take *);
#endif
