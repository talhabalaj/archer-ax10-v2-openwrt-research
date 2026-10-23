/* SPDX-License-Identifier: MIT */
#include "chipcommon_clock.h"

enum stage {
    ENTRY, BODY, CRYSTAL_BEGIN, CRYSTAL_CHECK, CRYSTAL_DONE,
    RMW_READ, RMW_WRITE, RMW_AFTER, SETTLE, POLL_READ, POLL_CHECK, FINISH
};
static void add(struct axd_cc_clock *job, enum axd_cc_clock_kind kind,
                enum axd_cc_clock_region region, uint32_t offset, uint32_t value)
{
    const struct axd_cc_clock_operation op = {kind, region, offset, value};
    job->operations[job->queued++] = op;
}
static void cc_read(struct axd_cc_clock *job, uint32_t offset)
{
    add(job, AXD_CC_READ32, AXD_CC_CHIPCOMMON, offset, 0);
    add(job, AXD_CC_FULL_BARRIER, AXD_CC_CHIPCOMMON, 0, 0);
}
static void dependency(struct axd_cc_clock *job, enum axd_cc_clock_dependency which)
{
    job->dependency = which;
    job->phase = AXD_CC_DEPENDENCY;
}
static void fail(struct axd_cc_clock *job, enum axd_cc_clock_error error)
{
    job->error = error;
    job->phase = AXD_CC_FAILED;
}
static void complete(struct axd_cc_clock *job, unsigned value)
{
    job->return_value = value;
    job->phase = AXD_CC_TRACE_COMPLETE;
}
static void crystal(struct axd_cc_clock *job, unsigned on, unsigned resume)
{
    job->crystal_on = on;
    job->resume = resume;
    job->stage = CRYSTAL_BEGIN;
}
static uint32_t rmw_offset(const struct axd_cc_clock *job)
{
    return job->context.revision < 10 ? 0xb8u :
           (job->context.revision < 20 ? 0xc0u : 0x1e0u);
}
static void prepare(struct axd_cc_clock *job)
{
    uint32_t next;
    job->queued = job->cursor = 0;
    switch (job->stage) {
    case ENTRY:
        if (job->context.revision <= 5)
            complete(job, 0);
        else if (!job->context.chipcommon_owned)
            dependency(job, AXD_CC_OWNER);
        else
            job->stage = BODY;
        break;
    case BODY:
        if (((job->context.capabilities & 0x40000u) == 0 && job->context.revision <= 19) ||
            (job->mode != 0 && job->mode != 2)) {
            complete(job, job->mode == 0);
        } else if (job->context.revision < 10 && job->mode == 0) {
            crystal(job, 1, RMW_READ);
        } else {
            job->stage = RMW_READ;
        }
        break;
    case CRYSTAL_BEGIN:
        job->crystal_called = 1;
        if (job->context.bus != 1 || job->context.bus_core_id == 0x820 ||
            job->context.bus_core_id == 0x83c) {
            job->crystal_return = job->context.bus == 2 ? 0 : -1;
            job->crystal_return_valid = 1;
            job->stage = job->resume;
        } else if (!job->context.pci_config_owned) {
            dependency(job, AXD_CC_PCI_OWNER);
        } else {
            add(job, AXD_CC_PCI_READ32, AXD_CC_PCI_CONFIG, 0xb0, 0);
            add(job, AXD_CC_PCI_READ32, AXD_CC_PCI_CONFIG, 0xb4, 0);
            add(job, AXD_CC_PCI_READ32, AXD_CC_PCI_CONFIG, 0xb8, 0);
            job->stage = CRYSTAL_CHECK;
        }
        break;
    case CRYSTAL_CHECK:
        if (job->crystal_on && (job->crystal_input & 0x40u)) {
            job->stage = CRYSTAL_DONE;
        } else {
            next = job->crystal_on ? job->crystal_output | 0x40u :
                                    job->crystal_output & ~0x40u;
            add(job, AXD_CC_PCI_WRITE32, AXD_CC_PCI_CONFIG, 0xb4, next);
            add(job, AXD_CC_PCI_WRITE32, AXD_CC_PCI_CONFIG, 0xb8,
                job->crystal_enable | 0x40u);
            if (job->crystal_on)
                add(job, AXD_CC_DELAY_US, AXD_CC_NONE, 0, 1000);
            job->stage = CRYSTAL_DONE;
        }
        break;
    case CRYSTAL_DONE:
        job->crystal_return = 0;
        job->crystal_return_valid = 1;
        job->stage = job->resume;
        break;
    case RMW_READ:
        if (job->context.revision >= 10 || job->mode == 0)
            add(job, AXD_CC_STORE_BARRIER, AXD_CC_CHIPCOMMON, 0, 0);
        cc_read(job, rmw_offset(job));
        job->stage = RMW_WRITE;
        break;
    case RMW_WRITE:
        if (job->context.revision < 10) {
            next = job->last_read & ~0x3800u;
            if (job->mode == 0)
                next |= 0x1000u;
            else {
                if ((job->last_read & 7u) != 1)
                    next |= 0x2000u;
                add(job, AXD_CC_STORE_BARRIER, AXD_CC_CHIPCOMMON, 0, 0);
            }
        } else {
            uint32_t bit = job->context.revision < 20 ? 0x10u : 2u;
            next = job->mode == 0 ? job->last_read | bit : job->last_read & ~bit;
        }
        job->rmw_value = next;
        add(job, AXD_CC_WRITE32, AXD_CC_CHIPCOMMON, rmw_offset(job), next);
        job->stage = RMW_AFTER;
        break;
    case RMW_AFTER:
        if (job->context.revision < 10 && job->mode == 2 && (job->rmw_value & 0x2000u))
            crystal(job, 0, SETTLE);
        else
            job->stage = SETTLE;
        break;
    case SETTLE:
        if ((job->context.capabilities & 0x10000000u) == 0) {
            add(job, AXD_CC_DELAY_US, AXD_CC_NONE, 0, 150);
            job->stage = FINISH;
        } else {
            job->stage = POLL_READ;
        }
        break;
    case POLL_READ:
        cc_read(job, 0x1e0);
        job->stage = POLL_CHECK;
        break;
    case POLL_CHECK:
        if (((job->last_read & 0x20000u) != 0) == (job->mode == 0)) {
            job->stage = FINISH;
        } else if (job->poll_delays == 1500) {
            job->poll_exhausted = 1;
            job->stage = FINISH;
        } else {
            add(job, AXD_CC_DELAY_US, AXD_CC_NONE, 0, 10);
            cc_read(job, 0x1e0);
        }
        break;
    case FINISH:
        complete(job, job->mode == 0);
        break;
    default:
        fail(job, AXD_CC_BAD_ARGUMENT);
        break;
    }
}
int axd_cc_clock_begin(struct axd_cc_clock *job, uint32_t mode,
                       const struct axd_cc_clock_context *context)
{
    static const struct axd_cc_clock empty={0};
    struct axd_cc_clock_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context;
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_CC_BAD_ARGUMENT);
    if (context == NULL || context->chipcommon_owned > 1 || context->pci_config_owned > 1)
        return -1;
    job->context = *context;
    job->mode = mode;
    job->stage = ENTRY;
    job->error = AXD_CC_NO_ERROR;
    job->phase = AXD_CC_RUNNING;
    return 0;
}
enum axd_cc_clock_phase axd_cc_clock_step(struct axd_cc_clock *job,
                                         axd_cc_clock_io_fn io, void *context)
{
    const struct axd_cc_clock_operation *op;
    uint32_t value = 0;
    if (job == NULL)
        return AXD_CC_INVALID;
    if (job->phase != AXD_CC_RUNNING)
        return job->phase;
    if (job->cursor == job->queued) {
        prepare(job);
        if (job->phase != AXD_CC_RUNNING || job->queued == 0)
            return job->phase;
    }
    if (io == NULL) {
        fail(job, AXD_CC_BAD_ARGUMENT);
        return job->phase;
    }
    op = &job->operations[job->cursor];
    job->attempted_ops++;
    if (io(context, op, &value) != 0) {
        fail(job, AXD_CC_IO_ERROR);
        return job->phase;
    }
    job->completed_ops++;
    if (op->kind == AXD_CC_READ32 || op->kind == AXD_CC_PCI_READ32)
        job->last_read = value;
    if (op->kind == AXD_CC_READ32) {
        job->last_cc_sample = value;
        job->last_cc_offset = op->offset;
        job->cc_sample_observed = 1;
        job->cc_writes_since_sample = 0;
        if (op->offset == 0x1e0) {
            job->last_status = value;
            job->status_observed = 1;
            job->clock_writes_since_status = 0;
        }
    } else if (op->kind == AXD_CC_WRITE32) {
        job->cc_writes_since_sample = 1;
        if (op->offset == 0x1e0)
            job->clock_writes_since_status = 1;
    } else if (op->kind == AXD_CC_PCI_READ32) {
        if (op->offset == 0xb0)
            job->crystal_input = value;
        else if (op->offset == 0xb4)
            job->crystal_output = value;
        else
            job->crystal_enable = value;
    } else if (op->kind == AXD_CC_DELAY_US && op->value == 10) {
        job->poll_delays++;
    }
    job->cursor++;
    return job->phase;
}
enum axd_cc_clock_phase axd_cc_clock_cancel(struct axd_cc_clock *job)
{
    if (job == NULL)
        return AXD_CC_INVALID;
    if (job->phase == AXD_CC_RUNNING)
        job->phase = AXD_CC_CANCELLED;
    return job->phase;
}
