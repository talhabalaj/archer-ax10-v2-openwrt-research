/* SPDX-License-Identifier: MIT */
#include "../driver/phy_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Literal reason2 wrapper traces from retained nesting/override assembly. */
struct event { enum axd_d11_kind kind; uint32_t offset, value, reply; };
#define SB {AXD_D11_STORE_BARRIER, 0, 0, 0}
#define FB {AXD_D11_FULL_BARRIER, 0, 0, 0}
#define W(o,v) {AXD_D11_WRITE32, (o), (v), 0}
#define R(o,v) {AXD_D11_READ32, (o), 0, (v)}
#define H(o,v) {AXD_D11_READ16, (o), 0, (v)}
#define D(v) {AXD_D11_DELAY_US, 0, (v), 0}
#define SHM(v) SB, W(0x160, 0x10010), R(0x160, 0xdeadbeefu), FB, H(0x164, (v)), FB
#define N(a) (sizeof(a) / sizeof((a)[0]))
static const struct event forced_enter[] = {SB, W(0x120, 0x04000003), D(40), SHM(2), SHM(2)};
static const struct event no_band_enter[] = {SB, W(0x120, 0x04000003)};
static const struct event policy_read[] = {R(0x120, 0x04000003), FB};
static const struct event clear_exit[] = {R(0x120, 0x04000003), FB, SB, W(0x120, 3)};
struct trace {
    const struct event *events;
    size_t count, cursor, fail_at, cancel_at;
    struct axd_phy_guard *cancel_job;
};
static struct axd_phy_guard_context context_for(unsigned depth)
{
    struct axd_phy_guard_context c;
    memset(&c, 0, sizeof(c)); c.depth = (uint8_t)depth;
    c.mac.core = 130; c.mac.minor = 1; c.mac.chip_id = 63178;
    c.mac.main_control = 3; c.mac.wake_reasons = 8; c.mac.band_present = 1;
    return c;
}
static struct trace trace_for(const struct event *events, size_t count)
{
    struct trace t = {events, count, 0, (size_t)-1, (size_t)-1, NULL}; return t;
}
static int io(void *opaque, const struct axd_d11_operation *op, uint32_t *reply)
{
    struct trace *t = opaque;
    size_t i = t->cursor++;
    const struct event *e;
    assert(i < t->count); e = &t->events[i];
    assert(op->kind == e->kind && op->offset == e->offset && op->value == e->value);
    if (t->cancel_job != NULL && i == t->cancel_at)
        assert(axd_phy_guard_cancel(t->cancel_job) == AXD_GUARD_CANCELLED);
    if (i == t->fail_at) return -1;
    *reply = e->reply; return 0;
}
static void terminal(struct axd_phy_guard *job, struct trace *t, enum axd_phy_guard_phase phase)
{
    struct axd_phy_guard before = *job;
    size_t cursor = t->cursor;
    assert(axd_phy_guard_step(job, io, t) == phase);
    assert(axd_phy_guard_step(job, NULL, NULL) == phase);
    assert(axd_phy_guard_cancel(job) == phase);
    assert(t->cursor == cursor && memcmp(&before, job, sizeof(before)) == 0);
}
static void run(struct axd_phy_guard *job, struct trace *t, unsigned expected_depth)
{
    size_t i;
    for (i = 0; i < t->count * 5 + 64; i++) {
        size_t before = t->cursor;
        enum axd_phy_guard_phase phase = axd_phy_guard_step(job, io, t);
        assert(t->cursor - before <= 1 && job->context.depth == expected_depth);
        if (phase != AXD_GUARD_RUNNING) return;
    }
    assert(0 && "guard exceeded independent trace bound");
}
static struct axd_phy_guard execute(enum axd_phy_guard_command command,
    const struct axd_phy_guard_context *c, const struct event *events, size_t count,
    enum axd_phy_guard_phase phase, unsigned pending)
{
    struct axd_phy_guard job;
    struct trace t = trace_for(events, count);
    assert(axd_phy_guard_begin(&job, command, c) == 0);
    run(&job, &t, command == AXD_PHY_ENTER ? c->depth + 1u : c->depth - 1u);
    assert(job.phase == phase && job.error == AXD_GUARD_NO_ERROR && job.pending_callouts == pending);
    assert(t.cursor == count && job.attempted_ops == count && job.completed_ops == count);
    terminal(&job, &t, phase); return job;
}
static void sweep(enum axd_phy_guard_command command, const struct axd_phy_guard_context *c,
                    const struct event *events, size_t count)
{
    size_t i; unsigned mode;
    for (i = 0; i < count; i++) for (mode = 0; mode < 3; mode++) {
        struct axd_phy_guard job;
        struct trace t = trace_for(events, count);
        if (mode != 1) t.fail_at = i;
        if (mode != 0) { t.cancel_at = i; t.cancel_job = &job; }
        assert(axd_phy_guard_begin(&job, command, c) == 0);
        run(&job, &t, command == AXD_PHY_ENTER ? c->depth + 1u : c->depth - 1u);
        assert(job.phase == (mode == 1 ? AXD_GUARD_CANCELLED : AXD_GUARD_FAILED));
        assert(job.error == (mode == 1 ? AXD_GUARD_NO_ERROR : AXD_GUARD_IO_ERROR));
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1 && job.completed_ops == i + (mode == 1));
        if (command == AXD_PHY_ENTER) {
            assert(job.context.mac.wake_reasons == 2 && job.context.mac.main_control == 0x04000003);
        } else {
            assert(job.context.mac.wake_reasons == 0);
            assert(job.context.mac.main_control == (i < 2 ? 0x04000003u : 3u));
        }
        terminal(&job, &t, job.phase);
    }
}
static void normal_paths(void)
{
    struct axd_phy_guard_context c = context_for(0);
    struct axd_phy_guard job = execute(AXD_PHY_ENTER, &c, NULL, 0, AXD_GUARD_TRACE_COMPLETE, 0);
    assert(job.context.mac.wake_reasons == 10);
    c.depth = 1; c.mac.wake_reasons = 10;
    job = execute(AXD_PHY_EXIT, &c, NULL, 0, AXD_GUARD_TRACE_COMPLETE, 0);
    assert(job.context.mac.wake_reasons == 8);
    c = context_for(1);
    job = execute(AXD_PHY_ENTER, &c, NULL, 0, AXD_GUARD_TRACE_COMPLETE, 0);
    assert(job.context.mac.wake_reasons == 8 && job.context.depth == 2);
    c.depth = 2;
    job = execute(AXD_PHY_EXIT, &c, NULL, 0, AXD_GUARD_TRACE_COMPLETE, 0);
    assert(job.context.mac.wake_reasons == 8 && job.context.depth == 1);
    c = context_for(0); c.mac.wake_reasons = 0;
    job = execute(AXD_PHY_ENTER, &c, forced_enter, N(forced_enter), AXD_GUARD_TRACE_COMPLETE, 0);
    assert(job.context.mac.wake_reasons == 2 && job.context.mac.wake_hint == 1);
    sweep(AXD_PHY_ENTER, &c, forced_enter, N(forced_enter));
    c.mac.band_present = 0;
    execute(AXD_PHY_ENTER, &c, no_band_enter, N(no_band_enter), AXD_GUARD_TRACE_COMPLETE, 0);
    sweep(AXD_PHY_ENTER, &c, no_band_enter, N(no_band_enter));
    c = context_for(1); c.mac.wake_reasons = 2; c.mac.main_control = 0x04000003;
    job = execute(AXD_PHY_EXIT, &c, NULL, 0, AXD_GUARD_DEPENDENCY, AXD_PSM_WAKE_CONTROL);
    assert(job.context.mac.wake_reasons == 0 && job.context.depth == 0);
    c.policy.public_up_known = 1;
    execute(AXD_PHY_EXIT, &c, NULL, 0, AXD_GUARD_TRACE_COMPLETE, 0); /* Known down. */
    c.policy.public_up = 1;
    execute(AXD_PHY_EXIT, &c, policy_read, N(policy_read), AXD_GUARD_DEPENDENCY, AXD_PSM_WAKE_CONTROL);
    sweep(AXD_PHY_EXIT, &c, policy_read, N(policy_read));
    c.policy.stay_awake_known = 1;
    job = execute(AXD_PHY_EXIT, &c, clear_exit, N(clear_exit), AXD_GUARD_TRACE_COMPLETE, 0);
    assert(job.context.mac.main_control == 3 && job.psm_job.wake_control_observed == 1);
    sweep(AXD_PHY_EXIT, &c, clear_exit, N(clear_exit));
}
static void semantic_wake_failure(void)
{
    struct axd_phy_guard_context c = context_for(0);
    struct event failed[N(forced_enter)];
    struct axd_phy_guard job;
    struct trace t;
    c.mac.wake_reasons = 0;
    memcpy(failed, forced_enter, sizeof(failed)); failed[N(failed) - 2].reply = 3;
    t = trace_for(failed, N(failed));
    assert(axd_phy_guard_begin(&job, AXD_PHY_ENTER, &c) == 0); run(&job, &t, 1);
    assert(job.phase == AXD_GUARD_FAILED && job.error == AXD_GUARD_PSM_ERROR);
    assert(job.psm_job.error == AXD_PSM_WAKE_STATE && job.pending_callouts == AXD_PSM_FATAL);
    assert(job.context.depth == 1 && job.context.mac.fatal_reason == 3 && job.context.mac.wake_reasons == 2);
    assert(job.completed_ops == N(failed)); terminal(&job, &t, AXD_GUARD_FAILED);
}

