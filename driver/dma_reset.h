/* SPDX-License-Identifier: MIT */
#ifndef AXD_DMA_RESET_H
#define AXD_DMA_RESET_H
#include "d11_io.h"
#include <stddef.h>

enum axd_dma_direction { AXD_DMA_TX_RESET = 1, AXD_DMA_RX_RESET };
enum axd_dma_region { AXD_DMA_TX_CHANNEL = 1, AXD_DMA_RX_CHANNEL, AXD_DMA_QUEUE_SELECTOR };
struct axd_dma_operation {
    enum axd_d11_kind kind;
    enum axd_dma_region region;
    uint32_t offset, value; /* Channel-relative offset; selector region offset0. */
};
struct axd_dma_reset_context {
    uint16_t core, minor, descriptor_count;
    uint8_t indirect, post_disable_poll;
    uint32_t selector_value, tx_cache;
    uint8_t selector_cache;
};
enum axd_dma_phase {
    AXD_DMA_INVALID = 0, AXD_DMA_RUNNING, AXD_DMA_TRACE_COMPLETE,
    AXD_DMA_FAILED, AXD_DMA_CANCELLED
};
enum axd_dma_error {
    AXD_DMA_NO_ERROR = 0, AXD_DMA_BAD_ARGUMENT,
    AXD_DMA_UNSUPPORTED, AXD_DMA_IO_ERROR
};
struct axd_dma_reset {
    enum axd_dma_phase phase;
    enum axd_dma_error error;
    enum axd_dma_direction direction;
    struct axd_dma_reset_context context;
    uint32_t last_read;
    unsigned stage, queued, cursor, repetitions;
    unsigned return_value, disabled_observed, suspend_exhausted;
    struct axd_dma_operation operations[4];
    uint64_t attempted_ops, completed_ops;
};
typedef int (*axd_dma_io_fn)(void *context,
                           const struct axd_dma_operation *operation,
                           uint32_t *read_value);
/* Runtime caller supplies the actual channel/selector mapping in its backend.
 * This library neither discovers resources nor allocates/maps DMA buffers.
 * post_disable_poll represents current TX flagword bit0x100; direct TX always
 * polls. RX never programs the indirect selector, regardless of these flags.
 */
int axd_dma_reset_begin(struct axd_dma_reset *job, enum axd_dma_direction direction,
                        const struct axd_dma_reset_context *context);
/* One synchronous callback or bounded internal transition per step. Every wait
 * has initial status read and at most 1000 delay-10/read iterations, no extra final
 * confirmation. TX suspend exhaustion still proceeds to disable; TX disabled
 * timeout requests 300us then returns false. RX has no 300us delay. Completed
 * paths expose the stock return value, even timeout false. Empty/indirect no-poll
 * success does not imply observed disabled; inspect disabled_observed.
 * No rollback, automatic retry, interrupts, DMA reclamation or OS cleanup.
 * Single-threaded; callbacks may cancel, not reenter or mutate fields. Successful
 * callback cancellation persists; simultaneous callback error takes precedence.
 */
enum axd_dma_phase axd_dma_reset_step(struct axd_dma_reset *job,
                                      axd_dma_io_fn io, void *context);
enum axd_dma_phase axd_dma_reset_cancel(struct axd_dma_reset *job);
#endif
