/* SPDX-License-Identifier: MIT */
#include "ai_reset.h"
#include <stddef.h>

enum stage {
    WRAPPER_BEGIN, BUSY_READ, BUSY_EVALUATE, ASSERT_RESET, AFTER_ASSERT,
    INITIAL_CONTROL, INITIAL_CLEAR, BEFORE_RELEASE, READ_RESET, EVALUATE_RESET,
    RELEASE_RESET, AFTER_RELEASE, FINAL_CONTROL, FINAL_CLEAR, WRAPPER_END
};
static void add(struct axd_ai_reset *job, enum axd_reset_kind kind,
                uint32_t offset, uint32_t value)
{
    struct axd_reset_operation op = {kind, job->wrapper, offset, value};
    job->operations[job->queued++] = op;
}
static void read32(struct axd_ai_reset *job, uint32_t offset)
{
    add(job, AXD_RESET_READ32, offset, 0);
    add(job, AXD_RESET_FULL_BARRIER, 0, 0);
}
static void write32(struct axd_ai_reset *job, uint32_t offset, uint32_t value)
{
    add(job, AXD_RESET_STORE_BARRIER, 0, 0);
    add(job, AXD_RESET_WRITE32, offset, value);
}
static void busy(struct axd_ai_reset *job, unsigned resume)
{
    job->busy_repetitions = 0;
    job->after_busy = resume;
    job->stage = BUSY_READ;
}
static void fail(struct axd_ai_reset *job, enum axd_reset_error error)
{
    job->error = error;
    job->phase = AXD_RESET_FAILED;
}
static void prepare(struct axd_ai_reset *job)
{
    job->queued = job->cursor = 0;
    switch (job->stage) {
    case WRAPPER_BEGIN:
        job->release_writes = 0;
        busy(job, ASSERT_RESET);
        break;
    case BUSY_READ:
        read32(job, 0x804);
        job->stage = BUSY_EVALUATE;
        break;
    case BUSY_EVALUATE:
        if (job->last_read == 0 || job->busy_repetitions == 30) {
            if (job->last_read != 0)
                job->busy_exhaustions++;
            job->stage = job->after_busy;
        } else {
            add(job, AXD_RESET_DELAY_US, 0, 10);
            read32(job, 0x804);
            job->busy_repetitions++;
        }
        break;
    case ASSERT_RESET:
        write32(job, 0x800, 1);
        add(job, AXD_RESET_DELAY_US, 0, 10);
        job->stage = AFTER_ASSERT;
        break;
    case AFTER_ASSERT:
        busy(job, INITIAL_CONTROL);
        break;
    case INITIAL_CONTROL:
        write32(job, 0x408, job->context.bits | job->context.reset_bits | 3u);
        read32(job, 0x408);
        job->stage = INITIAL_CLEAR;
        break;
    case INITIAL_CLEAR:
        add(job, AXD_RESET_DELAY_US, 0, 1);
        write32(job, 0x408, job->last_read & ~2u);
        job->stage = BEFORE_RELEASE;
        break;
    case BEFORE_RELEASE:
        busy(job, READ_RESET);
        break;
    case READ_RESET:
        read32(job, 0x800);
        job->stage = EVALUATE_RESET;
        break;
    case EVALUATE_RESET:
        if (job->last_read == 0 || job->release_writes == 9) {
            if (job->last_read != 0)
                job->reset_nonzero_mask |= 1u << job->wrapper;
            job->stage = FINAL_CONTROL;
        } else {
            busy(job, RELEASE_RESET);
        }
        break;
    case RELEASE_RESET:
        write32(job, 0x800, 0);
        job->stage = AFTER_RELEASE;
        break;
    case AFTER_RELEASE:
        busy(job, READ_RESET);
        break;
    case FINAL_CONTROL:
        write32(job, 0x408, job->context.bits | 3u);
        read32(job, 0x408);
        job->stage = FINAL_CLEAR;
        break;
    case FINAL_CLEAR:
        add(job, AXD_RESET_DELAY_US, 0, 1);
        write32(job, 0x408, job->last_read & ~2u);
        add(job, AXD_RESET_DELAY_US, 0, 1);
        job->stage = WRAPPER_END;
        break;
    case WRAPPER_END:
        job->completed_wrappers++;
        if (job->wrapper == 1) {
            job->wrapper = 0;
            job->stage = WRAPPER_BEGIN;
        } else {
            job->phase = AXD_RESET_TRACE_COMPLETE;
        }
        break;
    default:
        fail(job, AXD_RESET_BAD_ARGUMENT);
        break;
    }
}
int axd_ai_reset_begin(struct axd_ai_reset *job,
                        const struct axd_reset_context *context)
{
    const struct axd_ai_reset empty = {0};
    if (job == NULL)
        return -1;
    *job = empty;
    fail(job, AXD_RESET_BAD_ARGUMENT);
    if (context == NULL || context->secondary_present > 1)
        return -1;
    if (context->core_id != 0x812 || context->revision != 130 ||
        (context->interconnect != 1 && context->interconnect != 3 &&
         context->interconnect != 4)) {
        job->error = AXD_RESET_UNSUPPORTED;
        return -1;
    }
    job->context = *context;
    job->wrapper = context->secondary_present ? 1u : 0u;
    job->stage = WRAPPER_BEGIN;
    job->error = AXD_RESET_NO_ERROR;
    job->phase = AXD_RESET_RUNNING;
    return 0;
}
enum axd_reset_phase axd_ai_reset_step(struct axd_ai_reset *job,
                                        axd_reset_io_fn io, void *context)
{
    const struct axd_reset_operation *op;
    uint32_t value = 0;
    if (job == NULL)
        return AXD_RESET_INVALID;
    if (job->phase != AXD_RESET_RUNNING)
        return job->phase;
    if (job->cursor == job->queued) {
        prepare(job);
        if (job->phase != AXD_RESET_RUNNING || job->queued == 0)
            return job->phase;
    }
    if (io == NULL) {
        fail(job, AXD_RESET_BAD_ARGUMENT);
        return job->phase;
    }
    op = &job->operations[job->cursor];
    job->attempted_ops++;
    if (io(context, op, &value) != 0) {
        fail(job, AXD_RESET_IO_ERROR);
        return job->phase;
    }
    job->completed_ops++;
    if (op->kind == AXD_RESET_READ32)
        job->last_read = value;
    if (op->kind == AXD_RESET_WRITE32 && op->offset == 0x800 && op->value == 0)
        job->release_writes++;
    job->cursor++;
    return job->phase;
}
enum axd_reset_phase axd_ai_reset_cancel(struct axd_ai_reset *job)
{
    if (job == NULL)
        return AXD_RESET_INVALID;
    if (job->phase == AXD_RESET_RUNNING)
        job->phase = AXD_RESET_CANCELLED;
    return job->phase;
}
