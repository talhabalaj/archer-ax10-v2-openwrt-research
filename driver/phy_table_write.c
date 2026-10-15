/* SPDX-License-Identifier: MIT */
#include "phy_table_write.h"

enum stage {
    ENTRY, SUSPEND, READ_GUARD, GUARD_DECIDE, PROGRAM_ID, PROGRAM_OFFSET,
    DATA, NEXT_RECORD, CLEAR_GUARD, ENABLE_MAC, FINISH, PACKED, RUN_PHY, RUN_MAC
};
struct relay { struct axd_phy_table_write *job; axd_d11_io_fn io; void *context; };
static void fail(struct axd_phy_table_write *j, enum axd_phy_table_write_error e)
{ j->phase = AXD_TW_FAILED; j->error = e; }
static void dependency(struct axd_phy_table_write *j, enum axd_phy_table_write_dependency d)
{ j->phase = AXD_TW_DEPENDENCY; j->dependency = d; }
static void add(struct axd_phy_table_write *j, enum axd_d11_kind k, uint32_t o, uint32_t v)
{
    const struct axd_d11_operation op = {k, o, v};
    j->operations[j->queued++] = op;
}
static void packed(struct axd_phy_table_write *j, uint16_t selector, uint16_t value)
{
    if (j->context.shared_bus_word == 1) {
        j->context.phy.bookkeeping = (uint16_t)(j->context.phy.bookkeeping + 1u);
        if (j->context.phy.bookkeeping >= j->context.write_threshold) {
            j->context.phy.bookkeeping = 0;
            add(j, AXD_D11_READ16, 0x3e0, 0);
            add(j, AXD_D11_FULL_BARRIER, 0, 0);
        }
    }
    add(j, AXD_D11_STORE_BARRIER, 0, 0);
    add(j, AXD_D11_WRITE32, 0x3fc, selector | ((uint32_t)value << 16));
}
static void phy(struct axd_phy_table_write *j, enum axd_phy_command command,
                 uint16_t selector, uint16_t mask, uint16_t value, unsigned resume)
{
    if (axd_phy_register_begin(&j->phy_job, command, selector, mask, value, &j->context.phy) != 0)
        fail(j, AXD_TW_PHY_ERROR);
    else { j->stage = RUN_PHY; j->after_child = resume; }
}
static void mac(struct axd_phy_table_write *j, enum axd_psm_command command, unsigned resume)
{
    if (!j->context.mac_context_valid)
        dependency(j, AXD_TW_MAC_CONTEXT);
    else if (j->context.mac.core != j->context.phy.core || j->context.mac.minor != j->context.phy.minor)
        fail(j, AXD_TW_MAC_ERROR);
    else if (axd_psm_control_begin_policy(&j->mac_job, command, &j->context.mac, &j->context.policy) != 0)
        fail(j, AXD_TW_MAC_ERROR);
    else { j->stage = RUN_MAC; j->after_child = resume; }
}
static uint16_t half(const uint8_t *data)
{ return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8)); }
static void data(struct axd_phy_table_write *j)
{
    const uint8_t *bytes;
    uint16_t value, selector;
    if (j->index == j->request.count) { j->stage = CLEAR_GUARD; return; }
    j->queued_data = 1;
    bytes = j->request.data + (size_t)j->index * j->stride;
    if (j->request.width == 8) { value = bytes[0]; selector = 0xf; }
    else if (j->request.width == 16) { value = half(bytes); selector = 0xf; }
    else if (j->request.width == 32) {
        value = half(bytes + (j->part == 0 ? 2 : 0));
        selector = (uint16_t)(j->part == 0 ? 0x10 : 0xf);
    } else {
        value = half(bytes + j->part * 2u); selector = 0x11;
    }
    if (j->request.width <= 32 || j->part == 0)
        packed(j, selector, value);
    else {
        add(j, AXD_D11_STORE_BARRIER, 0, 0);
        add(j, AXD_D11_WRITE16, 0x3fe, value);
    }
    if (++j->part == j->parts) j->stage = NEXT_RECORD;
}
static void prepare(struct axd_phy_table_write *j)
{
    j->queued = j->cursor = 0;
    j->queued_data = 0;
    switch (j->stage) {
    case ENTRY:
        add(j, AXD_D11_READ32, 0x120, 0);
        add(j, AXD_D11_FULL_BARRIER, 0, 0); j->stage = SUSPEND; break;
    case SUSPEND:
        if (j->initial_control & 1u) mac(j, AXD_PSM_SUSPEND_MAIN, READ_GUARD);
        else j->stage = READ_GUARD;
        break;
    case READ_GUARD: phy(j, AXD_PHY_READ, 0x19e, 0, 0, GUARD_DECIDE); break;
    case GUARD_DECIDE:
        if ((j->phy_job.result & 2u) == 0) phy(j, AXD_PHY_MODIFY, 0x19e, 2, 2, PROGRAM_ID);
        else j->stage = PROGRAM_ID;
        break;
    case PROGRAM_ID: packed(j, 0xd, (uint16_t)j->request.table_id); j->stage = PROGRAM_OFFSET; break;
    case PROGRAM_OFFSET: packed(j, 0xe, (uint16_t)j->request.offset); j->stage = DATA; break;
    case DATA: data(j); break;
    case NEXT_RECORD: j->index++; j->part = 0; j->stage = DATA; break;
    case CLEAR_GUARD:
        if (j->changed_guard) phy(j, AXD_PHY_MODIFY, 0x19e, 2, 0, ENABLE_MAC);
        else j->stage = ENABLE_MAC;
        break;
    case ENABLE_MAC:
        if (j->initial_control & 1u) mac(j, AXD_PSM_ENABLE_MAIN, FINISH);
        else j->stage = FINISH;
        break;
    case PACKED: packed(j, j->packed_selector, j->packed_value); j->stage = FINISH; break;
    case FINISH: j->phase = AXD_TW_TRACE_COMPLETE; break;
    default: fail(j, AXD_TW_BAD_ARGUMENT); break;
    }
}
static int perform(void *opaque, const struct axd_d11_operation *op, uint32_t *value)
{
    struct relay *r = opaque;
    struct axd_phy_table_write *j = r->job;
    if (j->stage == RUN_PHY) j->context.phy = j->phy_job.context;
    else if (j->stage == RUN_MAC) j->context.mac = j->mac_job.context;
    if (r->io == NULL) { fail(j, AXD_TW_BAD_ARGUMENT); return -1; }
    j->attempted_ops++;
    if (r->io(r->context, op, value) != 0) { fail(j, AXD_TW_IO_ERROR); return -1; }
    j->completed_ops++;
    if (op->kind == AXD_D11_READ16 && op->offset == 0x3e0) j->flush_reads++;
    if (j->stage == RUN_PHY && j->after_child == PROGRAM_ID &&
        op->kind == AXD_D11_WRITE16 && op->offset == 0x3fe) j->changed_guard = 1;
    return 0;
}
static void child_step(struct axd_phy_table_write *j, struct relay *r)
{
    if (j->stage == RUN_PHY) {
        enum axd_phy_phase p = axd_phy_register_step(&j->phy_job, perform, r);
        j->context.phy = j->phy_job.context;
        if (j->phase != AXD_TW_RUNNING) return;
        if (p == AXD_PHY_TRACE_COMPLETE) j->stage = j->after_child;
        else if (p == AXD_PHY_CANCELLED) j->phase = AXD_TW_CANCELLED;
        else if (p != AXD_PHY_RUNNING) fail(j, AXD_TW_PHY_ERROR);
    } else {
        enum axd_psm_phase p = axd_psm_control_step(&j->mac_job, perform, r);
        j->context.mac = j->mac_job.context;
        j->pending_callouts |= j->mac_job.pending_callouts;
        if (j->phase != AXD_TW_RUNNING) return;
        if (p == AXD_PSM_DONE) j->stage = j->after_child;
        else if (p == AXD_PSM_CANCELLED) j->phase = AXD_TW_CANCELLED;
        else if (p == AXD_PSM_DEPENDENCY) dependency(j, AXD_TW_MAC_CALLOUT);
        else if (p != AXD_PSM_RUNNING) fail(j, AXD_TW_MAC_ERROR);
    }
}
int axd_phy_table_write_begin(struct axd_phy_table_write *j,
                               const struct axd_phy_table_write_request *request,
                               const struct axd_phy_table_context *context)
{
    const struct axd_phy_table_write empty = {0};
    struct axd_phy_table_context c;
    struct axd_phy_table_write_request q;
    unsigned stride, parts;
    if (j == NULL) return -1;
    if (context != NULL) { c = *context; context = &c; }
    if (request != NULL) { q = *request; request = &q; }
    *j = empty; fail(j, AXD_TW_BAD_ARGUMENT);
    if (context == NULL || request == NULL || context->mac_context_valid > 1) return -1;
    j->context = *context; j->request = *request;
    switch (request->width) {
    case 8: stride = 1; parts = 1; break;
    case 16: stride = 2; parts = 1; break;
    case 32: stride = 4; parts = 2; break;
    case 42: case 48: stride = 6; parts = 3; break;
    case 60: case 64: stride = 8; parts = 4; break;
    default: fail(j, AXD_TW_UNSUPPORTED); return -1;
    }
    if ((request->count != 0 && request->data == NULL) ||
        request->count > SIZE_MAX / stride || (size_t)request->count * stride > request->data_length) {
        fail(j, AXD_TW_BAD_DATA); return -1;
    }
    if (axd_phy_register_begin(&j->phy_job, AXD_PHY_READ, 0, 0, 0, &context->phy) != 0) {
        fail(j, AXD_TW_PHY_ERROR); return -1;
    }
    j->stride = stride; j->parts = parts; j->stage = ENTRY;
    j->phase = AXD_TW_RUNNING; j->error = AXD_TW_NO_ERROR;
    return 0;
}
int axd_phy_packed_write_begin(struct axd_phy_table_write *j, uint16_t selector,
                                uint16_t value, const struct axd_phy_table_context *context)
{
    const struct axd_phy_table_write_request request = {0, 0, 0, 8, NULL, 0};
    if (axd_phy_table_write_begin(j, &request, context) != 0) return -1;
    if (selector >= 0xc000) { fail(j, AXD_TW_UNSUPPORTED); return -1; }
    j->direct = 1; j->packed_selector = selector; j->packed_value = value; j->stage = PACKED;
    return 0;
}
enum axd_phy_table_write_phase axd_phy_table_write_step(struct axd_phy_table_write *j,
                                                        axd_d11_io_fn io, void *context)
{
    struct relay r = {j, io, context};
    const struct axd_d11_operation *op;
    uint32_t value = 0;
    if (j == NULL) return AXD_TW_INVALID;
    if (j->phase != AXD_TW_RUNNING) return j->phase;
    if (j->stage == RUN_PHY || j->stage == RUN_MAC) { child_step(j, &r); return j->phase; }
    if (j->cursor == j->queued) {
        prepare(j);
        if (j->phase != AXD_TW_RUNNING || j->queued == 0) return j->phase;
    }
    op = &j->operations[j->cursor];
    if (perform(&r, op, &value) != 0) return j->phase;
    if (op->kind == AXD_D11_READ32 && op->offset == 0x120) j->initial_control = value;
    if ((op->kind == AXD_D11_WRITE32 || op->kind == AXD_D11_WRITE16) &&
        j->queued_data) j->data_writes++;
    j->cursor++;
    return j->phase;
}
enum axd_phy_table_write_phase axd_phy_table_write_cancel(struct axd_phy_table_write *j)
{
    if (j == NULL) return AXD_TW_INVALID;
    if (j->phase == AXD_TW_RUNNING) {
        j->phase = AXD_TW_CANCELLED;
        if (j->stage == RUN_PHY) axd_phy_register_cancel(&j->phy_job);
        else if (j->stage == RUN_MAC) axd_psm_control_cancel(&j->mac_job);
    }
    return j->phase;
}
