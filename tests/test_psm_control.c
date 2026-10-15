/* SPDX-License-Identifier: MIT */
#include "../driver/psm_control.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original synthetic fixtures. The SHM macro is a literal six-event sequence,
 * not a call into the production decoder, state machine, or port generator.
 */
struct event { enum axd_d11_kind kind; uint32_t offset, value, response; };
#define SB {AXD_D11_STORE_BARRIER, 0, 0, 0}
#define FB {AXD_D11_FULL_BARRIER, 0, 0, 0}
#define W(o,v) {AXD_D11_WRITE32, (o), (v), 0}
#define R(o,v) {AXD_D11_READ32, (o), 0, (v)}
#define H(o,v) {AXD_D11_READ16, (o), 0, (v)}
#define D(v) {AXD_D11_DELAY_US, 0, (v), 0}
#define SHM(v) SB, W(0x160, 0x10010), R(0x160, 0xdeadbeefu), FB, H(0x164, (v)), FB
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const struct event wake_now[] = {D(40), SHM(2), SHM(2)};
static const struct event suspend_main[] = {
    R(0x120, 3), FB, R(0x120, 3), FB, R(0x128, 0), FB,
    SB, W(0x120, 0x04000002), R(0x128, 1), FB, R(0x128, 1), FB, R(0x120, 2), FB
};
static const struct event suspend_ext[] = {
    R(0x1b4, 3), FB, R(0x1b4, 3), FB, R(0x1c0, 0), FB,
    SB, W(0x1b4, 2), R(0x1c0, 1), FB, R(0x1c0, 1), FB, R(0x1b4, 2), FB
};
static const struct event enable_main[] = {
    R(0x120, 2), FB, SB, W(0x120, 0x04000003), SB, W(0x128, 1),
    R(0x120, 3), FB, R(0x128, 0), FB, SHM(2)
};
static const struct event enable_ext[] = {
    R(0x1b4, 2), FB, R(0x1b4, 2), FB, SB, W(0x1b4, 3),
    SB, W(0x1c0, 1), R(0x1b4, 3), FB, R(0x1c0, 0), FB
};
static const struct event forced_wake[] = {
    R(0x120, 3), FB,
    SB, W(0x120, 0x04000003), SB, W(0x324, 0x04000800),
    D(40), SHM(2), SHM(2),
    R(0x120, 0x04000003), FB, R(0x128, 0), FB,
    SB, W(0x120, 0x04000002), R(0x128, 1), FB, R(0x128, 1), FB,
    R(0x120, 0x04000002), FB
};
struct trace {
    const struct event *events;
    size_t count, cursor, fail_at, cancel_at, shm_reads, delays;
    uint64_t delay_us;
    struct axd_psm_control *cancel_job;
};
static struct trace trace_for(const struct event *events, size_t count)
{
    struct trace t;
    memset(&t, 0, sizeof(t));
    t.events = events; t.count = count;
    t.fail_at = t.cancel_at = (size_t)-1;
    return t;
}
static int io(void *context, const struct axd_d11_operation *op, uint32_t *value)
{
    struct trace *t = context;
    size_t current = t->cursor++;
    const struct event *e;
    assert(current < t->count);
    e = &t->events[current];
    assert(op->kind == e->kind && op->offset == e->offset && op->value == e->value);
    if (op->kind == AXD_D11_READ16 && op->offset == 0x164) t->shm_reads++;
    if (op->kind == AXD_D11_DELAY_US) { t->delays++; t->delay_us += op->value; }
    if (t->cancel_job != NULL && current == t->cancel_at)
        assert(axd_psm_control_cancel(t->cancel_job) == AXD_PSM_CANCELLED);
    if (current == t->fail_at) return -1;
    *value = e->response;
    return 0;
}
static struct axd_psm_context context_for(void)
{
    struct axd_psm_context c;
    memset(&c, 0, sizeof(c));
    c.core = 130; c.minor = 1; c.chip_id = 63178;
    c.main_control = c.extended_control = 3;
    c.wake_reasons = 8; c.band_present = 1;
    return c;
}
static void terminal(struct axd_psm_control *job, struct trace *t, enum axd_psm_phase phase)
{
    struct axd_psm_control before = *job;
    size_t cursor = t->cursor;
    assert(axd_psm_control_step(job, io, t) == phase);
    assert(axd_psm_control_step(job, NULL, NULL) == phase);
    assert(axd_psm_control_cancel(job) == phase);
    assert(t->cursor == cursor && memcmp(&before, job, sizeof(before)) == 0);
}
static void run(struct axd_psm_control *job, struct trace *t)
{
    size_t i;
    for (i = 0; i < t->count * 2 + 128; i++)
        if (axd_psm_control_step(job, io, t) != AXD_PSM_RUNNING) return;
    assert(0 && "control exceeded independent trace bound");
}
static struct axd_psm_control execute(enum axd_psm_command command,
    const struct axd_psm_context *c, struct trace *t, enum axd_psm_phase phase,
    enum axd_psm_error error, enum axd_psm_result result, unsigned callouts)
{
    struct axd_psm_control job;
    assert(axd_psm_control_begin(&job, command, c) == 0);
    run(&job, t);
    assert(job.phase == phase && job.error == error && job.result == result);
    assert(job.pending_callouts == callouts);
    assert(t->cursor == t->count && job.attempted_ops == t->count && job.completed_ops == t->count);
    terminal(&job, t, phase);
    return job;
}
static void fault_matrix(enum axd_psm_command command, const struct axd_psm_context *c,
                         const struct event *events, size_t count)
{
    size_t i;
    unsigned mode;
    for (i = 0; i < count; i++) for (mode = 0; mode < 3; mode++) {
        struct axd_psm_control job;
        struct trace t = trace_for(events, count);
        if (mode != 1) t.fail_at = i;
        if (mode != 0) { t.cancel_at = i; t.cancel_job = &job; }
        assert(axd_psm_control_begin(&job, command, c) == 0);
        run(&job, &t);
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1);
        if (mode == 1) {
            assert(job.phase == AXD_PSM_CANCELLED && job.error == AXD_PSM_NO_ERROR);
            assert(job.completed_ops == i + 1);
        } else {
            assert(job.phase == AXD_PSM_FAILED && job.error == AXD_PSM_IO_ERROR);
            assert(job.completed_ops == i);
        }
        terminal(&job, &t, job.phase);
    }
}

