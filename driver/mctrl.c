/* SPDX-License-Identifier: MIT */
#include "mctrl.h"

static void fail(struct axd_mctrl *job, enum axd_mctrl_error error)
{
    job->phase = AXD_MCTRL_FAILED;
    job->error = error;
}
static void write32(struct axd_mctrl *job, uint32_t offset, uint32_t value)
{
    const struct axd_d11_operation barrier = {AXD_D11_STORE_BARRIER, 0, 0};
    const struct axd_d11_operation write = {AXD_D11_WRITE32, offset, value};
    job->operations[job->queued++] = barrier;
    job->operations[job->queued++] = write;
}
static void prepare(struct axd_mctrl *job)
{
    struct axd_psm_context *psm = &job->context.psm;
    uint32_t next, mirror_mask;
    unsigned i;
    job->prepared = 1;
    if (job->reset) {
        psm->main_control = 0;
        for (i = 0; i < 9; i++)
            job->context.suspended_bitmap[i] = 0;
        job->context.suspended_count = 0;
        psm->wake_reasons = 0;
        psm->mute_override = 0;
        job->context.reset_halfword = 0;
    }
    next = (psm->main_control & ~job->mask) | job->value;
    if (next == psm->main_control)
        return;
    psm->main_control = next;
    job->effective = next;
    if (psm->wake_reasons != 0)
        job->effective |= 0x04000000u;
    if (psm->mute_override != 0)
        job->effective = (job->effective & ~0x40000u) | 0x20000u;
    write32(job, 0x120, job->effective);
    mirror_mask = job->mask & 0xcfc72706u;
    if (psm->mirror_enabled != 0 && mirror_mask != 0) {
        job->mirror_pending = (psm->mirror_control & ~mirror_mask) |
                              (job->value & 0xcfc72706u);
        if (job->mirror_pending != psm->mirror_control) {
            job->mirror_pending_valid = 1;
            write32(job, 0x324, job->mirror_pending);
        }
    }
}
int axd_mctrl_begin(struct axd_mctrl *job, uint32_t mask, uint32_t value,
                    const struct axd_mctrl_context *context)
{
    static const struct axd_mctrl empty={0};
    struct axd_mctrl_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context;
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_MCTRL_BAD_ARGUMENT);
    if (context == NULL || context->psm.mute_override > 1 ||
        context->psm.mirror_enabled > 1)
        return -1;
    if (context->psm.core != 130 || context->psm.minor > 1) {
        job->error = AXD_MCTRL_UNSUPPORTED;
        return -1;
    }
    job->context = *context;
    job->mask = mask;
    job->value = value;
    job->phase = AXD_MCTRL_RUNNING;
    job->error = AXD_MCTRL_NO_ERROR;
    return 0;
}
int axd_mctrl_reset_begin(struct axd_mctrl *job,
                          const struct axd_mctrl_context *context)
{
    if (axd_mctrl_begin(job, UINT32_MAX, 0x04000400u, context) != 0)
        return -1;
    job->reset = 1;
    return 0;
}
enum axd_mctrl_phase axd_mctrl_step(struct axd_mctrl *job,
                                    axd_d11_io_fn io, void *context)
{
    uint32_t ignored = 0;
    if (job == NULL)
        return AXD_MCTRL_INVALID;
    if (job->phase != AXD_MCTRL_RUNNING)
        return job->phase;
    if (!job->prepared)
        prepare(job);
    if (job->cursor == job->queued) {
        job->phase = AXD_MCTRL_DONE;
        return job->phase;
    }
    /* Mirror cache is reached only after the primary write returns, before
     * its own ST; a failure/cancel at either primary event leaves it intact.
     */
    if (job->mirror_pending_valid && job->cursor == 2) {
        job->context.psm.mirror_control = job->mirror_pending;
        job->mirror_pending_valid = 0;
    }
    if (io == NULL) {
        fail(job, AXD_MCTRL_BAD_ARGUMENT);
        return job->phase;
    }
    job->attempted_ops++;
    if (io(context, &job->operations[job->cursor], &ignored) != 0) {
        fail(job, AXD_MCTRL_IO_ERROR);
        return job->phase;
    }
    job->completed_ops++;
    job->cursor++;
    return job->phase;
}
enum axd_mctrl_phase axd_mctrl_cancel(struct axd_mctrl *job)
{
    if (job == NULL)
        return AXD_MCTRL_INVALID;
    if (job->phase == AXD_MCTRL_RUNNING)
        job->phase = AXD_MCTRL_CANCELLED;
    return job->phase;
}
