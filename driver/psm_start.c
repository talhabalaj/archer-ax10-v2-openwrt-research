/* SPDX-License-Identifier: MIT */
#include "psm_start.h"

enum stage {
    CLEAR_MAIN, MAIN_RESET, MAIN_RUN, POLL_INITIAL, POLL_EVALUATE,
    POLL_CONFIRM, CLEAR_EXT, EXT_CONTROL, FIFO_READ, FIFO_CONFIRM
};

static void add(struct axd_psm_start *job, enum axd_d11_kind kind,
                uint32_t offset, uint32_t value)
{
    struct axd_d11_operation op = {kind, offset, value};
    job->operations[job->queued++] = op;
}

static void write32(struct axd_psm_start *job, uint32_t offset, uint32_t value)
{
    add(job, AXD_D11_STORE_BARRIER, 0, 0);
    add(job, AXD_D11_WRITE32, offset, value);
}

static void main_control(struct axd_psm_start *job, uint32_t value)
{
    uint32_t effective = value;
    uint32_t mirror = (job->context.mirror_control & ~0xcfc72706u) |
                      (value & 0xcfc72706u);
    if (job->context.main_control == value)
        return;
    job->context.main_control = value;
    if (job->context.wake_override)
        effective |= 0x04000000u;
    if (job->context.mute_override)
        effective = (effective & ~0x40000u) | 0x20000u;
    write32(job, 0x120, effective);
    if (job->context.mirror_enabled && job->context.mirror_control != mirror) {
        job->context.mirror_control = mirror;
        write32(job, 0x324, mirror);
    }
}

static uint32_t status_offset(const struct axd_psm_start *job)
{
    return job->bank == 0 ? 0x128u : 0x1c0u;
}

static void read_status(struct axd_psm_start *job)
{
    add(job, AXD_D11_READ32, status_offset(job), 0);
    add(job, AXD_D11_FULL_BARRIER, 0, 0);
}

static void fail(struct axd_psm_start *job, enum axd_start_error error)
{
    job->error = error;
    job->phase = AXD_START_FAILED;
}

/* One bounded internal transition. Polling repeats only across public steps. */
static void prepare(struct axd_psm_start *job)
{
    job->queued = job->cursor = 0;
    switch (job->stage) {
    case CLEAR_MAIN:
        write32(job, 0x128, 0xffffffffu);
        job->stage = MAIN_RESET;
        break;
    case MAIN_RESET:
        main_control(job, 0x04020406u);
        job->stage = MAIN_RUN;
        break;
    case MAIN_RUN:
        main_control(job, 0x04020402u);
        job->stage = POLL_INITIAL;
        break;
    case POLL_INITIAL:
        job->repetitions = 0;
        read_status(job);
        job->stage = POLL_EVALUATE;
        break;
    case POLL_EVALUATE:
        if ((job->last_read & 1u) != 0 || job->repetitions == 100000u) {
            read_status(job); /* Separate final confirmation, even after ACK. */
            job->stage = POLL_CONFIRM;
        } else {
            add(job, AXD_D11_DELAY_US, 0, 10);
            read_status(job);
            job->repetitions++;
        }
        break;
    case POLL_CONFIRM:
        if ((job->last_read & 1u) == 0) {
            fail(job, job->bank == 0 ? AXD_START_MAIN_ACK_MISSING : AXD_START_EXT_ACK_MISSING);
        } else if (job->bank == 0 && job->context.psmx_enabled) {
            job->bank = 1;
            job->stage = CLEAR_EXT;
        } else {
            job->stage = FIFO_READ;
        }
        break;
    case CLEAR_EXT:
        write32(job, 0x1c0, 0xffffffffu);
        job->stage = EXT_CONTROL;
        break;
    case EXT_CONTROL:
        if (job->context.extended_control != 0x402u) {
            job->context.extended_control = 0x402;
            write32(job, 0x1b4, 0x402);
        }
        job->stage = POLL_INITIAL;
        break;
    case FIFO_READ:
        /* SHM byte offset 0x422: selector 0x10000 | (offset >> 2), high lane. */
        write32(job, 0x160, 0x10108);
        add(job, AXD_D11_READ32, 0x160, 0);
        add(job, AXD_D11_FULL_BARRIER, 0, 0);
        add(job, AXD_D11_READ16, 0x166, 0);
        add(job, AXD_D11_FULL_BARRIER, 0, 0);
        job->stage = FIFO_CONFIRM;
        break;
    case FIFO_CONFIRM:
        if (((job->last_read & 0xffffu) >> 13) != 2u)
            fail(job, AXD_START_FIFO_LAYOUT);
        else
            job->phase = AXD_START_SUSPENDED;
        break;
    default:
        fail(job, AXD_START_BAD_ARGUMENT);
        break;
    }
}

int axd_psm_start_begin(struct axd_psm_start *job,
                        const struct axd_start_context *context)
{
    const struct axd_psm_start empty = {0};
    struct axd_start_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context; /* Permit a new job from its previous context. */
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_START_BAD_ARGUMENT);
    if (context == NULL || context->wake_override > 1 || context->mute_override > 1 ||
        context->mirror_enabled > 1 || context->psmx_enabled > 1)
        return -1;
    if (context->core != 130 || context->minor > 1) {
        job->error = AXD_START_UNSUPPORTED_CORE;
        return -1;
    }
    job->context = *context;
    job->error = AXD_START_NO_ERROR;
    job->phase = AXD_START_RUNNING;
    job->stage = CLEAR_MAIN;
    return 0;
}

enum axd_start_phase axd_psm_start_step(struct axd_psm_start *job,
                                        axd_d11_io_fn io, void *context)
{
    uint32_t value = 0;
    const struct axd_d11_operation *op;
    if (job == NULL)
        return AXD_START_INVALID;
    if (job->phase != AXD_START_RUNNING)
        return job->phase;
    if (job->cursor == job->queued) {
        prepare(job);
        if (job->phase != AXD_START_RUNNING || job->queued == 0)
            return job->phase;
    }
    if (io == NULL) {
        fail(job, AXD_START_BAD_ARGUMENT);
        return job->phase;
    }
    op = &job->operations[job->cursor];
    job->attempted_ops++;
    if (io(context, op, &value) != 0) {
        fail(job, AXD_START_IO_ERROR);
        return job->phase;
    }
    job->completed_ops++;
    if (op->kind == AXD_D11_READ32)
        job->last_read = value;
    else if (op->kind == AXD_D11_READ16)
        job->last_read = value & 0xffffu;
    job->cursor++;
    return job->phase;
}

enum axd_start_phase axd_psm_start_cancel(struct axd_psm_start *job)
{
    if (job == NULL)
        return AXD_START_INVALID;
    if (job->phase == AXD_START_RUNNING)
        job->phase = AXD_START_CANCELLED;
    return job->phase;
}
