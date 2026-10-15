/* SPDX-License-Identifier: MIT */
#include "dma_rx_take.h"
enum stage {
    ENTRY, CHECK_CURRENT, STATUS_READ, STATUS_SY, STATUS_STORE,
    SLOT_READ, SLOT_CLEAR, ADVANCE, AVAILABLE, RETURN_VALUE
};
static void fail(struct axd_dma_rx_take *j, enum axd_rx_take_error error)
{ j->phase = AXD_RX_TAKE_FAILED; j->error = error; }
static void done(struct axd_dma_rx_take *j, uint32_t result)
{ j->return_value = result; j->return_valid = 1; j->phase = AXD_RX_TAKE_DONE; }
int axd_dma_rx_take_begin(struct axd_dma_rx_take *j, uint32_t force,
                            const struct axd_rx_take_context *context)
{
    struct axd_rx_take_context c;
    const struct axd_dma_rx_take empty = {0};
    if (j == NULL) return -1;
    if (context != NULL) { c = *context; context = &c; }
    *j = empty; fail(j, AXD_RX_TAKE_BAD_ARGUMENT);
    if (context == NULL || c.status_owned > 1 || c.slots_owned > 1) return -1;
    j->context = c; j->force = force;
    if (c.core != 130 || c.minor > 1) { fail(j, AXD_RX_TAKE_UNSUPPORTED); return -1; }
    if (axd_dma64_ring_check(&c.ring) != AXD_DMA64_OK) {
        fail(j, AXD_RX_TAKE_BOUNDS); return -1;
    }
    j->phase = AXD_RX_TAKE_RUNNING; j->error = AXD_RX_TAKE_NO_ERROR;
    return 0;
}
enum axd_rx_take_phase axd_dma_rx_take_step(struct axd_dma_rx_take *j,
                                            axd_rx_take_io_fn io, void *context)
{
    struct axd_rx_take_operation op = {0};
    uint32_t value = 0, index, used, available;
    unsigned direct = 0;
    if (j == NULL) return AXD_RX_TAKE_INVALID;
    if (j->phase != AXD_RX_TAKE_RUNNING) return j->phase;
    switch (j->stage) {
    case ENTRY:
        if (j->context.ring.in == j->context.ring.out) done(j, 0);
        else j->stage = CHECK_CURRENT;
        break;
    case CHECK_CURRENT:
        j->stage = j->context.cached_current == j->context.ring.in ? STATUS_READ : SLOT_READ;
        break;
    case STATUS_READ: case STATUS_SY:
        if (!j->context.status_owned) { j->phase = AXD_RX_TAKE_DEPENDENCY; break; }
        op.kind = j->stage == STATUS_READ ? AXD_RX_TAKE_STATUS_READ32 : AXD_RX_TAKE_FULL_BARRIER;
        op.identity = j->context.status_identity;
        op.offset = j->stage == STATUS_READ ? 0x10u : 0; direct = 1;
        break;
    case STATUS_STORE:
        if (axd_dma64_status_index(&j->context.ring, j->status_sample, &index) != AXD_DMA64_OK) {
            fail(j, AXD_RX_TAKE_BOUNDS); break;
        }
        j->context.cached_current = (uint16_t)index;
        if (index == j->context.ring.in && j->force == 0) done(j, 0);
        else j->stage = SLOT_READ;
        break;
    case SLOT_READ: case SLOT_CLEAR:
        if (j->context.ring.in >= j->context.slot_count) { fail(j, AXD_RX_TAKE_BOUNDS); break; }
        if (!j->context.slots_owned) { j->phase = AXD_RX_TAKE_DEPENDENCY; break; }
        op.kind = j->stage == SLOT_READ ? AXD_RX_TAKE_SLOT_READ32 : AXD_RX_TAKE_SLOT_WRITE32;
        op.identity = j->context.slots_identity; op.offset = j->context.ring.in * 4u; direct = 1;
        break;
    case ADVANCE:
        j->context.ring.in = (j->context.ring.in + 1u) & (j->context.ring.count - 1u);
        j->stage = AVAILABLE;
        break;
    case AVAILABLE:
        if (axd_dma64_ring_counts(&j->context.ring, &used, &available) != AXD_DMA64_OK)
            fail(j, AXD_RX_TAKE_BOUNDS);
        else { j->context.available = available; j->stage = RETURN_VALUE; }
        break;
    case RETURN_VALUE:
        if (j->slot_sample == 0x80000000u &&
            (j->context.sentinel_mode_a != 0 || j->context.sentinel_mode_b != 0)) {
            j->skipped_sentinels++;
            if (j->context.ring.in == j->context.ring.out) done(j, 0);
            else j->stage = CHECK_CURRENT;
        } else done(j, j->slot_sample);
        break;
    default: fail(j, AXD_RX_TAKE_BAD_ARGUMENT); break;
    }
    if (direct) {
        unsigned before = j->stage;
        if (io == NULL) { fail(j, AXD_RX_TAKE_BAD_ARGUMENT); return j->phase; }
        j->attempted_ops++;
        if (io(context, &op, &value) != 0) { fail(j, AXD_RX_TAKE_IO_ERROR); return j->phase; }
        j->completed_ops++;
        if (op.kind == AXD_RX_TAKE_STATUS_READ32) j->status_sample = value;
        else if (op.kind == AXD_RX_TAKE_SLOT_READ32) j->slot_sample = value;
        else if (op.kind == AXD_RX_TAKE_SLOT_WRITE32) j->removed_slots++;
        j->stage = before + 1;
    }
    return j->phase;
}
enum axd_rx_take_phase axd_dma_rx_take_cancel(struct axd_dma_rx_take *j)
{
    if (j == NULL) return AXD_RX_TAKE_INVALID;
    if (j->phase == AXD_RX_TAKE_RUNNING) j->phase = AXD_RX_TAKE_CANCELLED;
    return j->phase;
}
