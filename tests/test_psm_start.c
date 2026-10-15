/* SPDX-License-Identifier: MIT */
#include "../driver/psm_start.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Synthetic source-derived oracle. Immediate startup is a literal event list;
 * long waits append independent delay/read/full-barrier triples. No production
 * state or operation generator is consulted. The FIFO SHM wrapper delegates
 * selector0x10000 to the evidenced six-event object-memory read helper.
 */
struct event {
    enum axd_d11_kind kind;
    uint32_t offset, value, response;
};
#define SB {AXD_D11_STORE_BARRIER, 0, 0, 0}
#define FB {AXD_D11_FULL_BARRIER, 0, 0, 0}
#define W(o,v) {AXD_D11_WRITE32, (o), (v), 0}
#define R(o,v) {AXD_D11_READ32, (o), 0, (v)}
#define H(o,v) {AXD_D11_READ16, (o), 0, (v)}
#define D {AXD_D11_DELAY_US, 0, 10, 0}
static const struct event main_prefix[] = {
    SB, W(0x128, 0xffffffffu), SB, W(0x120, 0x04020406), SB, W(0x120, 0x04020402)
};
static const struct event ext_prefix[] = {
    SB, W(0x1c0, 0xffffffffu), SB, W(0x1b4, 0x402)
};
static const struct event fifo_suffix[] = {
    SB, W(0x160, 0x10108), R(0x160, 0xdeadbeef), FB, H(0x166, 0x4000), FB
};
static const struct event immediate[] = {
    SB, W(0x128, 0xffffffffu),
    SB, W(0x120, 0x04020406), SB, W(0x120, 0x04020402),
    R(0x128, 1), FB, R(0x128, 1), FB,
    SB, W(0x1c0, 0xffffffffu), SB, W(0x1b4, 0x402),
    R(0x1c0, 1), FB, R(0x1c0, 1), FB,
    SB, W(0x160, 0x10108), R(0x160, 0xdeadbeef), FB, H(0x166, 0x4000), FB
};
static const struct event one_delay_each[] = {
    SB, W(0x128, 0xffffffffu),
    SB, W(0x120, 0x04020406), SB, W(0x120, 0x04020402),
    R(0x128, 0xfffffffeu), FB, D, R(0x128, 5), FB, R(0x128, 5), FB,
    SB, W(0x1c0, 0xffffffffu), SB, W(0x1b4, 0x402),
    R(0x1c0, 2), FB, D, R(0x1c0, 3), FB, R(0x1c0, 3), FB,
    SB, W(0x160, 0x10108), R(0x160, 0), FB, H(0x166, 0x4000), FB
};
struct trace {
    const struct event *events;
    size_t count, cursor, fail_at, cancel_at, delays[2], reads[2];
    struct axd_psm_start *cancel_job;
    unsigned bank;
};
static struct trace trace_for(const struct event *events, size_t count)
{
    struct trace t;
    memset(&t, 0, sizeof(t));
    t.events = events;
    t.count = count;
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
    if (op->kind == AXD_D11_WRITE32 && op->offset == 0x1c0)
        t->bank = 1;
    if (op->kind == AXD_D11_DELAY_US)
        t->delays[t->bank]++;
    if (op->kind == AXD_D11_READ32 && (op->offset == 0x128 || op->offset == 0x1c0))
        t->reads[t->bank]++;
    if (t->cancel_job != NULL && current == t->cancel_at)
        assert(axd_psm_start_cancel(t->cancel_job) == AXD_START_CANCELLED);
    if (current == t->fail_at)
        return -1; /* Simulated side effects may already have occurred. */
    *value = e->response;
    return 0;
}
static struct axd_start_context context_for(unsigned ext)
{
    struct axd_start_context c = {130, 1, 0, 0, 0, 0, 0, 0, 0};
    c.psmx_enabled = (uint8_t)ext;
    return c;
}
static void run(struct axd_psm_start *job, struct trace *t)
{
    size_t i;
    for (i = 0; i < t->count + 32; i++) {
        if (axd_psm_start_step(job, io, t) != AXD_START_RUNNING)
            return;
    }
    assert(0 && "startup exceeded bounded independent trace");
}
static void terminal(struct axd_psm_start *job, struct trace *t,
                     enum axd_start_phase phase)
{
    struct axd_psm_start before = *job;
    size_t cursor = t->cursor;
    assert(axd_psm_start_step(job, io, t) == phase);
    assert(axd_psm_start_step(job, NULL, NULL) == phase);
    assert(axd_psm_start_cancel(job) == phase);
    assert(t->cursor == cursor && memcmp(&before, job, sizeof(before)) == 0);
}
static void success(const struct axd_start_context *c, const struct event *events,
                    size_t count, uint32_t mirror)
{
    struct axd_psm_start job;
    struct trace t = trace_for(events, count);
    assert(axd_psm_start_begin(&job, c) == 0);
    run(&job, &t);
    assert(job.phase == AXD_START_SUSPENDED && job.error == AXD_START_NO_ERROR);
    assert(t.cursor == count && job.attempted_ops == count && job.completed_ops == count);
    assert(job.context.main_control == 0x04020402 && job.context.mirror_control == mirror);
    if (c->psmx_enabled)
        assert(job.context.extended_control == 0x402);
    terminal(&job, &t, AXD_START_SUSPENDED);
}

