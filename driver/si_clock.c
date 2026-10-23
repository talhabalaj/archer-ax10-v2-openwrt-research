/* SPDX-License-Identifier: MIT */
#include "si_clock.h"

enum stage {
    ENTRY, DISABLE_GUARD, DISABLE_IRQ, SELECT_CC, BODY, SELECT_RESTORE,
    RESTORE_GUARD, RESTORE_IRQ, FINISH, RUN_SELECT, RUN_CLOCK
};
struct relay { struct axd_si_clock *job; axd_si_clock_io_fn io; void *context; };
static void fail(struct axd_si_clock *j, enum axd_si_clock_error error)
{
    j->phase = AXD_SIC_FAILED; j->error = error;
}
static void dependency(struct axd_si_clock *j, enum axd_si_clock_dependency which)
{
    j->phase = AXD_SIC_DEPENDENCY; j->dependency = which;
}
static int perform(struct relay *r, const struct axd_si_clock_operation *op,
                     struct axd_si_clock_reply *reply)
{
    if (r->io == NULL) { fail(r->job, AXD_SIC_BAD_ARGUMENT); return -1; }
    r->job->attempted_ops++;
    if (r->io(r->context, op, reply) != 0) {
        fail(r->job, AXD_SIC_IO_ERROR);
        return -1;
    }
    r->job->completed_ops++;
    return 0;
}
static int select_io(void *opaque, const struct axd_si_operation *op, uint32_t *value)
{
    struct relay *r = opaque;
    struct axd_si_clock_operation converted = {0};
    struct axd_si_clock_reply reply = {0};
    if (op->kind == AXD_SI_MAP) {
        converted.kind = AXD_SIC_MAP;
        converted.resource = op->resource == AXD_SI_CORE ? AXD_SIC_CORE :
            (op->resource == AXD_SI_PRIMARY_WRAPPER ? AXD_SIC_PRIMARY_WRAPPER : AXD_SIC_SECONDARY_WRAPPER);
        converted.address = op->address;
        converted.size = op->size; converted.map_type = op->map_type;
    } else {
        converted.kind = AXD_SIC_PCI_WRITE32; converted.resource = AXD_SIC_PCI_CONFIG;
        converted.offset = op->address; converted.size = op->size; converted.value = op->value;
    }
    if (perform(r, &converted, &reply) != 0) return -1;
    *value = reply.value;
    return 0;
}
static int clock_io(void *opaque, const struct axd_cc_clock_operation *op, uint32_t *value)
{
    struct relay *r = opaque;
    struct axd_si_clock_operation converted = {0};
    struct axd_si_clock_reply reply = {0};
    if (op->region == AXD_CC_CHIPCOMMON) {
        converted.resource = AXD_SIC_CHIPCOMMON; converted.address = r->job->mapping;
    } else if (op->region == AXD_CC_PCI_CONFIG) {
        converted.resource = AXD_SIC_PCI_CONFIG;
    }
    converted.offset = op->offset; converted.value = op->value;
    switch (op->kind) {
    case AXD_CC_STORE_BARRIER: converted.kind = AXD_SIC_STORE_BARRIER; break;
    case AXD_CC_FULL_BARRIER: converted.kind = AXD_SIC_FULL_BARRIER; break;
    case AXD_CC_READ32: converted.kind = AXD_SIC_READ32; converted.size = 4; break;
    case AXD_CC_WRITE32: converted.kind = AXD_SIC_WRITE32; converted.size = 4; break;
    case AXD_CC_PCI_READ32: converted.kind = AXD_SIC_PCI_READ32; converted.size = 4; break;
    case AXD_CC_PCI_WRITE32: converted.kind = AXD_SIC_PCI_WRITE32; converted.size = 4; break;
    case AXD_CC_DELAY_US: converted.kind = AXD_SIC_DELAY_US; break;
    default: fail(r->job, AXD_SIC_BAD_ARGUMENT); return -1;
    }
    if ((op->kind == AXD_CC_READ32 || op->kind == AXD_CC_WRITE32) && r->job->mapping == 0) {
        fail(r->job, AXD_SIC_BINDING);
        return -1;
    }
    if (perform(r, &converted, &reply) != 0) return -1;
    *value = reply.value;
    return 0;
}
static int core_guard(struct axd_si_clock *j)
{
    const struct axd_si_context *c = &j->context.si;
    uint32_t index = j->context.state.current_index;
    if (c->catalog == NULL) { dependency(j, AXD_SIC_SELECTION); return 0; }
    if (index >= c->core_count || index >= AXD_SI_MAX_CORES || (size_t)index >= c->catalog_length) {
        fail(j, AXD_SIC_BINDING);
        return 0;
    }
    return c->catalog[index].core_id == j->context.irq_core_id;
}
static void select_begin(struct axd_si_clock *j, enum axd_si_command command,
                          uint32_t requested, unsigned resume)
{
    if (axd_si_select_begin(&j->selector, command, requested, 0,
                             &j->context.si, &j->context.state) != 0) {
        fail(j, AXD_SIC_SELECT_ERROR); return;
    }
    j->after_child = resume; j->stage = RUN_SELECT;
}
static void irq(struct axd_si_clock *j, struct relay *r, unsigned restore)
{
    struct axd_si_clock_operation op = {0};
    struct axd_si_clock_reply reply = {0};
    op.kind = restore ? AXD_SIC_IRQ_RESTORE : AXD_SIC_IRQ_DISABLE;
    op.callback = restore ? j->context.restore_callback : j->context.disable_callback;
    op.callback_context = j->context.irq_context;
    op.value = restore ? j->irq_token : 0;
    op.state = &j->context.state;
    /* Entering an external IRQ callback can change SI even if it later errors.
     * Preserve the old snapshot as diagnostics, but do not call it current.
     */
    if (r->io != NULL) j->state_valid = 0;
    if (perform(r, &op, &reply) != 0) return;
    if (restore) j->restore_called = 1;
    else { j->disable_called = 1; j->irq_token = reply.value; }
    j->state_valid = reply.irq_state_known == 1;
    if (j->state_valid) j->context.state = reply.irq_state;
    if (j->phase != AXD_SIC_RUNNING) return;
    if (reply.irq_state_known == 0) { dependency(j, AXD_SIC_IRQ_STATE); return; }
    if (reply.irq_state_known != 1) { fail(j, AXD_SIC_BAD_ARGUMENT); return; }
    if (!restore) j->saved_index = j->context.state.current_index;
    j->stage = restore ? FINISH : SELECT_CC;
}
static void child_step(struct axd_si_clock *j, struct relay *r)
{
    if (j->stage == RUN_SELECT) {
        enum axd_si_phase phase = axd_si_select_step(&j->selector, select_io, r);
        j->context.state = j->selector.state;
        if (j->phase != AXD_SIC_RUNNING) return;
        if (phase == AXD_SI_TRACE_COMPLETE) {
            if (j->after_child == BODY) {
                j->selection_completed = 1; j->mapping = j->selector.return_value;
            } else j->restoration_completed = 1;
            j->stage = j->after_child;
        } else if (phase == AXD_SI_DEPENDENCY) dependency(j, AXD_SIC_SELECTION);
        else if (phase == AXD_SI_CANCELLED) j->phase = AXD_SIC_CANCELLED;
        else if (phase != AXD_SI_RUNNING) fail(j, AXD_SIC_SELECT_ERROR);
    } else {
        enum axd_cc_clock_phase phase = axd_cc_clock_step(&j->clock_job, clock_io, r);
        if (j->phase != AXD_SIC_RUNNING) return;
        if (phase == AXD_CC_TRACE_COMPLETE) {
            j->body_completed = 1;
            j->stage = j->fast_mapping ? FINISH : SELECT_RESTORE;
        } else if (phase == AXD_CC_DEPENDENCY) dependency(j, AXD_SIC_CLOCK_OWNER);
        else if (phase == AXD_CC_CANCELLED) j->phase = AXD_SIC_CANCELLED;
        else if (phase != AXD_CC_RUNNING) fail(j, AXD_SIC_CLOCK_ERROR);
    }
}
int axd_si_clock_begin(struct axd_si_clock *j, uint32_t mode,
                        const struct axd_si_clock_context *context)
{
    static const struct axd_si_clock empty={0};
    struct axd_si_clock_context snapshot;
    if (j == NULL) return -1;
    if (context != NULL) { snapshot = *context; context = &snapshot; }
    *j = empty; fail(j, AXD_SIC_BAD_ARGUMENT);
    if (context == NULL) return -1;
    j->context = *context; j->mode = mode;
    j->stage = ENTRY; j->state_valid = 1;
    j->phase = AXD_SIC_RUNNING; j->error = AXD_SIC_NO_ERROR;
    return 0;
}
enum axd_si_clock_phase axd_si_clock_step(struct axd_si_clock *j,
                                          axd_si_clock_io_fn io, void *context)
{
    struct relay relay = {j, io, context};
    if (j == NULL) return AXD_SIC_INVALID;
    if (j->phase != AXD_SIC_RUNNING) return j->phase;
    switch (j->stage) {
    case ENTRY:
        if (j->context.revision <= 5) {
            j->return_value = 0; j->phase = AXD_SIC_TRACE_COMPLETE;
        } else if (j->context.si.bus == 1 &&
                   (j->context.si.bus_core_id == 0x83c || j->context.si.bus_core_id == 0x820 ||
                    (j->context.si.bus_core_id == 0x804 && j->context.bus_core_revision > 12))) {
            j->fast_mapping = 1;
            if (j->context.state.current_map == 0) {
                j->map_skipped = 1; j->stage = FINISH;
            } else {
                j->mapping = j->context.state.current_map + 0x3000u;
                if (j->mapping == 0) { j->map_skipped = 1; j->stage = FINISH; }
                else j->stage = BODY;
            }
        } else {
            j->saved_index = j->context.state.current_index;
            j->stage = DISABLE_GUARD;
        }
        break;
    case DISABLE_GUARD:
        if (j->context.disable_callback == 0) j->stage = SELECT_CC;
        else if (core_guard(j)) j->stage = DISABLE_IRQ;
        else if (j->phase == AXD_SIC_RUNNING) j->stage = SELECT_CC;
        break;
    case DISABLE_IRQ:
        irq(j, &relay, 0);
        break;
    case SELECT_CC:
        select_begin(j, AXD_SI_BY_ID, 0x800, BODY);
        break;
    case BODY:
        if (((j->context.capabilities & 0x40000u) == 0 && j->context.revision <= 19) ||
            (j->mode != 0 && j->mode != 2)) {
            j->body_completed = 1;
            j->stage = j->fast_mapping ? FINISH : SELECT_RESTORE;
        } else {
            const struct axd_cc_clock_context c = {
                j->context.revision, j->context.capabilities, j->context.si.bus,
                j->context.si.bus_core_id, j->context.bus_core_revision,
                j->context.chipcommon_owned, j->context.si.pci_config_owned
            };
            if (axd_cc_clock_begin(&j->clock_job, j->mode, &c) != 0)
                fail(j, AXD_SIC_CLOCK_ERROR);
            else j->stage = RUN_CLOCK;
        }
        break;
    case SELECT_RESTORE:
        select_begin(j, AXD_SI_BY_INDEX, j->saved_index, RESTORE_GUARD);
        break;
    case RESTORE_GUARD:
        if (j->context.restore_callback == 0) j->stage = FINISH;
        else if (core_guard(j)) j->stage = RESTORE_IRQ;
        else if (j->phase == AXD_SIC_RUNNING) j->stage = FINISH;
        break;
    case RESTORE_IRQ:
        irq(j, &relay, 1);
        break;
    case FINISH:
        j->return_value = j->mode == 0;
        j->phase = AXD_SIC_TRACE_COMPLETE;
        break;
    case RUN_SELECT:
    case RUN_CLOCK:
        child_step(j, &relay);
        break;
    default: fail(j, AXD_SIC_BAD_ARGUMENT); break;
    }
    return j->phase;
}
enum axd_si_clock_phase axd_si_clock_cancel(struct axd_si_clock *j)
{
    if (j == NULL) return AXD_SIC_INVALID;
    if (j->phase == AXD_SIC_RUNNING) {
        j->phase = AXD_SIC_CANCELLED;
        if (j->stage == RUN_SELECT) axd_si_select_cancel(&j->selector);
        else if (j->stage == RUN_CLOCK) axd_cc_clock_cancel(&j->clock_job);
    }
    return j->phase;
}
