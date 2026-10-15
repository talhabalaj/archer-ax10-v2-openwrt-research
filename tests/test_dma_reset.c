/* SPDX-License-Identifier: MIT */
#include "../driver/dma_reset.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Independent oracle: current-WL dma_txreset/dma_rxreset assembly, summarized
 * in DRIVER_RESOURCE_AUDIT. Replies are synthetic, never vendor payloads.
 * Fixed counts below come from assembly counters, not model stage numbers.
 */
struct event {
    enum axd_d11_kind kind;
    enum axd_dma_region region;
    uint32_t offset, value, reply;
};
#define E(k, r, o, v, a) {AXD_D11_##k, AXD_DMA_##r, o, v, a}
static const struct event rx_literal[] = {
    E(STORE_BARRIER, RX_CHANNEL, 0, 0, 0),
    E(WRITE32, RX_CHANNEL, 0, 0, 0),
    E(READ32, RX_CHANNEL, 0x10, 0, 0x01234567),
    E(FULL_BARRIER, RX_CHANNEL, 0, 0, 0)
};
static const struct event tx_literal[] = {
    E(READ32, TX_CHANNEL, 0x10, 0, 0xf0000011),
    E(FULL_BARRIER, TX_CHANNEL, 0, 0, 0),
    E(STORE_BARRIER, TX_CHANNEL, 0, 0, 0),
    E(WRITE32, TX_CHANNEL, 0, 2, 0),
    E(READ32, TX_CHANNEL, 0x10, 0, 0x30000022),
    E(FULL_BARRIER, TX_CHANNEL, 0, 0, 0),
    E(STORE_BARRIER, TX_CHANNEL, 0, 0, 0),
    E(WRITE32, TX_CHANNEL, 0, 0, 0),
    E(READ32, TX_CHANNEL, 0x10, 0, 0x02345678),
    E(FULL_BARRIER, TX_CHANNEL, 0, 0, 0)
};
static const struct event tx_initial_disabled[] = {
    E(READ32, TX_CHANNEL, 0x10, 0, 0x0fffffff),
    E(FULL_BARRIER, TX_CHANNEL, 0, 0, 0)
};
static const struct event indirect_literal[] = {
    E(STORE_BARRIER, QUEUE_SELECTOR, 0, 0, 0),
    E(WRITE32, QUEUE_SELECTOR, 0, 0xcafe12ab, 0),
    E(STORE_BARRIER, TX_CHANNEL, 0, 0, 0),
    E(WRITE32, TX_CHANNEL, 0, 0, 0)
};
#undef E
#define N(a) (sizeof(a) / sizeof((a)[0]))
#define NEVER ((size_t)-1)
#define CACHE UINT32_C(0x13579bdf)
struct script { struct event events[6012]; size_t count; };
struct recorder {
    const struct event *events;
    size_t count, calls, fail_at, cancel_at;
    struct axd_dma_reset *job;
    uint64_t delay_us, fake_elapsed_us;
};
static struct axd_dma_reset_context context(unsigned indirect, unsigned poll)
{
    const struct axd_dma_reset_context c = {
        130, 1, 8, (uint8_t)indirect, (uint8_t)poll,
        0xcafe12ab, CACHE, 0xab
    };
    return c;
}
static struct recorder recorder(struct axd_dma_reset *job,
                               const struct event *events, size_t count)
{
    const struct recorder r = {events, count, 0, NEVER, NEVER, job, 0, 0};
    return r;
}
static int observe(void *opaque, const struct axd_dma_operation *op, uint32_t *reply)
{
    struct recorder *r = opaque;
    const struct event *e;
    size_t i = r->calls;
    assert(i < r->count);
    e = &r->events[i];
    assert(op->kind == e->kind && op->region == e->region);
    assert(op->offset == e->offset && op->value == e->value);
    assert(r->job->attempted_ops == i + 1 && r->job->completed_ops == i);
    *reply = e->reply;
    r->calls++;
    /* This is counter-based polling: arbitrary host time jumps do not shorten
     * the stock iteration budget or insert extra deadline-confirmation reads.
     */
    r->fake_elapsed_us += 1000000;
    if (op->kind == AXD_D11_DELAY_US) r->delay_us += op->value;
    if (i == r->cancel_at)
        assert(axd_dma_reset_cancel(r->job) == AXD_DMA_CANCELLED);
    return i == r->fail_at ? -1 : 0;
}
static void run(struct axd_dma_reset *job, struct recorder *r)
{
    size_t step, limit = r->count * 5 + 50;
    for (step = 0; step < limit && job->phase == AXD_DMA_RUNNING; step++) {
        size_t before = r->calls;
        enum axd_dma_phase phase = axd_dma_reset_step(job, observe, r);
        assert(phase == job->phase);
        assert(r->calls - before <= 1);
    }
    assert(job->phase != AXD_DMA_RUNNING);
}
static void terminal(struct axd_dma_reset *job, struct recorder *r,
                     enum axd_dma_phase phase)
{
    struct axd_dma_reset before;
    size_t calls = r->calls;
    memcpy(&before, job, sizeof(before));
    assert(axd_dma_reset_step(job, observe, r) == phase);
    assert(axd_dma_reset_step(job, NULL, NULL) == phase);
    assert(axd_dma_reset_cancel(job) == phase);
    assert(r->calls == calls && memcmp(job, &before, sizeof(before)) == 0);
}
static void add(struct script *s, enum axd_d11_kind kind, enum axd_dma_region region,
                uint32_t offset, uint32_t value, uint32_t reply)
{
    const struct event e = {kind, region, offset, value, reply};
    assert(s->count < N(s->events));
    s->events[s->count++] = e;
}
static void read_status(struct script *s, enum axd_dma_region region, uint32_t reply)
{
    add(s, AXD_D11_READ32, region, 0x10, 0, reply);
    add(s, AXD_D11_FULL_BARRIER, region, 0, 0, 0);
}
static void write_control(struct script *s, enum axd_dma_region region, uint32_t value)
{
    add(s, AXD_D11_STORE_BARRIER, region, 0, 0, 0);
    add(s, AXD_D11_WRITE32, region, 0, value, 0);
}
static void delay(struct script *s, enum axd_dma_region region, uint32_t us)
{
    add(s, AXD_D11_DELAY_US, region, 0, us, 0);
}
static void append(struct script *s, const struct event *events, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        assert(s->count < N(s->events));
        s->events[s->count++] = events[i];
    }
}
/* One initial read, then exactly n delay/read pairs. End replies exercise
 * masked-zero and masked-suspended independently of their low status bits.
 */
