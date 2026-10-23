/* SPDX-License-Identifier: MIT */
#include "phy_register.h"

enum stage {
    ENTRY, READ_BEGIN, READ_FINISH, MODIFY_BEGIN, MODIFY_READ, MODIFY_WRITE,
    TSSI_NEXT, PULSE_ASSERT, PULSE_DATA, PULSE_WRITE, PULSE_DELAY, FINISH
};
static void add(struct axd_phy_register *job, enum axd_d11_kind kind,
                uint32_t offset, uint32_t value)
{
    const struct axd_d11_operation op = {kind, offset, value};
    job->operations[job->queued++] = op;
}
static void read16(struct axd_phy_register *job, uint32_t offset)
{
    add(job, AXD_D11_READ16, offset, 0);
    add(job, AXD_D11_FULL_BARRIER, 0, 0);
}
static void store16(struct axd_phy_register *job, uint32_t offset, uint16_t value)
{
    add(job, AXD_D11_STORE_BARRIER, 0, 0);
    add(job, AXD_D11_WRITE16, offset, value);
}
static void fail(struct axd_phy_register *job, enum axd_phy_error error)
{
    job->phase = AXD_PHY_FAILED;
    job->error = error;
}
static void tssi_begin(struct axd_phy_register *job)
{
    job->selector = (uint16_t)(0x645u + 0x200u * job->tssi_slot);
    job->after_read = TSSI_NEXT;
    if (job->command == AXD_PHY_TSSI_SNAPSHOT) {
        job->stage = READ_BEGIN;
    } else {
        job->mask = 0x3ff;
        job->value = job->context.saved_tssi[job->tssi_slot];
        job->stage = MODIFY_BEGIN;
    }
}
static void prepare(struct axd_phy_register *job)
{
    job->queued = job->cursor = 0;
    switch (job->stage) {
    case ENTRY:
        if ((job->command == AXD_PHY_READ && job->selector >= 0xc000) ||
            ((job->command == AXD_PHY_TSSI_SNAPSHOT || job->command == AXD_PHY_TSSI_RESTORE) &&
             job->context.phy_core_count > 2)) {
            job->dependency = AXD_PHY_TABLE_READ;
            job->phase = AXD_PHY_DEPENDENCY;
        } else if (job->command == AXD_PHY_READ) {
            job->after_read = FINISH;
            job->stage = READ_BEGIN;
        } else if (job->command == AXD_PHY_MODIFY) {
            job->after_read = FINISH;
            job->stage = job->selector == 0xffff ? FINISH : MODIFY_BEGIN;
        } else if (job->command == AXD_PHY_ANALOG_RESET) {
            store16(job, 0x3e6, 0);
            job->stage = FINISH;
        } else if (job->command == AXD_PHY_RESET_PULSE) {
            read16(job, 0x7c0);
            job->stage = PULSE_ASSERT;
        } else {
            job->tssi_slot = job->context.phy_core_count - 1u;
            if (job->command == AXD_PHY_TSSI_SNAPSHOT)
                job->context.snapshot_valid &= (uint8_t)~((1u << job->context.phy_core_count) - 1u);
            tssi_begin(job);
        }
        break;
    case READ_BEGIN:
        store16(job, 0x3fc, job->selector);
        read16(job, 0x3fe);
        job->stage = READ_FINISH;
        break;
    case READ_FINISH:
        job->stage = job->after_read;
        break;
    case MODIFY_BEGIN:
        store16(job, 0x3fc, job->selector);
        add(job, AXD_D11_STORE_BARRIER, 0, 0);
        job->stage = MODIFY_READ;
        break;
    case MODIFY_READ:
        read16(job, 0x3fe);
        job->stage = MODIFY_WRITE;
        break;
    case MODIFY_WRITE:
        add(job, AXD_D11_WRITE16, 0x3fe,
            (uint16_t)((job->last_read & (uint16_t)~job->mask) | (job->value & job->mask)));
        job->stage = job->after_read;
        break;
    case TSSI_NEXT:
        if (job->tssi_slot == 0) {
            job->stage = FINISH;
        } else {
            job->tssi_slot--;
            tssi_begin(job);
        }
        break;
    case PULSE_ASSERT:
        job->pulse_original = job->last_read;
        store16(job, 0x7c0, (uint16_t)(job->pulse_original | 0x80));
        store16(job, 0x3fc, 0x1409);
        job->stage = PULSE_DATA;
        break;
    case PULSE_DATA:
        read16(job, 0x3fe);
        job->stage = PULSE_WRITE;
        break;
    case PULSE_WRITE:
        store16(job, 0x3fe, (uint16_t)(job->last_read | 0x8000));
        store16(job, 0x7c0, (uint16_t)(job->pulse_original & (uint16_t)~0x80));
        job->stage = PULSE_DELAY;
        break;
    case PULSE_DELAY:
        add(job, AXD_D11_DELAY_US, 0, 100);
        job->stage = FINISH;
        break;
    case FINISH:
        job->phase = AXD_PHY_TRACE_COMPLETE;
        break;
    default:
        fail(job, AXD_PHY_BAD_ARGUMENT);
        break;
    }
}
int axd_phy_register_begin(struct axd_phy_register *job,
                           enum axd_phy_command command, uint16_t selector,
                           uint16_t mask, uint16_t value,
                           const struct axd_phy_context *context)
{
    static const struct axd_phy_register empty={0};
    struct axd_phy_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context;
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_PHY_BAD_ARGUMENT);
    if (context == NULL || command < AXD_PHY_READ || command > AXD_PHY_TSSI_RESTORE ||
        context->phy_core_count < 1 || context->phy_core_count > 4 || context->snapshot_valid > 3 ||
        (command == AXD_PHY_READ && (mask != 0 || value != 0)) ||
        (command != AXD_PHY_READ && command != AXD_PHY_MODIFY && (selector != 0 || mask != 0 || value != 0)))
        return -1;
    if (context->core != 130 || context->minor > 1 || context->phy_type != 0xb || context->phy_revision != 51) {
        job->error = AXD_PHY_UNSUPPORTED;
        return -1;
    }
    if (command == AXD_PHY_TSSI_RESTORE && context->phy_core_count <= 2 &&
        (context->snapshot_valid & ((1u << context->phy_core_count) - 1u)) !=
        ((1u << context->phy_core_count) - 1u)) {
        job->error = AXD_PHY_MISSING_SNAPSHOT;
        return -1;
    }
    job->context = *context;
    job->command = command;
    job->selector = selector;
    job->mask = mask;
    job->value = value;
    job->stage = ENTRY;
    job->phase = AXD_PHY_RUNNING;
    job->error = AXD_PHY_NO_ERROR;
    return 0;
}
enum axd_phy_phase axd_phy_register_step(struct axd_phy_register *job,
                                         axd_d11_io_fn io, void *context)
{
    const struct axd_d11_operation *op;
    uint32_t value = 0;
    if (job == NULL)
        return AXD_PHY_INVALID;
    if (job->phase != AXD_PHY_RUNNING)
        return job->phase;
    if (job->cursor == job->queued) {
        prepare(job);
        if (job->phase != AXD_PHY_RUNNING || job->queued == 0)
            return job->phase;
    }
    if (io == NULL) {
        fail(job, AXD_PHY_BAD_ARGUMENT);
        return job->phase;
    }
    op = &job->operations[job->cursor];
    job->attempted_ops++;
    if (io(context, op, &value) != 0) {
        fail(job, AXD_PHY_IO_ERROR);
        return job->phase;
    }
    job->completed_ops++;
    if (op->kind == AXD_D11_READ16)
        job->last_read = (uint16_t)value;
    if (job->command == AXD_PHY_READ || job->command == AXD_PHY_TSSI_SNAPSHOT) {
        if (op->kind == AXD_D11_WRITE16 && op->offset == 0x3fc)
            job->context.bookkeeping = 0;
        if (op->kind == AXD_D11_FULL_BARRIER && job->stage == READ_FINISH) {
            job->result = job->last_read;
            job->result_valid = 1;
            if (job->command == AXD_PHY_TSSI_SNAPSHOT) {
                job->context.saved_tssi[job->tssi_slot] = job->last_read & 0x3ffu;
                job->context.snapshot_valid |= (uint8_t)(1u << job->tssi_slot);
            }
        }
    } else if ((job->command == AXD_PHY_MODIFY || job->command == AXD_PHY_TSSI_RESTORE) &&
               op->kind == AXD_D11_WRITE16 && op->offset == 0x3fe) {
        job->context.bookkeeping = 0;
    }
    job->cursor++;
    return job->phase;
}
enum axd_phy_phase axd_phy_register_cancel(struct axd_phy_register *job)
{
    if (job == NULL)
        return AXD_PHY_INVALID;
    if (job->phase == AXD_PHY_RUNNING)
        job->phase = AXD_PHY_CANCELLED;
    return job->phase;
}
