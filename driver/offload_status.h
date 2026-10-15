/* SPDX-License-Identifier: MIT */
#ifndef AXD_OFFLOAD_STATUS_H
#define AXD_OFFLOAD_STATUS_H
#include "si_switch.h"
struct axd_offload_status_context {
    uint32_t count, consumer, producer, bitmap, bitmap_bytes, registers;
    struct axd_si_switch_context si;
    uint8_t bitmap_owned, registers_owned, os_owned, si_known;
};
enum axd_offload_status_kind { AXD_OS_BITMAP_READ8=1, AXD_OS_BITMAP_WRITE8, AXD_OS_STORE_BARRIER, AXD_OS_WRITE32, AXD_OS_SI };
struct axd_offload_status_operation {
    enum axd_offload_status_kind kind;
    uint32_t identity, offset, value;
    struct axd_si_switch_operation si;
};
struct axd_offload_status_reply { uint32_t value; struct axd_si_switch_reply si; };
typedef int (*axd_offload_status_io_fn)(void *,const struct axd_offload_status_operation *,struct axd_offload_status_reply *);
enum axd_offload_status_phase { AXD_OS_INVALID=0, AXD_OS_RUNNING, AXD_OS_TRACE_COMPLETE, AXD_OS_DEPENDENCY, AXD_OS_FAILED, AXD_OS_CANCELLED };
enum axd_offload_status_error { AXD_OS_NO_ERROR=0, AXD_OS_BAD_ARGUMENT, AXD_OS_BOUNDS, AXD_OS_BINDING, AXD_OS_CHILD_ERROR, AXD_OS_IO_ERROR };
enum axd_offload_status_dependency { AXD_OS_NO_DEPENDENCY=0, AXD_OS_BITMAP_OWNER, AXD_OS_REGISTER_OWNER, AXD_OS_OS_OWNER, AXD_OS_SI_BINDING, AXD_OS_SI_CHILD };
struct axd_offload_status {
    enum axd_offload_status_phase phase;
    enum axd_offload_status_error error;
    enum axd_offload_status_dependency dependency;
    struct axd_offload_status_context context;
    struct axd_si_switch si_job;
    uint32_t index, old_consumer, saved_index, irq_token;
    uint8_t bitmap_sample;
    unsigned stage, after_si, scanned, consumer_stores, register_writes, si_state_valid;
    uint64_t attempted_ops, completed_ops;
};
/* Actual wlc_offload_release_rxstatus: byte RMW, source-ordered consumer stores,
 * active-bit/producer scan and ST/W32(+20), with real HND switch/restore children
 * when bus1. No invented map/readback/final barrier. Serialized software fields
 * are copied; bitmap reads are fresh. IRQ effects update actual SI state only.
 * Policy narrows stock unchecked memory: count power-of-two 2..65536, valid
 * cursors, nonnegative in-range index, adequate byte capacity and bounded scan.
 * Ownership/identity/SI checks are lazy, at actual use. No hardware backend or
 * proof of physical buffer reuse. Each step makes <=1 callback; terminals stay
 * silent. Successful callback samples/IRQ state survive cancellation; callback
 * error wins. No automatic cleanup/restore/retry after a terminal exit.
 */
int axd_offload_status_begin(struct axd_offload_status *,int32_t index,const struct axd_offload_status_context *);
enum axd_offload_status_phase axd_offload_status_step(struct axd_offload_status *,axd_offload_status_io_fn,void *);
enum axd_offload_status_phase axd_offload_status_cancel(struct axd_offload_status *);
#endif
