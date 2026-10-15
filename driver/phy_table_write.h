/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_TABLE_WRITE_H
#define AXD_PHY_TABLE_WRITE_H
#include "phy_table.h"

struct axd_phy_table_write_request {
    uint32_t table_id, count, offset, width;
    const uint8_t *data;
    size_t data_length;
};
enum axd_phy_table_write_phase {
    AXD_TW_INVALID = 0, AXD_TW_RUNNING, AXD_TW_TRACE_COMPLETE,
    AXD_TW_DEPENDENCY, AXD_TW_FAILED, AXD_TW_CANCELLED
};
enum axd_phy_table_write_error {
    AXD_TW_NO_ERROR = 0, AXD_TW_BAD_ARGUMENT, AXD_TW_BAD_DATA,
    AXD_TW_UNSUPPORTED, AXD_TW_PHY_ERROR, AXD_TW_MAC_ERROR, AXD_TW_IO_ERROR
};
enum axd_phy_table_write_dependency {
    AXD_TW_NO_DEPENDENCY = 0, AXD_TW_MAC_CONTEXT, AXD_TW_MAC_CALLOUT
};
struct axd_phy_table_write {
    enum axd_phy_table_write_phase phase;
    enum axd_phy_table_write_error error;
    enum axd_phy_table_write_dependency dependency;
    struct axd_phy_table_context context;
    struct axd_phy_table_write_request request;
    struct axd_phy_register phy_job;
    struct axd_psm_control mac_job;
    uint32_t initial_control, index;
    uint16_t packed_selector, packed_value;
    unsigned stage, after_child, queued, cursor, stride, part, parts;
    unsigned direct, changed_guard, flush_reads, pending_callouts, queued_data;
    struct axd_d11_operation operations[4];
    uint64_t attempted_ops, completed_ops, data_writes;
};
/* Actual AC table-write wrapper: physical maincontrol/SY, optional real PSM
 * suspend, PHY19e guard, table headers/data, conditional guard clear and real
 * PSM enable. Target core130.0/1, AC PHY51. Actual HWACI disable/restore leaves
 * in this stock input are proven no-ops. No PHY nesting/lock is invented.
 * Supported widths8/16/32/42/48/60/64 consume little-endian byte strides
 * 1/2/4/6/6/8/8. All supplied bits are written, including width42/60 padding.
 * ID/offset writes use low16; count0 still writes both headers. Unsupported
 * widths/short data reject before I/O (a stricter policy than stock).
 * Payload storage is borrowed immutable, outside all jobs until terminal;
 * at least count*stride bytes must exist. No vendor payload is embedded.
 */
int axd_phy_table_write_begin(struct axd_phy_table_write *,
                               const struct axd_phy_table_write_request *,
                               const struct axd_phy_table_context *);
/* Exact direct phy_utils_write_phyreg leaf for selector<c000, including the
 * u16 counter/threshold flush and packed ST/W32(3fc) write. This entry does
 * not invoke the table wrapper/MAC/guard. High selectors are unsupported here.
 */
int axd_phy_packed_write_begin(struct axd_phy_table_write *, uint16_t selector,
                                uint16_t value, const struct axd_phy_table_context *);
/* Inputs are snapshotted before clearing, including own aliases. One callback
 * maximum per step; pre-I/O counter stores survive errors. Error wins cancel;
 * successful cancelling callbacks retain effects. Terminal states are silent.
 * No cleanup/retry/rollback, resource acquisition, MMIO or readiness claim.
 */
enum axd_phy_table_write_phase axd_phy_table_write_step(struct axd_phy_table_write *,
                                                        axd_d11_io_fn, void *);
enum axd_phy_table_write_phase axd_phy_table_write_cancel(struct axd_phy_table_write *);
#endif
