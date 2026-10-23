/* SPDX-License-Identifier: MIT */
#include "dma_reset.h"

enum stage {
    ENTRY, TX_INITIAL_CHECK, TX_SUSPEND, TX_SUSPEND_CHECK, SELECT_QUEUE,
    SELECT_COMMIT, DISABLE, POST_READ, POST_CHECK, TX_TIMEOUT, FINISH
};
static enum axd_dma_region channel(const struct axd_dma_reset *job)
{
    return job->direction == AXD_DMA_TX_RESET ? AXD_DMA_TX_CHANNEL : AXD_DMA_RX_CHANNEL;
}
static void add(struct axd_dma_reset *job, enum axd_d11_kind kind,
                enum axd_dma_region region, uint32_t offset, uint32_t value)
{
    const struct axd_dma_operation op = {kind, region, offset, value};
    job->operations[job->queued++] = op;
}
static void read_status(struct axd_dma_reset *job)
{
    add(job, AXD_D11_READ32, channel(job), 0x10, 0);
    add(job, AXD_D11_FULL_BARRIER, channel(job), 0, 0);
}
static void write_control(struct axd_dma_reset *job, uint32_t value)
{
    add(job, AXD_D11_STORE_BARRIER, channel(job), 0, 0);
    add(job, AXD_D11_WRITE32, channel(job), 0, value);
}
static void fail(struct axd_dma_reset *job, enum axd_dma_error error)
{
    job->phase = AXD_DMA_FAILED;
    job->error = error;
}
static void complete(struct axd_dma_reset *job, unsigned value)
{
    job->return_value = value;
    job->phase = AXD_DMA_TRACE_COMPLETE;
}
static int disabled(const struct axd_dma_reset *job)
{
    return (job->last_read & 0xf0000000u) == 0;
}
static void prepare(struct axd_dma_reset *job)
{
    job->queued = job->cursor = 0;
    switch (job->stage) {
    case ENTRY:
        if (job->context.descriptor_count == 0) {
            complete(job, 1);
        } else if (job->direction == AXD_DMA_RX_RESET) {
            job->stage = DISABLE;
        } else if (job->context.indirect) {
            job->stage = SELECT_QUEUE;
        } else {
            read_status(job);
            job->stage = TX_INITIAL_CHECK;
        }
        break;
    case TX_INITIAL_CHECK:
        if (disabled(job)) {
            job->disabled_observed = 1;
            complete(job, 1); /* This early return does not touch tx_cache. */
        } else {
            job->stage = TX_SUSPEND;
        }
        break;
    case TX_SUSPEND:
        write_control(job, 2);
        read_status(job);
        job->repetitions = 0;
        job->stage = TX_SUSPEND_CHECK;
        break;
    case TX_SUSPEND_CHECK:
        if (disabled(job) || (job->last_read & 0xe0000000u) == 0x20000000u) {
            job->stage = DISABLE;
        } else if (job->repetitions == 1000) {
            job->suspend_exhausted = 1;
            job->stage = DISABLE;
        } else {
            add(job, AXD_D11_DELAY_US, channel(job), 0, 10);
            read_status(job);
            job->repetitions++;
        }
        break;
    case SELECT_QUEUE:
        add(job, AXD_D11_STORE_BARRIER, AXD_DMA_QUEUE_SELECTOR, 0, 0);
        add(job, AXD_D11_WRITE32, AXD_DMA_QUEUE_SELECTOR, 0, job->context.selector_value);
        job->stage = SELECT_COMMIT;
        break;
    case SELECT_COMMIT:
        job->context.selector_cache = (uint8_t)job->context.selector_value;
        job->stage = DISABLE;
        break;
    case DISABLE:
        write_control(job, 0);
        job->repetitions = 0;
        if (job->direction == AXD_DMA_RX_RESET || !job->context.indirect ||
            job->context.post_disable_poll)
            job->stage = POST_READ;
        else
            job->stage = FINISH;
        break;
    case POST_READ:
        read_status(job);
        job->stage = POST_CHECK;
        break;
    case POST_CHECK:
        if (disabled(job)) {
            job->disabled_observed = 1;
            job->stage = FINISH;
        } else if (job->repetitions == 1000) {
            if (job->direction == AXD_DMA_TX_RESET) {
                add(job, AXD_D11_DELAY_US, channel(job), 0, 300);
                job->stage = TX_TIMEOUT;
            } else {
                complete(job, 0);
            }
        } else {
            add(job, AXD_D11_DELAY_US, channel(job), 0, 10);
            read_status(job);
            job->repetitions++;
        }
        break;
    case TX_TIMEOUT:
        job->context.tx_cache = 0xffffffffu;
        complete(job, 0);
        break;
    case FINISH:
        if (job->direction == AXD_DMA_TX_RESET)
            job->context.tx_cache = 0xffffffffu;
        complete(job, 1);
        break;
    default:
        fail(job, AXD_DMA_BAD_ARGUMENT);
        break;
    }
}
int axd_dma_reset_begin(struct axd_dma_reset *job, enum axd_dma_direction direction,
                        const struct axd_dma_reset_context *context)
{
    static const struct axd_dma_reset empty={0};
    struct axd_dma_reset_context snapshot;
    if (job == NULL)
        return -1;
    if (context != NULL) {
        snapshot = *context;
        context = &snapshot;
    }
    *job = empty;
    fail(job, AXD_DMA_BAD_ARGUMENT);
    if (context == NULL || (direction != AXD_DMA_TX_RESET && direction != AXD_DMA_RX_RESET) ||
        context->indirect > 1 || context->post_disable_poll > 1)
        return -1;
    if (context->core != 130 || context->minor > 1) {
        job->error = AXD_DMA_UNSUPPORTED;
        return -1;
    }
    job->context = *context;
    job->direction = direction;
    job->stage = ENTRY;
    job->phase = AXD_DMA_RUNNING;
    job->error = AXD_DMA_NO_ERROR;
    return 0;
}
enum axd_dma_phase axd_dma_reset_step(struct axd_dma_reset *job,
                                      axd_dma_io_fn io, void *context)
{
    const struct axd_dma_operation *op;
    uint32_t value = 0;
    if (job == NULL)
        return AXD_DMA_INVALID;
    if (job->phase != AXD_DMA_RUNNING)
        return job->phase;
    if (job->cursor == job->queued) {
        prepare(job);
        if (job->phase != AXD_DMA_RUNNING || job->queued == 0)
            return job->phase;
    }
    if (io == NULL) {
        fail(job, AXD_DMA_BAD_ARGUMENT);
        return job->phase;
    }
    op = &job->operations[job->cursor];
    job->attempted_ops++;
    if (io(context, op, &value) != 0) {
        fail(job, AXD_DMA_IO_ERROR);
        return job->phase;
    }
    job->completed_ops++;
    if (op->kind == AXD_D11_READ32)
        job->last_read = value;
    job->cursor++;
    return job->phase;
}
enum axd_dma_phase axd_dma_reset_cancel(struct axd_dma_reset *job)
{
    if (job == NULL)
        return AXD_DMA_INVALID;
    if (job->phase == AXD_DMA_RUNNING)
        job->phase = AXD_DMA_CANCELLED;
    return job->phase;
}
