/* SPDX-License-Identifier: MIT */
#include "object_memory.h"

enum stage { ENTRY, COPY_BODY, AUTO_BODY, FINISH };
static void fail(struct axd_object_memory *j, enum axd_object_error error)
{ j->phase = AXD_OBJECT_FAILED; j->error = error; }
static uint32_t load(const uint8_t *p, unsigned width)
{
    uint32_t value = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    if (width == 4) value |= (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return value;
}
static void add(struct axd_object_memory *j, enum axd_d11_kind kind,
                  uint32_t offset, uint32_t value)
{
    const struct axd_d11_operation op = {kind, offset, value};
    j->operations[j->queued++] = op;
}
static void address(struct axd_object_memory *j, uint32_t byte_address, uint32_t selector)
{
    add(j, AXD_D11_STORE_BARRIER, 0, 0);
    add(j, AXD_D11_WRITE32, 0x160, selector | (byte_address >> 2));
    add(j, AXD_D11_READ32, 0x160, 0);
    add(j, AXD_D11_FULL_BARRIER, 0, 0);
}
static void data(struct axd_object_memory *j, unsigned width, uint32_t offset, uint32_t value)
{
    add(j, AXD_D11_STORE_BARRIER, 0, 0);
    add(j, width == 2 ? AXD_D11_WRITE16 : AXD_D11_WRITE32, offset, value);
}
static void prepare(struct axd_object_memory *j)
{
    const struct axd_object_request *r = &j->request;
    uint32_t remaining;
    unsigned width;
    j->queued = j->cursor = 0;
    switch (j->stage) {
    case ENTRY:
        if (r->command == AXD_OBJECT_COPY && r->byte_count <= 0) {
            j->phase = AXD_OBJECT_DONE; break;
        }
        if (!j->context.owned) { j->phase = AXD_OBJECT_DEPENDENCY; break; }
        if (r->command == AXD_OBJECT_WRITE16) {
            address(j, r->address, r->selector);
            data(j, 2, 0x164 + (r->address & 2u), r->value & 0xffffu);
            j->stage = FINISH;
        } else if (r->command == AXD_OBJECT_COPY) j->stage = COPY_BODY;
        else {
            address(j, r->address, r->selector | 0x03000000u);
            j->stage = AUTO_BODY;
        }
        break;
    case COPY_BODY:
        remaining = (uint32_t)r->byte_count - j->completed_bytes;
        if (remaining == 0) { j->phase = AXD_OBJECT_DONE; break; }
        width = j->halfword_route || remaining < 4 ? 2 : 4;
        address(j, r->address + j->completed_bytes, r->selector);
        data(j, width, 0x164 + (width == 2 ? (r->address + j->completed_bytes) & 2u : 0),
             load(r->data + j->completed_bytes, width));
        break;
    case AUTO_BODY:
        if (r->byte_count <= 0 || j->completed_bytes == (uint32_t)r->byte_count) {
            j->phase = AXD_OBJECT_DONE; break;
        }
        data(j, 4, 0x164, r->command == AXD_OBJECT_SET32 ? r->value :
             load(r->data + j->completed_bytes, 4));
        break;
    case FINISH: j->phase = AXD_OBJECT_DONE; break;
    default: fail(j, AXD_OBJECT_BAD_ARGUMENT); break;
    }
}
int axd_object_memory_begin(struct axd_object_memory *j,
                              const struct axd_object_request *request,
                              const struct axd_object_context *context)
{
    struct axd_object_request r;
    struct axd_object_context c;
    const struct axd_object_memory empty = {0};
    uint32_t count, alignment;
    if (j == NULL) return -1;
    if (request != NULL) { r = *request; request = &r; }
    if (context != NULL) { c = *context; context = &c; }
    *j = empty; fail(j, AXD_OBJECT_BAD_ARGUMENT);
    if (request == NULL || context == NULL || context->owned > 1 ||
        request->command < AXD_OBJECT_COPY || request->command > AXD_OBJECT_WRITE16) return -1;
    j->request = *request; j->context = *context;
    if (context->core != 130 || context->minor > 1) {
        fail(j, AXD_OBJECT_UNSUPPORTED); return -1;
    }
    j->halfword_route = (r.selector & 0x50000u) == 0x10000u ||
                       (r.selector & 0x70000u) == 0x20000u;
    count = r.command == AXD_OBJECT_WRITE16 ? 2u :
        r.byte_count > 0 ? (uint32_t)r.byte_count : 0u;
    alignment = r.command == AXD_OBJECT_WRITE16 ||
        (r.command == AXD_OBJECT_COPY && (j->halfword_route || count < 4)) ? 2u : 4u;
    if ((count != 0 || r.command != AXD_OBJECT_COPY) && r.address % alignment != 0) return -1;
    if (r.command != AXD_OBJECT_WRITE16 && count % (r.command == AXD_OBJECT_COPY ? 2u : 4u) != 0)
        return -1;
    if (count != 0 && count - 1u > UINT32_MAX - r.address) {
        fail(j, AXD_OBJECT_BOUNDS); return -1;
    }
    if (count != 0 && (r.command == AXD_OBJECT_COPY || r.command == AXD_OBJECT_COPY32) &&
        (r.data == NULL || count > r.data_length)) {
        fail(j, AXD_OBJECT_BOUNDS); return -1;
    }
    j->phase = AXD_OBJECT_RUNNING; j->error = AXD_OBJECT_NO_ERROR; j->stage = ENTRY;
    return 0;
}
enum axd_object_phase axd_object_memory_step(struct axd_object_memory *j,
                                              axd_d11_io_fn io, void *context)
{
    uint32_t value = 0;
    const struct axd_d11_operation *op;
    if (j == NULL) return AXD_OBJECT_INVALID;
    if (j->phase != AXD_OBJECT_RUNNING) return j->phase;
    if (j->cursor == j->queued) {
        prepare(j);
        if (j->phase != AXD_OBJECT_RUNNING || j->queued == 0) return j->phase;
    }
    if (io == NULL) { fail(j, AXD_OBJECT_BAD_ARGUMENT); return j->phase; }
    op = &j->operations[j->cursor]; j->attempted_ops++;
    if (io(context, op, &value) != 0) { fail(j, AXD_OBJECT_IO_ERROR); return j->phase; }
    j->completed_ops++;
    if (op->kind == AXD_D11_READ32) j->last_read = value;
    if (op->kind == AXD_D11_WRITE32 && op->offset == 0x160) j->address_writes++;
    else if (op->kind == AXD_D11_WRITE16 || op->kind == AXD_D11_WRITE32)
        j->completed_bytes += op->kind == AXD_D11_WRITE16 ? 2u : 4u;
    j->cursor++;
    return j->phase;
}
enum axd_object_phase axd_object_memory_cancel(struct axd_object_memory *j)
{
    if (j == NULL) return AXD_OBJECT_INVALID;
    if (j->phase == AXD_OBJECT_RUNNING) j->phase = AXD_OBJECT_CANCELLED;
    return j->phase;
}
