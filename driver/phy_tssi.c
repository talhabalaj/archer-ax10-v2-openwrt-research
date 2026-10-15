/* SPDX-License-Identifier: MIT */
#include "phy_tssi.h"

enum stage { ENTRY, RUN_PHY, RUN_TABLE, NEXT_SLOT, FINISH };
struct relay {
    struct axd_phy_tssi *job;
    axd_d11_io_fn io;
    void *context;
};
static void fail(struct axd_phy_tssi *job, enum axd_tssi_error error)
{
    job->phase = AXD_TSSI_FAILED;
    job->error = error;
}
static int perform(void *opaque, const struct axd_d11_operation *operation, uint32_t *value)
{
    struct relay *relay = opaque;
    if (relay->io == NULL) {
        fail(relay->job, AXD_TSSI_BAD_ARGUMENT);
        return -1;
    }
    relay->job->attempted_ops++;
    if (relay->io(relay->context, operation, value) != 0) {
        fail(relay->job, AXD_TSSI_IO_ERROR);
        return -1;
    }
    relay->job->completed_ops++;
    return 0;
}
static void select_slot(struct axd_phy_tssi *job)
{
    uint16_t selector;
    if (job->slot >= 2) {
        if (job->command == AXD_TSSI_RESTORE) {
            job->stage = NEXT_SLOT; /* Actual modify(ffff) emits nothing. */
        } else if (axd_phy_table_begin(&job->table_job, 0xffff, &job->context.hardware) != 0) {
            fail(job, AXD_TSSI_TABLE_ERROR);
        } else {
            job->stage = RUN_TABLE;
        }
        return;
    }
    selector = (uint16_t)(0x645u + 0x200u * job->slot);
    if (axd_phy_register_begin(&job->phy_job,
                              job->command == AXD_TSSI_SNAPSHOT ? AXD_PHY_READ : AXD_PHY_MODIFY,
                              selector, job->command == AXD_TSSI_SNAPSHOT ? 0 : 0x3ff,
                              job->command == AXD_TSSI_SNAPSHOT ? 0 : job->context.saved[job->slot],
                              &job->context.hardware.phy) != 0)
        fail(job, AXD_TSSI_PHY_ERROR);
    else
        job->stage = RUN_PHY;
}
static void commit(struct axd_phy_tssi *job, uint16_t value)
{
    job->context.saved[job->slot] = value & 0x3ffu;
    job->context.valid |= (uint8_t)(1u << job->slot);
}
static void child_step(struct axd_phy_tssi *job, struct relay *relay)
{
    if (job->stage == RUN_PHY) {
        enum axd_phy_phase phase = axd_phy_register_step(&job->phy_job, perform, relay);
        job->context.hardware.phy = job->phy_job.context;
        if (job->command == AXD_TSSI_SNAPSHOT && job->phy_job.result_valid)
            commit(job, job->phy_job.result);
        if (job->phase != AXD_TSSI_RUNNING)
            return;
        if (phase == AXD_PHY_TRACE_COMPLETE)
            job->stage = NEXT_SLOT;
        else if (phase == AXD_PHY_FAILED || phase == AXD_PHY_INVALID || phase == AXD_PHY_DEPENDENCY)
            fail(job, AXD_TSSI_PHY_ERROR);
        else if (phase == AXD_PHY_CANCELLED)
            job->phase = AXD_TSSI_CANCELLED;
    } else {
        enum axd_phy_table_phase phase = axd_phy_table_step(&job->table_job, perform, relay);
        job->context.hardware = job->table_job.context;
        job->pending_callouts |= job->table_job.pending_callouts;
        if (job->phase != AXD_TSSI_RUNNING)
            return;
        if (phase == AXD_TABLE_TRACE_COMPLETE) {
            commit(job, (uint16_t)job->table_job.result);
            job->stage = NEXT_SLOT;
        } else if (phase == AXD_TABLE_FAILED || phase == AXD_TABLE_INVALID)
            fail(job, AXD_TSSI_TABLE_ERROR);
        else if (phase == AXD_TABLE_CANCELLED)
            job->phase = AXD_TSSI_CANCELLED;
        else if (phase == AXD_TABLE_DEPENDENCY)
            job->phase = AXD_TSSI_DEPENDENCY;
    }
}
int axd_phy_tssi_begin(struct axd_phy_tssi *job, enum axd_tssi_command command,
                       const struct axd_tssi_context *context)
{
    const struct axd_phy_tssi empty = {0};
    struct axd_tssi_context snapshot;
    unsigned count, active;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context;
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_TSSI_BAD_ARGUMENT);
    if (context == NULL || command < AXD_TSSI_SNAPSHOT || command > AXD_TSSI_RESTORE || context->valid > 0xf)
        return -1;
    job->context = *context;
    if (axd_phy_table_begin(&job->table_job, 0xffff, &context->hardware) != 0) {
        fail(job, AXD_TSSI_TABLE_ERROR);
        return -1;
    }
    count = context->hardware.phy.phy_core_count;
    active = (1u << count) - 1u;
    if (command == AXD_TSSI_RESTORE && (context->valid & active) != active) {
        fail(job, AXD_TSSI_MISSING_SNAPSHOT);
        return -1;
    }
    job->command = command;
    job->slot = count - 1u;
    job->stage = ENTRY;
    job->phase = AXD_TSSI_RUNNING;
    job->error = AXD_TSSI_NO_ERROR;
    return 0;
}
enum axd_tssi_phase axd_phy_tssi_step(struct axd_phy_tssi *job,
                                     axd_d11_io_fn io, void *context)
{
    struct relay relay = {job, io, context};
    if (job == NULL)
        return AXD_TSSI_INVALID;
    if (job->phase != AXD_TSSI_RUNNING)
        return job->phase;
    switch (job->stage) {
    case ENTRY:
        if (job->command == AXD_TSSI_SNAPSHOT)
            job->context.valid &= (uint8_t)~((1u << job->context.hardware.phy.phy_core_count) - 1u);
        select_slot(job);
        break;
    case RUN_PHY:
    case RUN_TABLE:
        child_step(job, &relay);
        break;
    case NEXT_SLOT:
        if (job->slot == 0)
            job->stage = FINISH;
        else {
            job->slot--;
            select_slot(job);
        }
        break;
    case FINISH:
        job->phase = AXD_TSSI_TRACE_COMPLETE;
        break;
    default:
        fail(job, AXD_TSSI_BAD_ARGUMENT);
        break;
    }
    return job->phase;
}
enum axd_tssi_phase axd_phy_tssi_cancel(struct axd_phy_tssi *job)
{
    if (job == NULL)
        return AXD_TSSI_INVALID;
    if (job->phase == AXD_TSSI_RUNNING) {
        job->phase = AXD_TSSI_CANCELLED;
        if (job->stage == RUN_PHY)
            axd_phy_register_cancel(&job->phy_job);
        else if (job->stage == RUN_TABLE)
            axd_phy_table_cancel(&job->table_job);
    }
    return job->phase;
}