/* Independent prefix boundary: commit mirror cache just before mirror ST,
 * after primary ST/W have returned successfully without cancellation.
 */
static uint32_t mirror_prefix(const struct event *events, size_t count,
                               size_t stopped_at, uint32_t initial)
{
    size_t j;
    for (j = 0; j <= stopped_at && j + 1 < count; j++)
        if (events[j].kind == AXD_D11_STORE_BARRIER &&
            events[j + 1].kind == AXD_D11_WRITE32 && events[j + 1].offset == 0x324)
            initial = events[j + 1].value;
    return initial;
}
static void fault_matrix(const struct axd_start_context *context,
                         const struct event *events, size_t count)
{
    struct axd_psm_start job;
    struct axd_start_context c = *context;
    size_t i;
    for (i = 0; i < count; i++) {
        struct trace t = trace_for(events, count);
        t.fail_at = i;
        assert(axd_psm_start_begin(&job, &c) == 0);
        run(&job, &t);
        assert(job.phase == AXD_START_FAILED && job.error == AXD_START_IO_ERROR);
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1 && job.completed_ops == i);
        assert(job.context.mirror_control == mirror_prefix(events, count, i, c.mirror_control));
        terminal(&job, &t, AXD_START_FAILED);

        t = trace_for(events, count);
        t.cancel_at = i;
        t.cancel_job = &job;
        assert(axd_psm_start_begin(&job, &c) == 0);
        run(&job, &t);
        assert(job.phase == AXD_START_CANCELLED && job.error == AXD_START_NO_ERROR);
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1 && job.completed_ops == i + 1);
        assert(job.context.mirror_control == mirror_prefix(events, count, i, c.mirror_control));
        terminal(&job, &t, AXD_START_CANCELLED);

        t = trace_for(events, count);
        t.fail_at = t.cancel_at = i;
        t.cancel_job = &job;
        assert(axd_psm_start_begin(&job, &c) == 0);
        run(&job, &t);
        assert(job.phase == AXD_START_FAILED && job.error == AXD_START_IO_ERROR);
        assert(t.cursor == i + 1 && job.completed_ops == i && job.attempted_ops == i + 1);
        assert(job.context.mirror_control == mirror_prefix(events, count, i, c.mirror_control));
        terminal(&job, &t, AXD_START_FAILED);
    }
}

/* Source-derived poll scripts: initial read, exactly N delay/read iterations,
 * then a distinct final confirmation. Values model status, not written data.
 */