struct script { struct event *events; size_t count; };
static struct script new_script(void)
{
    struct script s = {NULL, 0};
    s.events = malloc(60000 * sizeof(*s.events));
    assert(s.events != NULL);
    return s;
}
static void append(struct script *s, const struct event *events, size_t count)
{
    assert(s->count + count <= 60000);
    memcpy(s->events + s->count, events, count * sizeof(*events)); s->count += count;
}
static void append_shm(struct script *s, uint32_t value)
{
    const struct event events[] = {SHM(value)};
    append(s, events, COUNT(events));
}
static void append_delay(struct script *s, uint32_t us)
{
    const struct event event[] = {D(0)};
    struct event e = event[0]; e.value = us; append(s, &e, 1);
}
static void append_pair(struct script *s, uint32_t offset, uint32_t value)
{
    const struct event events[] = {R(0, 0), FB};
    struct event e[2]; memcpy(e, events, sizeof(e));
    e[0].offset = offset; e[0].response = value; append(s, e, 2);
}
static void wake_case(uint16_t budget, uint32_t delays, unsigned loop_ack,
                      unsigned final_ack, uint32_t reason)
{
    struct script s = new_script();
    struct axd_psm_context c = context_for();
    struct axd_psm_control job;
    struct trace t;
    uint32_t i;
    c.wake_budget_us = budget; c.fatal_reason = reason;
    append_delay(&s, 40); append_shm(&s, delays == 0 && loop_ack ? 2 : 3);
    for (i = 0; i < delays; i++) {
        append_delay(&s, 10); append_shm(&s, loop_ack && i + 1 == delays ? 2 : 3);
    }
    append_shm(&s, final_ack ? 2 : 3);
    t = trace_for(s.events, s.count);
    job = execute(AXD_PSM_WAIT_WAKE, &c, &t,
        final_ack ? AXD_PSM_DONE : AXD_PSM_FAILED,
        final_ack ? AXD_PSM_NO_ERROR : AXD_PSM_WAKE_STATE,
        final_ack ? AXD_PSM_WOKEN : AXD_PSM_NO_RESULT,
        final_ack ? 0 : AXD_PSM_FATAL);
    assert(t.shm_reads == delays + 2 && t.delays == delays + 1);
    assert(t.delay_us == 40u + (uint64_t)delays * 10u);
    assert(job.context.fatal_reason == (final_ack ? reason : (reason ? reason : 3)));
    assert(job.context.wake_hint == (final_ack ? 1 : 0));
    if (delays <= 2) fault_matrix(AXD_PSM_WAIT_WAKE, &c, s.events, s.count);
    free(s.events);
}
static void suspend_case(unsigned bank, uint32_t delays, unsigned loop_ack,
                          unsigned final_ack, uint32_t chip)
{
    struct script s = new_script();
    struct axd_psm_context c = context_for();
    struct axd_psm_control job;
    struct trace t;
    uint32_t status = bank ? 0x1c0u : 0x128u, control = bank ? 0x1b4u : 0x120u;
    const struct event *prefix = bank ? suspend_ext : suspend_main;
    uint32_t i;
    c.chip_id = chip;
    append(&s, prefix, 8);
    append_pair(&s, status, delays == 0 && loop_ack ? 1 : 0);
    for (i = 0; i < delays; i++) {
        append_delay(&s, 10); append_pair(&s, status, loop_ack && i + 1 == delays ? 1 : 0);
    }
    append_pair(&s, status, final_ack ? 1 : 0);
    if (final_ack) append_pair(&s, control, 2);
    t = trace_for(s.events, s.count);
    job = execute(bank ? AXD_PSM_SUSPEND_EXT : AXD_PSM_SUSPEND_MAIN, &c, &t,
        final_ack ? AXD_PSM_DONE : AXD_PSM_FAILED,
        final_ack ? AXD_PSM_NO_ERROR : AXD_PSM_ACK_MISSING,
        final_ack ? AXD_PSM_SUSPENDED : AXD_PSM_NO_RESULT,
        final_ack || (!bank && chip == 0xa8d1) ? 0 : AXD_PSM_FATAL);
    assert(t.delays == delays && t.delay_us == (uint64_t)delays * 10u);
    assert(job.context.timeout_flags == (final_ack ? 0u : 0x2000u));
    assert(job.context.main_depth == (bank ? 0u : 1u));
    assert(job.context.extended_depth == (bank ? 1u : 0u));
    if (delays == 1) fault_matrix(bank ? AXD_PSM_SUSPEND_EXT : AXD_PSM_SUSPEND_MAIN,
                                &c, s.events, s.count);
    free(s.events);
}
static void enable_case(uint32_t busy_reads, uint32_t final_state,
                         uint32_t timeout_flags, uint32_t fatal_reason)
{
    struct script s = new_script();
    struct axd_psm_context c = context_for();
    struct axd_psm_control job;
    struct trace t;
    uint32_t i;
    unsigned good = final_state == 2;
    c.main_control = 2; c.main_depth = 1; c.wake_reasons = 12;
    c.timeout_flags = timeout_flags; c.fatal_reason = fatal_reason;
    append(&s, enable_main, 10);
    for (i = 0; i < busy_reads; i++) { append_shm(&s, 3); append_delay(&s, 10); }
    append_shm(&s, final_state);
    t = trace_for(s.events, s.count);
    job = execute(AXD_PSM_ENABLE_MAIN, &c, &t, good ? AXD_PSM_DONE : AXD_PSM_FAILED,
        good ? AXD_PSM_NO_ERROR : AXD_PSM_ENABLE_STATE,
        good ? AXD_PSM_ENABLED : AXD_PSM_NO_RESULT,
        good || (timeout_flags & 0x2000u) ? 0 : AXD_PSM_FATAL);
    assert(t.shm_reads == busy_reads + 1 && t.delays == busy_reads);
    assert(t.delay_us == (uint64_t)busy_reads * 10u && job.context.main_depth == 0);
    assert(job.context.fatal_reason == (good || (timeout_flags & 0x2000u) ? fatal_reason :
                                       (fatal_reason ? fatal_reason : 0x34u)));
    assert(job.context.wake_reasons == (good ? 8u : 12u));
    if (busy_reads == 1) fault_matrix(AXD_PSM_ENABLE_MAIN, &c, s.events, s.count);
    free(s.events);
}

