/* SPDX-License-Identifier: MIT */
#include "core_reset.h"
#include "job_reset.h"

enum stage {
    ENTRY, DEFAULT_PHY, FAST, INITIAL_UP, DMA_GATE, TX_ENTRY, TX_AFTER_PRIMARY,
    TX_NEXT, FIFO_CLEAR, FIFO_COUNT, FIFO_REASON, WAKE, RX_ENTRY, RX_NEXT,
    RESET_GATE, NORESET_CONTROL, CLOCK_INVALID, LOW_BEGIN, LOW_RETURN,
    BAND, VERSION_TYPE_READ, VERSION_TYPE_SY, VERSION_TYPE_STORE,
    VERSION_REV_READ, VERSION_REV_SY, VERSION_REV_STORE,
    SHARED_PHY, SHARED_STATE, FLAGS, FLAGS_NEXT, CONTROL_RESET, EXTENDED,
    EXTENDED_ST, EXTENDED_WRITE, CAP_CLOCK, PHY_GATE, PLL_UP, PLL_BEGIN,
    TAIL_WORD, TAIL_BYTE, FINAL_CLOCK, FINAL_WORD, FINISH,
    RUN_WL, RUN_SI, RUN_DMA, RUN_LOW, RUN_MCTRL, RUN_PSM, RUN_PHY, RUN_PLL
};
struct relay { struct axd_core_reset *job; axd_core_reset_io_fn io; void *context; };
static void fail(struct axd_core_reset *j, enum axd_core_reset_error error)
{ j->phase = AXD_CORE_FAILED; j->error = error; }
static int known(struct axd_core_reset *j, enum axd_core_reset_field field, unsigned k)
{
    j->last_field = field; j->metadata_accesses++;
    if (k == 1) return 1;
    if (k == 0) j->phase = AXD_CORE_DEPENDENCY;
    else fail(j, AXD_CORE_BAD_ARGUMENT);
    return 0;
}
static int perform(struct relay *r, const struct axd_core_reset_operation *op,
                     struct axd_core_reset_reply *reply)
{
    if (r->io == NULL) { fail(r->job, AXD_CORE_BAD_ARGUMENT); return -1; }
    r->job->attempted_ops++;
    if (r->io(r->context, op, reply) != 0) {
        fail(r->job, AXD_CORE_IO_ERROR); return -1;
    }
    r->job->completed_ops++;
    return 0;
}
static int wl_io(void *opaque, const struct axd_wl_clock_operation *op,
                   struct axd_wl_clock_reply *reply)
{
    struct axd_core_reset_operation converted = {0};
    struct axd_core_reset_reply result = {0};
    converted.path = AXD_CORE_WL; converted.operation.wl = *op;
    if (perform(opaque, &converted, &result) != 0) return -1;
    reply->value = result.value; reply->irq_state_known = result.irq_state_known;
    reply->irq_state = result.irq_state; return 0;
}
#define RELAY(name, type, member, tag) \
static int name(void *opaque, const struct type *op, uint32_t *value) \
{ \
    struct axd_core_reset_operation converted = {0}; \
    struct axd_core_reset_reply reply = {0}; \
    converted.path = tag; converted.operation.member = *op; \
    if (perform(opaque, &converted, &reply) != 0) return -1; \
    *value = reply.value; return 0; \
}
RELAY(si_io, axd_si_core_operation, si, AXD_CORE_SI)
RELAY(phy_io, axd_phy_reset_operation, phy, AXD_CORE_PHY)
RELAY(pll_io, axd_clock_operation, pll, AXD_CORE_PLL)
#undef RELAY
static int low_io(void *opaque, const struct axd_core_reset_low_operation *op, uint32_t *value)
{
    struct relay *r = opaque;
    struct axd_core_reset_operation converted = {0};
    struct axd_core_reset_reply reply = {0};
    r->job->context.low = r->job->low_job.context;
    r->job->context.control = r->job->context.low.mctrl;
    converted.path = AXD_CORE_LOW; converted.operation.low = *op;
    if (perform(r, &converted, &reply) != 0) return -1;
    *value = reply.value; return 0;
}
static int d11_io(void *opaque, const struct axd_d11_operation *op, uint32_t *value)
{
    struct relay *r = opaque;
    struct axd_core_reset_operation converted = {0};
    struct axd_core_reset_reply reply = {0};
    if (r->job->stage == RUN_MCTRL)
        r->job->context.control = r->job->mctrl_job.context;
    else if (r->job->stage == RUN_PSM)
        r->job->context.control.psm = r->job->psm_job.context;
    converted.path = AXD_CORE_D11; converted.operation.d11 = *op;
    if (perform(r, &converted, &reply) != 0) return -1;
    *value = reply.value; return 0;
}
static int dma_io(void *opaque, const struct axd_dma_operation *op, uint32_t *value)
{
    struct relay *r = opaque;
    struct axd_core_reset_operation converted = {0};
    struct axd_core_reset_reply reply = {0};
    const struct axd_core_dma_slot *slot = &r->job->context.dma[r->job->queue];
    struct axd_core_dma_slot *current = &r->job->context.dma[r->job->queue];
    if (r->job->dma_job.direction == AXD_DMA_RX_RESET)
        current->rx = r->job->dma_job.context;
    else if (r->job->secondary) current->paired_tx = r->job->dma_job.context;
    else current->tx = r->job->dma_job.context;
    converted.path = AXD_CORE_DMA; converted.operation.dma = *op;
    converted.queue = r->job->queue; converted.secondary = r->job->secondary;
    converted.dma_identity = r->job->secondary ? slot->secondary.value : slot->primary.value;
    if (perform(r, &converted, &reply) != 0) return -1;
    *value = reply.value; return 0;
}
static void wl(struct axd_core_reset *j, uint32_t mode, unsigned resume)
{
    j->context.clock.d11.wake_reasons = j->context.control.psm.wake_reasons;
    j->context.clock.fallback.si = j->context.low.si;
    j->context.clock.fallback.state = j->context.low.state;
    if (!j->si_state_known) { j->phase = AXD_CORE_DEPENDENCY; return; }
    if (axd_wl_clock_begin(&j->wl_job, mode, &j->context.clock) != 0)
        fail(j, AXD_CORE_CHILD_ERROR);
    else { j->stage = RUN_WL; j->after_child = resume; }
}
static void si(struct axd_core_reset *j, enum axd_si_core_command command,
                 uint32_t mask, uint32_t value, unsigned resume)
{
    struct axd_si_core_context c = {0};
    const struct axd_si_context *s = &j->context.low.si;
    uint32_t index = j->context.low.state.current_index;
    c.interconnect = s->interconnect; c.chip_id = j->context.control.psm.chip_id;
    c.primary_wrapper = j->context.low.state.primary_wrapper;
    c.wrapper_known = (uint8_t)j->si_state_known;
    if (command == AXD_SICO_FLAGS &&
        (s->interconnect == 1 || s->interconnect == 3 || s->interconnect == 4)) {
        j->last_field = AXD_CORE_SI_CATALOG; j->metadata_accesses++;
        if (s->catalog == NULL) { j->phase = AXD_CORE_DEPENDENCY; return; }
        if (s->core_count > AXD_SI_MAX_CORES || index >= s->core_count ||
            index >= s->catalog_length) { fail(j, AXD_CORE_BOUNDS); return; }
        c.core_id = s->catalog[index].core_id;
    }
    if (axd_si_core_begin(&j->si_job, command, mask, value, &c) != 0)
        fail(j, AXD_CORE_CHILD_ERROR);
    else { j->stage = RUN_SI; j->after_child = resume; }
}
static int slot(struct axd_core_reset *j)
{
    if (j->context.dma_slots > AXD_CORE_MAX_QUEUES ||
        j->queue >= AXD_CORE_MAX_QUEUES || j->queue >= j->context.dma_slots) {
        fail(j, AXD_CORE_BOUNDS); return 0;
    }
    return 1;
}
static void dma(struct axd_core_reset *j, enum axd_dma_direction direction,
                  unsigned secondary, unsigned resume)
{
    const struct axd_core_dma_slot *s = &j->context.dma[j->queue];
    const struct axd_dma_reset_context *c = direction == AXD_DMA_RX_RESET ?
        &s->rx : secondary ? &s->paired_tx : &s->tx;
    j->secondary = secondary;
    if (c->core != j->context.clock.d11.core || c->minor != j->context.clock.d11.minor)
        fail(j, AXD_CORE_BINDING);
    else if (axd_dma_reset_begin(&j->dma_job, direction, c) != 0)
        fail(j, AXD_CORE_CHILD_ERROR);
    else { j->stage = RUN_DMA; j->after_child = resume; }
}
static void mctrl(struct axd_core_reset *j, unsigned reset, unsigned resume)
{
    int result = reset ? axd_mctrl_reset_begin(&j->mctrl_job, &j->context.control) :
        axd_mctrl_begin(&j->mctrl_job, 3, 0, &j->context.control);
    if (result != 0) fail(j, AXD_CORE_CHILD_ERROR);
    else { j->stage = RUN_MCTRL; j->after_child = resume; }
}
static void bind_band(struct axd_core_reset *j)
{
    j->context.control.psm.band_binding_required = 1;
    j->context.control.psm.band_presence_known = j->context.band.known;
    if (j->context.band.known == 1)
        j->context.control.psm.band_present = j->context.band.value != 0;
}
static void child_step(struct axd_core_reset *j, struct relay *r)
{
    unsigned done = 0, dependency = 0, cancelled = 0, running = 0;
    switch (j->stage) {
    case RUN_WL: {
        enum axd_wl_clock_phase p = axd_wl_clock_step(&j->wl_job, wl_io, r);
        j->context.clock = j->wl_job.context;
        j->context.control.psm.wake_reasons = j->context.clock.d11.wake_reasons;
        j->context.low.state = j->context.clock.fallback.state;
        if (j->wl_job.path == AXD_WLC_SI)
            j->si_state_known = j->wl_job.si_job.state_valid;
        done = p == AXD_WLC_TRACE_COMPLETE; dependency = p == AXD_WLC_DEPENDENCY;
        cancelled = p == AXD_WLC_CANCELLED; running = p == AXD_WLC_RUNNING;
        break;
    }
    case RUN_SI: {
        enum axd_si_core_phase p = axd_si_core_step(&j->si_job, si_io, r);
        done = p == AXD_SICO_DONE; dependency = p == AXD_SICO_DEPENDENCY;
        cancelled = p == AXD_SICO_CANCELLED; running = p == AXD_SICO_RUNNING;
        if (done && j->phase == AXD_CORE_RUNNING && j->si_job.command == AXD_SICO_IS_UP) {
            if (j->after_child == DMA_GATE) {
                j->initial_up = j->si_job.result; j->initial_up_valid = 1;
            } else {
                j->pll_up = j->si_job.result; j->pll_up_valid = 1;
                j->context.clock.d11.core_up = (uint8_t)j->si_job.result;
            }
        }
        break;
    }
    case RUN_DMA: {
        enum axd_dma_phase p = axd_dma_reset_step(&j->dma_job, dma_io, r);
        struct axd_core_dma_slot *s = &j->context.dma[j->queue];
        if (j->dma_job.direction == AXD_DMA_RX_RESET) s->rx = j->dma_job.context;
        else if (j->secondary) s->paired_tx = j->dma_job.context;
        else s->tx = j->dma_job.context;
        done = p == AXD_DMA_TRACE_COMPLETE;
        cancelled = p == AXD_DMA_CANCELLED; running = p == AXD_DMA_RUNNING;
        break;
    }
    case RUN_LOW: {
        enum axd_core_reset_low_phase p = axd_core_reset_low_step(&j->low_job, low_io, r);
        j->context.low = j->low_job.context;
        j->context.control = j->context.low.mctrl;
        done = p == AXD_LOW_DONE; dependency = p == AXD_LOW_DEPENDENCY;
        cancelled = p == AXD_LOW_CANCELLED; running = p == AXD_LOW_RUNNING;
        if (done && j->phase == AXD_CORE_RUNNING) j->low_completed = 1;
        break;
    }
    case RUN_MCTRL: {
        enum axd_mctrl_phase p = axd_mctrl_step(&j->mctrl_job, d11_io, r);
        j->context.control = j->mctrl_job.context;
        done = p == AXD_MCTRL_DONE;
        cancelled = p == AXD_MCTRL_CANCELLED; running = p == AXD_MCTRL_RUNNING;
        if (done && j->phase == AXD_CORE_RUNNING && j->mctrl_job.reset)
            j->control_reset_completed = 1;
        break;
    }
    case RUN_PSM: {
        enum axd_psm_phase p = axd_psm_control_step(&j->psm_job, d11_io, r);
        j->context.control.psm = j->psm_job.context;
        j->pending_callouts |= j->psm_job.pending_callouts;
        done = p == AXD_PSM_DONE; dependency = p == AXD_PSM_DEPENDENCY;
        cancelled = p == AXD_PSM_CANCELLED; running = p == AXD_PSM_RUNNING;
        break;
    }
    case RUN_PHY: {
        enum axd_reset_phase p = axd_phy_reset_step(&j->phy_job, phy_io, r);
        j->context.phy = j->phy_job.context;
        j->context.control.psm = j->context.phy.idle.tssi.hardware.mac;
        j->pending_callouts |= j->phy_job.pending_callouts;
        done = p == AXD_RESET_TRACE_COMPLETE; dependency = p == AXD_RESET_DEPENDENCY;
        cancelled = p == AXD_RESET_CANCELLED; running = p == AXD_RESET_RUNNING;
        if (done && j->phase == AXD_CORE_RUNNING) j->phy_completed = 1;
        break;
    }
    case RUN_PLL: {
        enum axd_clock_phase p = axd_clock_control_step(&j->pll_job, pll_io, r);
        j->context.clock.d11 = j->pll_job.context;
        done = p == AXD_CLOCK_TRACE_COMPLETE; dependency = p == AXD_CLOCK_DEPENDENCY;
        cancelled = p == AXD_CLOCK_CANCELLED; running = p == AXD_CLOCK_RUNNING;
        if (done && j->phase == AXD_CORE_RUNNING) j->pll_completed = 1;
        break;
    }
    default: fail(j, AXD_CORE_BAD_ARGUMENT); return;
    }
    /* Context propagation precedes the outer callback's terminal/cancel check. */
    j->context.clock.d11.wake_reasons = j->context.control.psm.wake_reasons;
    if (j->phase != AXD_CORE_RUNNING) return;
    if (done) j->stage = j->after_child;
    else if (dependency) j->phase = AXD_CORE_DEPENDENCY;
    else if (cancelled) j->phase = AXD_CORE_CANCELLED;
    else if (!running) fail(j, AXD_CORE_CHILD_ERROR);
}
int axd_core_reset_begin(struct axd_core_reset *j, uint32_t flags,
                          const struct axd_core_reset_context *context)
{
    if (j == NULL) return -1;
    if (axd_job_overlaps(j, sizeof(*j), context, sizeof(*context), &j->context)) context = NULL;
    else if (context != NULL && context != &j->context) j->context = *context;
    axd_job_zero_except(j, sizeof(*j), offsetof(struct axd_core_reset, context),
                        context ? sizeof(j->context) : 0, 0, 0);
    fail(j, AXD_CORE_BAD_ARGUMENT);
    if (context == NULL) return -1;
    j->flags = flags;
    if (j->context.clock.d11.core != 130 || j->context.clock.d11.minor > 1) {
        fail(j, AXD_CORE_UNSUPPORTED); return -1;
    }
    j->si_state_known = 1; j->stage = ENTRY;
    j->phase = AXD_CORE_RUNNING; j->error = AXD_CORE_NO_ERROR;
    return 0;
}
enum axd_core_reset_phase axd_core_reset_step(struct axd_core_reset *j,
                                              axd_core_reset_io_fn io, void *context)
{
    struct relay relay = {j, io, context};
    struct axd_core_reset_operation op = {0};
    struct axd_core_reset_reply reply = {0};
    unsigned direct = 0;
    if (j == NULL) return AXD_CORE_INVALID;
    if (j->phase != AXD_CORE_RUNNING) return j->phase;
    switch (j->stage) {
    case ENTRY:
        if (j->flags != UINT32_MAX) j->stage = FAST;
        else if (known(j, AXD_CORE_BAND, j->context.band.known)) {
            if (j->context.band.value == 0) fail(j, AXD_CORE_BINDING);
            else j->stage = DEFAULT_PHY;
        }
        break;
    case DEFAULT_PHY:
        if (known(j, AXD_CORE_PHY_POINTER, j->context.phy.phy_pointer.known)) {
            j->flags = j->context.phy.phy_pointer.value ? j->context.band_flags : 0;
            j->stage = FAST;
        }
        break;
    case FAST:
        j->original_fast = j->context.clock.d11.fast_clock;
        if (j->original_fast == 0) wl(j, 0, INITIAL_UP);
        else j->stage = INITIAL_UP;
        break;
    case INITIAL_UP: si(j, AXD_SICO_IS_UP, 0, 0, DMA_GATE); break;
    case DMA_GATE:
        j->queue = 0;
        j->stage = j->initial_up ? TX_ENTRY : RESET_GATE;
        break;
    case TX_ENTRY:
        if (j->queue >= j->context.tx_count) { j->queue = 0; j->stage = RX_ENTRY; }
        else if (slot(j) && known(j, AXD_CORE_PRIMARY_DMA, j->context.dma[j->queue].primary.known)) {
            if (j->context.dma[j->queue].primary.value == 0) j->stage = TX_NEXT;
            else dma(j, AXD_DMA_TX_RESET, 0, TX_AFTER_PRIMARY);
        }
        break;
    case TX_AFTER_PRIMARY:
        if (j->context.paired_dma == 0) j->stage = FIFO_CLEAR;
        else if (known(j, AXD_CORE_SECONDARY_DMA, j->context.dma[j->queue].secondary.known)) {
            if (j->context.dma[j->queue].secondary.value == 0) j->stage = TX_NEXT;
            else dma(j, AXD_DMA_TX_RESET, 1, FIFO_CLEAR);
        }
        break;
    case FIFO_CLEAR:
        if (j->context.control.suspended_count == 0 ||
            (j->context.control.suspended_bitmap[j->queue >> 3] & (1u << (j->queue & 7))) == 0)
            j->stage = TX_NEXT;
        else {
            j->context.control.suspended_bitmap[j->queue >> 3] &= (uint8_t)~(1u << (j->queue & 7));
            j->stage = FIFO_COUNT;
        }
        break;
    case FIFO_COUNT:
        j->context.control.suspended_count--;
        j->stage = j->context.control.suspended_count == 0 ? FIFO_REASON : TX_NEXT;
        break;
    case FIFO_REASON:
        j->context.control.psm.wake_reasons &= ~8u;
        j->context.clock.d11.wake_reasons = j->context.control.psm.wake_reasons;
        j->stage = j->context.control.psm.wake_reasons == 0 ? WAKE : TX_NEXT;
        break;
    case WAKE:
        bind_band(j);
        if (axd_psm_wake_control_begin(&j->psm_job, &j->context.control.psm, &j->context.policy) != 0)
            fail(j, AXD_CORE_CHILD_ERROR);
        else { j->stage = RUN_PSM; j->after_child = TX_NEXT; }
        break;
    case TX_NEXT: j->queue++; j->stage = TX_ENTRY; break;
    case RX_ENTRY:
        if (j->queue == 4) j->stage = RESET_GATE;
        else if (slot(j) && known(j, AXD_CORE_PRIMARY_DMA, j->context.dma[j->queue].primary.known)) {
            if (j->context.dma[j->queue].primary.value != 0 &&
                (j->queue == 0 || (j->queue == 3 && j->context.rx_fifo3 != 0)))
                dma(j, AXD_DMA_RX_RESET, 0, RX_NEXT);
            else j->stage = RX_NEXT;
        }
        break;
    case RX_NEXT: j->queue++; j->stage = RX_ENTRY; break;
    case RESET_GATE:
        if (j->context.no_reset != 0) { j->context.word164 = 0; j->stage = NORESET_CONTROL; }
        else j->stage = CLOCK_INVALID;
        break;
    case NORESET_CONTROL: mctrl(j, 0, FINISH); break;
    case CLOCK_INVALID: j->context.clock.d11.core_clock_valid = 0; j->stage = LOW_BEGIN; break;
    case LOW_BEGIN:
        j->context.low.hw_present = 1;
        j->context.low.revision = j->context.clock.d11.core;
        j->context.low.chip_id = j->context.control.psm.chip_id;
        j->context.low.flags = j->flags | 4u; j->context.low.reset_bits = 0;
        j->context.low.mctrl = j->context.control;
        if (axd_core_reset_low_begin(&j->low_job, &j->context.low) != 0)
            fail(j, AXD_CORE_CHILD_ERROR);
        else { j->stage = RUN_LOW; j->after_child = LOW_RETURN; }
        break;
    case LOW_RETURN: j->context.clock.d11.core_clock_valid = 1; j->stage = BAND; break;
    case BAND:
        if (known(j, AXD_CORE_BAND, j->context.band.known)) {
            if (j->context.band.value == 0) j->stage = CONTROL_RESET;
            else if (j->context.phy.band_type == 0 && j->context.phy.band_revision == 0)
                j->stage = VERSION_TYPE_READ;
            else j->stage = SHARED_PHY;
        }
        break;
    case VERSION_TYPE_READ: case VERSION_REV_READ:
        op.operation.d11.kind = AXD_D11_READ16; op.operation.d11.offset = 0x3e0;
        direct = 1; break;
    case VERSION_TYPE_SY: case VERSION_REV_SY:
        op.operation.d11.kind = AXD_D11_FULL_BARRIER; direct = 1; break;
    case VERSION_TYPE_STORE: {
        unsigned type = (j->sample >> 8) & 0xfu;
        j->context.phy.band_type = (uint16_t)(type == 0xd ? 0xb : type);
        j->stage = VERSION_REV_READ; break;
    }
    case VERSION_REV_STORE:
        j->context.phy.band_revision = (uint16_t)(j->sample & 0xffu);
        j->stage = SHARED_PHY; break;
    case SHARED_PHY:
        if (known(j, AXD_CORE_PHY_POINTER, j->context.phy.phy_pointer.known))
            j->stage = j->context.phy.phy_pointer.value ? SHARED_STATE : FLAGS;
        break;
    case SHARED_STATE:
        if (known(j, AXD_CORE_PHY_SHARED, j->context.phy_shared.known)) {
            if (j->context.phy_shared.value != 0) j->context.phy_shared_clock = 1;
            j->stage = FLAGS;
        }
        break;
    case FLAGS:
        if (j->context.phy.band_type != 0xb) j->stage = CONTROL_RESET;
        else {
            const uint32_t masks[3] = {0x300, 6, 4}, values[3] = {0x100, 0, 4};
            si(j, AXD_SICO_FLAGS, masks[j->flags_index], values[j->flags_index], FLAGS_NEXT);
        }
        break;
    case FLAGS_NEXT:
        j->flags_index++; j->stage = j->flags_index == 3 ? CONTROL_RESET : FLAGS; break;
    case CONTROL_RESET: mctrl(j, 1, EXTENDED); break;
    case EXTENDED:
        if (j->context.extended_supported != 0) {
            j->context.control.psm.extended_control = 0x400; j->stage = EXTENDED_ST;
        } else j->stage = CAP_CLOCK;
        break;
    case EXTENDED_ST: op.operation.d11.kind = AXD_D11_STORE_BARRIER; direct = 1; break;
    case EXTENDED_WRITE:
        op.operation.d11.kind = AXD_D11_WRITE32;
        op.operation.d11.offset = 0x1b4; op.operation.d11.value = 0x400; direct = 1; break;
    case CAP_CLOCK:
        if ((j->context.clock.d11.si_capabilities & 0x10000000u) != 0) wl(j, 0, PHY_GATE);
        else j->stage = PHY_GATE;
        break;
    case PHY_GATE:
        if (j->context.band.value == 0) j->stage = PLL_UP;
        else if (known(j, AXD_CORE_PHY_POINTER, j->context.phy.phy_pointer.known)) {
            if (j->context.phy.phy_pointer.value == 0) j->stage = PLL_UP;
            else {
                struct axd_phy_reset_context *p = &j->context.phy;
                bind_band(j);
                p->idle.tssi.hardware.mac = j->context.control.psm;
                p->idle.tssi.hardware.policy = j->context.policy;
                p->clock.phy_type = p->band_type; p->clock.phy_revision = p->band_revision;
                p->idle.tssi.hardware.phy.phy_type = p->band_type;
                p->idle.tssi.hardware.phy.phy_revision = p->band_revision;
                if (p->clock.core != j->context.clock.d11.core ||
                    p->clock.minor != j->context.clock.d11.minor ||
                    p->clock.chip_id != j->context.control.psm.chip_id ||
                    p->clock.interconnect != j->context.low.si.interconnect)
                    fail(j, AXD_CORE_BINDING);
                else if (axd_phy_reset_begin(&j->phy_job, p) != 0)
                    fail(j, AXD_CORE_CHILD_ERROR);
                else { j->stage = RUN_PHY; j->after_child = PLL_UP; }
            }
        }
        break;
    case PLL_UP:
        if (j->context.clock.d11.core_clock_valid == 0 || j->context.clock.d11.pll_inhibit != 0)
            j->stage = PLL_BEGIN;
        else si(j, AXD_SICO_IS_UP, 0, 0, PLL_BEGIN);
        break;
    case PLL_BEGIN:
        j->context.clock.d11.wake_reasons = j->context.control.psm.wake_reasons;
        if (axd_clock_control_begin(&j->pll_job, AXD_CLOCK_PLL, 1, &j->context.clock.d11) != 0)
            fail(j, AXD_CORE_CHILD_ERROR);
        else { j->stage = RUN_PLL; j->after_child = TAIL_WORD; }
        break;
    case TAIL_WORD: j->context.word164 = 0; j->stage = TAIL_BYTE; break;
    case TAIL_BYTE: j->context.tail_byte = 0; j->stage = FINAL_CLOCK; break;
    case FINAL_CLOCK:
        if (j->original_fast == 0) wl(j, 2, FINAL_WORD);
        else j->stage = FINAL_WORD;
        break;
    case FINAL_WORD: j->context.word22c = UINT32_MAX; j->stage = FINISH; break;
    case FINISH: j->phase = AXD_CORE_TRACE_COMPLETE; break;
    default: child_step(j, &relay); break;
    }
    if (direct) {
        unsigned stage = j->stage;
        op.path = AXD_CORE_D11;
        if (perform(&relay, &op, &reply) == 0) {
            if (stage == VERSION_TYPE_READ || stage == VERSION_REV_READ) {
                j->sample = (uint16_t)reply.value; j->stage++;
            } else if (stage == VERSION_TYPE_SY) {
                j->stage = VERSION_TYPE_STORE;
            } else if (stage == VERSION_REV_SY) {
                j->stage = VERSION_REV_STORE;
            } else j->stage = stage == EXTENDED_ST ? EXTENDED_WRITE : CAP_CLOCK;
        }
    }
    return j->phase;
}
enum axd_core_reset_phase axd_core_reset_cancel(struct axd_core_reset *j)
{
    if (j == NULL) return AXD_CORE_INVALID;
    if (j->phase == AXD_CORE_RUNNING) {
        j->phase = AXD_CORE_CANCELLED;
        switch (j->stage) {
        case RUN_WL: axd_wl_clock_cancel(&j->wl_job); break;
        case RUN_SI: axd_si_core_cancel(&j->si_job); break;
        case RUN_DMA: axd_dma_reset_cancel(&j->dma_job); break;
        case RUN_LOW: axd_core_reset_low_cancel(&j->low_job); break;
        case RUN_MCTRL: axd_mctrl_cancel(&j->mctrl_job); break;
        case RUN_PSM: axd_psm_control_cancel(&j->psm_job); break;
        case RUN_PHY: axd_phy_reset_cancel(&j->phy_job); break;
        case RUN_PLL: axd_clock_control_cancel(&j->pll_job); break;
        default: break;
        }
    }
    return j->phase;
}
