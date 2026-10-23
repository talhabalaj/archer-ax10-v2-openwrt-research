/* SPDX-License-Identifier: MIT */
#ifndef AXD_DMA_RX_STATUS_FILL_H
#define AXD_DMA_RX_STATUS_FILL_H
#include "dma_rx_status_list.h"
#include "wake_policy.h"
struct axd_rx_status_fill_context {
    struct axd_rx_status_list_context rx;
    struct axd_wake_u32 pool;
    uint32_t flags,receive_posts,no_buffer_count,data_offset_low,data_offset_high;
    uint16_t buffer_size;
    uint8_t disabled,pool_owned,data_owned,os_owned;
};
enum axd_rx_status_fill_kind {
    AXD_RSF_READ32=1,AXD_RSF_WRITE32,AXD_RSF_WRITE16,AXD_RSF_WRITE8,
    AXD_RSF_TRANSLATE,AXD_RSF_STORE_BARRIER,AXD_RSF_FULL_BARRIER
};
enum axd_rx_status_fill_resource {
    AXD_RSF_POOL=1,AXD_RSF_NODE,AXD_RSF_DATA,AXD_RSF_DESCRIPTOR,AXD_RSF_CHANNEL
};
struct axd_rx_status_fill_operation {
    enum axd_rx_status_fill_kind kind;
    enum axd_rx_status_fill_resource resource;
    uint32_t identity,offset,value;
};
typedef int (*axd_rx_status_fill_io_fn)(void *,const struct axd_rx_status_fill_operation *,uint32_t *);
enum axd_rx_status_fill_phase {
    AXD_RSF_INVALID=0,AXD_RSF_RUNNING,AXD_RSF_TRACE_COMPLETE,
    AXD_RSF_DEPENDENCY,AXD_RSF_FAILED,AXD_RSF_CANCELLED
};
enum axd_rx_status_fill_error {
    AXD_RSF_NO_ERROR=0,AXD_RSF_BAD_ARGUMENT,AXD_RSF_BINDING,AXD_RSF_BOUNDS,
    AXD_RSF_UNSUPPORTED,AXD_RSF_IO_ERROR
};
enum axd_rx_status_fill_dependency {
    AXD_RSF_NO_DEPENDENCY=0,AXD_RSF_POOL_BINDING,AXD_RSF_POOL_OWNER,
    AXD_RSF_NODE_OWNER,AXD_RSF_DATA_OWNER,AXD_RSF_OS_OWNER,
    AXD_RSF_DESCRIPTOR_OWNER,AXD_RSF_CHANNEL_OWNER
};
struct axd_dma_rx_status_fill {
    enum axd_rx_status_fill_phase phase;
    enum axd_rx_status_fill_error error;
    enum axd_rx_status_fill_dependency dependency;
    struct axd_rx_status_fill_context context;
    struct axd_dma64_descriptor descriptor;
    uint32_t target,needed,index,node,next,tail,data,physical;
    uint32_t pool_count,pool_failures,status_sample,producer_sample,parity_sample,last_read,producer,return_value;
    unsigned stage,cursor,byte_cursor,return_valid,taken_nodes,prepared_nodes,completed_nodes;
    unsigned pop_bytes,post_bytes,index_writes,list_links,head_stores,tail_stores;
    unsigned descriptor_writes,parity_reads,no_buffer_committed,producer_written,out_committed,idle_observed;
    uint64_t attempted_ops,completed_ops;
};
/* Actual current dma_sts_rxfill: complete inline status-pool pop, eight total
 * NEXT byte clears per node, real virt_to_phys OS service, index16 and bulk
 * link stores, exact descriptor updater effects and ST->producer->software out.
 * All reads are fresh typed observations. Pool/serialized nodes/data/capacities
 * and actual translation belong to the runtime; no allocator/helper success
 * callback or inferred cache/DMA mapping is supplied. Pool-empty counts a pool
 * failure, first-empty executes the real idle R32/SY/R32/SY observations, and
 * missing-buffer accounting follows. The current body DISCARDS idle's result:
 * only outstanding>target returns1; all normal/no-work/disabled/failure returns0.
 * Core130.0/1 strict ring/capacity/null policy narrows unchecked stock behavior.
 * Count0 with zero cursors still publishes unchanged producer. Own context
 * aliases are snapshotted, at most1 callback/step, errors win cancellation,
 * successful effects survive, later stages stop, terminal calls stay silent.
 * No invented cleanup, ownership transfer, readiness or physical RX claim. */
int axd_dma_rx_status_fill_begin(struct axd_dma_rx_status_fill *,const struct axd_rx_status_fill_context *);
enum axd_rx_status_fill_phase axd_dma_rx_status_fill_step(struct axd_dma_rx_status_fill *,axd_rx_status_fill_io_fn,void *);
enum axd_rx_status_fill_phase axd_dma_rx_status_fill_cancel(struct axd_dma_rx_status_fill *);
#endif