struct script { struct event *events; size_t count; };
static void append(struct script *s, const struct event *events, size_t count)
{
    assert(s->count + count <= 600032);
    memcpy(s->events + s->count, events, count * sizeof(*events));
    s->count += count;
}
static void poll_script(struct script *s, uint32_t offset, uint32_t delays,
                        unsigned iteration_ack, unsigned final_ack)
{
    struct event pair[] = {R(0, 0), FB};
    uint32_t i;
    pair[0].offset = offset;
    pair[0].response = delays == 0 && iteration_ack ? 1u : 0u;
    append(s, pair, 2);
    for (i = 0; i < delays; i++) {
        struct event triple[] = {D, R(0, 0), FB};
        triple[1].offset = offset;
        triple[1].response = iteration_ack && i + 1 == delays ? 1u : 0u;
        append(s, triple, 3);
    }
    pair[0].response = final_ack ? 1u : 0u;
    append(s, pair, 2);
}
static void poll_case(uint32_t main_delays, unsigned main_loop_ack, unsigned main_final,
                      uint32_t ext_delays, unsigned ext_loop_ack, unsigned ext_final)
{
    struct script s = {NULL, 0};
    struct axd_start_context c = context_for(1);
    struct axd_psm_start job;
    struct trace t;
    s.events = malloc(600032 * sizeof(*s.events));
    assert(s.events != NULL);
    append(&s, main_prefix, sizeof(main_prefix) / sizeof(main_prefix[0]));
    poll_script(&s, 0x128, main_delays, main_loop_ack, main_final);
    if (main_final) {
        append(&s, ext_prefix, sizeof(ext_prefix) / sizeof(ext_prefix[0]));
        poll_script(&s, 0x1c0, ext_delays, ext_loop_ack, ext_final);
        if (ext_final)
            append(&s, fifo_suffix, sizeof(fifo_suffix) / sizeof(fifo_suffix[0]));
    }
    t = trace_for(s.events, s.count);
    assert(axd_psm_start_begin(&job, &c) == 0);
    run(&job, &t);
    assert(t.cursor == s.count && job.completed_ops == s.count && job.attempted_ops == s.count);
    assert(t.delays[0] == main_delays && t.reads[0] == main_delays + 2u);
    assert(t.delays[1] == (main_final ? ext_delays : 0u));
    assert(t.reads[1] == (main_final ? ext_delays + 2u : 0u));
    if (!main_final) {
        assert(job.phase == AXD_START_FAILED && job.error == AXD_START_MAIN_ACK_MISSING);
        assert(job.context.extended_control == 0); /* No extended startup. */
    } else if (!ext_final) {
        assert(job.phase == AXD_START_FAILED && job.error == AXD_START_EXT_ACK_MISSING);
    } else {
        assert(job.phase == AXD_START_SUSPENDED && job.error == AXD_START_NO_ERROR);
    }
    terminal(&job, &t, job.phase);
    free(s.events);
}

