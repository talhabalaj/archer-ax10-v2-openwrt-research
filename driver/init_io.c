/* SPDX-License-Identifier: MIT */
#include "init_io.h"

struct relay { struct axd_init_io *job; axd_init_io_fn io; void *context; };
static void fail(struct axd_init_io *j, enum axd_init_io_error error)
{ j->phase = AXD_INIT_IO_FAILED; j->error = error; }
static int perform(struct relay *r, enum axd_init_io_path path,
                     const struct axd_d11_operation *op, uint32_t *value)
{
    const struct axd_init_io_operation converted = {path, *op};
    if (!r->job->context.owned) { r->job->phase = AXD_INIT_IO_DEPENDENCY; return -1; }
    if (r->io == NULL) { fail(r->job, AXD_INIT_IO_BAD_ARGUMENT); return -1; }
    r->job->attempted_ops++;
    if (r->io(r->context, &converted, value) != 0) {
        fail(r->job, AXD_INIT_IO_CALLBACK_ERROR); return -1;
    }
    r->job->completed_ops++;
    return 0;
}
static int object_io(void *opaque, const struct axd_d11_operation *op, uint32_t *value)
{ return perform(opaque, AXD_INIT_IO_OBJECT, op, value); }
static int semantic(void *opaque, const struct axd_init_operation *op)
{
    struct relay *r = opaque;
    struct axd_init_io *j = r->job;
    struct axd_d11_operation converted = {AXD_D11_STORE_BARRIER, op->offset, op->value};
    uint32_t ignored = 0;
    if (op->kind == AXD_INIT_OBJECT_COPY16) {
        struct axd_object_request request = {0};
        j->object_data[0] = (uint8_t)op->value;
        j->object_data[1] = (uint8_t)(op->value >> 8);
        request.command = AXD_OBJECT_COPY;
        request.address = op->offset; request.selector = op->selector;
        request.data = j->object_data; request.data_length = 2; request.byte_count = 2;
        if (axd_object_memory_begin(&j->object, &request, &j->context) != 0) {
            fail(j, AXD_INIT_IO_OBJECT_ERROR); return -1;
        }
        j->object_active = 1;
        return 0;
    }
    if (op->kind == AXD_INIT_WRITE16) converted.kind = AXD_D11_WRITE16;
    else if (op->kind == AXD_INIT_WRITE32) converted.kind = AXD_D11_WRITE32;
    return perform(r, AXD_INIT_IO_DIRECT, &converted, &ignored);
}
int axd_init_io_begin(struct axd_init_io *j, const struct axd_init_io_request *request,
                        const struct axd_object_context *context)
{
    struct axd_init_io_request r;
    struct axd_object_context c;
    uint32_t addend = 0;
    unsigned known_addend = 0;
    static const struct axd_init_io empty={0};
    if (j == NULL) return -1;
    if (request != NULL) {
        r = *request; request = &r;
        if (r.offset130_addend != NULL) { addend = *r.offset130_addend; known_addend = 1; }
    }
    if (context != NULL) { c = *context; context = &c; }
    *j = empty; fail(j, AXD_INIT_IO_BAD_ARGUMENT);
    if (request == NULL || context == NULL || c.owned > 1) return -1;
    j->request = r; j->context = c; j->addend = addend;
    j->request.offset130_addend = known_addend ? &j->addend : NULL;
    if (axd_effective_init_begin(&j->effective, c.core, c.minor, r.data, r.length,
                                  j->request.offset130_addend, r.mmio_window, r.object_window) != 0) {
        fail(j, AXD_INIT_IO_TABLE_ERROR); return -1;
    }
    j->phase = j->effective.phase == AXD_INIT_COMPLETE ? AXD_INIT_IO_DONE : AXD_INIT_IO_RUNNING;
    j->error = AXD_INIT_IO_NO_ERROR;
    return 0;
}
enum axd_init_io_phase axd_init_io_step(struct axd_init_io *j, axd_init_io_fn io, void *context)
{
    struct relay relay = {j, io, context};
    if (j == NULL) return AXD_INIT_IO_INVALID;
    if (j->phase != AXD_INIT_IO_RUNNING) return j->phase;
    if (j->object_active) {
        enum axd_object_phase phase = axd_object_memory_step(&j->object, object_io, &relay);
        if (j->phase != AXD_INIT_IO_RUNNING) return j->phase;
        if (phase == AXD_OBJECT_DONE) {
            j->object_active = 0; j->completed_records++;
            if (j->effective.phase == AXD_INIT_COMPLETE) j->phase = AXD_INIT_IO_DONE;
        } else if (phase == AXD_OBJECT_DEPENDENCY) j->phase = AXD_INIT_IO_DEPENDENCY;
        else if (phase == AXD_OBJECT_CANCELLED) j->phase = AXD_INIT_IO_CANCELLED;
        else if (phase != AXD_OBJECT_RUNNING) fail(j, AXD_INIT_IO_OBJECT_ERROR);
    } else {
        size_t records = j->effective.completed_records;
        enum axd_init_phase phase = axd_effective_init_step(&j->effective, semantic, &relay);
        if (!j->object_active) j->completed_records += j->effective.completed_records - records;
        j->skipped_records = j->effective.skipped_records;
        if (j->phase != AXD_INIT_IO_RUNNING) return j->phase;
        if (phase == AXD_INIT_COMPLETE && !j->object_active) j->phase = AXD_INIT_IO_DONE;
        else if (phase == AXD_INIT_FAILED) fail(j, AXD_INIT_IO_TABLE_ERROR);
        else if (phase == AXD_INIT_CANCELLED) j->phase = AXD_INIT_IO_CANCELLED;
    }
    return j->phase;
}
enum axd_init_io_phase axd_init_io_cancel(struct axd_init_io *j)
{
    if (j == NULL) return AXD_INIT_IO_INVALID;
    if (j->phase == AXD_INIT_IO_RUNNING) {
        j->phase = AXD_INIT_IO_CANCELLED;
        axd_effective_init_cancel(&j->effective);
        if (j->object_active) axd_object_memory_cancel(&j->object);
    }
    return j->phase;
}