static void literal_paths(void)
{
    struct axd_psm_context c = context_for();
    struct axd_psm_control job;
    struct trace t = trace_for(wake_now, COUNT(wake_now));
    job = execute(AXD_PSM_WAIT_WAKE, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_WOKEN, 0);
    assert(job.context.wake_hint == 1);
    fault_matrix(AXD_PSM_WAIT_WAKE, &c, wake_now, COUNT(wake_now));
    {
        struct event masked[COUNT(wake_now)];
        memcpy(masked, wake_now, sizeof(masked));
        masked[5].response = masked[11].response = 0x10002;
        c.main_control = 0x02000000;
        t = trace_for(masked, COUNT(masked));
        job = execute(AXD_PSM_WAIT_WAKE, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_WOKEN, 0);
        assert(job.context.wake_hint == 0); /* Cached control suppresses hint. */
        c = context_for(); c.wake_hint = 1;
        t = trace_for(wake_now, COUNT(wake_now));
        job = execute(AXD_PSM_WAIT_WAKE, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_WOKEN, 0);
        assert(job.context.wake_hint == 1);
        c = context_for();
    }
    t = trace_for(suspend_main, COUNT(suspend_main));
    job = execute(AXD_PSM_SUSPEND_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_SUSPENDED, 0);
    assert(job.context.main_control == 2 && job.context.wake_reasons == 12);
    fault_matrix(AXD_PSM_SUSPEND_MAIN, &c, suspend_main, COUNT(suspend_main));
    t = trace_for(suspend_ext, COUNT(suspend_ext));
    job = execute(AXD_PSM_SUSPEND_EXT, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_SUSPENDED, 0);
    assert(job.context.extended_control == 2 && job.context.wake_reasons == 8);
    fault_matrix(AXD_PSM_SUSPEND_EXT, &c, suspend_ext, COUNT(suspend_ext));
    c.main_control = 2; c.main_depth = 1; c.wake_reasons = 12;
    t = trace_for(enable_main, COUNT(enable_main));
    job = execute(AXD_PSM_ENABLE_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_ENABLED, 0);
    assert(job.context.main_control == 3 && job.context.main_depth == 0 && job.context.wake_reasons == 8);
    fault_matrix(AXD_PSM_ENABLE_MAIN, &c, enable_main, COUNT(enable_main));
    c = context_for(); c.extended_control = 2; c.extended_depth = 1;
    t = trace_for(enable_ext, COUNT(enable_ext));
    job = execute(AXD_PSM_ENABLE_EXT, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_ENABLED, 0);
    assert(job.context.extended_depth == 0 && job.context.extended_control == 3 && t.shm_reads == 0);
    fault_matrix(AXD_PSM_ENABLE_EXT, &c, enable_ext, COUNT(enable_ext));
    c = context_for(); c.wake_reasons = 0; c.mirror_enabled = 1; c.mirror_control = 0x800;
    t = trace_for(forced_wake, COUNT(forced_wake));
    job = execute(AXD_PSM_SUSPEND_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_SUSPENDED, 0);
    assert(job.context.main_control == 0x04000002 && job.context.mirror_control == 0x04000800);
    assert(job.context.wake_reasons == 4 && job.context.wake_hint == 1);
    fault_matrix(AXD_PSM_SUSPEND_MAIN, &c, forced_wake, COUNT(forced_wake));
}

