/* SPDX-License-Identifier: MIT */
#include "../driver/ai_reset.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Independently transcribed from the retained current-HND assembly/ledger.
 * Synthetic replies deliberately differ from writes. No hardware or vendor
 * payloads: these are protocol events, not a reset-success assertion.
 */
struct event { enum axd_reset_kind kind; unsigned wrapper; uint32_t offset, value, reply; };
#define E(k, o, v, r) {AXD_RESET_##k, 0, o, v, r}
static const struct event literal[] = {
    E(READ32, 0x804, 0, 0), E(FULL_BARRIER, 0, 0, 0),
    E(STORE_BARRIER, 0, 0, 0), E(WRITE32, 0x800, 1, 0), E(DELAY_US, 0, 10, 0),
    E(READ32, 0x804, 0, 0), E(FULL_BARRIER, 0, 0, 0),
    E(STORE_BARRIER, 0, 0, 0), E(WRITE32, 0x408, 0x123, 0),
    E(READ32, 0x408, 0, 0xc0de0007), E(FULL_BARRIER, 0, 0, 0),
    E(DELAY_US, 0, 1, 0), E(STORE_BARRIER, 0, 0, 0), E(WRITE32, 0x408, 0xc0de0005, 0),
    E(READ32, 0x804, 0, 0), E(FULL_BARRIER, 0, 0, 0),
    E(READ32, 0x800, 0, 1), E(FULL_BARRIER, 0, 0, 0),
    E(READ32, 0x804, 0, 0), E(FULL_BARRIER, 0, 0, 0),
    E(STORE_BARRIER, 0, 0, 0), E(WRITE32, 0x800, 0, 0),
    E(READ32, 0x804, 0, 0), E(FULL_BARRIER, 0, 0, 0),
    E(READ32, 0x800, 0, 0), E(FULL_BARRIER, 0, 0, 0),
    E(STORE_BARRIER, 0, 0, 0), E(WRITE32, 0x408, 0x103, 0),
    E(READ32, 0x408, 0, 0xa55a000b), E(FULL_BARRIER, 0, 0, 0),
    E(DELAY_US, 0, 1, 0), E(STORE_BARRIER, 0, 0, 0),
    E(WRITE32, 0x408, 0xa55a0009, 0), E(DELAY_US, 0, 1, 0)
};
#undef E
#define N(a) (sizeof(a) / sizeof((a)[0]))
#define NEVER ((size_t)-1)
struct script { struct event events[4096]; size_t count; };
struct recorder {
    const struct event *events;
    size_t count, calls, fail_at, cancel_at;
    struct axd_ai_reset *job;
    uint64_t fake_elapsed_us, delay_us;
};

static struct axd_reset_context context(unsigned secondary, unsigned interconnect)
{
    const struct axd_reset_context c = {0x812, 130, (uint8_t)interconnect,
                                      (uint8_t)secondary, 0x100, 0x20};
    return c;
}
static struct recorder recorder(struct axd_ai_reset *job,
                               const struct event *events, size_t count)
{
    const struct recorder r = {events, count, 0, NEVER, NEVER, job, 0, 0};
    return r;
}
static int observe(void *opaque, const struct axd_reset_operation *op, uint32_t *reply)
{
    struct recorder *r = opaque;
    size_t i = r->calls;
    const struct event *want;
    assert(i < r->count);
    want = &r->events[i];
    assert(op->kind == want->kind && op->wrapper == want->wrapper);
    assert(op->offset == want->offset && op->value == want->value);
    assert(r->job->attempted_ops == i + 1 && r->job->completed_ops == i);
    *reply = want->reply;
    r->calls++;
    /* Arbitrary elapsed jumps do not consume the counter-based poll budget. */
    r->fake_elapsed_us += 1000000;
    if (op->kind == AXD_RESET_DELAY_US)
        r->delay_us += op->value;
    if (i == r->cancel_at)
        assert(axd_ai_reset_cancel(r->job) == AXD_RESET_CANCELLED);
    return i == r->fail_at ? -1 : 0;
}
static void run(struct axd_ai_reset *job, struct recorder *r)
{
    size_t limit = r->count * 12 + 100, step;
    for (step = 0; step < limit && job->phase == AXD_RESET_RUNNING; step++) {
        size_t before = r->calls;
        enum axd_reset_phase result = axd_ai_reset_step(job, observe, r);
        assert(result == job->phase && r->calls - before <= 1);
    }
    assert(job->phase != AXD_RESET_RUNNING);
}
static void terminal(struct axd_ai_reset *job, struct recorder *r,
                     enum axd_reset_phase phase)
{
    uint64_t attempted = job->attempted_ops, completed = job->completed_ops;
    unsigned wrappers = job->completed_wrappers;
    size_t calls = r->calls;
    assert(axd_ai_reset_step(job, observe, r) == phase);
    assert(axd_ai_reset_step(job, NULL, NULL) == phase);
    assert(axd_ai_reset_cancel(job) == phase);
    assert(r->calls == calls && job->attempted_ops == attempted);
    assert(job->completed_ops == completed && job->completed_wrappers == wrappers);
}
static void add(struct script *s, enum axd_reset_kind k, unsigned wrapper,
                uint32_t offset, uint32_t value, uint32_t reply)
{
    const struct event e = {k, wrapper, offset, value, reply};
    assert(s->count < N(s->events));
    s->events[s->count++] = e;
}
static void read_event(struct script *s, unsigned w, uint32_t off, uint32_t reply)
{
    add(s, AXD_RESET_READ32, w, off, 0, reply);
    add(s, AXD_RESET_FULL_BARRIER, w, 0, 0, 0);
}
static void write_event(struct script *s, unsigned w, uint32_t off, uint32_t value)
{
    add(s, AXD_RESET_STORE_BARRIER, w, 0, 0, 0);
    add(s, AXD_RESET_WRITE32, w, off, value, 0);
}
static void delay_event(struct script *s, unsigned w, uint32_t us)
{
    add(s, AXD_RESET_DELAY_US, w, 0, us, 0);
}
/* Literal counter oracle from assembly: one initial read plus at most thirty
 * delay/read pairs. clear_last distinguishes success on the last allowed read
 * from exhaustion with the same number of callbacks.
 */