static void poll(struct script *s, enum axd_dma_region region, unsigned n,
                 uint32_t end)
{
    unsigned i;
    read_status(s, region, n ? 0xfedcba98 : end);
    for (i = 0; i < n; i++) {
        delay(s, region, 10);
        read_status(s, region, i + 1 == n ? end : 0xfedcba98);
    }
}
static void rx_script(struct script *s, unsigned n, int disabled)
{
    s->count = 0;
    write_control(s, AXD_DMA_RX_CHANNEL, 0);
    poll(s, AXD_DMA_RX_CHANNEL, n, disabled ? 0x07654321 : 0x10000000);
}
static void tx_script(struct script *s, unsigned suspend_n, uint32_t suspend_end,
                      unsigned post_n, int disabled)
{
    s->count = 0;
    read_status(s, AXD_DMA_TX_CHANNEL, 0x10000000);
    write_control(s, AXD_DMA_TX_CHANNEL, 2);
    poll(s, AXD_DMA_TX_CHANNEL, suspend_n, suspend_end);
    write_control(s, AXD_DMA_TX_CHANNEL, 0);
    poll(s, AXD_DMA_TX_CHANNEL, post_n, disabled ? 0x0fffffff : 0xa0000000);
    if (!disabled) delay(s, AXD_DMA_TX_CHANNEL, 300);
}
static void success(const struct event *events, size_t count,
                    enum axd_dma_direction direction,
                    const struct axd_dma_reset_context *input,
                    unsigned result, unsigned observed, unsigned exhausted,
                    uint32_t cache, uint8_t selector_cache, uint64_t delay_us)
{
    struct axd_dma_reset job;
    struct axd_dma_reset_context c = *input;
    struct recorder r = recorder(&job, events, count);
    assert(axd_dma_reset_begin(&job, direction, &c) == 0);
    c.core = 0; c.descriptor_count = 0; c.indirect ^= 1;
    c.post_disable_poll ^= 1; c.selector_value = 0; c.tx_cache = 0;
    assert(job.context.core == input->core && job.context.minor == input->minor);
    assert(job.context.descriptor_count == input->descriptor_count);
    assert(job.context.indirect == input->indirect);
    assert(job.context.post_disable_poll == input->post_disable_poll);
    assert(job.context.selector_value == input->selector_value);
    assert(job.context.tx_cache == input->tx_cache);
    assert(job.context.selector_cache == input->selector_cache);
    run(&job, &r);
    assert(job.phase == AXD_DMA_TRACE_COMPLETE && job.error == AXD_DMA_NO_ERROR);
    assert(r.calls == count && job.attempted_ops == count && job.completed_ops == count);
    assert(job.return_value == result && job.disabled_observed == observed);
    assert(job.suspend_exhausted == exhausted && job.context.tx_cache == cache);
    assert(job.context.selector_cache == selector_cache && r.delay_us == delay_us);
    assert(r.fake_elapsed_us == count * UINT64_C(1000000));
    terminal(&job, &r, AXD_DMA_TRACE_COMPLETE);
}
/* Every callback, including each final delay, has three exits: error,
 * successful cancellation, and cancellation plus error (error wins).
 * The terminal model must not invent software cache commit or cleanup.
 */
