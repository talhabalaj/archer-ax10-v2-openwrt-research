/* SPDX-License-Identifier: MIT */
#ifndef AXD_DMA_RX_LIST_H
#define AXD_DMA_RX_LIST_H
#include "dma_descriptor.h"
struct axd_rx_list_context {
    struct axd_dma64_ring ring;
    uint16_t core, minor, cached_current;
    uint32_t head, tail, available;
    uint32_t status_identity, descriptor_identity, descriptor_bytes;
    uint8_t status_owned, descriptor_owned, packet_owned;
    uint8_t linked_mode, linked_known, linked_owned;
    uint32_t linked_identity, linked_available;
    uint16_t linked_in;
};
enum axd_rx_list_kind {
    AXD_RX_LIST_STATUS_READ32=1, AXD_RX_LIST_FULL_BARRIER,
    AXD_RX_LIST_PACKET_NEXT_READ32, AXD_RX_LIST_PACKET_NEXT_WRITE32,
    AXD_RX_LIST_DESCRIPTOR_READ32
};
struct axd_rx_list_operation {
    enum axd_rx_list_kind kind;
    uint32_t identity, offset, value;
};
typedef int (*axd_rx_list_io_fn)(void *, const struct axd_rx_list_operation *, uint32_t *);
enum axd_rx_list_phase {
    AXD_RX_LIST_INVALID=0, AXD_RX_LIST_RUNNING, AXD_RX_LIST_DONE,
    AXD_RX_LIST_DEPENDENCY, AXD_RX_LIST_FAILED, AXD_RX_LIST_CANCELLED
};
enum axd_rx_list_error {
    AXD_RX_LIST_NO_ERROR=0, AXD_RX_LIST_BAD_ARGUMENT, AXD_RX_LIST_UNSUPPORTED,
    AXD_RX_LIST_BOUNDS, AXD_RX_LIST_BINDING, AXD_RX_LIST_IO_ERROR
};
struct axd_dma_rx_list {
    enum axd_rx_list_phase phase;
    enum axd_rx_list_error error;
    struct axd_rx_list_context context;
    uint32_t budget, force_all, status_sample, descriptor_sample, next_sample;
    uint32_t original_head, packet, last_packet, index, end, return_value;
    unsigned stage, return_valid, planned_entries, traversed_entries;
    unsigned head_stores, tail_detached, index_committed, available_committed;
    unsigned linked_in_committed, linked_available_committed, tail_cleared;
    uint64_t attempted_ops, completed_ops;
};
/* Exact current CLOSED bulk _dma_getnext_rxlist, not a call to the separate
 * dma_getnextrxp slot leaf. budget is unsigned; force_all!=0 selects software
 * out and bypasses budget/status. Count0 is an absent direction/no-I/O return.
 * Other rings use the existing stricter count/mask/boundary/sample policy.
 * Primary/linked context stores are copied software state, committed separately
 * in source order; owners propagate those snapshots to actual serialized state.
 * Packet identity is an opaque ARM32 label; NEXT operations use its byte4 field.
 * Descriptor reads are mandatory fresh observations at index*16+8 even though
 * the current binary discards them. This build makes NO cache/unmap/OS calls.
 * Borrowed runtime facts stay immutable; stock live reloads are narrowed.
 * Begin snapshots its own context aliases before clearing the job.
 */
int axd_dma_rx_list_begin(struct axd_dma_rx_list *, uint32_t budget,
                           uint32_t force_all, const struct axd_rx_list_context *);
/* At most one callback/step. Reached unowned resources/unknown linked context
 * are dependencies; explicit null packet/linked bindings fail instead of stock
 * invalid dereference. Fresh NEXT reply -> primary head software store -> fresh
 * descriptor read; last NEXT clear -> in -> available -> optional linked in and
 * available -> optional primary tail clear -> original-head return. Errors win
 * simultaneous cancel, successful callback effects survive, terminals are silent.
 * No packet/free/unmap/cache/IRQ/ownership or successful reception claim. */
enum axd_rx_list_phase axd_dma_rx_list_step(struct axd_dma_rx_list *, axd_rx_list_io_fn, void *);
enum axd_rx_list_phase axd_dma_rx_list_cancel(struct axd_dma_rx_list *);
#endif
