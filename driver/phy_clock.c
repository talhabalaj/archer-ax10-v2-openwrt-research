/* SPDX-License-Identifier: MIT */
#include "phy_clock.h"

enum stage {
    ENTRY, PREPARE_FLAGS, PREPARE_DELAY, CLOCK_ENTRY, ENABLE_CLEAR, ENABLE_DELAY,
    WORKAROUND, ENABLE_FINAL, ENABLE_FINAL_DELAY, DISABLE_FINAL, DISABLE_DELAY,
    FLAGS_READ, FLAGS_WRITE, FLAGS_READBACK, FLAGS_FINISH, FINISH
};
static void add(struct axd_phy_clock *job, enum axd_d11_kind kind,
                enum axd_phy_clock_region region, uint32_t offset, uint32_t value)
{
    const struct axd_phy_clock_operation op = {kind, region, offset, value};
    job->operations[job->queued++] = op;
}
static void read_flags(struct axd_phy_clock *job)
{
    enum axd_phy_clock_region region = (enum axd_phy_clock_region)job->region;
    add(job, AXD_D11_READ32, region, 0x408, 0);
    add(job, AXD_D11_FULL_BARRIER, region, 0, 0);
}
static void flags(struct axd_phy_clock *job, enum axd_phy_clock_region region,
                  uint32_t mask, uint32_t value, unsigned resume)
{
    job->region = (unsigned)region;
    job->mask = mask;
    job->value = value;
    job->resume = resume;
    job->stage = FLAGS_READ;
}
static void delay(struct axd_phy_clock *job, uint32_t us, unsigned resume)
{
    add(job, AXD_D11_DELAY_US, AXD_PHY_CLOCK_NO_REGION, 0, us);
    job->stage = resume;
}
static void fail(struct axd_phy_clock *job, enum axd_phy_clock_error error)
{
    job->error = error;
    job->phase = AXD_PHY_CLOCK_FAILED;
}
static void dependency(struct axd_phy_clock *job, enum axd_phy_clock_dependency owner)
{
    job->dependency = owner;
    job->phase = AXD_PHY_CLOCK_DEPENDENCY;
}
static uint32_t bandwidth_flags(uint16_t chanspec)
{
    switch (chanspec & 0x3800u) {
    case 0x1000: return 0x40;
    case 0x1800: return 0x80;
    case 0x2000: case 0x3000: return 0xc0;
    case 0x2800: return 0x800;
    default: return 0;
    }
}
static void prepare(struct axd_phy_clock *job)
{
    job->queued = job->cursor = 0;
    switch (job->stage) {
    case ENTRY:
        if (job->command == AXD_PHY_CLOCK_RECORD_BANDWIDTH) {
            job->context.bandwidth_state = job->context.chanspec & 0x3800u;
            job->phase = AXD_PHY_CLOCK_TRACE_COMPLETE;
        } else {
            job->stage = job->command == AXD_PHY_CLOCK_RESET_PREPARE ?
                         PREPARE_FLAGS : CLOCK_ENTRY;
        }
        break;
    case PREPARE_FLAGS:
        if (!job->context.current_owned)
            dependency(job, AXD_PHY_CLOCK_CURRENT_OWNER);
        else
            flags(job, AXD_PHY_CLOCK_CURRENT_WRAPPER, 0x8ce,
                  job->bandwidth_flags | 0xeu, PREPARE_DELAY);
        break;
    case PREPARE_DELAY:
        delay(job, 2, CLOCK_ENTRY);
        break;
    case CLOCK_ENTRY:
        job->context.phy_clock = job->command != AXD_PHY_CLOCK_DISABLE;
        if (!job->context.current_owned) {
            dependency(job, AXD_PHY_CLOCK_CURRENT_OWNER);
        } else if (job->context.phy_clock) {
            flags(job, AXD_PHY_CLOCK_CURRENT_WRAPPER, 0x300, 0x100, ENABLE_CLEAR);
        } else {
            flags(job, AXD_PHY_CLOCK_CURRENT_WRAPPER, 0x200a, 0xa, DISABLE_DELAY);
        }
        break;
    case ENABLE_CLEAR:
        flags(job, AXD_PHY_CLOCK_CURRENT_WRAPPER, 0xe, 0, ENABLE_DELAY);
        break;
    case ENABLE_DELAY:
        delay(job, 1, WORKAROUND);
        break;
    case WORKAROUND:
        if (job->context.rsdb && !job->context.workaround_done) {
            if (!job->context.unit1_owned)
                dependency(job, AXD_PHY_CLOCK_UNIT1_OWNER);
            else
                flags(job, AXD_PHY_CLOCK_UNIT1_WRAPPER, 0x1f, 0xf, ENABLE_FINAL);
        } else {
            job->stage = ENABLE_FINAL;
        }
        break;
    case ENABLE_FINAL:
        flags(job, AXD_PHY_CLOCK_CURRENT_WRAPPER, 4, 4, ENABLE_FINAL_DELAY);
        break;
    case ENABLE_FINAL_DELAY:
        delay(job, 1, FINISH);
        break;
    case DISABLE_DELAY:
        delay(job, 1, DISABLE_FINAL);
        break;
    case DISABLE_FINAL:
        flags(job, AXD_PHY_CLOCK_CURRENT_WRAPPER, 0xa, 8, ENABLE_FINAL_DELAY);
        break;
    case FLAGS_READ:
        read_flags(job);
        job->stage = FLAGS_WRITE;
        break;
    case FLAGS_WRITE:
        job->initial_sample = job->last_read;
        job->write_value = (job->initial_sample & ~job->mask) | job->value;
        add(job, AXD_D11_STORE_BARRIER, (enum axd_phy_clock_region)job->region, 0, 0);
        add(job, AXD_D11_WRITE32, (enum axd_phy_clock_region)job->region, 0x408,
            job->write_value);
        job->stage = FLAGS_READBACK;
        break;
    case FLAGS_READBACK:
        read_flags(job);
        job->stage = FLAGS_FINISH;
        break;
    case FLAGS_FINISH:
        job->stage = job->resume;
        break;
    case FINISH:
        job->phase = AXD_PHY_CLOCK_TRACE_COMPLETE;
        break;
    default:
        fail(job, AXD_PHY_CLOCK_BAD_ARGUMENT);
        break;
    }
}
int axd_phy_clock_begin(struct axd_phy_clock *job, enum axd_phy_clock_command command,
                        const struct axd_phy_clock_context *context)
{
    const struct axd_phy_clock empty = {0};
    struct axd_phy_clock_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context;
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_PHY_CLOCK_BAD_ARGUMENT);
    if (context == NULL || command < AXD_PHY_CLOCK_ENABLE ||
        command > AXD_PHY_CLOCK_RECORD_BANDWIDTH || context->rsdb > 1 ||
        context->workaround_done > 1 || context->phy_clock > 1 ||
        context->current_owned > 1 || context->unit1_owned > 1)
        return -1;
    if (context->core != 130 || context->minor > 1 || context->phy_type != 0xb ||
        context->phy_revision != 51 || context->chip_id != 0xf6ca ||
        (context->interconnect != 1 && context->interconnect != 3 && context->interconnect != 4)) {
        job->error = AXD_PHY_CLOCK_UNSUPPORTED;
        return -1;
    }
    job->context = *context;
    job->command = command;
    job->bandwidth_flags = bandwidth_flags(context->chanspec);
    job->stage = ENTRY;
    job->error = AXD_PHY_CLOCK_NO_ERROR;
    job->phase = AXD_PHY_CLOCK_RUNNING;
    return 0;
}
enum axd_phy_clock_phase axd_phy_clock_step(struct axd_phy_clock *job,
                                           axd_phy_clock_io_fn io, void *context)
{
    const struct axd_phy_clock_operation *op;
    uint32_t value = 0;
    if (job == NULL)
        return AXD_PHY_CLOCK_INVALID;
    if (job->phase != AXD_PHY_CLOCK_RUNNING)
        return job->phase;
    if (job->cursor == job->queued) {
        prepare(job);
        if (job->phase != AXD_PHY_CLOCK_RUNNING || job->queued == 0)
            return job->phase;
    }
    if (io == NULL) {
        fail(job, AXD_PHY_CLOCK_BAD_ARGUMENT);
        return job->phase;
    }
    op = &job->operations[job->cursor];
    job->attempted_ops++;
    if (io(context, op, &value) != 0) {
        fail(job, AXD_PHY_CLOCK_IO_ERROR);
        return job->phase;
    }
    job->completed_ops++;
    if (op->kind == AXD_D11_READ32) {
        job->last_read = value;
        if (op->region == AXD_PHY_CLOCK_CURRENT_WRAPPER) {
            job->current_sample = value;
            job->current_observed = 1;
            job->current_writes_since_sample = 0;
        } else {
            job->unit1_sample = value;
            job->unit1_observed = 1;
            job->unit1_writes_since_sample = 0;
        }
    } else if (op->kind == AXD_D11_WRITE32) {
        if (op->region == AXD_PHY_CLOCK_CURRENT_WRAPPER)
            job->current_writes_since_sample = 1;
        else
            job->unit1_writes_since_sample = 1;
    } else if (op->kind == AXD_D11_FULL_BARRIER && job->stage == FLAGS_FINISH) {
        if (op->region == AXD_PHY_CLOCK_CURRENT_WRAPPER) {
            job->current_updates++;
        } else {
            job->unit1_updates++;
            job->context.workaround_done = 1;
        }
    }
    job->cursor++;
    return job->phase;
}
enum axd_phy_clock_phase axd_phy_clock_cancel(struct axd_phy_clock *job)
{
    if (job == NULL)
        return AXD_PHY_CLOCK_INVALID;
    if (job->phase == AXD_PHY_CLOCK_RUNNING)
        job->phase = AXD_PHY_CLOCK_CANCELLED;
    return job->phase;
}
