/* SPDX-License-Identifier: MIT */
#ifndef AXD_DMA_RX_STATUS_LIST_H
#define AXD_DMA_RX_STATUS_LIST_H
#include "dma_descriptor.h"
struct axd_rx_status_list_context {
    struct axd_dma64_ring ring;
    uint16_t core,minor,cached_current;
    uint32_t head,tail,available;
    uint32_t status_identity,descriptor_identity,descriptor_bytes;
    uint8_t status_owned,descriptor_owned,node_owned;
};
enum axd_rx_status_list_kind {
    AXD_RX_STATUS_LIST_STATUS_READ32=1,AXD_RX_STATUS_LIST_FULL_BARRIER,
    AXD_RX_STATUS_LIST_NODE_NEXT_READ32,AXD_RX_STATUS_LIST_NODE_NEXT_WRITE8,
    AXD_RX_STATUS_LIST_DESCRIPTOR_READ32
};
struct axd_rx_status_list_operation {
    enum axd_rx_status_list_kind kind;
    uint32_t identity,offset,value;
};
typedef int (*axd_rx_status_list_io_fn)(void *,const struct axd_rx_status_list_operation *,uint32_t *);
enum axd_rx_status_list_phase {
    AXD_RX_STATUS_LIST_INVALID=0,AXD_RX_STATUS_LIST_RUNNING,AXD_RX_STATUS_LIST_DONE,
    AXD_RX_STATUS_LIST_DEPENDENCY,AXD_RX_STATUS_LIST_FAILED,AXD_RX_STATUS_LIST_CANCELLED
};
enum axd_rx_status_list_error {
    AXD_RX_STATUS_LIST_NO_ERROR=0,AXD_RX_STATUS_LIST_BAD_ARGUMENT,AXD_RX_STATUS_LIST_UNSUPPORTED,
    AXD_RX_STATUS_LIST_BOUNDS,AXD_RX_STATUS_LIST_BINDING,AXD_RX_STATUS_LIST_IO_ERROR
};
struct axd_dma_rx_status_list {
    enum axd_rx_status_list_phase phase;
    enum axd_rx_status_list_error error;
    struct axd_rx_status_list_context context;
    uint32_t force_all,status_sample,descriptor_sample,next_sample;
    uint32_t original_head,node,last_node,index,end,return_value;
    unsigned stage,return_valid,planned_entries,traversed_entries,head_stores;
    unsigned tail_bytes_cleared,index_committed,tail_cleared,available_committed;
    uint64_t attempted_ops,completed_ops;
};
/* Current closed _dma_getnext_sts_rxlist. force_all!=0 uses software out;
 * otherwise unequal cached_current bypasses status, equal cache requires fresh
 * R32/SY. Source NEXT is byte0x1c. Each head store precedes a fresh discarded
 * descriptor read at index*16+8. Last NEXT clears as FOUR separate WRITE8 events,
 * then in -> optional tail clear -> available -> original head return.
 * Unlike normal packet bulk, there is no budget or linked-index mirror.
 * Snapshot validation and one-callback/error/cancel contracts match other DMA
 * leaves. Missing owners are reached dependencies; explicit null nodes fail.
 * No OS/cache/unmap/helper stubs, packet ownership or physical RX claim. */
int axd_dma_rx_status_list_begin(struct axd_dma_rx_status_list *,uint32_t force_all,const struct axd_rx_status_list_context *);
enum axd_rx_status_list_phase axd_dma_rx_status_list_step(struct axd_dma_rx_status_list *,axd_rx_status_list_io_fn,void *);
enum axd_rx_status_list_phase axd_dma_rx_status_list_cancel(struct axd_dma_rx_status_list *);
#endif
