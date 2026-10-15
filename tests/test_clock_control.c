/* SPDX-License-Identifier: MIT */
#include "../driver/clock_control.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Literal synthetic traces from the retained WL resource ledger/assembly.
 * C=current D11; U=owned unit1 transport. RMW is ST,R,SY,W: no extra ST.
 */
struct event {
    enum axd_d11_kind kind;
    enum axd_clock_region region;
    uint32_t offset, value, reply;
};
#define C AXD_CLOCK_CURRENT_D11
#define U AXD_CLOCK_D11_UNIT1
#define SB(r) {AXD_D11_STORE_BARRIER, (r), 0, 0, 0}
#define FB(r) {AXD_D11_FULL_BARRIER, (r), 0, 0, 0}
#define R(r,v) {AXD_D11_READ32, (r), 0x1e0, 0, (v)}
#define W(r,v) {AXD_D11_WRITE32, (r), 0x1e0, (v), 0}
#define D(v) {AXD_D11_DELAY_US, C, 0, (v), 0}
#define N(a) (sizeof(a) / sizeof((a)[0]))
static const struct event fast[] = {SB(C), R(C, 0x10), FB(C), W(C, 0x12), D(64), R(C, 0x20000), FB(C)};
static const struct event dynamic_direct[] = {SB(C), R(C, 0xabcdefffu), FB(C), W(C, 0xabcdeffdu)};
static const struct event dynamic_mask_clear[] = {
    R(C, 0x20000), FB(C), SB(C), R(C, 0xabcdefffu), FB(C), W(C, 0xabcdeffdu)
};
static const struct event dynamic_ready[] = {
    R(C, 0x10), FB(C), R(C, 0x20000), FB(C),
    SB(C), R(C, 0x80000003u), FB(C), W(C, 0x80000001u)
};
static const struct event pll[] = {
    R(C, 0), FB(C), SB(C), R(C, 0x20), FB(C), W(C, 0x320),
    R(C, 0x03000000), FB(C), R(C, 0), FB(C) /* Final loss is not checked. */
};
static const struct event pll_a_ready[] = {
    R(C, 0x03000000), FB(C), SB(C), R(C, 0x20), FB(C), W(C, 0x320),
    D(16), D(16), D(16), D(16), R(C, 0x03000000), FB(C), R(C, 0), FB(C)
};
static const struct event pll_b_ready[] = {
    R(C, 0), FB(C), SB(C), R(C, 0x03000020), FB(C), W(C, 0x03000320),
    R(C, 0x03000000), FB(C), R(C, 0), FB(C) /* B never causes four16us delays. */
};
static const struct event pll_partial[] = {
    R(C, 0x100), FB(C), SB(C), R(C, 0x200), FB(C), W(C, 0x300),
    R(C, 0x01000000), FB(C), D(10), R(C, 0x02000000), FB(C),
    D(10), R(C, 0x03000000), FB(C), R(C, 0x1234), FB(C)
};
static const struct event pll_rsdb[] = {
    SB(C), R(C, 0x25), FB(C), W(C, 5),
    R(C, 0), FB(C), SB(C), R(C, 0x42), FB(C), W(C, 0x342),
    R(C, 0x03000000), FB(C), R(C, 0x55), FB(C),
    SB(U), R(U, 0xabcdef3fu), FB(U), W(U, 0xabcdef1fu)
};
static const struct event disable[] = {
    SB(C), R(C, 0x33ff), FB(C), W(C, 0x30ff), R(C, 0x77), FB(C)
};
static const struct event disable_rsdb[] = {
    SB(C), R(C, 0x33ff), FB(C), W(C, 0x30ff), R(C, 0x77), FB(C),
    SB(U), R(U, 0x9000), FB(U), W(U, 0x9020)
};
static const struct event secondary_clear[] = {SB(U), R(U, 0xabcdef3fu), FB(U), W(U, 0xabcdef1fu)};
static const struct event secondary_set[] = {SB(U), R(U, 0x9000), FB(U), W(U, 0x9020)};
struct trace {
    const struct event *events;
    size_t count, cursor, fail_at, cancel_at, reads[2], delays;
    uint64_t delay_us;
    uint32_t last_status, last_read;
    unsigned observed, writes_after;
    struct axd_clock_control *cancel_job;
};
static struct trace trace_for(const struct event *events, size_t count)
{
    struct trace t;
    memset(&t, 0, sizeof(t)); t.events = events; t.count = count;
    t.fail_at = t.cancel_at = (size_t)-1;
    return t;
}
static int io(void *opaque, const struct axd_clock_operation *op, uint32_t *reply)
{
    struct trace *t = opaque;
    size_t current = t->cursor++;
    const struct event *e;
    assert(current < t->count); e = &t->events[current];
    assert(op->kind == e->kind && op->region == e->region);
    assert(op->offset == e->offset && op->value == e->value);
    if (op->kind == AXD_D11_READ32) t->reads[op->region == U]++;
    if (op->kind == AXD_D11_DELAY_US) { t->delays++; t->delay_us += op->value; }
    if (t->cancel_job != NULL && current == t->cancel_at)
        assert(axd_clock_control_cancel(t->cancel_job) == AXD_CLOCK_CANCELLED);
    if (current == t->fail_at) return -1;
    *reply = e->reply;
    return 0;
}
static struct axd_clock_context context_for(void)
{
    const struct axd_clock_context c = {130, 1, 0x10000000, 0, 0x80, 1, 0, 0, 1, 0, 0};
    return c;
}
static void terminal(struct axd_clock_control *job, struct trace *t, enum axd_clock_phase phase)
{
    struct axd_clock_control before = *job;
    size_t cursor = t->cursor;
    assert(axd_clock_control_step(job, io, t) == phase);
    assert(axd_clock_control_step(job, NULL, NULL) == phase);
    assert(axd_clock_control_cancel(job) == phase);
    assert(t->cursor == cursor && memcmp(&before, job, sizeof(before)) == 0);
}
static void run(struct axd_clock_control *job, struct trace *t)
{
    size_t i;
    for (i = 0; i < t->count * 2 + 64; i++) {
        size_t before = t->cursor;
        enum axd_clock_phase phase = axd_clock_control_step(job, io, t);
        assert(t->cursor - before <= 1);
        if (t->cursor != before && before != t->fail_at) {
            const struct event *e = &t->events[before];
            if (e->kind == AXD_D11_READ32) {
                t->last_read = e->reply;
                if (e->region == C) {
                    t->last_status = e->reply; t->observed = 1; t->writes_after = 0;
                }
            } else if (e->kind == AXD_D11_WRITE32 && e->region == C) {
                t->writes_after = 1;
            }
        }
        /* Independent per-success telemetry oracle, including callback cancel. */
        assert(job->last_status == t->last_status && job->last_read == t->last_read);
        assert(job->status_observed == t->observed && job->writes_since_status == t->writes_after);
        if (phase != AXD_CLOCK_RUNNING) return;
    }
    assert(0 && "clock helper exceeded independent trace bound");
}
static struct axd_clock_control execute(enum axd_clock_command cmd, uint32_t argument,
    const struct axd_clock_context *c, struct trace *t, enum axd_clock_phase phase,
    enum axd_clock_dependency dependency)
{
    struct axd_clock_control job;
    assert(axd_clock_control_begin(&job, cmd, argument, c) == 0);
    run(&job, t);
    assert(job.phase == phase && job.error == AXD_CLOCK_NO_ERROR && job.dependency == dependency);
    assert(t->cursor == t->count && job.attempted_ops == t->count && job.completed_ops == t->count);
    terminal(&job, t, phase);
    return job;
}
static void sweep(enum axd_clock_command cmd, uint32_t argument,
                   const struct axd_clock_context *c, const struct event *events, size_t count)
{
    size_t i; unsigned mode;
    for (i = 0; i < count; i++) for (mode = 0; mode < 3; mode++) {
        struct axd_clock_control job;
        struct trace t = trace_for(events, count);
        if (mode != 1) t.fail_at = i;
        if (mode != 0) { t.cancel_at = i; t.cancel_job = &job; }
        assert(axd_clock_control_begin(&job, cmd, argument, c) == 0);
        run(&job, &t);
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1);
        assert(job.completed_ops == i + (mode == 1));
        assert(job.phase == (mode == 1 ? AXD_CLOCK_CANCELLED : AXD_CLOCK_FAILED));
        assert(job.error == (mode == 1 ? AXD_CLOCK_NO_ERROR : AXD_CLOCK_IO_ERROR));
        terminal(&job, &t, job.phase);
    }
}
static void literal_paths(void)
{
    struct axd_clock_context c = context_for();
    struct axd_clock_control job;
    struct trace t = trace_for(fast, N(fast));
    job = execute(AXD_CLOCK_SELECT, 0, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.context.fast_clock == 1 && job.context.wake_reasons == 0x80 && job.poll_exhausted == 0);
    assert(t.delay_us == 64 && t.reads[0] == 2);
    sweep(AXD_CLOCK_SELECT, 0, &c, fast, N(fast));
    c.si_word20 = 1; c.fast_clock = 1;
    t = trace_for(dynamic_direct, N(dynamic_direct));
    job = execute(AXD_CLOCK_SELECT, UINT32_MAX, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.context.fast_clock == 0 && job.writes_since_status == 1 && t.reads[0] == 1);
    sweep(AXD_CLOCK_SELECT, 7, &c, dynamic_direct, N(dynamic_direct));
    c.si_word20 = 0;
    t = trace_for(dynamic_mask_clear, N(dynamic_mask_clear));
    execute(AXD_CLOCK_SELECT, 2, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    sweep(AXD_CLOCK_SELECT, 2, &c, dynamic_mask_clear, N(dynamic_mask_clear));
    t = trace_for(dynamic_ready, N(dynamic_ready));
    job = execute(AXD_CLOCK_SELECT, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.last_status == 0x80000003u && job.writes_since_status == 1 && t.reads[0] == 3);
    sweep(AXD_CLOCK_SELECT, 1, &c, dynamic_ready, N(dynamic_ready));
    c = context_for();
    t = trace_for(pll, N(pll));
    job = execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.sample_a == 0 && job.last_status == 0 && job.poll_exhausted == 0 && job.request_skipped == 0);
    sweep(AXD_CLOCK_PLL, 1, &c, pll, N(pll));
    t = trace_for(pll_a_ready, N(pll_a_ready));
    job = execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.sample_a == 0x03000000 && job.delay_count == 4 && t.delays == 4 && t.delay_us == 64);
    sweep(AXD_CLOCK_PLL, 1, &c, pll_a_ready, N(pll_a_ready));
    t = trace_for(pll_b_ready, N(pll_b_ready));
    job = execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.sample_a == 0 && job.delay_count == 0 && t.delays == 0);
    sweep(AXD_CLOCK_PLL, 1, &c, pll_b_ready, N(pll_b_ready));
    {
        struct event partial[N(pll_partial)];
        unsigned a;
        for (a = 0x100; a <= 0x200; a += 0x100) {
            memcpy(partial, pll_partial, sizeof(partial)); partial[0].reply = a;
            t = trace_for(partial, N(partial));
            job = execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
            assert(job.sample_a == a && job.request_skipped == 0 && job.repetitions == 2);
            assert(job.poll_exhausted == 0 && t.delay_us == 20 && job.last_status == 0x1234);
            sweep(AXD_CLOCK_PLL, 1, &c, partial, N(partial));
        }
    }
    c.rsdb = c.secondary_owned = 1;
    t = trace_for(pll_rsdb, N(pll_rsdb));
    job = execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.last_status == 0x55 && job.last_read == 0xabcdef3fu && job.writes_since_status == 0);
    assert(t.reads[1] == 1);
    sweep(AXD_CLOCK_PLL, 1, &c, pll_rsdb, N(pll_rsdb));
    t = trace_for(disable_rsdb, N(disable_rsdb));
    job = execute(AXD_CLOCK_PLL, 0, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.last_status == 0x77 && job.last_read == 0x9000 && job.writes_since_status == 0);
    sweep(AXD_CLOCK_PLL, 0, &c, disable_rsdb, N(disable_rsdb));
    c.rsdb = 0;
    t = trace_for(disable, N(disable));
    execute(AXD_CLOCK_PLL, 0, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    sweep(AXD_CLOCK_PLL, 0, &c, disable, N(disable));
}

