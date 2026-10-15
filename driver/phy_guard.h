/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_GUARD_H
#define AXD_PHY_GUARD_H
#include "psm_control.h"

enum axd_phy_guard_command { AXD_PHY_ENTER = 1, AXD_PHY_EXIT };
struct axd_phy_guard_context {
    struct axd_psm_context mac;
    struct axd_psm_policy policy;
    uint8_t depth;
};
enum axd_phy_guard_phase {
    AXD_GUARD_INVALID = 0, AXD_GUARD_RUNNING, AXD_GUARD_TRACE_COMPLETE,
    AXD_GUARD_DEPENDENCY, AXD_GUARD_FAILED, AXD_GUARD_CANCELLED
};
enum axd_phy_guard_error {
    AXD_GUARD_NO_ERROR = 0, AXD_GUARD_BAD_ARGUMENT, AXD_GUARD_BAD_NESTING,
    AXD_GUARD_PSM_ERROR, AXD_GUARD_IO_ERROR
};
struct axd_phy_guard {
    enum axd_phy_guard_phase phase;
    enum axd_phy_guard_error error;
    enum axd_phy_guard_command command;
    struct axd_phy_guard_context context;
    struct axd_psm_control psm_job;
    unsigned stage, pending_callouts;
    uint64_t attempted_ops, completed_ops;
};
/* Actual PHY-register byte nesting and reason2 wake-override wrappers. Reject
 * enter255/exit0 before wrapping; stock unchecked byte arithmetic can wrap.
 * Only outer enter/exit execute override SET/CLEAR; inner transitions do noI/O.
 * Software depth changes precede override work and survive failure/cancellation.
 * No resource acquisition, locking, MAC suspend/enable or fake wake policy.
 */
int axd_phy_guard_begin(struct axd_phy_guard *job, enum axd_phy_guard_command command,
                        const struct axd_phy_guard_context *context);
/* One user callback per step maximum. Child context, counters and pending
 * callouts propagate on every step. Error wins over cancellation; successful
 * cancellation effects survive. Terminal states are silent, no rollback/retry.
 */
enum axd_phy_guard_phase axd_phy_guard_step(struct axd_phy_guard *job,
                                           axd_d11_io_fn io, void *context);
enum axd_phy_guard_phase axd_phy_guard_cancel(struct axd_phy_guard *job);
#endif
