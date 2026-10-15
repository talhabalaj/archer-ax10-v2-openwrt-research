/* SPDX-License-Identifier: MIT */
#ifndef AXD_IDLE_TSSI_H
#define AXD_IDLE_TSSI_H
#include "phy_guard.h"
#include "phy_tssi.h"

struct axd_idle_tssi_context {
    struct axd_tssi_context tssi;
    uint8_t phy_depth, callback_present;
    uint8_t callback_binding_required;
    uint32_t phy_identity;
    struct axd_wake_u32 callback_context, callback_phy;
};
enum axd_idle_phase {
    AXD_IDLE_INVALID = 0, AXD_IDLE_RUNNING, AXD_IDLE_TRACE_COMPLETE,
    AXD_IDLE_DEPENDENCY, AXD_IDLE_FAILED, AXD_IDLE_CANCELLED
};
enum axd_idle_error {
    AXD_IDLE_NO_ERROR = 0, AXD_IDLE_BAD_ARGUMENT, AXD_IDLE_MAC_CONTEXT,
    AXD_IDLE_MAC_ERROR, AXD_IDLE_GUARD_ERROR, AXD_IDLE_TSSI_ERROR, AXD_IDLE_IO_ERROR,
    AXD_IDLE_CALLBACK_BINDING
};
enum axd_idle_binding_field { AXD_IDLE_NO_BINDING = 0, AXD_IDLE_CONTEXT, AXD_IDLE_CONTEXT_PHY };
struct axd_idle_tssi {
    enum axd_idle_phase phase;
    enum axd_idle_error error;
    enum axd_tssi_command command;
    struct axd_idle_tssi_context context;
    struct axd_psm_control mac_job;
    struct axd_phy_guard guard_job;
    struct axd_phy_tssi tssi_job;
    unsigned stage, after_child, pending_callouts;
    unsigned entered, exited, callback_completed;
    enum axd_idle_binding_field binding_field;
    unsigned binding_accesses;
    uint64_t attempted_ops, completed_ops;
};
/* Actual enclosing idle-TSSI wrapper: optional callback absent returns without
 * touching MAC/PHY/buffer. Present callback runs MAC suspend, PHY enter,
 * all-core callback, PHY exit, MAC enable. Context includes explicit MAC and
 * policy facts; known-down policy is a proven no-op, unknown policy dependency.
 * Present RESTORE prevalidates active saved slots before any I/O (stricter than
 * stock buffer). Caller still owns powered/mapped/serialized resources.
 * Inactive MAC run-bit guards are preserved by actual PSM children, not an
 * unconditional requirement for running microcode. No automatic unwind.
 * Optional callback_binding_required checks the actual late callback context
 * after suspend/PHY enter: context must be known nonnull, its first-word PHY
 * must equal phy_identity. Reached missing fields end DEPENDENCY; invalid/null
 * or mismatched fields fail CALLBACK_BINDING. Legacy flag0 retains the owner's
 * normalized callback/resource-coherence obligation, not raw pointer validation.
 */
int axd_idle_tssi_begin(struct axd_idle_tssi *job, enum axd_tssi_command command,
                        const struct axd_idle_tssi_context *context);
/* At most one user callback per step. All child partial context and pending
 * callouts propagate, including software depth already changed at a failed
 * override. Error wins over cancel; successful-cancel effects survive.
 * Terminal states silent; callback may cancel, not reenter/mutate job fields.
 */
enum axd_idle_phase axd_idle_tssi_step(struct axd_idle_tssi *job,
                                       axd_d11_io_fn io, void *context);
enum axd_idle_phase axd_idle_tssi_cancel(struct axd_idle_tssi *job);
#endif
