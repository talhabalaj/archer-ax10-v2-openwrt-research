/* SPDX-License-Identifier: MIT */
#ifndef AXD_DMA_RX_FILL_H
#define AXD_DMA_RX_FILL_H
#include "dma_rx_list.h"
#include "wake_policy.h"
struct axd_rx_fill_context {
    struct axd_rx_list_context rx;
    uint32_t flags,receive_posts,extra_headroom,no_buffer_count;
    uint32_t data_offset_low,data_offset_high,os_context;
    struct axd_wake_u32 allocator;
    uint16_t buffer_size;
    uint8_t disabled,split_receive,segment_mode;
    uint8_t os_owned,data_owned,aux_owned,slots_owned;
    uint32_t slots_identity,slot_count;
};
enum axd_rx_fill_kind {
    AXD_RXF_ALLOCATE=1,AXD_RXF_ACCOUNT,AXD_RXF_PULL,AXD_RXF_TRIM,
    AXD_RXF_TRANSLATE,AXD_RXF_READ32,AXD_RXF_WRITE32,
    AXD_RXF_STORE_BARRIER,AXD_RXF_FULL_BARRIER
};
enum axd_rx_fill_resource {
    AXD_RXF_ALLOCATOR=1,AXD_RXF_OS,AXD_RXF_PACKET,AXD_RXF_AUX,
    AXD_RXF_DATA,AXD_RXF_SLOT,AXD_RXF_DESCRIPTOR,AXD_RXF_CHANNEL
};
struct axd_rx_fill_operation {
    enum axd_rx_fill_kind kind;
    enum axd_rx_fill_resource resource;
    uint32_t identity,offset,value,argument;
};
typedef int (*axd_rx_fill_io_fn)(void *,const struct axd_rx_fill_operation *,uint32_t *);
enum axd_rx_fill_phase {
    AXD_RXF_INVALID=0,AXD_RXF_RUNNING,AXD_RXF_TRACE_COMPLETE,
    AXD_RXF_DEPENDENCY,AXD_RXF_FAILED,AXD_RXF_CANCELLED
};
enum axd_rx_fill_error {
    AXD_RXF_NO_ERROR=0,AXD_RXF_BAD_ARGUMENT,AXD_RXF_BOUNDS,AXD_RXF_BINDING,
    AXD_RXF_UNSUPPORTED,AXD_RXF_IO_ERROR
};
enum axd_rx_fill_dependency {
    AXD_RXF_NO_DEPENDENCY=0,AXD_RXF_SPLIT_PATH,AXD_RXF_SEGMENT_PATH,
    AXD_RXF_ALLOCATOR_BINDING,AXD_RXF_OS_OWNER,AXD_RXF_PACKET_OWNER,
    AXD_RXF_AUX_OWNER,AXD_RXF_DATA_OWNER,AXD_RXF_SLOT_OWNER,
    AXD_RXF_DESCRIPTOR_OWNER,AXD_RXF_CHANNEL_OWNER
};
struct axd_dma_rx_fill {
    enum axd_rx_fill_phase phase;
    enum axd_rx_fill_error error;
    enum axd_rx_fill_dependency dependency;
    struct axd_rx_fill_context context;
    struct axd_dma64_descriptor descriptor;
    uint32_t alignment,extra,allocation_size,target,needed,index,packet,aux;
    uint32_t packet_flags,data,length,nonlinear,aligned_length,physical,tail;
    uint32_t status_sample,producer_sample,parity_sample,last_read,producer,return_value;
    unsigned stage,cursor,return_valid,allocated_buffers,completed_buffers;
    unsigned descriptor_writes,parity_reads,slot_writes,list_links;
    unsigned head_stores,tail_stores,no_buffer_committed,producer_written,out_committed;
    uint64_t attempted_ops,completed_ops;
};
/* Complete current dma_rxfill normal packet path. Nonzero disabled byte returns
 * zero immediately. Split/header and segmented-buffer branches are explicit
 * reached dependencies. Packet fields are semantic current offsets, not a
 * portable skb ABI. OS requests are actual BPM allocation, packet accounting,
 * skb_pull/trim and virt_to_phys boundaries; no generic DMA-map/cache operation
 * is invented. Allocator is the explicit runtime gbpm_g+0x24 entry binding.
 * ALLOCATE(value=size,argument=0) returns an opaque packet label, zero on genuine
 * allocation failure. ACCOUNT uses OS identity with value1/argument1. PULL/TRIM
 * use packet identity/value length; TRANSLATE uses DATA identity and returns
 * the real ARM32 address. Backend owns allocations, capacity, OS effects and
 * coherent metadata. Every subsequent packet/descriptor read must be fresh.
 * First allocation failure executes real rxidle R32/SY/R32/SY; partial failures
 * retain buffers/descriptors and missing-buffer accounting. Source publication
 * is ST -> producer W32 -> software out, even when zero new buffers are needed.
 * One callback/step, error wins cancel, completed effects survive, terminals
 * are silent. No invented cleanup, packet ownership transfer or physical RX.
 */
int axd_dma_rx_fill_begin(struct axd_dma_rx_fill *,const struct axd_rx_fill_context *);
enum axd_rx_fill_phase axd_dma_rx_fill_step(struct axd_dma_rx_fill *,axd_rx_fill_io_fn,void *);
enum axd_rx_fill_phase axd_dma_rx_fill_cancel(struct axd_dma_rx_fill *);
#endif