/* A coordinator can feed the completed job's own updated context into begin.
 * Reusing the suspended cache/depth avoids caller-side reconstruction.
 */
static void chained_context_alias(void)
{
    struct axd_psm_context c = context_for();
    struct axd_psm_control job;
    struct trace t = trace_for(suspend_main, COUNT(suspend_main));
    job = execute(AXD_PSM_SUSPEND_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR,
                  AXD_PSM_SUSPENDED, 0);
    assert(job.context.main_depth == 1 && job.context.main_control == 2);
    assert(job.context.wake_reasons == 12);
    assert(axd_psm_control_begin(&job, AXD_PSM_ENABLE_MAIN, &job.context) == 0);
    assert(job.context.core == 130 && job.context.minor == 1);
    assert(job.context.main_depth == 1 && job.context.main_control == 2);
    assert(job.context.wake_reasons == 12 && job.attempted_ops == 0 && job.completed_ops == 0);
    t = trace_for(enable_main, COUNT(enable_main));
    run(&job, &t);
    assert(job.phase == AXD_PSM_DONE && job.result == AXD_PSM_ENABLED);
    assert(job.error == AXD_PSM_NO_ERROR && job.pending_callouts == 0);
    assert(t.cursor == COUNT(enable_main) && job.completed_ops == COUNT(enable_main));
    assert(job.attempted_ops == job.completed_ops);
    assert(job.context.main_depth == 0 && job.context.main_control == 3);
    assert(job.context.wake_reasons == 8); /* Only suspend reason4 is removed. */
    terminal(&job, &t, AXD_PSM_DONE);
}

