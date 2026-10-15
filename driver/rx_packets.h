/* SPDX-License-Identifier: MIT */
#ifndef AXD_RX_PACKETS_H
#define AXD_RX_PACKETS_H
#include "dma_rx_list.h"
#include "wake_policy.h"
#include "rx_status.h"

struct axd_rx_packets_context {
    struct axd_rx_list_context list;
    struct axd_wake_u32 receive_offset, buffer_size, flags, os_context, status_context;
    struct axd_wake_u32 status_binding;
    struct axd_rx_status_context status;
    struct axd_rx_status_output status_output;
    uint8_t status_output_known;
    uint32_t work_limit;
    uint32_t error_count;
    uint8_t packets_owned, data_owned, os_owned;
};
struct axd_rx_packets_summary { uint32_t head, tail, count; };
struct axd_rx_packets_request {
    uint32_t budget, status_output;
    struct axd_rx_packets_summary output;
};
enum axd_rx_packet_field {
    AXD_RP_FRAGMENT = 1, AXD_RP_NEXT, AXD_RP_DATA, AXD_RP_LENGTH,
    AXD_RP_NONLINEAR, AXD_RP_TAIL
};
enum axd_rx_packets_kind {
    AXD_RP_LIST = 1, AXD_RP_PACKET_READ32, AXD_RP_PACKET_WRITE32,
    AXD_RP_DATA_READ16, AXD_RP_DATA_READ32, AXD_RP_DELAY_US,
    AXD_RP_TRIM, AXD_RP_FREE, AXD_RP_STATUS
};
struct axd_rx_packets_operation {
    enum axd_rx_packets_kind kind;
    struct axd_rx_list_operation list;
    struct axd_rx_status_operation status;
    enum axd_rx_packet_field field;
    uint32_t packet, data, value, os_context;
};
struct axd_rx_packets_reply { uint32_t value;struct axd_rx_status_reply status; };
typedef int (*axd_rx_packets_io_fn)(void *, const struct axd_rx_packets_operation *, struct axd_rx_packets_reply *);
enum axd_rx_packets_phase {
    AXD_RP_INVALID = 0, AXD_RP_RUNNING, AXD_RP_TRACE_COMPLETE,
    AXD_RP_DEPENDENCY, AXD_RP_FAILED, AXD_RP_CANCELLED
};
enum axd_rx_packets_error {
    AXD_RP_NO_ERROR = 0, AXD_RP_BAD_ARGUMENT, AXD_RP_BINDING,
    AXD_RP_BOUNDS, AXD_RP_CHILD_ERROR, AXD_RP_IO_ERROR
};
enum axd_rx_packets_dependency {
    AXD_RP_NO_DEPENDENCY = 0, AXD_RP_METADATA, AXD_RP_LIST_DEPENDENCY,
    AXD_RP_PACKET_OWNER, AXD_RP_DATA_OWNER, AXD_RP_OS_OWNER, AXD_RP_STATUS_PATH
};
enum axd_rx_packets_metadata {
    AXD_RP_NO_METADATA = 0, AXD_RP_RECEIVE_OFFSET, AXD_RP_BUFFER_SIZE,
    AXD_RP_FLAGS, AXD_RP_OS_CONTEXT, AXD_RP_STATUS_CONTEXT
};
struct axd_rx_packets {
    enum axd_rx_packets_phase phase;
    enum axd_rx_packets_error error;
    enum axd_rx_packets_dependency dependency;
    enum axd_rx_packets_metadata last_metadata;
    struct axd_rx_packets_context context;
    struct axd_rx_packets_request request;
    struct axd_rx_packets_summary output;
    struct axd_dma_rx_list list_job;
    struct axd_rx_status status_job;
    uint32_t head, current, accepted_tail, fragment_tail, next, data, trim_packet, free_packet;
    uint32_t length_sample, header_sample, trim_length, remaining;
    unsigned stage, after_list, after_trim, samples, delay_count, poll_remaining, fragments_left;
    unsigned visited_packets, freed_packets, trimmed_packets, result_stores;
    unsigned metadata_accesses;
    uint64_t attempted_ops, completed_ops;
};
/* Current dma_rx body, composing its actual bulk-list child. Packet field names
 * describe semantic ARM32 values, not ABI-compatible skb offsets. Every packet
 * field/data load is fresh; writes are real typed backend requests, not write
 * echoes. TRIM/FREE/DELAY are actual imported OS boundaries. The backend owns
 * serialized packets, capacities, allocations and those actual OS operations.
 * Source buffers and metadata stay coherent/immutable outside callback effects.
 * The optional status path composes the actual rx_status/list children;
 * unknown bindings or pool callbacks remain explicit dependencies; the known
 * offload registration composes its actual release/SI children. Typed nested
 * replies carry actual IRQ state, never an assumed unchanged context.
 * work_limit 1..65535 bounds hostile packet-link cycles; invalid negative or
 * over-buffer trims fail rather than reproduce unchecked stock corruption.
 * Output count is updated at each accepted packet; final head/tail stores wait
 * for the entire status tail. This is not BMAC RX metadata/IRQ/Linux delivery.
 */
int axd_rx_packets_begin(struct axd_rx_packets *, const struct axd_rx_packets_request *,
                         const struct axd_rx_packets_context *);
enum axd_rx_packets_phase axd_rx_packets_step(struct axd_rx_packets *, axd_rx_packets_io_fn, void *);
enum axd_rx_packets_phase axd_rx_packets_cancel(struct axd_rx_packets *);
#endif