struct script { struct event *events; size_t count; };
static struct script new_script(void)
{
    struct script s = {NULL, 0}; s.events = malloc(40000 * sizeof(*s.events));
    assert(s.events != NULL); return s;
}
static void append(struct script *s, const struct event *events, size_t count)
{
    assert(s->count + count <= 40000);
    memcpy(s->events + s->count, events, count * sizeof(*events)); s->count += count;
}
static void pair(struct script *s, uint32_t reply)
{
    struct event events[] = {R(C, 0), FB(C)}; events[0].reply = reply; append(s, events, N(events));
}
static void repeat_poll(struct script *s, unsigned repetitions, unsigned last_ready, uint32_t bits)
{
    unsigned i;
    for (i = 0; i < repetitions; i++) {
        const struct event delay[] = {D(10)};
        append(s, delay, N(delay)); pair(s, last_ready && i + 1 == repetitions ? bits : 0);
    }
}
static void select_poll(unsigned dynamic, unsigned repetitions, unsigned ready)
{
    static const struct event prefix[] = {SB(C), R(C, 0x10), FB(C), W(C, 0x12), D(64)};
    static const struct event dynamic_prefix[] = {R(C, 0x20002), FB(C)};
    struct script s = new_script();
    struct axd_clock_context c = context_for();
    struct axd_clock_control job;
    struct trace t;
    if (dynamic) append(&s, dynamic_prefix, N(dynamic_prefix)); else append(&s, prefix, N(prefix));
    pair(&s, 0); /* Distinct second dynamic sample drives waiting despite A ready. */
    repeat_poll(&s, repetitions, ready, 0x20000);
    if (dynamic) append(&s, dynamic_direct, N(dynamic_direct));
    t = trace_for(s.events, s.count);
    job = execute(AXD_CLOCK_SELECT, dynamic ? 9u : 0u, &c, &t,
                  AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.repetitions == repetitions && job.poll_exhausted == !ready);
    assert(t.delays == repetitions + !dynamic);
    assert(t.delay_us == (uint64_t)repetitions * 10u + (dynamic ? 0u : 64u));
    assert(t.reads[0] == repetitions + (dynamic ? 3u : 2u)); /* No final confirmation. */
    assert(job.writes_since_status == dynamic && job.context.fast_clock == !dynamic);
    if (repetitions == 1) sweep(AXD_CLOCK_SELECT, dynamic ? 9u : 0u, &c, s.events, s.count);
    free(s.events);
}
static void pll_poll(unsigned repetitions, unsigned ready, uint32_t final_value)
{
    struct script s = new_script();
    struct axd_clock_context c = context_for();
    struct axd_clock_control job;
    struct trace t;
    append(&s, pll, 6); pair(&s, 0);
    repeat_poll(&s, repetitions, ready, 0x03000000);
    pair(&s, final_value); /* Distinct final sample is retained, never rechecked. */
    t = trace_for(s.events, s.count);
    job = execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.repetitions == repetitions && job.poll_exhausted == !ready);
    assert(job.last_status == final_value && job.writes_since_status == 0);
    assert(t.delays == repetitions && t.reads[0] == repetitions + 4u);
    if (repetitions == 1) sweep(AXD_CLOCK_PLL, 1, &c, s.events, s.count);
    free(s.events);
}

