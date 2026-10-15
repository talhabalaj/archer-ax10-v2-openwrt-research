/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_TABLE_H
#define AXD_PHY_TABLE_H
#include "phy_register.h"
#include "psm_control.h"

struct axd_phy_table_context {
    struct axd_phy_context phy;
    struct axd_psm_context mac;
    uint32_t shared_bus_word;
    uint16_t write_threshold;
    uint8_t mac_context_valid;
};
enum axd_phy_table_phase {
    AXD_TABLE_INVALID = 0, AXD_TABLE_RUNNING, AXD_TABLE_TRACE_COMPLETE,
    AXD_TABLE_DEPENDENCY, AXD_TABLE_FAILED, AXD_TABLE_CANCELLED
};
enum axd_phy_table_error {
    AXD_TABLE_NO_ERROR = 0, AXD_TABLE_BAD_ARGUMENT,
    AXD_TABLE_PHY_ERROR, AXD_TABLE_MAC_ERROR, AXD_TABLE_IO_ERROR
};
enum axd_phy_table_dependency {
    AXD_TABLE_NO_DEPENDENCY = 0, AXD_TABLE_MAC_CONTEXT, AXD_TABLE_MAC_CALLOUT
};
struct axd_phy_table {
    enum axd_phy_table_phase phase;
    enum axd_phy_table_error error;
    enum axd_phy_table_dependency dependency;
    struct axd_phy_table_context context;
    struct axd_phy_register phy_job;
    struct axd_psm_control mac_job;
    uint32_t index, result, initial_control, last_read;
    uint16_t low_half, high_half;
    unsigned low_valid, word_valid, changed_guard, flush_reads;
    unsigned stage, after_child, queued, cursor, pending_callouts;
    struct axd_d11_operation operations[4];
    uint64_t attempted_ops, completed_ops;
};
/* Exact table0x29/count1/width32 wrapper for core130 minor0/1 AC PHY51.
 * Index is original u32; physical header writes use its low16, no c000 offset.
 * Caller owns stable powered PHY/D11 resources and serialization. No map/lock
 * or PHY-register enter/exit is synthesized. Shared bus word/threshold are
 * supplied actual inputs; no chip-derived defaults. MAC context may be absent
 * only until sampled maincontrol bit0(mask1) requires real PSM suspend/enable.
 * Those jobs' failures/policies/dependencies propagate; no successful stubs.
 * low_valid commits after successful low-read SY; word_valid/result commit
 * after high-read SY, including cancellation there. Neither means the wrapper
 * finished guard clear/MAC enable. A partial job performs no invented cleanup.
 * This does not implement other table IDs/widths/counts or PHY51 extra TSSI slots.
 * flush_reads counts successful version-read callbacks, before their SY.
 * changed_guard records a successful guard-set data write, not restored state.
 */
int axd_phy_table_begin(struct axd_phy_table *job, uint32_t index,
                        const struct axd_phy_table_context *context);
/* One callback maximum per step, including child work. Successful callback
 * cancellation preserves effects; coincident error wins. Counter increments
 * and flush-reset are pre-I/O software operations and remain on failure.
 * All terminal states are silent. No retries/rollback. Single-threaded;
 * callback may cancel, not reenter begin/step or mutate job fields.
 */
enum axd_phy_table_phase axd_phy_table_step(struct axd_phy_table *job,
                                           axd_d11_io_fn io, void *context);
enum axd_phy_table_phase axd_phy_table_cancel(struct axd_phy_table *job);
#endif
