/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_TABLE_READ_H
#define AXD_PHY_TABLE_READ_H
#include "phy_table_write.h"
struct axd_phy_table_read_request {
    uint32_t table_id,count,offset,width;
    uint8_t *data;
    size_t data_length;
};
enum axd_phy_table_read_phase {AXD_TR_INVALID=0,AXD_TR_RUNNING,
    AXD_TR_TRACE_COMPLETE,AXD_TR_DEPENDENCY,AXD_TR_FAILED,AXD_TR_CANCELLED};
enum axd_phy_table_read_error {AXD_TR_NO_ERROR=0,AXD_TR_BAD_ARGUMENT,
    AXD_TR_UNSUPPORTED,AXD_TR_BAD_DATA,AXD_TR_CHILD_ERROR,AXD_TR_IO_ERROR};
enum axd_phy_table_read_dependency {AXD_TR_NO_DEPENDENCY=0,
    AXD_TR_MAC_CONTEXT,AXD_TR_MAC_CALLOUT};
struct axd_phy_table_read {
    enum axd_phy_table_read_phase phase;enum axd_phy_table_read_error error;
    enum axd_phy_table_read_dependency dependency;
    struct axd_phy_table_context context;
    struct axd_phy_table_read_request request;
    struct axd_phy_table_write packed_job;
    struct axd_phy_register phy_job;
    struct axd_psm_control mac_job;
    uint32_t initial_control,index;
    uint16_t sample;
    unsigned stage,after_child,changed_guard,sample_valid,bytes_completed,pending_callouts;
    uint64_t attempted_ops,completed_ops;
};
/* Actual current AC wrapper and generic width8 reader, core130.0/1 PHY51.
 * Arbitrary table ID/count/offset use low16 physical headers. Count0 still
 * executes real maincontrol/guard/header protocol. Other widths stop before
 * I/O rather than silently replacing the existing table29/width32 primitive.
 * Output is caller-owned mutable storage outside this reader job, with at
 * least count bytes; it remains valid until terminal. Context/request aliases
 * are snapshotted. Output is stored only after each actual PHY read returns,
 * separately from its final SY callback; cancellation preserves prior bytes.
 * No fake reads, hardware mapping, table geometry or resource lifetime supplied.
 */
int axd_phy_table_read_begin(struct axd_phy_table_read *,const struct axd_phy_table_read_request *,const struct axd_phy_table_context *);
/* At most one callback per step; canonical child state is current before I/O
 * and after every step. Error wins cancellation, successful effects remain,
 * and terminal states are silent without cleanup/retry/rollback. */
enum axd_phy_table_read_phase axd_phy_table_read_step(struct axd_phy_table_read *,axd_d11_io_fn,void *);
enum axd_phy_table_read_phase axd_phy_table_read_cancel(struct axd_phy_table_read *);
#endif
