/* SPDX-License-Identifier: MIT */
#include "psm_control.h"

enum stage {
    INITIAL_READ, INITIAL_CHECK, EXT_NESTED_READ, EXT_NESTED_CHECK,
    SUSPEND_WAKE, PRE_CONTROL, CHECK_CONTROL, PRE_STATUS, CHECK_STATUS,
    CLEAR_ENABLE, SUSPEND_READ, SUSPEND_EVALUATE, SUSPEND_CONFIRM,
    FINAL_CONTROL, FINAL_CHECK, WAKE_DELAY, WAKE_READ, WAKE_EVALUATE,
    WAKE_CONFIRM, ENABLE_EXTRA_READ, SET_ENABLE, ENABLE_STATUS,
    ENABLE_CONTROL_READBACK, ENABLE_STATUS_READBACK, ENABLE_AFTER_READBACK,
    ENABLE_SHM, ENABLE_SHM_CHECK, ENABLE_POST
};
static void add(struct axd_psm_control *job, enum axd_d11_kind kind,
                uint32_t offset, uint32_t value)
{
    const struct axd_d11_operation op = {kind, offset, value};
    job->operations[job->queued++] = op;
}
static void read32(struct axd_psm_control *job, uint32_t offset)
{
    add(job, AXD_D11_READ32, offset, 0);
    add(job, AXD_D11_FULL_BARRIER, 0, 0);
}
static void write32(struct axd_psm_control *job, uint32_t offset, uint32_t value)
{
    add(job, AXD_D11_STORE_BARRIER, 0, 0);
    add(job, AXD_D11_WRITE32, offset, value);
}
static void shm_state(struct axd_psm_control *job)
{
    write32(job, 0x160, 0x10010); /* SHM byte offset0x40, selector0x10000. */
    add(job, AXD_D11_READ32, 0x160, 0);
    add(job, AXD_D11_FULL_BARRIER, 0, 0);
    add(job, AXD_D11_READ16, 0x164, 0);
    add(job, AXD_D11_FULL_BARRIER, 0, 0);
}
static uint32_t control_offset(const struct axd_psm_control *job)
{
    return job->bank ? 0x1b4u : 0x120u;
}
static uint32_t status_offset(const struct axd_psm_control *job)
{
    return job->bank ? 0x1c0u : 0x128u;
}
static uint32_t *depth(struct axd_psm_control *job)
{
    return job->bank ? &job->context.extended_depth : &job->context.main_depth;
}
static void fail(struct axd_psm_control *job, enum axd_psm_error error)
{
    job->phase = AXD_PSM_FAILED;
    job->error = error;
}
static void done(struct axd_psm_control *job, enum axd_psm_result result)
{
    job->phase = AXD_PSM_DONE;
    job->result = result;
}
static void gone(struct axd_psm_control *job)
{
    job->pending_callouts |= AXD_PSM_DOWN;
    fail(job, AXD_PSM_DEVICE_GONE);
}
static void mctrl(struct axd_psm_control *job, uint32_t mask, uint32_t value)
{
    uint32_t *cache = job->bank ? &job->context.extended_control : &job->context.main_control;
    uint32_t next = (*cache & ~mask) | value;
    uint32_t effective = next, mirror_mask = mask & 0xcfc72706u;
    if (*cache == next)
        return;
    *cache = next;
    if (!job->bank) {
        if (job->context.wake_reasons != 0)
            effective |= 0x04000000u;
        if (job->context.mute_override)
            effective = (effective & ~0x40000u) | 0x20000u;
    }
    write32(job, control_offset(job), effective);
    if (!job->bank && job->context.mirror_enabled && mirror_mask != 0) {
        uint32_t mirror = (job->context.mirror_control & ~mirror_mask) |
                          (value & 0xcfc72706u);
        if (mirror != job->context.mirror_control) {
            job->context.mirror_control = mirror;
            write32(job, 0x324, mirror);
        }
    }
}
static void begin_wake(struct axd_psm_control *job, unsigned resume)
{
    job->after_wake = resume;
    job->stage = WAKE_DELAY;
}
static void prepare(struct axd_psm_control *job)
{
    uint32_t *nesting;
    job->queued = job->cursor = 0;
    switch (job->stage) {
    case INITIAL_READ:
        read32(job, control_offset(job));
        job->stage = INITIAL_CHECK;
        break;
    case INITIAL_CHECK:
        if ((job->last_read & 2u) == 0) {
            done(job, AXD_PSM_NOOP);
            break;
        }
        nesting = depth(job);
        if (job->command == AXD_PSM_SUSPEND_MAIN || job->command == AXD_PSM_SUSPEND_EXT) {
            if (*nesting == UINT32_MAX) {
                fail(job, AXD_PSM_BAD_NESTING);
                break;
            }
            (*nesting)++;
            if (*nesting > 1) {
                if (job->bank) {
                    job->stage = EXT_NESTED_READ;
                } else if (job->last_read & 1u) {
                    job->pending_callouts |= AXD_PSM_FATAL;
                    fail(job, AXD_PSM_BAD_NESTING);
                } else {
                    done(job, AXD_PSM_NESTED);
                }
            } else {
                job->stage = job->bank ? PRE_CONTROL : SUSPEND_WAKE;
            }
        } else {
            if (!job->bank && (job->last_read & 1u) != 0 && *nesting == 0) {
                done(job, AXD_PSM_NOOP);
            } else if (*nesting == 0) {
                fail(job, AXD_PSM_BAD_NESTING);
            } else if (--(*nesting) != 0) {
                done(job, AXD_PSM_NESTED);
            } else {
                job->stage = job->bank ? ENABLE_EXTRA_READ : SET_ENABLE;
            }
        }
        break;
    case EXT_NESTED_READ:
        read32(job, control_offset(job));
        job->stage = EXT_NESTED_CHECK;
        break;
    case EXT_NESTED_CHECK:
        if (job->last_read & 1u) {
            job->pending_callouts |= AXD_PSM_FATAL;
            fail(job, AXD_PSM_BAD_NESTING);
        } else {
            done(job, AXD_PSM_NESTED);
        }
        break;
    case SUSPEND_WAKE:
        if (job->context.wake_reasons == 0 && (job->context.main_control & 0x04000000u) == 0) {
            job->context.wake_reasons = 4;
            mctrl(job, 0x04000000, 0x04000000);
            begin_wake(job, PRE_CONTROL);
        } else {
            job->context.wake_reasons |= 4u;
            job->stage = PRE_CONTROL;
        }
        break;
    case PRE_CONTROL:
        read32(job, control_offset(job));
        job->stage = CHECK_CONTROL;
        break;
    case CHECK_CONTROL:
        if (job->last_read == UINT32_MAX)
            gone(job);
        else
            job->stage = PRE_STATUS;
        break;
    case PRE_STATUS:
        read32(job, status_offset(job));
        job->stage = CHECK_STATUS;
        break;
    case CHECK_STATUS:
        if (job->last_read == UINT32_MAX)
            gone(job);
        else
            job->stage = CLEAR_ENABLE;
        break;
    case CLEAR_ENABLE:
        mctrl(job, 1, 0);
        job->repetitions = 0;
        job->stage = SUSPEND_READ;
        break;
    case SUSPEND_READ:
        read32(job, status_offset(job));
        job->stage = SUSPEND_EVALUATE;
        break;
    case SUSPEND_EVALUATE:
        if ((job->last_read & 1u) || job->repetitions == 8300) {
            read32(job, status_offset(job));
            job->stage = SUSPEND_CONFIRM;
        } else {
            add(job, AXD_D11_DELAY_US, 0, 10);
            read32(job, status_offset(job));
            job->repetitions++;
        }
        break;
    case SUSPEND_CONFIRM:
        if ((job->last_read & 1u) == 0) {
            job->context.timeout_flags |= 0x2000u;
            if (job->bank || job->context.chip_id != 0xa8d1)
                job->pending_callouts |= AXD_PSM_FATAL;
            fail(job, AXD_PSM_ACK_MISSING);
        } else {
            job->stage = FINAL_CONTROL;
        }
        break;
    case FINAL_CONTROL:
        read32(job, control_offset(job));
        job->stage = FINAL_CHECK;
        break;
    case FINAL_CHECK:
        if (job->last_read == UINT32_MAX)
            gone(job);
        else
            done(job, AXD_PSM_SUSPENDED);
        break;
    case WAKE_DELAY:
        if (!job->context.band_present) {
            if (job->command == AXD_PSM_WAIT_WAKE)
                done(job, AXD_PSM_NOOP);
            else
                job->stage = job->after_wake;
        } else {
            add(job, AXD_D11_DELAY_US, 0, 40);
            job->repetitions = 0;
            job->stage = WAKE_READ;
        }
        break;
    case WAKE_READ:
        shm_state(job);
        job->stage = WAKE_EVALUATE;
        break;
    case WAKE_EVALUATE:
        if (job->last_read == 2 || job->repetitions == job->wake_limit) {
            shm_state(job); /* Always separate final confirmation. */
            job->stage = WAKE_CONFIRM;
        } else {
            add(job, AXD_D11_DELAY_US, 0, 10);
            job->repetitions++;
            job->stage = WAKE_READ;
        }
        break;
    case WAKE_CONFIRM:
        if (job->last_read != 2) {
            if (job->context.fatal_reason == 0)
                job->context.fatal_reason = 3;
            job->pending_callouts |= AXD_PSM_FATAL;
            fail(job, AXD_PSM_WAKE_STATE);
        } else {
            if ((job->context.main_control & 0x02000000u) == 0 && job->context.wake_hint == 0)
                job->context.wake_hint = 1;
            if (job->command == AXD_PSM_WAIT_WAKE)
                done(job, AXD_PSM_WOKEN);
            else
                job->stage = job->after_wake;
        }
        break;
    case ENABLE_EXTRA_READ:
        read32(job, control_offset(job));
        job->stage = SET_ENABLE;
        break;
    case SET_ENABLE:
        mctrl(job, 1, 1);
        job->stage = ENABLE_STATUS;
        break;
    case ENABLE_STATUS:
        write32(job, status_offset(job), 1);
        job->stage = ENABLE_CONTROL_READBACK;
        break;
    case ENABLE_CONTROL_READBACK:
        read32(job, control_offset(job));
        job->stage = ENABLE_STATUS_READBACK;
        break;
    case ENABLE_STATUS_READBACK:
        read32(job, status_offset(job));
        job->stage = ENABLE_AFTER_READBACK;
        break;
    case ENABLE_AFTER_READBACK:
        if (job->bank) {
            done(job, AXD_PSM_ENABLED);
        } else {
            job->repetitions = 0;
            job->stage = ENABLE_SHM;
        }
        break;
    case ENABLE_SHM:
        shm_state(job);
        job->repetitions++; /* Queued SHM-read sequences, bounded at 1001; not completed-I/O count. */
        job->stage = ENABLE_SHM_CHECK;
        break;
    case ENABLE_SHM_CHECK:
        if (job->last_read == 3 && job->repetitions < 1001) {
            add(job, AXD_D11_DELAY_US, 0, 10);
            job->stage = ENABLE_SHM;
        } else if (job->last_read != 2) {
            if ((job->context.timeout_flags & 0x2000u) == 0) {
                if (job->context.fatal_reason == 0)
                    job->context.fatal_reason = 0x34;
                job->pending_callouts |= AXD_PSM_FATAL;
            }
            fail(job, AXD_PSM_ENABLE_STATE);
        } else {
            job->stage = ENABLE_POST;
        }
        break;
    case ENABLE_POST:
        job->context.wake_reasons &= ~4u;
        if (job->context.wake_reasons == 0)
            job->pending_callouts |= AXD_PSM_WAKE_CONTROL;
        if (job->context.mbss_enabled)
            job->pending_callouts |= AXD_PSM_MBSS_PRQ;
        if (job->pending_callouts != 0)
            job->phase = AXD_PSM_DEPENDENCY;
        else
            done(job, AXD_PSM_ENABLED);
        break;
    default:
        fail(job, AXD_PSM_BAD_ARGUMENT);
        break;
    }
}
int axd_psm_control_begin(struct axd_psm_control *job,
                          enum axd_psm_command command,
                          const struct axd_psm_context *context)
{
    const struct axd_psm_control empty = {0};
    struct axd_psm_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context; /* Permit a new job from its previous context. */
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_PSM_BAD_ARGUMENT);
    if (context == NULL || command < AXD_PSM_WAIT_WAKE || command > AXD_PSM_ENABLE_EXT ||
        context->band_present > 1 || context->mute_override > 1 ||
        context->mirror_enabled > 1 || context->wake_hint > 1 || context->mbss_enabled > 1)
        return -1;
    if (context->core != 130 || context->minor > 1) {
        job->error = AXD_PSM_UNSUPPORTED;
        return -1;
    }
    job->context = *context;
    job->command = command;
    job->bank = command == AXD_PSM_SUSPEND_EXT || command == AXD_PSM_ENABLE_EXT;
    job->wake_limit = ((uint32_t)context->wake_budget_us + 9u) / 10u;
    job->stage = command == AXD_PSM_WAIT_WAKE ? WAKE_DELAY : INITIAL_READ;
    job->error = AXD_PSM_NO_ERROR;
    job->phase = AXD_PSM_RUNNING;
    return 0;
}
enum axd_psm_phase axd_psm_control_step(struct axd_psm_control *job,
                                        axd_d11_io_fn io, void *context)
{
    const struct axd_d11_operation *op;
    uint32_t value = 0;
    if (job == NULL)
        return AXD_PSM_INVALID;
    if (job->phase != AXD_PSM_RUNNING)
        return job->phase;
    if (job->cursor == job->queued) {
        prepare(job);
        if (job->phase != AXD_PSM_RUNNING || job->queued == 0)
            return job->phase;
    }
    if (io == NULL) {
        fail(job, AXD_PSM_BAD_ARGUMENT);
        return job->phase;
    }
    op = &job->operations[job->cursor];
    job->attempted_ops++;
    if (io(context, op, &value) != 0) {
        fail(job, AXD_PSM_IO_ERROR);
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
enum axd_psm_phase axd_psm_control_cancel(struct axd_psm_control *job)
{
    if (job == NULL)
        return AXD_PSM_INVALID;
    if (job->phase == AXD_PSM_RUNNING)
        job->phase = AXD_PSM_CANCELLED;
    return job->phase;
}
