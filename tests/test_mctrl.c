/* SPDX-License-Identifier: MIT */
#include "../driver/mctrl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Original literal traces derived from the retained instruction order. No
 * default reads, echo reads, generated production traces or hardware access.
 */
struct event { enum axd_d11_kind kind; uint32_t offset, value; };
#define SB {AXD_D11_STORE_BARRIER, 0, 0}
#define W(o,v) {AXD_D11_WRITE32, (o), (v)}
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const struct event override_mirror[] = {
    SB, W(0x120, 0x04020002), SB, W(0x324, 2)
};
static const struct event reset_mirror[] = {
    SB, W(0x120, 0x04000400), SB, W(0x324, 0x3438dcf9)
};
static const struct event low_reset[] = {SB, W(0x120, 0x400)};
static const struct event zero_mask[] = {SB, W(0x120, 0x04020006)};
static const struct event same_mirror[] = {SB, W(0x120, 2)};
static const struct event excluded_mask[] = {SB, W(0x120, 0x80000003)};
struct trace {
    struct axd_mctrl *job;
    const struct event *events;
    size_t count, cursor, fail_at, cancel_at;
    uint32_t expected_cache, old_mirror, new_mirror;
    unsigned reset;
};
static struct axd_mctrl_context context_for(void)
{
    struct axd_mctrl_context c = {0};
    unsigned i;
    c.psm.core = 130;
    c.psm.minor = 1;
    c.psm.main_control = 0x40000;
    c.psm.extended_control = 0x76543210;
    c.psm.mirror_control = 0x40000;
    c.psm.main_depth = 0x12345678;
    c.psm.extended_depth = 0x87654321;
    c.psm.wake_reasons = 0x80000008;
    c.psm.mute_override = c.psm.mirror_enabled = 1;
    c.psm.fatal_reason = 0x31415926;
    c.psm.timeout_flags = 0x27182818;
    c.psm.band_present = 1;
    c.psm.wake_hint = 1;
    c.psm.mbss_enabled = 1;
    c.psm.wake_budget_us = 51;
    c.suspended_count = 0xd5;
    c.reset_halfword = 0xabcd;
    for (i = 0; i < 9; i++)
        c.suspended_bitmap[i] = (uint8_t)(0xa0u + i);
    return c;
}
static void cleared(const struct axd_mctrl *job)
{
    unsigned i;
    for (i = 0; i < 9; i++)
        assert(job->context.suspended_bitmap[i] == 0);
    assert(job->context.suspended_count == 0);
    assert(job->context.reset_halfword == 0);
    assert(job->context.psm.wake_reasons == 0);
    assert(job->context.psm.mute_override == 0);
    assert(job->context.psm.main_depth == 0x12345678);
    assert(job->context.psm.extended_depth == 0x87654321);
}
static int io(void *opaque, const struct axd_d11_operation *op, uint32_t *read)
{
    struct trace *t = opaque;
    size_t index = t->cursor++;
    const struct event *event;
    (void)read;
    assert(index < t->count);
    event = &t->events[index];
    assert(op->kind == event->kind && op->offset == event->offset && op->value == event->value);
    assert(t->job->context.psm.main_control == t->expected_cache);
    assert(t->job->context.psm.mirror_control == (index < 2 ? t->old_mirror : t->new_mirror));
    assert(t->job->context.psm.extended_control == 0x76543210);
    assert(t->job->context.psm.main_depth == 0x12345678);
    assert(t->job->context.psm.extended_depth == 0x87654321);
    if (t->reset)
        cleared(t->job);
    if (index == t->cancel_at)
        assert(axd_mctrl_cancel(t->job) == AXD_MCTRL_CANCELLED);
    return index == t->fail_at ? -1 : 0;
}
static struct trace trace_for(struct axd_mctrl *job,
                             const struct event *events, size_t count,
                             uint32_t cache, uint32_t old_mirror,
                             uint32_t new_mirror, unsigned reset)
{
    struct trace t = {0};
    t.job = job;
    t.events = events;
    t.count = count;
    t.fail_at = t.cancel_at = (size_t)-1;
    t.expected_cache = cache;
    t.old_mirror = old_mirror;
    t.new_mirror = new_mirror;
    t.reset = reset;
    return t;
}
static void terminal(struct axd_mctrl *job, struct trace *trace)
{
    struct axd_mctrl before = *job;
    size_t cursor = trace->cursor;
    enum axd_mctrl_phase phase = job->phase;
    assert(phase != AXD_MCTRL_RUNNING);
    assert(axd_mctrl_step(job, io, trace) == phase);
    assert(axd_mctrl_step(job, NULL, NULL) == phase);
    assert(axd_mctrl_cancel(job) == phase);
    assert(trace->cursor == cursor);
    assert(memcmp(&before, job, sizeof(before)) == 0);
}
static void finish(struct axd_mctrl *job, struct trace *trace)
{
    unsigned steps = 0;
    while (job->phase == AXD_MCTRL_RUNNING) {
        size_t cursor = trace->cursor;
        axd_mctrl_step(job, io, trace);
        assert(trace->cursor - cursor <= 1);
        assert(++steps < 12);
    }
}
static void unchanged_fields(const struct axd_mctrl_context *before,
                             const struct axd_mctrl_context *after, unsigned reset)
{
    struct axd_mctrl_context expected = *before;
    unsigned i;
    expected.psm.main_control = after->psm.main_control;
    expected.psm.mirror_control = after->psm.mirror_control;
    if (reset) {
        for (i = 0; i < 9; i++)
            expected.suspended_bitmap[i] = 0;
        expected.suspended_count = 0;
        expected.reset_halfword = 0;
        expected.psm.wake_reasons = 0;
        expected.psm.mute_override = 0;
    }
    assert(memcmp(&expected, after, sizeof(expected)) == 0);
}
static void basic(void)
{
    struct axd_mctrl job;
    struct axd_mctrl_context c = context_for();
    struct trace t;
    assert(axd_mctrl_begin(&job, 0x40000, 2, &c) == 0);
    t = trace_for(&job, override_mirror, COUNT(override_mirror), 2, 0x40000, 2, 0);
    finish(&job, &t);
    assert(job.phase == AXD_MCTRL_DONE && job.error == AXD_MCTRL_NO_ERROR);
    assert(t.cursor == 4 && job.attempted_ops == 4 && job.completed_ops == 4);
    unchanged_fields(&c, &job.context, 0);
    terminal(&job, &t);

    /* A repeated unchanged logical cache does not repair overrides/mirror. */
    c = job.context;
    c.psm.mirror_control = 0xdeadbeef;
    assert(axd_mctrl_begin(&job, 0x40000, 2, &c) == 0);
    assert(axd_mctrl_step(&job, NULL, NULL) == AXD_MCTRL_DONE);
    assert(memcmp(&c, &job.context, sizeof(c)) == 0);
    assert(job.attempted_ops == 0 && job.completed_ops == 0);
    t = trace_for(&job, NULL, 0, 2, 0, 0, 0);
    terminal(&job, &t);

    c = context_for();
    c.psm.main_control = 2;
    assert(axd_mctrl_begin(&job, 0, 4, &c) == 0);
    t = trace_for(&job, zero_mask, COUNT(zero_mask), 6, 0x40000, 0x40000, 0);
    finish(&job, &t);
    assert(job.phase == AXD_MCTRL_DONE && t.cursor == 2);

    c = context_for();
    c.psm.wake_reasons = 0;
    c.psm.mute_override = 0;
    c.psm.mirror_control = 2;
    assert(axd_mctrl_begin(&job, 0x40000, 2, &c) == 0);
    t = trace_for(&job, same_mirror, COUNT(same_mirror), 2, 2, 2, 0);
    finish(&job, &t);
    assert(job.phase == AXD_MCTRL_DONE && t.cursor == 2);

    c.psm.main_control = 3;
    assert(axd_mctrl_begin(&job, 0x10000000, 0x80000000, &c) == 0);
    t = trace_for(&job, excluded_mask, COUNT(excluded_mask), 0x80000003, 2, 2, 0);
    finish(&job, &t);
    assert(job.phase == AXD_MCTRL_DONE && t.cursor == 2);

    c = context_for();
    c.psm.wake_reasons = 0;
    c.psm.mute_override = c.psm.mirror_enabled = 0;
    assert(axd_mctrl_begin(&job, UINT32_MAX, 0x400, &c) == 0);
    t = trace_for(&job, low_reset, COUNT(low_reset), 0x400, 0x40000, 0x40000, 0);
    finish(&job, &t);
    assert(job.phase == AXD_MCTRL_DONE && t.cursor == 2);
    unchanged_fields(&c, &job.context, 0);
}
static void reset_and_alias(void)
{
    struct axd_mctrl job;
    struct axd_mctrl_context c = context_for();
    struct trace t;
    c.psm.main_control = 0x04000400; /* Reset still zeroes and writes it anew. */
    c.psm.mirror_control = UINT32_MAX;
    assert(axd_mctrl_reset_begin(&job, &c) == 0);
    assert(memcmp(&c, &job.context, sizeof(c)) == 0);
    t = trace_for(&job, reset_mirror, COUNT(reset_mirror), 0x04000400, UINT32_MAX, 0x3438dcf9, 1);
    finish(&job, &t);
    assert(job.phase == AXD_MCTRL_DONE && t.cursor == 4);
    unchanged_fields(&c, &job.context, 1);
    terminal(&job, &t);

    c = job.context;
    assert(axd_mctrl_begin(&job, UINT32_MAX, 0x04000400, &job.context) == 0);
    assert(memcmp(&c, &job.context, sizeof(c)) == 0);
    assert(axd_mctrl_step(&job, NULL, NULL) == AXD_MCTRL_DONE);

    c = context_for();
    c.psm.mirror_enabled = 0;
    assert(axd_mctrl_begin(&job, 0, 0, &c) == 0);
    assert(axd_mctrl_reset_begin(&job, &job.context) == 0);
    assert(memcmp(&c, &job.context, sizeof(c)) == 0);
    t = trace_for(&job, reset_mirror, 2, 0x04000400, 0x40000, 0x40000, 1);
    finish(&job, &t);
    assert(job.phase == AXD_MCTRL_DONE && t.cursor == 2);
    cleared(&job);
}
static void boundaries(void)
{
    unsigned reset, mode;
    size_t cut;
    for (reset = 0; reset < 2; reset++) {
        for (mode = 0; mode < 3; mode++) {
            for (cut = 0; cut < 4; cut++) {
                struct axd_mctrl job;
                struct axd_mctrl_context c = context_for();
                struct trace t;
                uint32_t old_mirror = reset ? UINT32_MAX : 0x40000;
                uint32_t new_mirror = reset ? 0x3438dcf9 : 2;
                c.psm.mirror_control = old_mirror;
                if (reset)
                    assert(axd_mctrl_reset_begin(&job, &c) == 0);
                else
                    assert(axd_mctrl_begin(&job, 0x40000, 2, &c) == 0);
                t = trace_for(&job, reset ? reset_mirror : override_mirror, 4,
                              reset ? 0x04000400 : 2, old_mirror, new_mirror, reset);
                if (mode != 1)
                    t.fail_at = cut;
                if (mode != 0)
                    t.cancel_at = cut;
                finish(&job, &t);
                assert(t.cursor == cut + 1 && job.attempted_ops == cut + 1);
                assert(job.completed_ops == cut + (mode == 1));
                assert(job.context.psm.mirror_control == (cut < 2 ? old_mirror : new_mirror));
                assert(job.phase == (mode == 1 ? AXD_MCTRL_CANCELLED : AXD_MCTRL_FAILED));
                assert(job.error == (mode == 1 ? AXD_MCTRL_NO_ERROR : AXD_MCTRL_IO_ERROR));
                unchanged_fields(&c, &job.context, reset);
                terminal(&job, &t);
            }
        }
    }
}
static void arguments_and_pre_cancel(void)
{
    struct axd_mctrl job;
    struct axd_mctrl_context c = context_for();
    struct trace t;
    assert(axd_mctrl_begin(NULL, 0, 0, &c) == -1);
    assert(axd_mctrl_reset_begin(NULL, &c) == -1);
    assert(axd_mctrl_step(NULL, NULL, NULL) == AXD_MCTRL_INVALID);
    assert(axd_mctrl_cancel(NULL) == AXD_MCTRL_INVALID);
    assert(axd_mctrl_begin(&job, 0, 0, NULL) == -1);
    assert(job.error == AXD_MCTRL_BAD_ARGUMENT);
    c.psm.core = 129;
    assert(axd_mctrl_reset_begin(&job, &c) == -1);
    assert(job.error == AXD_MCTRL_UNSUPPORTED);
    c = context_for();
    c.psm.minor = 2;
    assert(axd_mctrl_begin(&job, 0, 0, &c) == -1);
    assert(job.error == AXD_MCTRL_UNSUPPORTED);
    c = context_for();
    c.psm.mirror_enabled = 2;
    assert(axd_mctrl_reset_begin(&job, &c) == -1);
    assert(job.error == AXD_MCTRL_BAD_ARGUMENT);
    c = context_for();
    c.psm.mute_override = 2;
    assert(axd_mctrl_begin(&job, 0, 0, &c) == -1);
    assert(job.error == AXD_MCTRL_BAD_ARGUMENT);

    c = context_for();
    assert(axd_mctrl_reset_begin(&job, &c) == 0);
    assert(axd_mctrl_cancel(&job) == AXD_MCTRL_CANCELLED);
    assert(memcmp(&c, &job.context, sizeof(c)) == 0);
    t = trace_for(&job, NULL, 0, 0, 0, 0, 0);
    terminal(&job, &t);

    assert(axd_mctrl_reset_begin(&job, &c) == 0);
    assert(axd_mctrl_step(&job, NULL, NULL) == AXD_MCTRL_FAILED);
    assert(job.error == AXD_MCTRL_BAD_ARGUMENT && job.attempted_ops == 0);
    assert(job.context.psm.main_control == 0x04000400);
    assert(job.context.psm.mirror_control == 0x40000);
    cleared(&job);
    terminal(&job, &t);
}
int main(void)
{
    basic();
    reset_and_alias();
    boundaries();
    arguments_and_pre_cancel();
    puts("main-control/reset synthetic traces: ok");
    return 0;
}
