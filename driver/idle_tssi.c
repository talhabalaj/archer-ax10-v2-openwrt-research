/* SPDX-License-Identifier: MIT */
#include "idle_tssi.h"

enum stage { ENTRY, ENTER_PHY, CALLBACK, EXIT_PHY, ENABLE_MAC, FINISH, RUN_MAC, RUN_GUARD, RUN_TSSI };
struct relay { struct axd_idle_tssi *job; axd_d11_io_fn io; void *context; };
static void fail(struct axd_idle_tssi *job, enum axd_idle_error error)
{
    job->phase = AXD_IDLE_FAILED;
    job->error = error;
}
static int perform(void *opaque, const struct axd_d11_operation *operation, uint32_t *value)
{
    struct relay *relay = opaque;
    if (relay->io == NULL) {
        fail(relay->job, AXD_IDLE_BAD_ARGUMENT);
        return -1;
    }
    relay->job->attempted_ops++;
    if (relay->io(relay->context, operation, value) != 0) {
        fail(relay->job, AXD_IDLE_IO_ERROR);
        return -1;
    }
    relay->job->completed_ops++;
    return 0;
}
static void mac(struct axd_idle_tssi *job, enum axd_psm_command command, unsigned resume)
{
    if (axd_psm_control_begin_policy(&job->mac_job, command,
                                     &job->context.tssi.hardware.mac,
                                     &job->context.tssi.hardware.policy) != 0) {
        fail(job, AXD_IDLE_MAC_ERROR);
        return;
    }
    job->after_child = resume;
    job->stage = RUN_MAC;
}
static void guard(struct axd_idle_tssi *job, enum axd_phy_guard_command command, unsigned resume)
{
    const struct axd_phy_guard_context context = {
        job->context.tssi.hardware.mac, job->context.tssi.hardware.policy, job->context.phy_depth
    };
    if (axd_phy_guard_begin(&job->guard_job, command, &context) != 0) {
        fail(job, AXD_IDLE_GUARD_ERROR);
        return;
    }
    job->after_child = resume;
    job->stage = RUN_GUARD;
}
static void child_step(struct axd_idle_tssi *job, struct relay *relay)
{
    if (job->stage == RUN_MAC) {
        enum axd_psm_phase phase = axd_psm_control_step(&job->mac_job, perform, relay);
        job->context.tssi.hardware.mac = job->mac_job.context;
        job->pending_callouts |= job->mac_job.pending_callouts;
        if (job->phase != AXD_IDLE_RUNNING)
            return;
        if (phase == AXD_PSM_DONE)
            job->stage = job->after_child;
        else if (phase == AXD_PSM_DEPENDENCY)
            job->phase = AXD_IDLE_DEPENDENCY;
        else if (phase == AXD_PSM_CANCELLED)
            job->phase = AXD_IDLE_CANCELLED;
        else if (phase != AXD_PSM_RUNNING)
            fail(job, AXD_IDLE_MAC_ERROR);
    } else if (job->stage == RUN_GUARD) {
        enum axd_phy_guard_phase phase = axd_phy_guard_step(&job->guard_job, perform, relay);
        job->context.tssi.hardware.mac = job->guard_job.context.mac;
        job->context.phy_depth = job->guard_job.context.depth;
        job->pending_callouts |= job->guard_job.pending_callouts;
        if (job->phase != AXD_IDLE_RUNNING)
            return;
        if (phase == AXD_GUARD_TRACE_COMPLETE) {
            if (job->after_child == CALLBACK)
                job->entered = 1;
            else
                job->exited = 1;
            job->stage = job->after_child;
        } else if (phase == AXD_GUARD_DEPENDENCY)
            job->phase = AXD_IDLE_DEPENDENCY;
        else if (phase == AXD_GUARD_CANCELLED)
            job->phase = AXD_IDLE_CANCELLED;
        else if (phase != AXD_GUARD_RUNNING)
            fail(job, AXD_IDLE_GUARD_ERROR);
    } else {
        enum axd_tssi_phase phase = axd_phy_tssi_step(&job->tssi_job, perform, relay);
        job->context.tssi = job->tssi_job.context;
        job->pending_callouts |= job->tssi_job.pending_callouts;
        if (job->phase != AXD_IDLE_RUNNING)
            return;
        if (phase == AXD_TSSI_TRACE_COMPLETE) {
            job->callback_completed = 1;
            job->stage = EXIT_PHY;
        } else if (phase == AXD_TSSI_DEPENDENCY)
            job->phase = AXD_IDLE_DEPENDENCY;
        else if (phase == AXD_TSSI_CANCELLED)
            job->phase = AXD_IDLE_CANCELLED;
        else if (phase != AXD_TSSI_RUNNING)
            fail(job, AXD_IDLE_TSSI_ERROR);
    }
}
int axd_idle_tssi_begin(struct axd_idle_tssi *job, enum axd_tssi_command command,
                        const struct axd_idle_tssi_context *context)
{
    const struct axd_idle_tssi empty = {0};
    struct axd_idle_tssi_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) { snapshot = *context; context = &snapshot; }
    *job = empty;
    fail(job, AXD_IDLE_BAD_ARGUMENT);
    if (context == NULL || command < AXD_TSSI_SNAPSHOT || command > AXD_TSSI_RESTORE ||
        context->callback_present > 1)
        return -1;
    job->context = *context;
    if (context->callback_present) {
        if (!context->tssi.hardware.mac_context_valid) {
            fail(job, AXD_IDLE_MAC_CONTEXT);
            return -1;
        }
        if (axd_phy_tssi_begin(&job->tssi_job, command, &context->tssi) != 0) {
            fail(job, AXD_IDLE_TSSI_ERROR);
            return -1;
        }
        if (axd_psm_control_begin_policy(&job->mac_job, AXD_PSM_WAIT_WAKE,
                                         &context->tssi.hardware.mac, &context->tssi.hardware.policy) != 0) {
            fail(job, AXD_IDLE_MAC_ERROR);
            return -1;
        }
    }
    job->command = command;
    job->stage = ENTRY;
    job->phase = AXD_IDLE_RUNNING;
    job->error = AXD_IDLE_NO_ERROR;
    return 0;
}
enum axd_idle_phase axd_idle_tssi_step(struct axd_idle_tssi *job,
                                       axd_d11_io_fn io, void *context)
{
    struct relay relay = {job, io, context};
    if (job == NULL)
        return AXD_IDLE_INVALID;
    if (job->phase != AXD_IDLE_RUNNING)
        return job->phase;
    switch (job->stage) {
    case ENTRY:
        if (!job->context.callback_present)
            job->phase = AXD_IDLE_TRACE_COMPLETE;
        else
            mac(job, AXD_PSM_SUSPEND_MAIN, ENTER_PHY);
        break;
    case ENTER_PHY:
        guard(job, AXD_PHY_ENTER, CALLBACK);
        break;
    case CALLBACK:
        if (axd_phy_tssi_begin(&job->tssi_job, job->command, &job->context.tssi) != 0)
            fail(job, AXD_IDLE_TSSI_ERROR);
        else
            job->stage = RUN_TSSI;
        break;
    case EXIT_PHY:
        guard(job, AXD_PHY_EXIT, ENABLE_MAC);
        break;
    case ENABLE_MAC:
        mac(job, AXD_PSM_ENABLE_MAIN, FINISH);
        break;
    case FINISH:
        job->phase = AXD_IDLE_TRACE_COMPLETE;
        break;
    case RUN_MAC:
    case RUN_GUARD:
    case RUN_TSSI:
        child_step(job, &relay);
        break;
    default:
        fail(job, AXD_IDLE_BAD_ARGUMENT);
        break;
    }
    return job->phase;
}
enum axd_idle_phase axd_idle_tssi_cancel(struct axd_idle_tssi *job)
{
    if (job == NULL)
        return AXD_IDLE_INVALID;
    if (job->phase == AXD_IDLE_RUNNING) {
        job->phase = AXD_IDLE_CANCELLED;
        if (job->stage == RUN_MAC)
            axd_psm_control_cancel(&job->mac_job);
        else if (job->stage == RUN_GUARD)
            axd_phy_guard_cancel(&job->guard_job);
        else if (job->stage == RUN_TSSI)
            axd_phy_tssi_cancel(&job->tssi_job);
    }
    return job->phase;
}
