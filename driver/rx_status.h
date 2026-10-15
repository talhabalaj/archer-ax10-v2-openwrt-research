/* SPDX-License-Identifier: MIT */
#ifndef AXD_RX_STATUS_H
#define AXD_RX_STATUS_H
#include "dma_rx_status_list.h"
#include "wake_policy.h"
#include "offload_status.h"
struct axd_rx_status_context {
    struct axd_rx_status_list_context list;
    struct axd_wake_u32 pool;
    uint32_t error_count, work_limit;
    uint8_t nodes_owned, data_owned, pool_owned, os_owned;
    struct axd_wake_u32 offload_callback, offload_argument;
    struct axd_offload_status_context offload;
};
struct axd_rx_status_output { uint32_t head, tail, count; };
enum axd_rx_status_kind {
    AXD_RS_LIST=1, AXD_RS_NODE_READ32, AXD_RS_NODE_WRITE32, AXD_RS_NODE_WRITE8,
    AXD_RS_INDEX_READ16, AXD_RS_DATA_READ16, AXD_RS_POOL_READ32,
    AXD_RS_POOL_WRITE32, AXD_RS_DELAY_US, AXD_RS_OFFLOAD
};
struct axd_rx_status_operation {
    enum axd_rx_status_kind kind;
    struct axd_rx_status_list_operation list;
    struct axd_offload_status_operation offload;
    uint32_t identity, offset, value;
};
struct axd_rx_status_reply { uint32_t value; struct axd_offload_status_reply offload; };
typedef int (*axd_rx_status_io_fn)(void *,const struct axd_rx_status_operation *,struct axd_rx_status_reply *);
enum axd_rx_status_phase {
    AXD_RS_INVALID=0, AXD_RS_RUNNING, AXD_RS_TRACE_COMPLETE,
    AXD_RS_DEPENDENCY, AXD_RS_FAILED, AXD_RS_CANCELLED
};
enum axd_rx_status_error {
    AXD_RS_NO_ERROR=0, AXD_RS_BAD_ARGUMENT, AXD_RS_BINDING,
    AXD_RS_BOUNDS, AXD_RS_CHILD_ERROR, AXD_RS_IO_ERROR
};
enum axd_rx_status_dependency {
    AXD_RS_NO_DEPENDENCY=0, AXD_RS_LIST_DEPENDENCY, AXD_RS_NODE_OWNER,
    AXD_RS_DATA_OWNER, AXD_RS_OS_OWNER, AXD_RS_POOL_BINDING,
    AXD_RS_POOL_OWNER, AXD_RS_REGISTERED_POOL_CALLBACK, AXD_RS_OFFLOAD_PATH
};
struct axd_rx_status {
    enum axd_rx_status_phase phase;
    enum axd_rx_status_error error;
    enum axd_rx_status_dependency dependency;
    struct axd_rx_status_context context;
    struct axd_rx_status_output output;
    struct axd_dma_rx_status_list list_job;
    struct axd_offload_status offload_job;
    uint32_t head, current, accepted_tail, next, data, pool_node;
    uint32_t pool_count, pool_highwater, pool_head, callback, callback_argument;
    uint16_t length_sample, pool_index;
    unsigned stage, byte_cursor, poll_remaining, visited_nodes, pool_visits;
    unsigned length_reads, delays, returned_nodes, result_stores;
    uint64_t attempted_ops, completed_ops;
};
/* Current _dma_sts_rx composed with its actual closed status-list helper.
 * Every node/data/pool read is fresh; offset tags describe current semantic
 * fields and are not a portable kernel ABI. Timeout NEXT is cleared by FOUR
 * W8 requests before the real inlined pool-return stores. A nonnull pool
 * callback and index!=-1 reads its actual argument, then composes the real
 * offload release child only when both labels match caller-supplied known
 * registration facts. Unknown implementations remain explicit dependencies.
 * On child completion the two actual index bytes are cleared before pool count.
 * OS delay is an actual imported boundary. Owners provide serialized object
 * capacity, actual observations and effects, not implicit zero/write echoes.
 * Work limits bound hostile node/pool cycles. Error/cancel is terminal, no
 * invented reclaim, cleanup or retry. Partial output count/stores survive.
 */
int axd_rx_status_begin(struct axd_rx_status *,const struct axd_rx_status_output *,const struct axd_rx_status_context *);
enum axd_rx_status_phase axd_rx_status_step(struct axd_rx_status *,axd_rx_status_io_fn,void *);
enum axd_rx_status_phase axd_rx_status_cancel(struct axd_rx_status *);
#endif