static void nesting_and_allones(void)
{
    const struct event main_clear[] = {R(0x120, 2), FB};
    const struct event main_running[] = {R(0x120, 3), FB};
    const struct event ext_clear[] = {R(0x1b4, 2), FB};
    const struct event ext_nested[] = {R(0x1b4, 3), FB, R(0x1b4, 2), FB};
    const struct event ext_bad[] = {R(0x1b4, 2), FB, R(0x1b4, 3), FB};
    const struct event absent_main[] = {R(0x120, 1), FB};
    const struct event absent_ext[] = {R(0x1b4, 0), FB};
    struct axd_psm_context c = context_for();
    struct axd_psm_control job;
    struct trace t;
    unsigned bank, i;
    c.main_depth = 1;
    t = trace_for(main_clear, COUNT(main_clear));
    job = execute(AXD_PSM_SUSPEND_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_NESTED, 0);
    assert(job.context.main_depth == 2);
    t = trace_for(main_running, COUNT(main_running));
    execute(AXD_PSM_SUSPEND_MAIN, &c, &t, AXD_PSM_FAILED, AXD_PSM_BAD_NESTING, AXD_PSM_NO_RESULT, AXD_PSM_FATAL);
    c = context_for(); c.extended_depth = 1;
    t = trace_for(ext_nested, COUNT(ext_nested));
    job = execute(AXD_PSM_SUSPEND_EXT, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_NESTED, 0);
    assert(job.context.extended_depth == 2);
    t = trace_for(ext_bad, COUNT(ext_bad));
    execute(AXD_PSM_SUSPEND_EXT, &c, &t, AXD_PSM_FAILED, AXD_PSM_BAD_NESTING, AXD_PSM_NO_RESULT, AXD_PSM_FATAL);
    for (bank = 0; bank < 2; bank++) {
        const struct event *prefix = bank ? ext_clear : main_clear;
        enum axd_psm_command suspend = bank ? AXD_PSM_SUSPEND_EXT : AXD_PSM_SUSPEND_MAIN;
        enum axd_psm_command enable = bank ? AXD_PSM_ENABLE_EXT : AXD_PSM_ENABLE_MAIN;
        c = context_for();
        if (bank) c.extended_depth = UINT32_MAX; else c.main_depth = UINT32_MAX;
        t = trace_for(prefix, 2);
        execute(suspend, &c, &t, AXD_PSM_FAILED, AXD_PSM_BAD_NESTING, AXD_PSM_NO_RESULT, 0);
        c = context_for(); t = trace_for(prefix, 2);
        execute(enable, &c, &t, AXD_PSM_FAILED, AXD_PSM_BAD_NESTING, AXD_PSM_NO_RESULT, 0);
        if (bank) c.extended_depth = 2; else c.main_depth = 2;
        t = trace_for(prefix, 2);
        job = execute(enable, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_NESTED, 0);
        assert((bank ? job.context.extended_depth : job.context.main_depth) == 1);
        t = trace_for(bank ? absent_ext : absent_main, 2);
        execute(suspend, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_NOOP, 0);
        t = trace_for(bank ? absent_ext : absent_main, 2);
        execute(enable, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_NOOP, 0);
        for (i = 0; i < 3; i++) {
            struct event events[COUNT(suspend_main)];
            size_t index = i == 0 ? 2 : (i == 1 ? 4 : 12);
            memcpy(events, bank ? suspend_ext : suspend_main, sizeof(events));
            events[index].response = UINT32_MAX;
            c = context_for(); t = trace_for(events, index + 2);
            execute(suspend, &c, &t, AXD_PSM_FAILED, AXD_PSM_DEVICE_GONE, AXD_PSM_NO_RESULT, AXD_PSM_DOWN);
        }
    }
    c = context_for(); t = trace_for(main_running, 2);
    execute(AXD_PSM_ENABLE_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_NOOP, 0);
    { const struct event ext_running[] = {R(0x1b4, 3), FB};
      t = trace_for(ext_running, 2);
      execute(AXD_PSM_ENABLE_EXT, &c, &t, AXD_PSM_FAILED, AXD_PSM_BAD_NESTING, AXD_PSM_NO_RESULT, 0); }
}