static void secondary_and_skips(void)
{
    struct axd_clock_context c = context_for();
    struct axd_clock_control job;
    struct trace t;
    const struct event skip[] = {R(C, 0x300), FB(C)};
    const struct event skip_rsdb[] = {
        SB(C), R(C, 0x20), FB(C), W(C, 0), R(C, 0x300), FB(C),
        SB(U), R(U, 0xabcdef3fu), FB(U), W(U, 0xabcdef1fu)
    };
    unsigned i;
    t = trace_for(skip, N(skip));
    job = execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.request_skipped == 1 && job.sample_a == 0x300 && t.delays == 0 && t.reads[0] == 1);
    assert(job.last_status == 0x300 && job.writes_since_status == 0 && job.poll_exhausted == 0);
    sweep(AXD_CLOCK_PLL, 1, &c, skip, N(skip));
    c.rsdb = c.secondary_owned = 1;
    t = trace_for(skip_rsdb, N(skip_rsdb));
    job = execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.request_skipped == 1 && job.last_status == 0x300 && job.last_read == 0xabcdef3fu);
    sweep(AXD_CLOCK_PLL, 1, &c, skip_rsdb, N(skip_rsdb));
    t = trace_for(secondary_clear, N(secondary_clear));
    job = execute(AXD_CLOCK_SECONDARY, 0, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.status_observed == 0 && job.last_status == 0 && job.writes_since_status == 0);
    sweep(AXD_CLOCK_SECONDARY, 0, &c, secondary_clear, N(secondary_clear));
    t = trace_for(secondary_set, N(secondary_set));
    execute(AXD_CLOCK_SECONDARY, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    sweep(AXD_CLOCK_SECONDARY, 1, &c, secondary_set, N(secondary_set));
    c.core_clock_valid = 0; c.core_up = 0; c.pll_inhibit = 1;
    t = trace_for(secondary_clear, N(secondary_clear));
    execute(AXD_CLOCK_SECONDARY, 0, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    c.core_clock_valid = c.core_up = 1; c.pll_inhibit = 0;
    c.secondary_owned = 0; t = trace_for(NULL, 0);
    execute(AXD_CLOCK_SECONDARY, 0, &c, &t, AXD_CLOCK_DEPENDENCY, AXD_CLOCK_SECONDARY_OWNER);
    t = trace_for(pll_rsdb, N(pll_rsdb) - 4);
    job = execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_DEPENDENCY, AXD_CLOCK_SECONDARY_OWNER);
    assert(job.last_status == 0x55 && job.writes_since_status == 0);
    t = trace_for(disable, N(disable));
    job = execute(AXD_CLOCK_PLL, 0, &c, &t, AXD_CLOCK_DEPENDENCY, AXD_CLOCK_SECONDARY_OWNER);
    assert(job.last_status == 0x77 && job.writes_since_status == 0);
    c.rsdb = 0; t = trace_for(NULL, 0);
    execute(AXD_CLOCK_SECONDARY, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    for (i = 0; i < 3; i++) {
        c = context_for(); c.rsdb = 1;
        if (i == 0) c.core_clock_valid = 0;
        if (i == 1) c.pll_inhibit = 1;
        if (i == 2) c.core_up = 0;
        t = trace_for(NULL, 0);
        execute(AXD_CLOCK_PLL, 1, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
        t = trace_for(NULL, 0);
        execute(AXD_CLOCK_PLL, 0, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    }
    c = context_for(); c.si_capabilities = 0; c.core_clock_valid = 0;
    t = trace_for(NULL, 0);
    job = execute(AXD_CLOCK_SELECT, 0, &c, &t, AXD_CLOCK_DEPENDENCY, AXD_CLOCK_HND_FALLBACK);
    assert(job.context.fast_clock == 0 && job.context.wake_reasons == 0x80);
    c.si_capabilities = 0x10000000;
    t = trace_for(NULL, 0);
    job = execute(AXD_CLOCK_SELECT, 0, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.context.fast_clock == 1 && job.status_observed == 0);
    c.fast_clock = 1; t = trace_for(NULL, 0);
    job = execute(AXD_CLOCK_SELECT, 99, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(job.context.fast_clock == 0 && job.status_observed == 0);
}

static void alias_and_arguments(void)
{
    struct axd_clock_context c = context_for();
    struct axd_clock_control job, empty = {0};
    struct trace t = trace_for(fast, N(fast));
    unsigned i;
    c.si_word20 = 1;
    job = execute(AXD_CLOCK_SELECT, 0, &c, &t, AXD_CLOCK_TRACE_COMPLETE, AXD_CLOCK_NO_DEPENDENCY);
    assert(axd_clock_control_begin(&job, AXD_CLOCK_SELECT, 1, &job.context) == 0);
    assert(job.context.core == 130 && job.context.fast_clock == 1 && job.context.si_word20 == 1);
    assert(job.context.wake_reasons == 0x80 && job.attempted_ops == 0 && job.status_observed == 0);
    t = trace_for(dynamic_direct, N(dynamic_direct)); run(&job, &t);
    assert(job.phase == AXD_CLOCK_TRACE_COMPLETE && job.context.fast_clock == 0);
    assert(t.cursor == N(dynamic_direct) && job.completed_ops == N(dynamic_direct));
    terminal(&job, &t, AXD_CLOCK_TRACE_COMPLETE);
    assert(axd_clock_control_begin(NULL, AXD_CLOCK_PLL, 1, &c) != 0);
    assert(axd_clock_control_begin(&job, AXD_CLOCK_PLL, 1, NULL) != 0);
    t = trace_for(NULL, 0); terminal(&job, &t, AXD_CLOCK_FAILED);
    for (i = 0; i < 6; i++) {
        c = context_for();
        if (i == 0) c.core_clock_valid = 2;
        if (i == 1) c.fast_clock = 2;
        if (i == 2) c.pll_inhibit = 2;
        if (i == 3) c.core_up = 2;
        if (i == 4) c.rsdb = 2;
        if (i == 5) c.secondary_owned = 2;
        if (i == 1) {
            assert(axd_clock_control_begin(&job, AXD_CLOCK_PLL, 1, &c) == 0);
            assert(job.context.fast_clock == 2);
            continue;
        }
        assert(axd_clock_control_begin(&job, AXD_CLOCK_PLL, 1, &c) != 0);
        assert(job.error == AXD_CLOCK_BAD_ARGUMENT && job.attempted_ops == 0);
        terminal(&job, &t, AXD_CLOCK_FAILED);
    }
    c = context_for(); c.core = 129;
    assert(axd_clock_control_begin(&job, AXD_CLOCK_PLL, 1, &c) != 0 && job.error == AXD_CLOCK_UNSUPPORTED);
    c.core = 130; c.minor = 2;
    assert(axd_clock_control_begin(&job, AXD_CLOCK_PLL, 1, &c) != 0 && job.error == AXD_CLOCK_UNSUPPORTED);
    c.minor = 0;
    assert(axd_clock_control_begin(&job, (enum axd_clock_command)0, 0, &c) != 0);
    assert(axd_clock_control_begin(&job, (enum axd_clock_command)4, 0, &c) != 0);
    assert(axd_clock_control_begin(&job, AXD_CLOCK_PLL, 2, &c) != 0);
    assert(axd_clock_control_begin(&job, AXD_CLOCK_SECONDARY, 2, &c) != 0);
    assert(axd_clock_control_begin(&job, AXD_CLOCK_SELECT, 0, &c) == 0);
    c.si_capabilities = 0; assert(job.context.si_capabilities == 0x10000000);
    for (i = 0; i < 8 && job.phase == AXD_CLOCK_RUNNING; i++)
        axd_clock_control_step(&job, NULL, NULL);
    assert(job.phase == AXD_CLOCK_FAILED && job.error == AXD_CLOCK_BAD_ARGUMENT);
    assert(job.attempted_ops == 0 && job.completed_ops == 0);
    terminal(&job, &t, AXD_CLOCK_FAILED);
    c = context_for();
    assert(axd_clock_control_begin(&job, AXD_CLOCK_SELECT, 0, &c) == 0);
    assert(axd_clock_control_cancel(&job) == AXD_CLOCK_CANCELLED);
    terminal(&job, &t, AXD_CLOCK_CANCELLED);
    assert(axd_clock_control_step(NULL, io, &t) == AXD_CLOCK_INVALID);
    assert(axd_clock_control_cancel(NULL) == AXD_CLOCK_INVALID);
    terminal(&empty, &t, AXD_CLOCK_INVALID);
}
int main(void)
{
    literal_paths(); secondary_and_skips();
    select_poll(0, 1, 1); select_poll(1, 1, 1);
    select_poll(0, 1500, 1); select_poll(0, 1500, 0);
    select_poll(1, 1500, 1); select_poll(1, 1500, 0);
    pll_poll(1, 1, 0); pll_poll(10000, 1, 0);
    pll_poll(10000, 0, 0x03000000); pll_poll(10000, 0, 0);
    alias_and_arguments();
    puts("clock control: literal clock/PLL/secondary traces, counters, sample provenance and callback exits passed");
    return 0;
}