static void caches_and_flags(void)
{
    struct axd_start_context c = context_for(1);
    unsigned wake, mute;
    static const struct event reset_cached[] = {
        SB, W(0x128, 0xffffffffu), SB, W(0x120, 0x04020402),
        R(0x128, 1), FB, R(0x128, 1), FB,
        SB, W(0x1c0, 0xffffffffu), /* Extended cache already 0x402. */
        R(0x1c0, 1), FB, R(0x1c0, 1), FB,
        SB, W(0x160, 0x10108), R(0x160, 3), FB, H(0x166, 0x4000), FB
    };
    static const struct event mirrored[] = {
        SB, W(0x128, 0xffffffffu),
        SB, W(0x120, 0x04020406), SB, W(0x324, 0x04020c06),
        SB, W(0x120, 0x04020402), SB, W(0x324, 0x04020c02),
        R(0x128, 1), FB, R(0x128, 1), FB,
        SB, W(0x160, 0x10108), R(0x160, 7), FB, H(0x166, 0x4000), FB
    };
    static const struct event mirror_reset_cached[] = {
        SB, W(0x128, 0xffffffffu),
        SB, W(0x120, 0x04020406), /* Matching mirror: no mirror reset write. */
        SB, W(0x120, 0x04020402), SB, W(0x324, 0x04020c02),
        R(0x128, 1), FB, R(0x128, 1), FB,
        SB, W(0x160, 0x10108), R(0x160, 9), FB, H(0x166, 0x4000), FB
    };
    static const struct event mirror_no_changes[] = {
        SB, W(0x128, 0xffffffffu), SB, W(0x120, 0x04020402),
        R(0x128, 1), FB, R(0x128, 1), FB,
        SB, W(0x160, 0x10108), R(0x160, 5), FB, H(0x166, 0x4000), FB
    };
    for (wake = 0; wake < 2; wake++) {
        for (mute = 0; mute < 2; mute++) {
            c.wake_override = (uint8_t)wake;
            c.mute_override = (uint8_t)mute;
            success(&c, immediate, sizeof(immediate) / sizeof(immediate[0]), 0);
        }
    }
    c = context_for(1);
    c.main_control = 0x04020406;
    c.extended_control = 0x402;
    success(&c, reset_cached, sizeof(reset_cached) / sizeof(reset_cached[0]), 0);
    c = context_for(0);
    c.mirror_enabled = 1;
    c.mirror_control = 0x800; /* Preserve bits outside mask0xcfc72706. */
    success(&c, mirrored, sizeof(mirrored) / sizeof(mirrored[0]), 0x04020c02);
    fault_matrix(&c, mirrored, sizeof(mirrored) / sizeof(mirrored[0]));
    c.mirror_control = 0x04020c06;
    success(&c, mirror_reset_cached, sizeof(mirror_reset_cached) / sizeof(mirror_reset_cached[0]),
            0x04020c02);
    c.main_control = 0x04020406;
    c.mirror_control = 0x04020c02;
    success(&c, mirror_no_changes, sizeof(mirror_no_changes) / sizeof(mirror_no_changes[0]),
            0x04020c02);
    /* An initial RUN cache still transitions through RESET and back to RUN. */
    c.main_control = 0x04020402;
    c.mirror_control = 0x800;
    success(&c, mirrored, sizeof(mirrored) / sizeof(mirrored[0]), 0x04020c02);
}

static void restart_context_alias(void)
{
    /* The completed extended-control cache suppresses its second write. */
    static const struct event restarted[] = {
        SB, W(0x128, 0xffffffffu),
        SB, W(0x120, 0x04020406), SB, W(0x120, 0x04020402),
        R(0x128, 1), FB, R(0x128, 1), FB,
        SB, W(0x1c0, 0xffffffffu),
        R(0x1c0, 1), FB, R(0x1c0, 1), FB,
        SB, W(0x160, 0x10108), R(0x160, 0xdeadbeef), FB, H(0x166, 0x4000), FB
    };
    struct axd_start_context c = context_for(1);
    struct axd_psm_start job;
    struct trace t = trace_for(immediate, sizeof(immediate) / sizeof(immediate[0]));
    assert(axd_psm_start_begin(&job, &c) == 0);
    run(&job, &t);
    assert(job.phase == AXD_START_SUSPENDED && t.cursor == t.count);
    assert(job.context.main_control == 0x04020402 && job.context.extended_control == 0x402);
    assert(axd_psm_start_begin(&job, &job.context) == 0);
    assert(job.context.core == 130 && job.context.minor == 1 && job.context.psmx_enabled == 1);
    assert(job.context.main_control == 0x04020402 && job.context.extended_control == 0x402);
    assert(job.attempted_ops == 0 && job.completed_ops == 0);
    t = trace_for(restarted, sizeof(restarted) / sizeof(restarted[0]));
    run(&job, &t);
    assert(job.phase == AXD_START_SUSPENDED && job.error == AXD_START_NO_ERROR);
    assert(t.cursor == t.count && t.count == 22);
    assert(job.attempted_ops == t.count && job.completed_ops == t.count);
    terminal(&job, &t, AXD_START_SUSPENDED);
}