static void full_wait(struct script *s, unsigned w, int clear_last)
{
    unsigned i;
    read_event(s, w, 0x804, 0x80000000);
    for (i = 0; i < 30; i++) {
        delay_event(s, w, 10);
        read_event(s, w, 0x804, clear_last && i == 29 ? 0 : 0x80000000);
    }
}
static void long_script(struct script *s, int clear_last)
{
    unsigned i;
    s->count = 0;
    full_wait(s, 0, clear_last);
    write_event(s, 0, 0x800, 1); delay_event(s, 0, 10);
    full_wait(s, 0, clear_last);
    write_event(s, 0, 0x408, 0x123); read_event(s, 0, 0x408, 0xc0de0007);
    delay_event(s, 0, 1); write_event(s, 0, 0x408, 0xc0de0005);
    full_wait(s, 0, clear_last);
    /* Nonzero reset control remains legal after nine unsuccessful releases.
     * Only one pre-read and nine subsequent reads: no tenth release write.
     */
    read_event(s, 0, 0x800, 2);
    for (i = 0; i < 9; i++) {
        full_wait(s, 0, clear_last);
        write_event(s, 0, 0x800, 0);
        full_wait(s, 0, clear_last);
        read_event(s, 0, 0x800, 2);
    }
    write_event(s, 0, 0x408, 0x103); read_event(s, 0, 0x408, 0xa55a000b);
    delay_event(s, 0, 1); write_event(s, 0, 0x408, 0xa55a0009);
    delay_event(s, 0, 1);
}
static void success(const struct event *events, size_t count, unsigned secondary,
                    unsigned interconnect, unsigned busy_exhaustions,
                    unsigned reset_mask, uint64_t delays)
{
    struct axd_ai_reset job;
    struct axd_reset_context c = context(secondary, interconnect);
    struct recorder r = recorder(&job, events, count);
    assert(axd_ai_reset_begin(&job, &c) == 0);
    c.bits = 0; c.reset_bits = 0; c.secondary_present = 0; c.interconnect = 0;
    assert(job.context.bits == 0x100 && job.context.reset_bits == 0x20);
    run(&job, &r);
    assert(job.phase == AXD_RESET_TRACE_COMPLETE && job.error == AXD_RESET_NO_ERROR);
    assert(r.calls == count && job.attempted_ops == count && job.completed_ops == count);
    assert(job.completed_wrappers == secondary + 1);
    assert(job.busy_exhaustions == busy_exhaustions && job.reset_nonzero_mask == reset_mask);
    assert(r.delay_us == delays && r.fake_elapsed_us == count * UINT64_C(1000000));
    terminal(&job, &r, AXD_RESET_TRACE_COMPLETE);
}
static void sweep(const struct event *events, size_t count, unsigned secondary)
{
    size_t i;
    unsigned mode;
    for (mode = 0; mode < 3; mode++) {
        for (i = 0; i < count; i++) {
            struct axd_ai_reset job;
            struct axd_reset_context c = context(secondary, 1);
            struct recorder r = recorder(&job, events, count);
            enum axd_reset_phase phase = mode == 1 ? AXD_RESET_CANCELLED : AXD_RESET_FAILED;
            size_t j, successful_callbacks = i + (mode == 1);
            unsigned successful_releases = 0;
            assert(axd_ai_reset_begin(&job, &c) == 0);
            if (mode != 1) r.fail_at = i;
            if (mode != 0) r.cancel_at = i;
            run(&job, &r);
            assert(job.phase == phase && r.calls == i + 1);
            assert(job.attempted_ops == i + 1 && job.completed_ops == i + (mode == 1));
            assert(job.error == (mode == 1 ? AXD_RESET_NO_ERROR : AXD_RESET_IO_ERROR));
            for (j = 0; j < successful_callbacks; j++) {
                if (events[j].wrapper == events[i].wrapper &&
                    events[j].kind == AXD_RESET_WRITE32 &&
                    events[j].offset == 0x800 && events[j].value == 0)
                    successful_releases++;
            }
            /* A queued/barriered release is not yet a successful write. A
             * successful release whose callback cancels still counts once.
             */
            assert(job.release_writes == successful_releases);
            /* Wrapper completion is committed after its last callback, so a
             * cancelled/failed final delay cannot invent completion.
             */
            assert(job.completed_wrappers == (secondary && events[i].wrapper == 0 ? 1u : 0u));
            terminal(&job, &r, phase);
        }
    }
}
static void invalid(const struct axd_reset_context *c, enum axd_reset_error error)
{
    struct axd_ai_reset job;
    struct recorder r = recorder(&job, literal, N(literal));
    assert(axd_ai_reset_begin(&job, c) != 0);
    assert(job.phase == AXD_RESET_FAILED && job.error == error);
    assert(job.attempted_ops == 0 && job.completed_ops == 0 && job.completed_wrappers == 0);
    terminal(&job, &r, AXD_RESET_FAILED);
    assert(r.calls == 0);
}
static void arguments(void)
{
    unsigned i;
    const unsigned bad_interconnect[] = {0, 2, 5, 255};
    const unsigned bad_revision[] = {0, 61, 128, 129, 131, 132};
    struct axd_reset_context c = context(0, 1);
    struct axd_ai_reset job;
    struct recorder r = recorder(&job, literal, N(literal));
    assert(axd_ai_reset_begin(NULL, &c) != 0);
    assert(axd_ai_reset_step(NULL, NULL, NULL) == AXD_RESET_INVALID);
    assert(axd_ai_reset_cancel(NULL) == AXD_RESET_INVALID);
    invalid(NULL, AXD_RESET_BAD_ARGUMENT);
    c.secondary_present = 2; invalid(&c, AXD_RESET_BAD_ARGUMENT);
    c = context(0, 1); c.core_id = 0; invalid(&c, AXD_RESET_UNSUPPORTED);
    for (i = 0; i < N(bad_revision); i++) {
        c = context(0, 1); c.revision = (uint16_t)bad_revision[i];
        invalid(&c, AXD_RESET_UNSUPPORTED);
    }
    for (i = 0; i < N(bad_interconnect); i++) {
        c = context(0, bad_interconnect[i]); invalid(&c, AXD_RESET_UNSUPPORTED);
    }
    c = context(0, 1);
    assert(axd_ai_reset_begin(&job, &c) == 0);
    assert(axd_ai_reset_cancel(&job) == AXD_RESET_CANCELLED);
    terminal(&job, &r, AXD_RESET_CANCELLED);
    assert(job.attempted_ops == 0 && job.completed_ops == 0 && r.calls == 0);
    assert(axd_ai_reset_begin(&job, &c) == 0);
    for (i = 0; i < 10 && job.phase == AXD_RESET_RUNNING; i++)
        axd_ai_reset_step(&job, NULL, NULL);
    assert(job.phase == AXD_RESET_FAILED && job.error == AXD_RESET_BAD_ARGUMENT);
    assert(job.attempted_ops == 0 && job.completed_ops == 0);
    terminal(&job, &r, AXD_RESET_FAILED);
}
int main(void)
{
    struct script dual = {{{0}}, 0}, stuck = {{{0}}, 0}, boundary = {{{0}}, 0};
    const unsigned supported[] = {1, 3, 4};
    size_t i;
    assert(N(literal) == 34);
    arguments();
    for (i = 0; i < N(supported); i++)
        success(literal, N(literal), 0, supported[i], 0, 0, 13);
    for (i = 0; i < N(literal); i++) { dual.events[dual.count] = literal[i]; dual.events[dual.count++].wrapper = 1; }
    for (i = 0; i < N(literal); i++) dual.events[dual.count++] = literal[i];
    assert(dual.count == 68);
    success(dual.events, dual.count, 1, 4, 0, 0, 26);
    long_script(&stuck, 0); long_script(&boundary, 1);
    assert(stuck.count == 1988 && boundary.count == 1988);
    success(stuck.events, stuck.count, 0, 3, 21, 1, 6313);
    success(boundary.events, boundary.count, 0, 1, 0, 1, 6313);
    sweep(literal, N(literal), 0);
    sweep(dual.events, dual.count, 1);
    sweep(stuck.events, stuck.count, 0);
    sweep(boundary.events, boundary.count, 0);
    puts("AI reset: literal/secondary/busy-boundary/stuck traces and all callback exits passed");
    return 0;
}
