/* SPDX-License-Identifier: MIT */
#include "clock_control.h"

enum stage {
    ENTRY, RMW_READ, RMW_WRITE, CLOCK_FAST_DELAY, CLOCK_FAST_READ,
    CLOCK_PRE_FILTER, CLOCK_PRE_CHECK, CLOCK_POLL, CLOCK_FINISH,
    PLL_SAMPLE, PLL_DECIDE, PLL_SETTLE, PLL_READ, PLL_POLL, PLL_FINAL,
    PLL_AFTER_FINAL, PLL_DISABLED_READ, SECONDARY_ENTRY, SECONDARY_FINISH
};
static enum axd_clock_region region(const struct axd_clock_control *job)
{
    return job->after_rmw == SECONDARY_FINISH ? AXD_CLOCK_D11_UNIT1 : AXD_CLOCK_CURRENT_D11;
}
static void add(struct axd_clock_control *job, enum axd_d11_kind kind,
                enum axd_clock_region target, uint32_t offset, uint32_t value)
{
    const struct axd_clock_operation op = {kind, target, offset, value};
    job->operations[job->queued++] = op;
}
static void read_clock(struct axd_clock_control *job, enum axd_clock_region target)
{
    add(job, AXD_D11_READ32, target, 0x1e0, 0);
    add(job, AXD_D11_FULL_BARRIER, target, 0, 0);
}
static void rmw(struct axd_clock_control *job, uint32_t clear, uint32_t set, unsigned resume)
{
    job->rmw_clear = clear;
    job->rmw_set = set;
    job->after_rmw = resume;
    job->stage = RMW_READ;
}
static void fail(struct axd_clock_control *job, enum axd_clock_error error)
{
    job->phase = AXD_CLOCK_FAILED;
    job->error = error;
}
static void dependency(struct axd_clock_control *job, enum axd_clock_dependency needed)
{
    job->dependency = needed;
    job->phase = AXD_CLOCK_DEPENDENCY;
}
static void prepare(struct axd_clock_control *job)
{
    job->queued = job->cursor = 0;
    switch (job->stage) {
    case ENTRY:
        if (job->command == AXD_CLOCK_SELECT) {
            if ((job->context.si_capabilities & 0x10000000u) == 0) {
                dependency(job, AXD_CLOCK_HND_FALLBACK);
            } else if (!job->context.core_clock_valid) {
                job->stage = CLOCK_FINISH;
            } else if (job->argument == 0) {
                rmw(job, 0, 2, CLOCK_FAST_DELAY);
            } else if (job->context.si_word20 != 0) {
                rmw(job, 2, 0, CLOCK_FINISH);
            } else {
                read_clock(job, AXD_CLOCK_CURRENT_D11);
                job->stage = CLOCK_PRE_FILTER;
            }
        } else if (job->command == AXD_CLOCK_SECONDARY) {
            job->stage = SECONDARY_ENTRY;
        } else if (!job->context.core_clock_valid || job->context.pll_inhibit || !job->context.core_up) {
            job->phase = AXD_CLOCK_TRACE_COMPLETE;
        } else if (job->argument == 0) {
            rmw(job, 0x300, 0, PLL_DISABLED_READ);
        } else if (job->context.rsdb) {
            rmw(job, 0x20, 0, PLL_SAMPLE);
        } else {
            job->stage = PLL_SAMPLE;
        }
        break;
    case RMW_READ:
        add(job, AXD_D11_STORE_BARRIER, region(job), 0, 0);
        read_clock(job, region(job));
        job->stage = RMW_WRITE;
        break;
    case RMW_WRITE:
        add(job, AXD_D11_WRITE32, region(job), 0x1e0,
            (job->last_read & ~job->rmw_clear) | job->rmw_set);
        job->stage = job->after_rmw;
        break;
    case CLOCK_FAST_DELAY:
        add(job, AXD_D11_DELAY_US, AXD_CLOCK_CURRENT_D11, 0, 64);
        job->stage = CLOCK_FAST_READ;
        break;
    case CLOCK_FAST_READ:
        read_clock(job, AXD_CLOCK_CURRENT_D11);
        job->repetitions = 0;
        job->stage = CLOCK_POLL;
        break;
    case CLOCK_PRE_FILTER:
        if ((job->last_read & 0x12u) == 0) {
            rmw(job, 2, 0, CLOCK_FINISH);
        } else {
            read_clock(job, AXD_CLOCK_CURRENT_D11);
            job->stage = CLOCK_PRE_CHECK;
        }
        break;
    case CLOCK_PRE_CHECK:
        job->repetitions = 0;
        job->stage = CLOCK_POLL;
        break;
    case CLOCK_POLL:
        if ((job->last_read & 0x20000u) != 0 || job->repetitions == 1500) {
            if ((job->last_read & 0x20000u) == 0)
                job->poll_exhausted = 1;
            if (job->argument == 0)
                job->stage = CLOCK_FINISH;
            else
                rmw(job, 2, 0, CLOCK_FINISH);
        } else {
            add(job, AXD_D11_DELAY_US, AXD_CLOCK_CURRENT_D11, 0, 10);
            read_clock(job, AXD_CLOCK_CURRENT_D11);
            job->repetitions++;
        }
        break;
    case CLOCK_FINISH:
        job->context.fast_clock = job->argument == 0;
        job->phase = AXD_CLOCK_TRACE_COMPLETE;
        break;
    case PLL_SAMPLE:
        read_clock(job, AXD_CLOCK_CURRENT_D11);
        job->stage = PLL_DECIDE;
        break;
    case PLL_DECIDE:
        job->sample_a = job->last_read;
        if ((job->sample_a & 0x300u) == 0x300u) {
            job->request_skipped = 1;
            job->stage = SECONDARY_ENTRY;
        } else {
            rmw(job, 0, 0x300, PLL_SETTLE);
        }
        break;
    case PLL_SETTLE:
        if ((job->sample_a & 0x03000000u) == 0x03000000u && job->delay_count < 4) {
            add(job, AXD_D11_DELAY_US, AXD_CLOCK_CURRENT_D11, 0, 16);
            job->delay_count++;
        } else {
            job->stage = PLL_READ;
        }
        break;
    case PLL_READ:
        read_clock(job, AXD_CLOCK_CURRENT_D11);
        job->repetitions = 0;
        job->stage = PLL_POLL;
        break;
    case PLL_POLL:
        if ((job->last_read & 0x03000000u) == 0x03000000u || job->repetitions == 10000) {
            if ((job->last_read & 0x03000000u) != 0x03000000u)
                job->poll_exhausted = 1;
            job->stage = PLL_FINAL;
        } else {
            add(job, AXD_D11_DELAY_US, AXD_CLOCK_CURRENT_D11, 0, 10);
            read_clock(job, AXD_CLOCK_CURRENT_D11);
            job->repetitions++;
        }
        break;
    case PLL_FINAL:
        read_clock(job, AXD_CLOCK_CURRENT_D11);
        job->stage = PLL_AFTER_FINAL;
        break;
    case PLL_AFTER_FINAL:
        job->stage = SECONDARY_ENTRY;
        break;
    case PLL_DISABLED_READ:
        read_clock(job, AXD_CLOCK_CURRENT_D11);
        job->stage = SECONDARY_ENTRY;
        break;
    case SECONDARY_ENTRY:
        if (!job->context.rsdb) {
            job->phase = AXD_CLOCK_TRACE_COMPLETE;
        } else if (!job->context.secondary_owned) {
            dependency(job, AXD_CLOCK_SECONDARY_OWNER);
        } else {
            unsigned clear_bit = job->command == AXD_CLOCK_PLL ? job->argument : !job->argument;
            rmw(job, clear_bit ? 0x20u : 0u, clear_bit ? 0u : 0x20u, SECONDARY_FINISH);
        }
        break;
    case SECONDARY_FINISH:
        job->phase = AXD_CLOCK_TRACE_COMPLETE;
        break;
    default:
        fail(job, AXD_CLOCK_BAD_ARGUMENT);
        break;
    }
}
int axd_clock_control_begin(struct axd_clock_control *job,
                            enum axd_clock_command command, uint32_t argument,
                            const struct axd_clock_context *context)
{
    const struct axd_clock_control empty = {0};
    struct axd_clock_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context;
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_CLOCK_BAD_ARGUMENT);
    if (context == NULL || command < AXD_CLOCK_SELECT || command > AXD_CLOCK_SECONDARY ||
        (command != AXD_CLOCK_SELECT && argument > 1) || context->core_clock_valid > 1 ||
        context->pll_inhibit > 1 || context->core_up > 1 ||
        context->rsdb > 1 || context->secondary_owned > 1)
        return -1;
    if (context->core != 130 || context->minor > 1) {
        job->error = AXD_CLOCK_UNSUPPORTED;
        return -1;
    }
    job->context = *context;
    job->command = command;
    job->argument = argument;
    job->stage = ENTRY;
    job->phase = AXD_CLOCK_RUNNING;
    job->error = AXD_CLOCK_NO_ERROR;
    return 0;
}
enum axd_clock_phase axd_clock_control_step(struct axd_clock_control *job,
                                            axd_clock_io_fn io, void *context)
{
    const struct axd_clock_operation *op;
    uint32_t value = 0;
    if (job == NULL)
        return AXD_CLOCK_INVALID;
    if (job->phase != AXD_CLOCK_RUNNING)
        return job->phase;
    if (job->cursor == job->queued) {
        prepare(job);
        if (job->phase != AXD_CLOCK_RUNNING || job->queued == 0)
            return job->phase;
    }
    if (io == NULL) {
        fail(job, AXD_CLOCK_BAD_ARGUMENT);
        return job->phase;
    }
    op = &job->operations[job->cursor];
    job->attempted_ops++;
    if (io(context, op, &value) != 0) {
        fail(job, AXD_CLOCK_IO_ERROR);
        return job->phase;
    }
    job->completed_ops++;
    if (op->kind == AXD_D11_READ32) {
        job->last_read = value;
        if (op->region == AXD_CLOCK_CURRENT_D11) {
            job->last_status = value;
            job->status_observed = 1;
            job->writes_since_status = 0;
        }
    } else if (op->kind == AXD_D11_WRITE32 && op->region == AXD_CLOCK_CURRENT_D11) {
        job->writes_since_status = 1;
    }
    job->cursor++;
    return job->phase;
}
enum axd_clock_phase axd_clock_control_cancel(struct axd_clock_control *job)
{
    if (job == NULL)
        return AXD_CLOCK_INVALID;
    if (job->phase == AXD_CLOCK_RUNNING)
        job->phase = AXD_CLOCK_CANCELLED;
    return job->phase;
}
