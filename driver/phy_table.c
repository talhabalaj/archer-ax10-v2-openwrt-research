/* SPDX-License-Identifier: MIT */
#include "phy_table.h"

enum stage {
    ENTRY, SUSPEND, READ_GUARD, GUARD_DECIDE, PROGRAM_ID, PROGRAM_INDEX,
    READ_LOW, READ_HIGH, CLEAR_GUARD, ENABLE_MAC, FINISH, RUN_PHY, RUN_MAC
};
struct relay {
    struct axd_phy_table *job;
    axd_d11_io_fn io;
    void *context;
};
static void fail(struct axd_phy_table *job, enum axd_phy_table_error error)
{
    job->phase = AXD_TABLE_FAILED;
    job->error = error;
}
static void dependency(struct axd_phy_table *job, enum axd_phy_table_dependency which)
{
    job->phase = AXD_TABLE_DEPENDENCY;
    job->dependency = which;
}
static void add(struct axd_phy_table *job, enum axd_d11_kind kind,
                uint32_t offset, uint32_t value)
{
    const struct axd_d11_operation operation = {kind, offset, value};
    job->operations[job->queued++] = operation;
}
static void phy(struct axd_phy_table *job, enum axd_phy_command command,
                uint16_t selector, uint16_t mask, uint16_t value, unsigned resume)
{
    if (axd_phy_register_begin(&job->phy_job, command, selector, mask, value, &job->context.phy) != 0) {
        fail(job, AXD_TABLE_PHY_ERROR);
        return;
    }
    job->after_child = resume;
    job->stage = RUN_PHY;
}
static void mac(struct axd_phy_table *job, enum axd_psm_command command, unsigned resume)
{
    if (!job->context.mac_context_valid) {
        dependency(job, AXD_TABLE_MAC_CONTEXT);
        return;
    }
    if (axd_psm_control_begin_policy(&job->mac_job, command, &job->context.mac,
                                     &job->context.policy) != 0) {
        fail(job, AXD_TABLE_MAC_ERROR);
        return;
    }
    job->after_child = resume;
    job->stage = RUN_MAC;
}
static void packed(struct axd_phy_table *job, uint16_t selector, uint16_t value)
{
    if (job->context.shared_bus_word == 1) {
        job->context.phy.bookkeeping = (uint16_t)(job->context.phy.bookkeeping + 1u);
        if (job->context.phy.bookkeeping >= job->context.write_threshold) {
            job->context.phy.bookkeeping = 0;
            add(job, AXD_D11_READ16, 0x3e0, 0);
            add(job, AXD_D11_FULL_BARRIER, 0, 0);
        }
    }
    add(job, AXD_D11_STORE_BARRIER, 0, 0);
    add(job, AXD_D11_WRITE32, 0x3fc, selector | ((uint32_t)value << 16));
}
static void prepare(struct axd_phy_table *job)
{
    job->queued = job->cursor = 0;
    switch (job->stage) {
    case ENTRY:
        add(job, AXD_D11_READ32, 0x120, 0);
        add(job, AXD_D11_FULL_BARRIER, 0, 0);
        job->stage = SUSPEND;
        break;
    case SUSPEND:
        if (job->initial_control & 1u)
            mac(job, AXD_PSM_SUSPEND_MAIN, READ_GUARD);
        else
            job->stage = READ_GUARD;
        break;
    case READ_GUARD:
        phy(job, AXD_PHY_READ, 0x19e, 0, 0, GUARD_DECIDE);
        break;
    case GUARD_DECIDE:
        if ((job->phy_job.result & 2u) == 0)
            phy(job, AXD_PHY_MODIFY, 0x19e, 2, 2, PROGRAM_ID);
        else
            job->stage = PROGRAM_ID;
        break;
    case PROGRAM_ID:
        packed(job, 0xd, 0x29);
        job->stage = PROGRAM_INDEX;
        break;
    case PROGRAM_INDEX:
        packed(job, 0xe, (uint16_t)job->index);
        job->stage = READ_LOW;
        break;
    case READ_LOW:
        phy(job, AXD_PHY_READ, 0xf, 0, 0, READ_HIGH);
        break;
    case READ_HIGH:
        phy(job, AXD_PHY_READ, 0x10, 0, 0, CLEAR_GUARD);
        break;
    case CLEAR_GUARD:
        if (job->changed_guard)
            phy(job, AXD_PHY_MODIFY, 0x19e, 2, 0, ENABLE_MAC);
        else
            job->stage = ENABLE_MAC;
        break;
    case ENABLE_MAC:
        if (job->initial_control & 1u)
            mac(job, AXD_PSM_ENABLE_MAIN, FINISH);
        else
            job->stage = FINISH;
        break;
    case FINISH:
        job->phase = AXD_TABLE_TRACE_COMPLETE;
        break;
    default:
        fail(job, AXD_TABLE_BAD_ARGUMENT);
        break;
    }
}
static int perform(void *opaque, const struct axd_d11_operation *operation, uint32_t *value)
{
    struct relay *relay = opaque;
    struct axd_phy_table *job = relay->job;
    if (relay->io == NULL) {
        fail(job, AXD_TABLE_BAD_ARGUMENT);
        return -1;
    }
    job->attempted_ops++;
    if (relay->io(relay->context, operation, value) != 0) {
        fail(job, AXD_TABLE_IO_ERROR);
        return -1;
    }
    job->completed_ops++;
    if (operation->kind == AXD_D11_READ32)
        job->last_read = *value;
    else if (operation->kind == AXD_D11_READ16) {
        job->last_read = *value & 0xffffu;
        if (operation->offset == 0x3e0)
            job->flush_reads++;
    }
    if (job->stage == RUN_PHY && job->after_child == PROGRAM_ID &&
        operation->kind == AXD_D11_WRITE16 && operation->offset == 0x3fe)
        job->changed_guard = 1;
    return 0;
}
static void child_step(struct axd_phy_table *job, struct relay *relay)
{
    if (job->stage == RUN_PHY) {
        enum axd_phy_phase phase = axd_phy_register_step(&job->phy_job, perform, relay);
        job->context.phy = job->phy_job.context;
        if (job->phy_job.result_valid && job->after_child == READ_HIGH) {
            job->low_half = job->phy_job.result;
            job->low_valid = 1;
        } else if (job->phy_job.result_valid && job->after_child == CLEAR_GUARD) {
            job->high_half = job->phy_job.result;
            job->result = job->low_half | ((uint32_t)job->high_half << 16);
            job->word_valid = 1;
        }
        if (job->phase != AXD_TABLE_RUNNING)
            return;
        if (phase == AXD_PHY_TRACE_COMPLETE)
            job->stage = job->after_child;
        else if (phase == AXD_PHY_FAILED || phase == AXD_PHY_INVALID)
            fail(job, AXD_TABLE_PHY_ERROR);
        else if (phase == AXD_PHY_CANCELLED)
            job->phase = AXD_TABLE_CANCELLED;
        else if (phase == AXD_PHY_DEPENDENCY)
            fail(job, AXD_TABLE_PHY_ERROR); /* Internal selectors must be direct. */
    } else {
        enum axd_psm_phase phase = axd_psm_control_step(&job->mac_job, perform, relay);
        job->context.mac = job->mac_job.context;
        job->pending_callouts |= job->mac_job.pending_callouts;
        if (job->phase != AXD_TABLE_RUNNING)
            return;
        if (phase == AXD_PSM_DONE)
            job->stage = job->after_child;
        else if (phase == AXD_PSM_FAILED || phase == AXD_PSM_INVALID)
            fail(job, AXD_TABLE_MAC_ERROR);
        else if (phase == AXD_PSM_CANCELLED)
            job->phase = AXD_TABLE_CANCELLED;
        else if (phase == AXD_PSM_DEPENDENCY)
            dependency(job, AXD_TABLE_MAC_CALLOUT);
    }
}
int axd_phy_table_begin(struct axd_phy_table *job, uint32_t index,
                        const struct axd_phy_table_context *context)
{
    const struct axd_phy_table empty = {0};
    struct axd_phy_table_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context;
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_TABLE_BAD_ARGUMENT);
    if (context == NULL || context->mac_context_valid > 1)
        return -1;
    job->context = *context;
    if (axd_phy_register_begin(&job->phy_job, AXD_PHY_READ, 0, 0, 0, &context->phy) != 0) {
        fail(job, AXD_TABLE_PHY_ERROR);
        return -1;
    }
    if (context->mac_context_valid &&
        (context->mac.core != context->phy.core || context->mac.minor != context->phy.minor ||
         axd_psm_control_begin_policy(&job->mac_job, AXD_PSM_WAIT_WAKE,
                                      &context->mac, &context->policy) != 0)) {
        fail(job, AXD_TABLE_MAC_ERROR);
        return -1;
    }
    job->index = index;
    job->stage = ENTRY;
    job->phase = AXD_TABLE_RUNNING;
    job->error = AXD_TABLE_NO_ERROR;
    return 0;
}
enum axd_phy_table_phase axd_phy_table_step(struct axd_phy_table *job,
                                           axd_d11_io_fn io, void *context)
{
    struct relay relay = {job, io, context};
    const struct axd_d11_operation *operation;
    uint32_t value = 0;
    if (job == NULL)
        return AXD_TABLE_INVALID;
    if (job->phase != AXD_TABLE_RUNNING)
        return job->phase;
    if (job->stage == RUN_PHY || job->stage == RUN_MAC) {
        child_step(job, &relay);
        return job->phase;
    }
    if (job->cursor == job->queued) {
        prepare(job);
        if (job->phase != AXD_TABLE_RUNNING || job->queued == 0)
            return job->phase;
    }
    operation = &job->operations[job->cursor];
    if (perform(&relay, operation, &value) != 0)
        return job->phase;
    if (operation->kind == AXD_D11_READ32 && operation->offset == 0x120)
        job->initial_control = value;
    job->cursor++;
    return job->phase;
}
enum axd_phy_table_phase axd_phy_table_cancel(struct axd_phy_table *job)
{
    if (job == NULL)
        return AXD_TABLE_INVALID;
    if (job->phase == AXD_TABLE_RUNNING) {
        job->phase = AXD_TABLE_CANCELLED;
        if (job->stage == RUN_PHY)
            axd_phy_register_cancel(&job->phy_job);
        else if (job->stage == RUN_MAC)
            axd_psm_control_cancel(&job->mac_job);
    }
    return job->phase;
}
