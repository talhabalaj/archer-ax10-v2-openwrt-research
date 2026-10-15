/* SPDX-License-Identifier: MIT */
#include "phy_reset.h"

enum stage {
    ENTRY, SAVE_REVISION, SAVE_PHY, SAVE_MODULE, SAVE_TABLE, SAVE_CALLBACK, PREPARE, PULSE,
    BANDWIDTH_PHY, BANDWIDTH, ANALOG_PHY, ANALOG_MODULE, ANALOG_TABLE,
    ANALOG_CALLBACK, ANALOG_CONTEXT, ANALOG_CONTEXT_PHY,
    RESTORE_REVISION, RESTORE_PHY, RESTORE_MODULE, RESTORE_TABLE, RESTORE_CALLBACK,
    BF_FLAGS, BF_PUBLIC, BF_POINTER, BF_ENABLED, BF_BEGIN, FINISH,
    RUN_IDLE, RUN_CLOCK, RUN_PHY, RUN_BF
};
struct relay { struct axd_phy_reset *job; axd_phy_reset_io_fn io; void *context; };
static void fail(struct axd_phy_reset *job, enum axd_reset_error error)
{
    job->phase = AXD_RESET_FAILED;
    job->error = error;
}
static int access_field(struct axd_phy_reset *job, enum axd_reset_field field,
                         unsigned known)
{
    job->last_field = field;
    job->metadata_accesses++;
    if (known == 1)
        return 1;
    if (known == 0)
        job->phase = AXD_RESET_DEPENDENCY;
    else
        fail(job, AXD_RESET_BAD_ARGUMENT);
    return 0;
}
static int pointer(struct axd_phy_reset *job, enum axd_reset_field field,
                    struct axd_wake_u32 value)
{
    if (!access_field(job, field, value.known))
        return 0;
    if (value.value != 0)
        return 1;
    fail(job, AXD_RESET_BINDING);
    return 0;
}
static int callback(struct axd_phy_reset *job, enum axd_reset_field field,
                     struct axd_wake_u8 value)
{
    if (!access_field(job, field, value.known))
        return 0;
    if (value.value <= 1)
        return 1;
    fail(job, AXD_RESET_BAD_ARGUMENT);
    return 0;
}
static int perform(struct relay *relay, const struct axd_phy_reset_operation *operation,
                     uint32_t *value)
{
    if (relay->io == NULL) {
        fail(relay->job, AXD_RESET_BAD_ARGUMENT);
        return -1;
    }
    relay->job->attempted_ops++;
    if (relay->io(relay->context, operation, value) != 0) {
        fail(relay->job, AXD_RESET_IO_ERROR);
        return -1;
    }
    relay->job->completed_ops++;
    return 0;
}
static int d11_io(void *opaque, const struct axd_d11_operation *op, uint32_t *value)
{
    const struct axd_phy_reset_operation converted = {
        op->kind, op->kind == AXD_D11_DELAY_US ? AXD_RESET_NO_REGION : AXD_RESET_D11,
        op->offset, op->value
    };
    return perform(opaque, &converted, value);
}
static int clock_io(void *opaque, const struct axd_phy_clock_operation *op, uint32_t *value)
{
    enum axd_reset_region region = AXD_RESET_NO_REGION;
    struct axd_phy_reset_operation converted;
    if (op->region == AXD_PHY_CLOCK_CURRENT_WRAPPER)
        region = AXD_RESET_CURRENT_WRAPPER;
    else if (op->region == AXD_PHY_CLOCK_UNIT1_WRAPPER)
        region = AXD_RESET_UNIT1_WRAPPER;
    converted.kind = op->kind; converted.region = region;
    converted.offset = op->offset; converted.value = op->value;
    return perform(opaque, &converted, value);
}
static void idle(struct axd_phy_reset *job, enum axd_tssi_command command, unsigned resume)
{
    job->context.idle.callback_present = job->context.idle_callback.value;
    job->context.idle.callback_binding_required = 1;
    job->context.idle.phy_identity = job->context.phy_pointer.value;
    job->context.idle.callback_context = job->context.idle_context;
    job->context.idle.callback_phy = job->context.idle_context_phy;
    if (axd_idle_tssi_begin(&job->idle_job, command, &job->context.idle) != 0) {
        fail(job, AXD_RESET_IDLE_ERROR);
        return;
    }
    job->after_child = resume; job->stage = RUN_IDLE;
}
static void clock(struct axd_phy_reset *job, enum axd_phy_clock_command command, unsigned resume)
{
    if (axd_phy_clock_begin(&job->clock_job, command, &job->context.clock) != 0) {
        fail(job, AXD_RESET_CLOCK_ERROR);
        return;
    }
    job->after_child = resume; job->stage = RUN_CLOCK;
}
static void phy(struct axd_phy_reset *job, enum axd_phy_command command, unsigned resume)
{
    if (axd_phy_register_begin(&job->phy_job, command, 0, 0, 0,
                               &job->context.idle.tssi.hardware.phy) != 0) {
        fail(job, AXD_RESET_PHY_ERROR);
        return;
    }
    job->after_child = resume; job->stage = RUN_PHY;
}
static void child_step(struct axd_phy_reset *job, struct relay *relay)
{
    if (job->stage == RUN_IDLE) {
        enum axd_idle_phase phase = axd_idle_tssi_step(&job->idle_job, d11_io, relay);
        job->context.idle = job->idle_job.context;
        job->pending_callouts |= job->idle_job.pending_callouts;
        if (job->phase != AXD_RESET_RUNNING)
            return;
        if (phase == AXD_IDLE_TRACE_COMPLETE) {
            if (job->idle_job.command == AXD_TSSI_SNAPSHOT)
                job->snapshot_completed = 1;
            else
                job->restore_completed = 1;
            job->stage = job->after_child;
        } else if (phase == AXD_IDLE_DEPENDENCY)
            job->phase = AXD_RESET_DEPENDENCY;
        else if (phase == AXD_IDLE_CANCELLED)
            job->phase = AXD_RESET_CANCELLED;
        else if (phase != AXD_IDLE_RUNNING)
            fail(job, AXD_RESET_IDLE_ERROR);
    } else if (job->stage == RUN_CLOCK) {
        enum axd_phy_clock_phase phase = axd_phy_clock_step(&job->clock_job, clock_io, relay);
        job->context.clock = job->clock_job.context;
        if (job->phase != AXD_RESET_RUNNING)
            return;
        if (phase == AXD_PHY_CLOCK_TRACE_COMPLETE) {
            if (job->clock_job.command == AXD_PHY_CLOCK_RESET_PREPARE)
                job->prepare_completed = 1;
            else
                job->bandwidth_recorded = 1;
            job->stage = job->after_child;
        } else if (phase == AXD_PHY_CLOCK_DEPENDENCY)
            job->phase = AXD_RESET_DEPENDENCY;
        else if (phase == AXD_PHY_CLOCK_CANCELLED)
            job->phase = AXD_RESET_CANCELLED;
        else if (phase != AXD_PHY_CLOCK_RUNNING)
            fail(job, AXD_RESET_CLOCK_ERROR);
    } else if (job->stage == RUN_PHY) {
        enum axd_phy_phase phase = axd_phy_register_step(&job->phy_job, d11_io, relay);
        job->context.idle.tssi.hardware.phy = job->phy_job.context;
        if (job->phase != AXD_RESET_RUNNING)
            return;
        if (phase == AXD_PHY_TRACE_COMPLETE) {
            if (job->phy_job.command == AXD_PHY_RESET_PULSE)
                job->pulse_completed = 1;
            else
                job->analog_completed = 1;
            job->stage = job->after_child;
        } else if (phase == AXD_PHY_DEPENDENCY)
            job->phase = AXD_RESET_DEPENDENCY;
        else if (phase == AXD_PHY_CANCELLED)
            job->phase = AXD_RESET_CANCELLED;
        else if (phase != AXD_PHY_RUNNING)
            fail(job, AXD_RESET_PHY_ERROR);
    } else {
        enum axd_bf_phase phase = axd_bf_clean_step(&job->bf_job, d11_io, relay);
        job->context.bf = job->bf_job.context;
        job->context.idle.tssi.hardware.mac = job->bf_job.context.mac;
        job->pending_callouts |= job->bf_job.pending_callouts;
        if (job->phase != AXD_RESET_RUNNING)
            return;
        if (phase == AXD_BF_TRACE_COMPLETE) {
            job->bf_completed = 1;
            job->stage = FINISH;
        } else if (phase == AXD_BF_DEPENDENCY)
            job->phase = AXD_RESET_DEPENDENCY;
        else if (phase == AXD_BF_CANCELLED)
            job->phase = AXD_RESET_CANCELLED;
        else if (phase != AXD_BF_RUNNING)
            fail(job, AXD_RESET_BF_ERROR);
    }
}
int axd_phy_reset_begin(struct axd_phy_reset *job,
                         const struct axd_phy_reset_context *context)
{
    const struct axd_phy_reset empty = {0};
    struct axd_phy_reset_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) { snapshot = *context; context = &snapshot; }
    *job = empty;
    fail(job, AXD_RESET_BAD_ARGUMENT);
    if (context == NULL)
        return -1;
    job->context = *context;
    if (context->band_type != 0xb || context->band_revision != 51) {
        fail(job, AXD_RESET_UNSUPPORTED);
        return -1;
    }
    if (axd_phy_clock_begin(&job->clock_job, AXD_PHY_CLOCK_RESET_PREPARE, &context->clock) != 0) {
        fail(job, AXD_RESET_CLOCK_ERROR);
        return -1;
    }
    if (axd_phy_register_begin(&job->phy_job, AXD_PHY_RESET_PULSE, 0, 0, 0,
                               &context->idle.tssi.hardware.phy) != 0 ||
        context->clock.core != context->idle.tssi.hardware.phy.core ||
        context->clock.minor != context->idle.tssi.hardware.phy.minor) {
        fail(job, AXD_RESET_PHY_ERROR);
        return -1;
    }
    job->stage = ENTRY;
    job->phase = AXD_RESET_RUNNING;
    job->error = AXD_RESET_NO_ERROR;
    return 0;
}
enum axd_reset_phase axd_phy_reset_step(struct axd_phy_reset *job,
                                        axd_phy_reset_io_fn io, void *context)
{
    struct relay relay = {job, io, context};
    if (job == NULL)
        return AXD_RESET_INVALID;
    if (job->phase != AXD_RESET_RUNNING)
        return job->phase;
    switch (job->stage) {
    case ENTRY: {
        unsigned i;
        for (i = 0; i < 4; i++) job->context.idle.tssi.saved[i] = 0;
        job->context.idle.tssi.valid = 0;
        job->stage = SAVE_REVISION;
        break;
    }
    case SAVE_REVISION:
    case RESTORE_REVISION:
        if (access_field(job, AXD_RESET_PUBLIC_REVISION, job->context.public_revision.known))
            job->stage = job->context.public_revision.value == 51 ?
                (job->stage == SAVE_REVISION ? SAVE_PHY : RESTORE_PHY) :
                (job->stage == SAVE_REVISION ? PREPARE : BF_FLAGS);
        break;
    case SAVE_PHY:
    case RESTORE_PHY:
        if (pointer(job, AXD_RESET_PHY_POINTER, job->context.phy_pointer))
            job->stage = job->stage == SAVE_PHY ? SAVE_MODULE : RESTORE_MODULE;
        break;
    case SAVE_MODULE:
    case RESTORE_MODULE:
        if (pointer(job, AXD_RESET_IDLE_MODULE, job->context.idle_module))
            job->stage = job->stage == SAVE_MODULE ? SAVE_TABLE : RESTORE_TABLE;
        break;
    case SAVE_TABLE:
    case RESTORE_TABLE:
        if (pointer(job, AXD_RESET_IDLE_TABLE, job->context.idle_table))
            job->stage = job->stage == SAVE_TABLE ? SAVE_CALLBACK : RESTORE_CALLBACK;
        break;
    case SAVE_CALLBACK:
    case RESTORE_CALLBACK:
        if (callback(job, AXD_RESET_IDLE_CALLBACK, job->context.idle_callback))
            idle(job, job->stage == SAVE_CALLBACK ? AXD_TSSI_SNAPSHOT : AXD_TSSI_RESTORE,
                 job->stage == SAVE_CALLBACK ? PREPARE : BF_FLAGS);
        break;
    case PREPARE:
        clock(job, AXD_PHY_CLOCK_RESET_PREPARE, PULSE);
        break;
    case PULSE:
        phy(job, AXD_PHY_RESET_PULSE, BANDWIDTH_PHY);
        break;
    case BANDWIDTH_PHY:
        if (pointer(job, AXD_RESET_PHY_POINTER, job->context.phy_pointer))
            job->stage = BANDWIDTH;
        break;
    case BANDWIDTH:
        clock(job, AXD_PHY_CLOCK_RECORD_BANDWIDTH, ANALOG_PHY);
        break;
    case ANALOG_PHY:
        if (access_field(job, AXD_RESET_PHY_POINTER, job->context.phy_pointer.known))
            job->stage = job->context.phy_pointer.value == 0 ? RESTORE_REVISION : ANALOG_MODULE;
        break;
    case ANALOG_MODULE:
        if (pointer(job, AXD_RESET_ANALOG_MODULE, job->context.analog_module))
            job->stage = ANALOG_TABLE;
        break;
    case ANALOG_TABLE:
        if (pointer(job, AXD_RESET_ANALOG_TABLE, job->context.analog_table))
            job->stage = ANALOG_CALLBACK;
        break;
    case ANALOG_CALLBACK:
        if (callback(job, AXD_RESET_ANALOG_CALLBACK, job->context.analog_callback)) {
            if (job->context.analog_callback.value)
                job->stage = ANALOG_CONTEXT;
            else
                job->stage = RESTORE_REVISION;
        }
        break;
    case ANALOG_CONTEXT:
        if (pointer(job, AXD_RESET_ANALOG_CONTEXT, job->context.analog_context))
            job->stage = ANALOG_CONTEXT_PHY;
        break;
    case ANALOG_CONTEXT_PHY:
        if (pointer(job, AXD_RESET_ANALOG_CONTEXT_PHY, job->context.analog_context_phy)) {
            if (job->context.analog_context_phy.value != job->context.phy_pointer.value)
                fail(job, AXD_RESET_BINDING);
            else
                phy(job, AXD_PHY_ANALOG_RESET, RESTORE_REVISION);
        }
        break;
    case BF_FLAGS:
        if (access_field(job, AXD_RESET_BF_FLAGS, job->context.bf_flags.known))
            job->stage = (job->context.bf_flags.value & 1u) != 0 ? BF_PUBLIC : FINISH;
        break;
    case BF_PUBLIC:
        if (access_field(job, AXD_RESET_PUBLIC_BF, job->context.public_bf.known))
            job->stage = job->context.public_bf.value != 0 ? BF_POINTER : FINISH;
        break;
    case BF_POINTER:
        if (pointer(job, AXD_RESET_TXBF_POINTER, job->context.txbf_pointer))
            job->stage = BF_ENABLED;
        break;
    case BF_ENABLED:
        if (access_field(job, AXD_RESET_TXBF_ENABLED, job->context.txbf_enabled.known))
            job->stage = job->context.txbf_enabled.value != 0 ? BF_BEGIN : FINISH;
        break;
    case BF_BEGIN:
        if (job->context.bf.txbf_identity != job->context.txbf_pointer.value ||
            job->context.bf.wlc_identity != job->context.wlc_identity ||
            job->context.bf.hardware_identity != job->context.hardware_identity) {
            fail(job, AXD_RESET_BINDING);
        } else {
            job->context.bf.mac = job->context.idle.tssi.hardware.mac;
            job->context.bf.policy = job->context.idle.tssi.hardware.policy;
            if (axd_bf_clean_begin(&job->bf_job, &job->context.bf) != 0)
                fail(job, AXD_RESET_BF_ERROR);
            else
                job->stage = RUN_BF;
        }
        break;
    case FINISH:
        job->phase = AXD_RESET_TRACE_COMPLETE;
        break;
    case RUN_IDLE:
    case RUN_CLOCK:
    case RUN_PHY:
    case RUN_BF:
        child_step(job, &relay);
        break;
    default:
        fail(job, AXD_RESET_BAD_ARGUMENT);
        break;
    }
    return job->phase;
}
enum axd_reset_phase axd_phy_reset_cancel(struct axd_phy_reset *job)
{
    if (job == NULL)
        return AXD_RESET_INVALID;
    if (job->phase == AXD_RESET_RUNNING) {
        job->phase = AXD_RESET_CANCELLED;
        if (job->stage == RUN_IDLE) axd_idle_tssi_cancel(&job->idle_job);
        else if (job->stage == RUN_CLOCK) axd_phy_clock_cancel(&job->clock_job);
        else if (job->stage == RUN_PHY) axd_phy_register_cancel(&job->phy_job);
        else if (job->stage == RUN_BF) axd_bf_clean_cancel(&job->bf_job);
    }
    return job->phase;
}
