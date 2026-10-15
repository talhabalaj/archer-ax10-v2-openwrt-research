/* SPDX-License-Identifier: MIT */
#include "wl_clock.h"

enum stage { ENTRY, RUN_D11, RUN_SI, CACHE, REASON, FINISH };
struct relay { struct axd_wl_clock *job; axd_wl_clock_io_fn io; void *context; };
static void fail(struct axd_wl_clock *job, enum axd_wl_clock_error error)
{
    job->phase = AXD_WLC_FAILED;
    job->error = error;
}
static int perform(struct relay *relay, const struct axd_wl_clock_operation *operation,
                     struct axd_wl_clock_reply *reply)
{
    if (relay->io == NULL) {
        fail(relay->job, AXD_WLC_BAD_ARGUMENT);
        return -1;
    }
    relay->job->attempted_ops++;
    if (relay->io(relay->context, operation, reply) != 0) {
        fail(relay->job, AXD_WLC_IO_ERROR);
        return -1;
    }
    relay->job->completed_ops++;
    return 0;
}
static int d11_io(void *opaque, const struct axd_clock_operation *operation, uint32_t *value)
{
    struct axd_wl_clock_operation converted = {0};
    struct axd_wl_clock_reply reply = {0};
    converted.path = AXD_WLC_D11;
    converted.operation.d11 = *operation;
    if (perform(opaque, &converted, &reply) != 0)
        return -1;
    *value = reply.value;
    return 0;
}
static int si_io(void *opaque, const struct axd_si_clock_operation *operation,
                  struct axd_si_clock_reply *reply)
{
    struct axd_wl_clock_operation converted = {0};
    struct axd_wl_clock_reply result = {0};
    converted.path = AXD_WLC_SI;
    converted.operation.si = *operation;
    if (perform(opaque, &converted, &result) != 0)
        return -1;
    reply->value = result.value;
    reply->irq_state_known = result.irq_state_known;
    reply->irq_state = result.irq_state;
    return 0;
}
int axd_wl_clock_begin(struct axd_wl_clock *job, uint32_t mode,
                        const struct axd_wl_clock_context *context)
{
    const struct axd_wl_clock empty = {0};
    struct axd_wl_clock_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) { snapshot = *context; context = &snapshot; }
    *job = empty;
    fail(job, AXD_WLC_BAD_ARGUMENT);
    if (context == NULL)
        return -1;
    job->context = *context;
    if (context->d11.core != 130 || context->d11.minor > 1) {
        fail(job, AXD_WLC_UNSUPPORTED);
        return -1;
    }
    job->mode = mode;
    job->stage = ENTRY;
    job->phase = AXD_WLC_RUNNING;
    job->error = AXD_WLC_NO_ERROR;
    return 0;
}
enum axd_wl_clock_phase axd_wl_clock_step(struct axd_wl_clock *job,
                                          axd_wl_clock_io_fn io, void *context)
{
    struct relay relay = {job, io, context};
    if (job == NULL)
        return AXD_WLC_INVALID;
    if (job->phase != AXD_WLC_RUNNING)
        return job->phase;
    switch (job->stage) {
    case ENTRY:
        if ((job->context.d11.si_capabilities & 0x10000000u) != 0) {
            job->path = AXD_WLC_D11;
            if (axd_clock_control_begin(&job->d11_job, AXD_CLOCK_SELECT,
                                         job->mode, &job->context.d11) != 0)
                fail(job, AXD_WLC_D11_ERROR);
            else
                job->stage = RUN_D11;
        } else {
            job->path = AXD_WLC_SI;
            if (job->context.fallback.capabilities != job->context.d11.si_capabilities)
                fail(job, AXD_WLC_BINDING);
            else if (axd_si_clock_begin(&job->si_job, job->mode, &job->context.fallback) != 0)
                fail(job, AXD_WLC_SI_ERROR);
            else
                job->stage = RUN_SI;
        }
        break;
    case RUN_D11: {
        enum axd_clock_phase phase = axd_clock_control_step(&job->d11_job, d11_io, &relay);
        job->context.d11 = job->d11_job.context;
        if (job->phase != AXD_WLC_RUNNING)
            break;
        if (phase == AXD_CLOCK_TRACE_COMPLETE) {
            job->cache_committed = 1;
            job->phase = AXD_WLC_TRACE_COMPLETE;
        } else if (phase == AXD_CLOCK_DEPENDENCY)
            job->phase = AXD_WLC_DEPENDENCY;
        else if (phase == AXD_CLOCK_CANCELLED)
            job->phase = AXD_WLC_CANCELLED;
        else if (phase != AXD_CLOCK_RUNNING)
            fail(job, AXD_WLC_D11_ERROR);
        break;
    }
    case RUN_SI: {
        enum axd_si_clock_phase phase = axd_si_clock_step(&job->si_job, si_io, &relay);
        job->context.fallback = job->si_job.context;
        if (job->phase != AXD_WLC_RUNNING)
            break;
        if (phase == AXD_SIC_TRACE_COMPLETE) {
            job->return_value = job->si_job.return_value;
            job->return_valid = 1;
            job->stage = CACHE;
        } else if (phase == AXD_SIC_DEPENDENCY)
            job->phase = AXD_WLC_DEPENDENCY;
        else if (phase == AXD_SIC_CANCELLED)
            job->phase = AXD_WLC_CANCELLED;
        else if (phase != AXD_SIC_RUNNING)
            fail(job, AXD_WLC_SI_ERROR);
        break;
    }
    case CACHE:
        job->override_sample = job->context.d11.wake_reasons;
        job->context.d11.fast_clock = (uint8_t)job->return_value;
        job->cache_committed = 1;
        job->stage = REASON;
        break;
    case REASON:
        job->context.d11.wake_reasons = job->return_value != 0 ?
            job->override_sample | 0x10u : job->override_sample & ~0x10u;
        job->reason_committed = 1;
        job->stage = FINISH;
        break;
    case FINISH:
        job->phase = AXD_WLC_TRACE_COMPLETE;
        break;
    default:
        fail(job, AXD_WLC_BAD_ARGUMENT);
        break;
    }
    return job->phase;
}
enum axd_wl_clock_phase axd_wl_clock_cancel(struct axd_wl_clock *job)
{
    if (job == NULL)
        return AXD_WLC_INVALID;
    if (job->phase == AXD_WLC_RUNNING) {
        job->phase = AXD_WLC_CANCELLED;
        if (job->stage == RUN_D11)
            axd_clock_control_cancel(&job->d11_job);
        else if (job->stage == RUN_SI)
            axd_si_clock_cancel(&job->si_job);
    }
    return job->phase;
}