static void validation_and_fifo(void)
{
    struct axd_start_context c = context_for(1);
    struct axd_psm_start job, empty = {0};
    struct trace t = trace_for(immediate, sizeof(immediate) / sizeof(immediate[0]));
    struct event fifo[sizeof(immediate) / sizeof(immediate[0])];
    const uint32_t fifo_values[] = {0x4000, 0x5fff, 0x14000, 0, 0x2000, 0x6000, 0xe000};
    size_t i;
    assert(axd_psm_start_begin(NULL, &c) != 0);
    assert(axd_psm_start_begin(&job, NULL) != 0);
    assert(job.error == AXD_START_BAD_ARGUMENT);
    terminal(&job, &t, AXD_START_FAILED);
    for (i = 0; i < 4; i++) {
        c = context_for(1);
        if (i == 0) c.wake_override = 2;
        if (i == 1) c.mute_override = 2;
        if (i == 2) c.mirror_enabled = 2;
        if (i == 3) c.psmx_enabled = 2;
        assert(axd_psm_start_begin(&job, &c) != 0 && job.error == AXD_START_BAD_ARGUMENT);
        assert(job.attempted_ops == 0 && t.cursor == 0);
        terminal(&job, &t, AXD_START_FAILED);
    }
    c = context_for(1);
    c.core = 129;
    assert(axd_psm_start_begin(&job, &c) != 0 && job.error == AXD_START_UNSUPPORTED_CORE);
    c.core = 130;
    c.minor = 2;
    assert(axd_psm_start_begin(&job, &c) != 0 && job.error == AXD_START_UNSUPPORTED_CORE);
    terminal(&job, &t, AXD_START_FAILED);
    c.minor = 0;
    assert(axd_psm_start_begin(&job, &c) == 0);
    c.main_control = 0xdeadbeef; /* Context was copied. */
    assert(job.context.main_control == 0);
    assert(axd_psm_start_step(&job, NULL, NULL) == AXD_START_FAILED);
    assert(job.error == AXD_START_BAD_ARGUMENT && job.attempted_ops == 0);
    terminal(&job, &t, AXD_START_FAILED);
    c = context_for(1);
    assert(axd_psm_start_begin(&job, &c) == 0);
    assert(axd_psm_start_cancel(&job) == AXD_START_CANCELLED);
    terminal(&job, &t, AXD_START_CANCELLED);
    assert(axd_psm_start_step(NULL, io, &t) == AXD_START_INVALID);
    assert(axd_psm_start_cancel(NULL) == AXD_START_INVALID);
    terminal(&empty, &t, AXD_START_INVALID);
    for (i = 0; i < sizeof(fifo_values) / sizeof(fifo_values[0]); i++) {
        memcpy(fifo, immediate, sizeof(fifo));
        fifo[22].response = fifo_values[i];
        t = trace_for(fifo, sizeof(fifo) / sizeof(fifo[0]));
        assert(axd_psm_start_begin(&job, &c) == 0);
        run(&job, &t);
        assert(t.cursor == 24 && job.completed_ops == 24);
        if (i < 3)
            assert(job.phase == AXD_START_SUSPENDED && job.error == AXD_START_NO_ERROR);
        else
            assert(job.phase == AXD_START_FAILED && job.error == AXD_START_FIFO_LAYOUT);
        terminal(&job, &t, job.phase);
    }
}

int main(void)
{
    struct axd_start_context c = context_for(1);
    success(&c, immediate, sizeof(immediate) / sizeof(immediate[0]), 0);
    caches_and_flags();
    restart_context_alias();
    fault_matrix(&c, immediate, sizeof(immediate) / sizeof(immediate[0]));
    success(&c, one_delay_each, sizeof(one_delay_each) / sizeof(one_delay_each[0]), 0);
    fault_matrix(&c, one_delay_each, sizeof(one_delay_each) / sizeof(one_delay_each[0]));
    poll_case(3, 1, 1, 7, 1, 1); /* Delayed acknowledgement in each bank. */
    poll_case(100000, 1, 1, 100000, 1, 1); /* Last iteration in both banks. */
    poll_case(100000, 0, 1, 100000, 0, 1); /* Only final confirmations see ACK. */
    poll_case(100000, 0, 0, 0, 1, 1); /* Main timeout: no extended startup/FIFO. */
    poll_case(0, 1, 1, 100000, 0, 0); /* Extended timeout: deliberate fail-stop. */
    poll_case(0, 1, 0, 0, 1, 1); /* Initial main ACK lost at final read. */
    poll_case(0, 1, 1, 0, 1, 0); /* Initial extended ACK lost at final read. */
    validation_and_fifo();
    puts("PSM start: literal trace/cache/mirror, poll boundaries, FIFO predicate, every-event failure/cancellation passed");
    return 0;
}
