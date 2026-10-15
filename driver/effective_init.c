/* SPDX-License-Identifier: MIT */
#include "effective_init.h"

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void prepare(struct axd_effective_init *init)
{
    const uint8_t *record;
    uint16_t offset, width;
    uint32_t value;
    const struct axd_init_operation empty = {0};
    init->operation = empty;
    if (init->record_index == init->record_count) {
        init->phase = AXD_INIT_COMPLETE;
        return;
    }
    record = init->data + init->record_index * 8;
    offset = get16(record);
    width = get16(record + 2);
    value = get32(record + 4);
    if (offset == 0x130)
        value += init->addend; /* unsigned modulo-2^32 addition */
    if (offset >= 0x1000) {
        init->operation.kind = AXD_INIT_OBJECT_COPY16;
        init->operation.offset = (((uint32_t)offset - 0x400u) >> 1) << 2;
        init->operation.value = value & 0xffffu;
        init->operation.selector = 0x30000;
        init->phase = AXD_INIT_OPERATION;
    } else if (width == 2 || width == 4) {
        init->operation.kind = width == 2 ? AXD_INIT_WRITE16 : AXD_INIT_WRITE32;
        init->operation.offset = offset;
        init->operation.value = width == 2 ? value & 0xffffu : value;
        init->phase = AXD_INIT_BARRIER;
    } else {
        init->phase = AXD_INIT_SKIP;
    }
}

int axd_effective_init_begin(struct axd_effective_init *init,
                              uint16_t core, uint16_t minor,
                              const uint8_t *data, size_t length,
                              const uint32_t *offset130_addend,
                              uint32_t mmio_window, uint32_t object_window)
{
    const struct axd_effective_init empty = {0};
    size_t pos;
    if (init == NULL)
        return -1;
    *init = empty;
    init->phase = AXD_INIT_FAILED;
    init->error = AXD_INIT_BAD_ARGUMENT;
    if (core != 130 || minor > 1) {
        init->error = AXD_INIT_UNSUPPORTED_CORE;
        return -1;
    }
    if (data == NULL || mmio_window == 0 || mmio_window > 0x1000)
        return -1;
    init->error = AXD_INIT_BAD_TABLE;
    if (length == 0 || length % 8 != 0)
        return -1;
    for (pos = 0; pos < length; pos += 8) {
        uint16_t offset = get16(data + pos);
        uint16_t width = get16(data + pos + 2);
        if (offset == 0xffffu) {
            init->data = data;
            init->record_count = pos / 8;
            init->addend = offset130_addend == NULL ? 0 : *offset130_addend;
            init->error = AXD_INIT_NO_ERROR;
            prepare(init);
            return 0;
        }
        if (offset >= 0x1000) {
            uint32_t address = (((uint32_t)offset - 0x400u) >> 1) << 2;
            if (address >= object_window || 2u > object_window - address) {
                init->error = AXD_INIT_BOUNDS;
                return -1;
            }
        } else if (width == 2 || width == 4) {
            if (offset % width != 0 || (uint32_t)offset >= mmio_window ||
                (uint32_t)width > mmio_window - (uint32_t)offset) {
                init->error = AXD_INIT_BOUNDS;
                return -1;
            }
            if (offset == 0x130 && offset130_addend == NULL) {
                init->error = AXD_INIT_CONTEXT_MISSING;
                return -1;
            }
        }
    }
    return -1;
}

enum axd_init_phase axd_effective_init_step(struct axd_effective_init *init,
                                            axd_init_emit_fn emit, void *context)
{
    enum axd_init_phase current;
    struct axd_init_operation barrier = {AXD_INIT_STORE_BARRIER, 0, 0, 0};
    if (init == NULL)
        return AXD_INIT_INVALID;
    current = init->phase;
    if (current < AXD_INIT_BARRIER || current > AXD_INIT_SKIP)
        return current;
    if (current == AXD_INIT_SKIP) {
        init->skipped_records++;
    } else {
        if (emit == NULL) {
            init->error = AXD_INIT_BAD_ARGUMENT;
            init->phase = AXD_INIT_FAILED;
            return init->phase;
        }
        init->attempted_ops++;
        if (emit(context, current == AXD_INIT_BARRIER ? &barrier : &init->operation) != 0) {
            init->error = AXD_INIT_CALLBACK_ERROR;
            init->phase = AXD_INIT_FAILED;
            return init->phase;
        }
        init->completed_ops++;
    }
    if (current != AXD_INIT_BARRIER) {
        init->completed_records++;
        init->record_index++;
    }
    if (init->phase == AXD_INIT_CANCELLED)
        return init->phase;
    if (current == AXD_INIT_BARRIER)
        init->phase = AXD_INIT_OPERATION;
    else
        prepare(init);
    return init->phase;
}

enum axd_init_phase axd_effective_init_cancel(struct axd_effective_init *init)
{
    if (init == NULL)
        return AXD_INIT_INVALID;
    if (init->phase >= AXD_INIT_BARRIER && init->phase <= AXD_INIT_SKIP)
        init->phase = AXD_INIT_CANCELLED;
    return init->phase;
}