static void alias_and_badargs(void)
{
    struct axd_phy_guard_context c = context_for(0);
    struct axd_phy_guard job, empty = {0};
    struct trace t;
    unsigned i;
    c.mac.wake_reasons = 0; c.policy.public_up_known = 1; c.policy.public_up = 1; c.policy.stay_awake_known = 1;
    job = execute(AXD_PHY_ENTER, &c, forced_enter, N(forced_enter), AXD_GUARD_TRACE_COMPLETE, 0);
    assert(axd_phy_guard_begin(&job, AXD_PHY_EXIT, &job.context) == 0);
    assert(job.context.depth == 1 && job.context.mac.wake_reasons == 2 && job.context.policy.public_up);
    t = trace_for(clear_exit, N(clear_exit)); run(&job, &t, 0);
    assert(job.phase == AXD_GUARD_TRACE_COMPLETE && job.context.mac.wake_reasons == 0);
    terminal(&job, &t, AXD_GUARD_TRACE_COMPLETE);
    c = context_for(0); t = trace_for(NULL, 0);
    assert(axd_phy_guard_begin(NULL, AXD_PHY_ENTER, &c) != 0);
    assert(axd_phy_guard_begin(&job, AXD_PHY_ENTER, NULL) != 0);
    assert(axd_phy_guard_begin(&job, (enum axd_phy_guard_command)0, &c) != 0);
    assert(axd_phy_guard_begin(&job, (enum axd_phy_guard_command)3, &c) != 0);
    c.mac.core = 129;
    assert(axd_phy_guard_begin(&job, AXD_PHY_ENTER, &c) != 0);
    c = context_for(0); c.policy.public_up_known = 2;
    assert(axd_phy_guard_begin(&job, AXD_PHY_ENTER, &c) != 0);
    for (i = 0; i < 2; i++) {
        int result;
        c = context_for(i == 0 ? 255 : 0);
        result = axd_phy_guard_begin(&job, i == 0 ? AXD_PHY_ENTER : AXD_PHY_EXIT, &c);
        if (result == 0) {
            for (unsigned step = 0; step < 8 && job.phase == AXD_GUARD_RUNNING; step++) axd_phy_guard_step(&job, io, &t);
            assert(job.context.depth == c.depth);
        }
        assert(job.phase == AXD_GUARD_FAILED && job.error == AXD_GUARD_BAD_NESTING && job.attempted_ops == 0);
        terminal(&job, &t, AXD_GUARD_FAILED);
    }
    c = context_for(0); c.mac.wake_reasons = 0;
    assert(axd_phy_guard_begin(&job, AXD_PHY_ENTER, &c) == 0);
    for (i = 0; i < 16 && job.phase == AXD_GUARD_RUNNING; i++) axd_phy_guard_step(&job, NULL, NULL);
    assert(job.phase == AXD_GUARD_FAILED && job.error == AXD_GUARD_BAD_ARGUMENT && job.attempted_ops == 0);
    assert(job.context.depth == 1 && job.context.mac.wake_reasons == 2);
    terminal(&job, &t, AXD_GUARD_FAILED);
    assert(axd_phy_guard_begin(&job, AXD_PHY_ENTER, &c) == 0);
    assert(axd_phy_guard_cancel(&job) == AXD_GUARD_CANCELLED && job.context.depth == 0);
    terminal(&job, &t, AXD_GUARD_CANCELLED);
    assert(axd_phy_guard_step(NULL, io, &t) == AXD_GUARD_INVALID);
    assert(axd_phy_guard_cancel(NULL) == AXD_GUARD_INVALID);
    terminal(&empty, &t, AXD_GUARD_INVALID);
}
int main(void)
{
    normal_paths(); semantic_wake_failure(); alias_and_badargs();
    puts("PHY guard: independent byte-nesting/reason2/policy traces and callback exits passed");
    return 0;
}