static void sweep(const struct event *events, size_t count,
                  enum axd_dma_direction direction,
                  const struct axd_dma_reset_context *c)
{
    unsigned mode;
    size_t i;
    for (mode = 0; mode < 3; mode++) {
        uint32_t prior_read = 0;
        for (i = 0; i < count; i++) {
            struct axd_dma_reset job;
            struct recorder r = recorder(&job, events, count);
            enum axd_dma_phase phase = mode == 1 ? AXD_DMA_CANCELLED : AXD_DMA_FAILED;
            uint32_t expected_read = prior_read;
            assert(axd_dma_reset_begin(&job, direction, c) == 0);
            if (mode != 1) r.fail_at = i;
            if (mode != 0) r.cancel_at = i;
            run(&job, &r);
            assert(job.phase == phase && r.calls == i + 1);
            assert(job.error == (mode == 1 ? AXD_DMA_NO_ERROR : AXD_DMA_IO_ERROR));
            assert(job.attempted_ops == i + 1 && job.completed_ops == i + (mode == 1));
            assert(job.return_value == 0 && job.context.tx_cache == c->tx_cache);
            assert(job.disabled_observed == 0);
            if (mode == 1 && events[i].kind == AXD_D11_READ32)
                expected_read = events[i].reply;
            assert(job.last_read == expected_read);
            /* Selector-cache commit is a software transition after the forced
             * selector write. Cancellation on that write prevents the commit.
             */
            assert(job.context.selector_cache ==
                   (direction == AXD_DMA_TX_RESET && c->indirect && i >= 2
                    ? (uint8_t)c->selector_value : c->selector_cache));
            terminal(&job, &r, phase);
            if (events[i].kind == AXD_D11_READ32) prior_read = events[i].reply;
        }
    }
}
static void invalid(enum axd_dma_direction direction,
                    const struct axd_dma_reset_context *c, enum axd_dma_error error)
{
    struct axd_dma_reset job;
    struct recorder r = recorder(&job, NULL, 0);
    assert(axd_dma_reset_begin(&job, direction, c) != 0);
    assert(job.phase == AXD_DMA_FAILED && job.error == error);
    assert(job.attempted_ops == 0 && job.completed_ops == 0);
    terminal(&job, &r, AXD_DMA_FAILED);
}
static void arguments(void)
{
    struct axd_dma_reset job;
    struct axd_dma_reset_context c = context(0, 0);
    struct recorder r = recorder(&job, NULL, 0);
    unsigned i;
    assert(axd_dma_reset_begin(NULL, AXD_DMA_TX_RESET, &c) != 0);
    assert(axd_dma_reset_step(NULL, NULL, NULL) == AXD_DMA_INVALID);
    assert(axd_dma_reset_cancel(NULL) == AXD_DMA_INVALID);
    invalid(AXD_DMA_TX_RESET, NULL, AXD_DMA_BAD_ARGUMENT);
    invalid((enum axd_dma_direction)0, &c, AXD_DMA_BAD_ARGUMENT);
    invalid((enum axd_dma_direction)3, &c, AXD_DMA_BAD_ARGUMENT);
    c.indirect = 2; invalid(AXD_DMA_TX_RESET, &c, AXD_DMA_BAD_ARGUMENT);
    c = context(0, 2); invalid(AXD_DMA_RX_RESET, &c, AXD_DMA_BAD_ARGUMENT);
    c = context(0, 0); c.core = 129; invalid(AXD_DMA_RX_RESET, &c, AXD_DMA_UNSUPPORTED);
    c.core = 131; invalid(AXD_DMA_TX_RESET, &c, AXD_DMA_UNSUPPORTED);
    c.core = 130; c.minor = 2; invalid(AXD_DMA_RX_RESET, &c, AXD_DMA_UNSUPPORTED);
    c = context(0, 0);
    c.descriptor_count = 0;
    assert(axd_dma_reset_begin(&job, AXD_DMA_RX_RESET, &c) == 0);
    assert(axd_dma_reset_step(&job, NULL, NULL) == AXD_DMA_TRACE_COMPLETE);
    assert(job.return_value == 1 && job.disabled_observed == 0);
    terminal(&job, &r, AXD_DMA_TRACE_COMPLETE);
    c.descriptor_count = 8;
    assert(axd_dma_reset_begin(&job, AXD_DMA_RX_RESET, &c) == 0);
    assert(axd_dma_reset_cancel(&job) == AXD_DMA_CANCELLED);
    terminal(&job, &r, AXD_DMA_CANCELLED);
    assert(r.calls == 0 && job.attempted_ops == 0 && job.completed_ops == 0);
    assert(axd_dma_reset_begin(&job, AXD_DMA_RX_RESET, &c) == 0);
    for (i = 0; i < 10 && job.phase == AXD_DMA_RUNNING; i++)
        axd_dma_reset_step(&job, NULL, NULL);
    assert(job.phase == AXD_DMA_FAILED && job.error == AXD_DMA_BAD_ARGUMENT);
    assert(job.attempted_ops == 0 && job.completed_ops == 0);
    terminal(&job, &r, AXD_DMA_FAILED);
    /* Restart with a context inside job; begin must snapshot before clearing. */
    c = context(1, 0); c.selector_cache = 0x55;
    assert(axd_dma_reset_begin(&job, AXD_DMA_TX_RESET, &c) == 0);
    r = recorder(&job, indirect_literal, N(indirect_literal));
    run(&job, &r);
    assert(job.phase == AXD_DMA_TRACE_COMPLETE);
    assert(axd_dma_reset_begin(&job, AXD_DMA_TX_RESET, &job.context) == 0);
    assert(job.context.core == 130 && job.context.minor == 1);
    assert(job.context.descriptor_count == 8 && job.context.indirect == 1);
    assert(job.context.selector_value == 0xcafe12ab && job.context.selector_cache == 0xab);
    assert(job.context.tx_cache == 0xffffffffu && job.attempted_ops == 0);
    assert(job.completed_ops == 0 && job.last_read == 0 && job.disabled_observed == 0);
    assert(job.suspend_exhausted == 0 && job.return_value == 0);
    r = recorder(&job, indirect_literal, N(indirect_literal));
    run(&job, &r);
    assert(r.calls == N(indirect_literal) && job.phase == AXD_DMA_TRACE_COMPLETE);
    terminal(&job, &r, AXD_DMA_TRACE_COMPLETE);
}
int main(void)
{
    struct script rx_short = {{{0}}, 0}, rx_last = {{{0}}, 0}, rx_stuck = {{{0}}, 0};
    struct script tx_short = {{{0}}, 0}, tx_last = {{{0}}, 0}, tx_stuck = {{{0}}, 0};
    struct script tx_exhaust = {{{0}}, 0}, indirect_poll = {{{0}}, 0};
    struct axd_dma_reset_context direct = context(0, 0), indirect = context(1, 0);
    struct axd_dma_reset_context c;
    unsigned minor, flag;
    assert(N(rx_literal) == 4 && N(tx_literal) == 10 && N(indirect_literal) == 4);
    arguments();
    for (minor = 0; minor < 2; minor++) {
        for (flag = 0; flag < 2; flag++) {
            c = context(flag, flag); c.minor = (uint16_t)minor;
            c.descriptor_count = 0;
            success(NULL, 0, AXD_DMA_TX_RESET, &c, 1, 0, 0, CACHE, 0xab, 0);
            success(NULL, 0, AXD_DMA_RX_RESET, &c, 1, 0, 0, CACHE, 0xab, 0);
            c.descriptor_count = 8;
            /* RX ignores both indirect and current flagword bit0x100. */
            success(rx_literal, N(rx_literal), AXD_DMA_RX_RESET, &c, 1, 1, 0, CACHE, 0xab, 0);
            c.indirect = 0;
            success(tx_literal, N(tx_literal), AXD_DMA_TX_RESET, &c, 1, 1, 0, 0xffffffff, 0xab, 0);
            success(tx_initial_disabled, N(tx_initial_disabled), AXD_DMA_TX_RESET,
                    &c, 1, 1, 0, CACHE, 0xab, 0);
        }
    }
    /* Even an already matching selector cache cannot suppress this write. */
    success(indirect_literal, N(indirect_literal), AXD_DMA_TX_RESET, &indirect,
            1, 0, 0, 0xffffffff, 0xab, 0);
    indirect.selector_cache = 0x55;
    success(indirect_literal, N(indirect_literal), AXD_DMA_TX_RESET, &indirect,
            1, 0, 0, 0xffffffff, 0xab, 0);
    append(&indirect_poll, indirect_literal, N(indirect_literal));
    poll(&indirect_poll, AXD_DMA_TX_CHANNEL, 2, 0x07654321);
    assert(indirect_poll.count == 12);
    c = indirect; c.post_disable_poll = 1;
    success(indirect_poll.events, indirect_poll.count, AXD_DMA_TX_RESET, &c,
            1, 1, 0, 0xffffffff, 0xab, 20);
    sweep(indirect_poll.events, indirect_poll.count, AXD_DMA_TX_RESET, &c);
    rx_script(&rx_short, 2, 1); rx_script(&rx_last, 1000, 1); rx_script(&rx_stuck, 1000, 0);
    assert(rx_short.count == 10 && rx_last.count == 3004 && rx_stuck.count == 3004);
    success(rx_short.events, rx_short.count, AXD_DMA_RX_RESET, &direct, 1, 1, 0, CACHE, 0xab, 20);
    success(rx_last.events, rx_last.count, AXD_DMA_RX_RESET, &direct, 1, 1, 0, CACHE, 0xab, 10000);
    success(rx_stuck.events, rx_stuck.count, AXD_DMA_RX_RESET, &direct, 0, 0, 0, CACHE, 0xab, 10000);
    tx_script(&tx_short, 2, 0x20000001, 2, 1);
    tx_script(&tx_last, 1000, 0x30000002, 1000, 1);
    tx_script(&tx_stuck, 1000, 0xf0000000, 1000, 0);
    tx_script(&tx_exhaust, 1000, 0xf0000000, 0, 1);
    assert(tx_short.count == 22 && tx_last.count == 6010 && tx_stuck.count == 6011);
    assert(tx_exhaust.count == 3010);
    success(tx_short.events, tx_short.count, AXD_DMA_TX_RESET, &direct, 1, 1, 0, 0xffffffff, 0xab, 40);
    success(tx_last.events, tx_last.count, AXD_DMA_TX_RESET, &direct, 1, 1, 0, 0xffffffff, 0xab, 20000);
    success(tx_stuck.events, tx_stuck.count, AXD_DMA_TX_RESET, &direct, 0, 0, 1, 0xffffffff, 0xab, 20300);
    success(tx_exhaust.events, tx_exhaust.count, AXD_DMA_TX_RESET, &direct, 1, 1, 1, 0xffffffff, 0xab, 10000);
    /* A disabled suspend sample also exits without a delay; disabling still
     * happens, and post-disable state must independently be observed. */
    tx_script(&tx_short, 0, 0x00000001, 0, 1);
    success(tx_short.events, tx_short.count, AXD_DMA_TX_RESET, &direct, 1, 1, 0, 0xffffffff, 0xab, 0);
    /* Bit28 alone is neither disabled nor suspended; poll once before seeing
     * suspended, and then independently perform the post-disable read. */
    tx_script(&tx_short, 1, 0x20000000, 0, 1);
    tx_short.events[4].reply = 0x10000000;
    success(tx_short.events, tx_short.count, AXD_DMA_TX_RESET, &direct, 1, 1, 0, 0xffffffff, 0xab, 10);
    sweep(rx_literal, N(rx_literal), AXD_DMA_RX_RESET, &direct);
    sweep(tx_literal, N(tx_literal), AXD_DMA_TX_RESET, &direct);
    sweep(tx_initial_disabled, N(tx_initial_disabled), AXD_DMA_TX_RESET, &direct);
    sweep(indirect_literal, N(indirect_literal), AXD_DMA_TX_RESET, &indirect);
    sweep(rx_last.events, rx_last.count, AXD_DMA_RX_RESET, &direct);
    sweep(rx_stuck.events, rx_stuck.count, AXD_DMA_RX_RESET, &direct);
    sweep(tx_last.events, tx_last.count, AXD_DMA_TX_RESET, &direct);
    sweep(tx_stuck.events, tx_stuck.count, AXD_DMA_TX_RESET, &direct);
    puts("DMA reset: literal/direct/indirect, 1000-loop boundaries and every-event exits passed");
    return 0;
}