static void overrides_and_dependencies(void)
{
    struct axd_psm_context c = context_for();
    struct axd_psm_control job;
    struct trace t;
    struct event events[COUNT(suspend_main)];
    static const struct event cached_enable[] = {
        R(0x120, 2), FB, SB, W(0x128, 1), R(0x120, 3), FB, R(0x128, 0), FB, SHM(2)};
    static const struct event cached_ext[] = {
        R(0x1b4, 2), FB, R(0x1b4, 2), FB, SB, W(0x1c0, 1), R(0x1b4, 3), FB, R(0x1c0, 0), FB};
    c.main_control = 0x40003; c.mute_override = c.mirror_enabled = 1; c.mirror_control = 0x800;
    memcpy(events, suspend_main, sizeof(events)); events[7].value = 0x04020002;
    t = trace_for(events, COUNT(events));
    job = execute(AXD_PSM_SUSPEND_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_SUSPENDED, 0);
    assert(job.context.main_control == 0x40002 && job.context.mirror_control == 0x800);
    /* A cached wake bit avoids forcing/waiting, even with no prior reasons. */
    c = context_for(); c.main_control = 0x04000003; c.wake_reasons = 0;
    c.mirror_enabled = 1; c.mirror_control = 0x04000800;
    t = trace_for(suspend_main, COUNT(suspend_main));
    job = execute(AXD_PSM_SUSPEND_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_SUSPENDED, 0);
    assert(job.context.wake_reasons == 4 && job.context.wake_hint == 0);
    assert(job.context.mirror_control == 0x04000800 && t.shm_reads == 0);
    /* Missing band skips wake delay/reads after the explicit override writes. */
    {
        struct event no_band[18];
        memcpy(no_band, forced_wake, 6 * sizeof(*no_band));
        memcpy(no_band + 6, forced_wake + 19, 12 * sizeof(*no_band));
        c = context_for(); c.wake_reasons = 0; c.band_present = 0;
        c.mirror_enabled = 1; c.mirror_control = 0x800;
        t = trace_for(no_band, COUNT(no_band));
        job = execute(AXD_PSM_SUSPEND_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_SUSPENDED, 0);
        assert(job.context.wake_hint == 0 && t.shm_reads == 0 && t.delays == 0);
    }
    c = context_for(); c.extended_control = 0x40003; c.mute_override = c.mirror_enabled = 1;
    memcpy(events, suspend_ext, sizeof(events)); events[7].value = 0x40002;
    t = trace_for(events, COUNT(events));
    execute(AXD_PSM_SUSPEND_EXT, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_SUSPENDED, 0);
    c = context_for(); c.main_depth = 1; c.wake_reasons = 12;
    t = trace_for(cached_enable, COUNT(cached_enable));
    execute(AXD_PSM_ENABLE_MAIN, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_ENABLED, 0);
    c = context_for(); c.extended_depth = 1;
    t = trace_for(cached_ext, COUNT(cached_ext));
    execute(AXD_PSM_ENABLE_EXT, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_ENABLED, 0);
    c = context_for(); c.main_depth = 1; c.wake_reasons = 4;
    t = trace_for(cached_enable, COUNT(cached_enable));
    job = execute(AXD_PSM_ENABLE_MAIN, &c, &t, AXD_PSM_DEPENDENCY, AXD_PSM_NO_ERROR,
                  AXD_PSM_NO_RESULT, AXD_PSM_WAKE_CONTROL);
    assert(job.context.wake_reasons == 0);
    c.wake_reasons = 12; c.mbss_enabled = 1;
    t = trace_for(cached_enable, COUNT(cached_enable));
    job = execute(AXD_PSM_ENABLE_MAIN, &c, &t, AXD_PSM_DEPENDENCY, AXD_PSM_NO_ERROR,
                  AXD_PSM_NO_RESULT, AXD_PSM_MBSS_PRQ);
    assert(job.context.wake_reasons == 8);
    c.wake_reasons = 4;
    t = trace_for(cached_enable, COUNT(cached_enable));
    execute(AXD_PSM_ENABLE_MAIN, &c, &t, AXD_PSM_DEPENDENCY, AXD_PSM_NO_ERROR,
            AXD_PSM_NO_RESULT, AXD_PSM_WAKE_CONTROL | AXD_PSM_MBSS_PRQ);
}

