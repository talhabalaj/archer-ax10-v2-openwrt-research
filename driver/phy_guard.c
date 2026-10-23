/* SPDX-License-Identifier: MIT */
#include "phy_guard.h"

enum stage { ENTRY, CHILD };
struct relay { struct axd_phy_guard *job; axd_d11_io_fn io; void *context; };
static void fail(struct axd_phy_guard *job, enum axd_phy_guard_error error)
{
    job->phase = AXD_GUARD_FAILED;
    job->error = error;
}
static int perform(void *opaque, const struct axd_d11_operation *operation, uint32_t *value)
{
    struct relay *relay = opaque;
    if (relay->io == NULL) {
        fail(relay->job, AXD_GUARD_BAD_ARGUMENT);
        return -1;
    }
    relay->job->attempted_ops++;
    if (relay->io(relay->context, operation, value) != 0) {
        fail(relay->job, AXD_GUARD_IO_ERROR);
        return -1;
    }
    relay->job->completed_ops++;
    return 0;
}
int axd_phy_guard_begin(struct axd_phy_guard *job, enum axd_phy_guard_command command,
                        const struct axd_phy_guard_context *context)
{
    static const struct axd_phy_guard empty={0};
    struct axd_phy_guard_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) { snapshot = *context; context = &snapshot; }
    *job = empty;
    fail(job, AXD_GUARD_BAD_ARGUMENT);
    if (context == NULL || command < AXD_PHY_ENTER || command > AXD_PHY_EXIT)
        return -1;
    job->context = *context;
    if (axd_psm_control_begin_policy(&job->psm_job, AXD_PSM_WAIT_WAKE,
                                     &context->mac, &context->policy) != 0) {
        fail(job, AXD_GUARD_PSM_ERROR);
        return -1;
    }
    job->command = command;
    job->stage = ENTRY;
    job->phase = AXD_GUARD_RUNNING;
    job->error = AXD_GUARD_NO_ERROR;
    return 0;
}
enum axd_phy_guard_phase axd_phy_guard_step(struct axd_phy_guard *job,
                                           axd_d11_io_fn io, void *context)
{
    struct relay relay = {job, io, context};
    enum axd_psm_phase phase;
    if (job == NULL)
        return AXD_GUARD_INVALID;
    if (job->phase != AXD_GUARD_RUNNING)
        return job->phase;
    if (job->stage == ENTRY) {
        if ((job->command == AXD_PHY_ENTER && job->context.depth == UINT8_MAX) ||
            (job->command == AXD_PHY_EXIT && job->context.depth == 0)) {
            fail(job, AXD_GUARD_BAD_NESTING);
            return job->phase;
        }
        if (job->command == AXD_PHY_ENTER)
            job->context.depth++;
        else
            job->context.depth--;
        if ((job->command == AXD_PHY_ENTER && job->context.depth != 1) ||
            (job->command == AXD_PHY_EXIT && job->context.depth != 0)) {
            job->phase = AXD_GUARD_TRACE_COMPLETE;
        } else if (axd_psm_override_begin(&job->psm_job, job->command == AXD_PHY_ENTER,
                                         2, &job->context.mac, &job->context.policy) != 0) {
            fail(job, AXD_GUARD_PSM_ERROR);
        } else {
            job->stage = CHILD;
        }
        return job->phase;
    }
    phase = axd_psm_control_step(&job->psm_job, perform, &relay);
    job->context.mac = job->psm_job.context;
    job->pending_callouts |= job->psm_job.pending_callouts;
    if (job->phase != AXD_GUARD_RUNNING)
        return job->phase;
    if (phase == AXD_PSM_DONE)
        job->phase = AXD_GUARD_TRACE_COMPLETE;
    else if (phase == AXD_PSM_DEPENDENCY)
        job->phase = AXD_GUARD_DEPENDENCY;
    else if (phase == AXD_PSM_CANCELLED)
        job->phase = AXD_GUARD_CANCELLED;
    else if (phase != AXD_PSM_RUNNING)
        fail(job, AXD_GUARD_PSM_ERROR);
    return job->phase;
}
enum axd_phy_guard_phase axd_phy_guard_cancel(struct axd_phy_guard *job)
{
    if (job == NULL)
        return AXD_GUARD_INVALID;
    if (job->phase == AXD_GUARD_RUNNING) {
        job->phase = AXD_GUARD_CANCELLED;
        if (job->stage == CHILD)
            axd_psm_control_cancel(&job->psm_job);
    }
    return job->phase;
}