static void validation(void)
{
    struct axd_psm_context c = context_for();
    struct axd_psm_control job, empty = {0};
    struct trace t = trace_for(NULL, 0);
    unsigned i;
    assert(axd_psm_control_begin(NULL, AXD_PSM_WAIT_WAKE, &c) != 0);
    assert(axd_psm_control_begin(&job, AXD_PSM_WAIT_WAKE, NULL) != 0);
    terminal(&job, &t, AXD_PSM_FAILED);
    for (i = 0; i < 5; i++) {
        c = context_for();
        if (i == 0) c.band_present = 2;
        if (i == 1) c.mute_override = 2;
        if (i == 2) c.mirror_enabled = 2;
        if (i == 3) c.wake_hint = 2;
        if (i == 4) c.mbss_enabled = 2;
        assert(axd_psm_control_begin(&job, AXD_PSM_WAIT_WAKE, &c) != 0);
        assert(job.error == AXD_PSM_BAD_ARGUMENT && job.attempted_ops == 0);
        terminal(&job, &t, AXD_PSM_FAILED);
    }
    c = context_for(); c.core = 129;
    assert(axd_psm_control_begin(&job, AXD_PSM_WAIT_WAKE, &c) != 0 && job.error == AXD_PSM_UNSUPPORTED);
    c.core = 130; c.minor = 2;
    assert(axd_psm_control_begin(&job, AXD_PSM_WAIT_WAKE, &c) != 0 && job.error == AXD_PSM_UNSUPPORTED);
    c.minor = 0;
    assert(axd_psm_control_begin(&job, (enum axd_psm_command)0, &c) != 0);
    assert(axd_psm_control_begin(&job, (enum axd_psm_command)6, &c) != 0);
    assert(axd_psm_control_begin(&job, AXD_PSM_WAIT_WAKE, &c) == 0);
    c.band_present = 0; assert(job.context.band_present == 1);
    assert(axd_psm_control_step(&job, NULL, NULL) == AXD_PSM_FAILED && job.attempted_ops == 0);
    terminal(&job, &t, AXD_PSM_FAILED);
    assert(axd_psm_control_begin(&job, AXD_PSM_WAIT_WAKE, &c) == 0);
    job = execute(AXD_PSM_WAIT_WAKE, &c, &t, AXD_PSM_DONE, AXD_PSM_NO_ERROR, AXD_PSM_NOOP, 0);
    c = context_for(); assert(axd_psm_control_begin(&job, AXD_PSM_WAIT_WAKE, &c) == 0);
    assert(axd_psm_control_cancel(&job) == AXD_PSM_CANCELLED);
    terminal(&job, &t, AXD_PSM_CANCELLED);
    assert(axd_psm_control_step(NULL, io, &t) == AXD_PSM_INVALID);
    assert(axd_psm_control_cancel(NULL) == AXD_PSM_INVALID);
    terminal(&empty, &t, AXD_PSM_INVALID);
}

int main(void)
{
    literal_paths(); chained_context_alias(); nesting_and_allones(); overrides_and_dependencies(); validation();
    wake_case(0, 0, 1, 1, 0); wake_case(0, 0, 0, 1, 0); wake_case(0, 0, 1, 0, 7);
    wake_case(1, 1, 1, 1, 0); wake_case(9, 1, 0, 1, 0); wake_case(10, 1, 0, 0, 0);
    wake_case(11, 2, 0, 1, 0); wake_case(65535, 6554, 0, 1, 0);
    wake_case(65535, 6554, 0, 0, 9);
    suspend_case(0, 1, 1, 1, 63178); suspend_case(1, 1, 1, 1, 63178);
    suspend_case(0, 8300, 1, 1, 63178); suspend_case(1, 8300, 0, 1, 63178);
    suspend_case(0, 8300, 0, 0, 63178); suspend_case(1, 8300, 0, 0, 63178);
    suspend_case(0, 8300, 0, 0, 0xa8d1); suspend_case(1, 8300, 0, 0, 0xa8d1);
    suspend_case(0, 0, 1, 0, 63178); suspend_case(1, 0, 1, 0, 63178);
    enable_case(0, 2, 0, 0); enable_case(1, 2, 0, 0); enable_case(1000, 2, 0, 0);
    enable_case(1000, 3, 0, 0); enable_case(0, 4, 0, 0); enable_case(0, 0, 0, 7);
    enable_case(0, 4, 0x2000, 0); enable_case(1000, 3, 0x2000, 9);
    puts("PSM control: independent wake/suspend/enable traces, budgets, nesting, dependencies and every-event faults passed");
    return 0;
}
