/* SPDX-License-Identifier: MIT */
#include "../driver/phy_register.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Original literal fixtures derived from retained register/reset/TSSI assembly.
 * Software-commit annotations are an independent oracle: final SY commits the
 * full16 result and low10 snapshot, not the preceding data read.
 */
enum effects { BOOK_ZERO = 1, RESULT = 2, SLOT0 = 4, SLOT1 = 8 };
struct event {
    enum axd_d11_kind kind;
    uint32_t offset, value, reply;
    unsigned effects;
    uint16_t result, saved;
};
#define SB {AXD_D11_STORE_BARRIER, 0, 0, 0, 0, 0, 0}
#define FB {AXD_D11_FULL_BARRIER, 0, 0, 0, 0, 0, 0}
#define D(v) {AXD_D11_DELAY_US, 0, (v), 0, 0, 0, 0}
#define W(o,v) {AXD_D11_WRITE16, (o), (v), 0, 0, 0, 0}
#define WB(o,v) {AXD_D11_WRITE16, (o), (v), 0, BOOK_ZERO, 0, 0}
#define R(o,v) {AXD_D11_READ16, (o), 0, (v), 0, 0, 0}
#define RB(v) {AXD_D11_FULL_BARRIER, 0, 0, 0, RESULT, (v), 0}
#define S0(v,s) {AXD_D11_FULL_BARRIER, 0, 0, 0, RESULT | SLOT0, (v), (s)}
#define S1(v,s) {AXD_D11_FULL_BARRIER, 0, 0, 0, RESULT | SLOT1, (v), (s)}
#define N(a) (sizeof(a) / sizeof((a)[0]))
static const struct event read_direct[] = {SB, WB(0x3fc, 0x1234), R(0x3fe, 0xdeadbeefu), RB(0xbeef)};
static const struct event modify_direct[] = {
    SB, W(0x3fc, 0x1234), SB, R(0x3fe, 0xfeedabcdu), FB, WB(0x3fe, 0xa9a5)
}; /* mask0x03ff, value0x01a5: preserve high6 bits of sampleabcd. */
static const struct event analog[] = {SB, W(0x3e6, 0)};
static const struct event pulse[] = {
    R(0x7c0, 0xfeed12b4u), FB, SB, W(0x7c0, 0x12b4),
    SB, W(0x3fc, 0x1409), R(0x3fe, 0xdead4567u), FB,
    SB, W(0x3fe, 0xc567), SB, W(0x7c0, 0x1234), D(100)
};
static const struct event snapshot_one[] = {SB, WB(0x3fc, 0x645), R(0x3fe, 0x12345678), S0(0x5678, 0x278)};
static const struct event snapshot_two[] = {
    SB, WB(0x3fc, 0x845), R(0x3fe, 0xfeedabcdu), S1(0xabcd, 0x3cd),
    SB, WB(0x3fc, 0x645), R(0x3fe, 0x12345678), S0(0x5678, 0x278)
};
static const struct event restore_one[] = {
    SB, W(0x3fc, 0x645), SB, R(0x3fe, 0xdead1234u), FB, WB(0x3fe, 0x1155)
};
static const struct event restore_two[] = {
    SB, W(0x3fc, 0x845), SB, R(0x3fe, 0xfeedabcdu), FB, WB(0x3fe, 0xaaaa),
    SB, W(0x3fc, 0x645), SB, R(0x3fe, 0xdead1234u), FB, WB(0x3fe, 0x1155)
};
struct trace {
    const struct event *events;
    size_t count, cursor, fail_at, cancel_at;
    struct axd_phy_register *cancel_job;
    uint16_t bookkeeping, last_read, result, saved[2];
    uint8_t valid;
    unsigned snapshot_active, result_valid;
};
static struct axd_phy_context context_for(unsigned cores)
{
    const struct axd_phy_context c = {130, 1, 0xb, 51, 0x5aa5, {0x155, 0x2aa}, (uint8_t)cores, 3};
    return c;
}
static struct trace trace_for(const struct event *events, size_t count,
                              const struct axd_phy_context *c, enum axd_phy_command cmd)
{
    struct trace t;
    memset(&t, 0, sizeof(t)); t.events = events; t.count = count;
    t.fail_at = t.cancel_at = (size_t)-1;
    t.bookkeeping = c->bookkeeping; t.saved[0] = c->saved_tssi[0]; t.saved[1] = c->saved_tssi[1];
    t.valid = c->snapshot_valid;
    if (cmd == AXD_PHY_TSSI_SNAPSHOT && c->phy_core_count <= 2)
        t.snapshot_active = c->phy_core_count == 1 ? 1u : 3u;
    return t;
}
static int io(void *opaque, const struct axd_d11_operation *op, uint32_t *reply)
{
    struct trace *t = opaque;
    size_t i = t->cursor++;
    const struct event *e;
    assert(i < t->count); e = &t->events[i];
    assert(op->kind == e->kind && op->offset == e->offset && op->value == e->value);
    if (t->cancel_job != NULL && i == t->cancel_at)
        assert(axd_phy_register_cancel(t->cancel_job) == AXD_PHY_CANCELLED);
    if (i == t->fail_at) return -1;
    *reply = e->reply;
    return 0;
}
static void terminal(struct axd_phy_register *job, struct trace *t, enum axd_phy_phase phase)
{
    struct axd_phy_register before = *job;
    size_t cursor = t->cursor;
    assert(axd_phy_register_step(job, io, t) == phase);
    assert(axd_phy_register_step(job, NULL, NULL) == phase);
    assert(axd_phy_register_cancel(job) == phase);
    assert(t->cursor == cursor && memcmp(&before, job, sizeof(before)) == 0);
}
static void run(struct axd_phy_register *job, struct trace *t)
{
    size_t i;
    for (i = 0; i < t->count * 4 + 32; i++) {
        size_t before = t->cursor;
        enum axd_phy_phase phase = axd_phy_register_step(job, io, t);
        assert(t->cursor - before <= 1);
        if (i == 0) t->valid = (uint8_t)(t->valid & ~t->snapshot_active);
        if (t->cursor != before && before != t->fail_at) {
            const struct event *e = &t->events[before];
            if (e->kind == AXD_D11_READ16) t->last_read = (uint16_t)e->reply;
            if (e->effects & BOOK_ZERO) t->bookkeeping = 0;
            if (e->effects & RESULT) { t->result = e->result; t->result_valid = 1; }
            if (e->effects & SLOT0) { t->saved[0] = e->saved; t->valid |= 1; }
            if (e->effects & SLOT1) { t->saved[1] = e->saved; t->valid |= 2; }
        }
        assert(job->context.bookkeeping == t->bookkeeping && job->last_read == t->last_read);
        assert(job->result == t->result && job->result_valid == t->result_valid);
        assert(job->context.snapshot_valid == t->valid);
        assert(job->context.saved_tssi[0] == t->saved[0] && job->context.saved_tssi[1] == t->saved[1]);
        if (phase != AXD_PHY_RUNNING) return;
    }
    assert(0 && "PHY helper exceeded independent literal trace");
}
static struct axd_phy_register execute(enum axd_phy_command cmd, uint16_t selector,
    uint16_t mask, uint16_t value, const struct axd_phy_context *c,
    const struct event *events, size_t count, enum axd_phy_phase phase)
{
    struct axd_phy_register job;
    struct trace t = trace_for(events, count, c, cmd);
    assert(axd_phy_register_begin(&job, cmd, selector, mask, value, c) == 0);
    run(&job, &t);
    assert(job.phase == phase && job.error == AXD_PHY_NO_ERROR);
    assert(job.dependency == (phase == AXD_PHY_DEPENDENCY ? AXD_PHY_TABLE_READ : AXD_PHY_NO_DEPENDENCY));
    assert(t.cursor == count && job.attempted_ops == count && job.completed_ops == count);
    terminal(&job, &t, phase);
    return job;
}
static void sweep(enum axd_phy_command cmd, uint16_t selector, uint16_t mask, uint16_t value,
                   const struct axd_phy_context *c, const struct event *events, size_t count)
{
    size_t i; unsigned mode;
    for (i = 0; i < count; i++) for (mode = 0; mode < 3; mode++) {
        struct axd_phy_register job;
        struct trace t = trace_for(events, count, c, cmd);
        if (mode != 1) t.fail_at = i;
        if (mode != 0) { t.cancel_at = i; t.cancel_job = &job; }
        assert(axd_phy_register_begin(&job, cmd, selector, mask, value, c) == 0);
        run(&job, &t);
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1 && job.completed_ops == i + (mode == 1));
        assert(job.phase == (mode == 1 ? AXD_PHY_CANCELLED : AXD_PHY_FAILED));
        assert(job.error == (mode == 1 ? AXD_PHY_NO_ERROR : AXD_PHY_IO_ERROR));
        terminal(&job, &t, job.phase);
    }
}
static void literal_paths(void)
{
    struct axd_phy_context c = context_for(1);
    struct axd_phy_register job;
    job = execute(AXD_PHY_READ, 0x1234, 0, 0, &c, read_direct, N(read_direct), AXD_PHY_TRACE_COMPLETE);
    assert(job.result == 0xbeef && job.context.bookkeeping == 0);
    sweep(AXD_PHY_READ, 0x1234, 0, 0, &c, read_direct, N(read_direct));
    job = execute(AXD_PHY_MODIFY, 0x1234, 0x3ff, 0x1a5, &c, modify_direct, N(modify_direct), AXD_PHY_TRACE_COMPLETE);
    assert(job.result == 0 && job.last_read == 0xabcd && job.context.bookkeeping == 0);
    sweep(AXD_PHY_MODIFY, 0x1234, 0x3ff, 0x1a5, &c, modify_direct, N(modify_direct));
    job = execute(AXD_PHY_ANALOG_RESET, 0, 0, 0, &c, analog, N(analog), AXD_PHY_TRACE_COMPLETE);
    assert(job.context.bookkeeping == 0x5aa5);
    sweep(AXD_PHY_ANALOG_RESET, 0, 0, 0, &c, analog, N(analog));
    job = execute(AXD_PHY_RESET_PULSE, 0, 0, 0, &c, pulse, N(pulse), AXD_PHY_TRACE_COMPLETE);
    assert(job.pulse_original == 0x12b4 && job.last_read == 0x4567 && job.context.bookkeeping == 0x5aa5);
    sweep(AXD_PHY_RESET_PULSE, 0, 0, 0, &c, pulse, N(pulse));
    job = execute(AXD_PHY_TSSI_SNAPSHOT, 0, 0, 0, &c, snapshot_one, N(snapshot_one), AXD_PHY_TRACE_COMPLETE);
    assert(job.context.snapshot_valid == 3 && job.context.saved_tssi[0] == 0x278 && job.context.saved_tssi[1] == 0x2aa);
    sweep(AXD_PHY_TSSI_SNAPSHOT, 0, 0, 0, &c, snapshot_one, N(snapshot_one));
    execute(AXD_PHY_TSSI_RESTORE, 0, 0, 0, &c, restore_one, N(restore_one), AXD_PHY_TRACE_COMPLETE);
    sweep(AXD_PHY_TSSI_RESTORE, 0, 0, 0, &c, restore_one, N(restore_one));
    c = context_for(2);
    job = execute(AXD_PHY_TSSI_SNAPSHOT, 0, 0, 0, &c, snapshot_two, N(snapshot_two), AXD_PHY_TRACE_COMPLETE);
    assert(job.context.saved_tssi[0] == 0x278 && job.context.saved_tssi[1] == 0x3cd && job.context.snapshot_valid == 3);
    sweep(AXD_PHY_TSSI_SNAPSHOT, 0, 0, 0, &c, snapshot_two, N(snapshot_two));
    execute(AXD_PHY_TSSI_RESTORE, 0, 0, 0, &c, restore_two, N(restore_two), AXD_PHY_TRACE_COMPLETE);
    sweep(AXD_PHY_TSSI_RESTORE, 0, 0, 0, &c, restore_two, N(restore_two));
}
static void boundaries_and_alias(void)
{
    struct axd_phy_context c = context_for(2);
    struct axd_phy_register job;
    struct event read[N(read_direct)], modify[N(modify_direct)];
    struct trace t;
    unsigned cores;
    memcpy(read, read_direct, sizeof(read)); read[1].value = 0xbfff;
    execute(AXD_PHY_READ, 0xbfff, 0, 0, &c, read, N(read), AXD_PHY_TRACE_COMPLETE);
    execute(AXD_PHY_READ, 0xc000, 0, 0, &c, NULL, 0, AXD_PHY_DEPENDENCY);
    execute(AXD_PHY_READ, 0xffff, 0, 0, &c, NULL, 0, AXD_PHY_DEPENDENCY);
    memcpy(modify, modify_direct, sizeof(modify)); modify[1].value = 0xc000;
    execute(AXD_PHY_MODIFY, 0xc000, 0x3ff, 0x1a5, &c, modify, N(modify), AXD_PHY_TRACE_COMPLETE);
    execute(AXD_PHY_MODIFY, 0xffff, 0xffff, 0xffff, &c, NULL, 0, AXD_PHY_TRACE_COMPLETE);
    {
        const uint16_t masks[] = {0, 0xffff, 0x000f};
        const uint16_t values[] = {0xffff, 0x1234, 0xfff1};
        const uint16_t writes[] = {0xabcd, 0x1234, 0xabc1};
        unsigned i;
        for (i = 0; i < N(masks); i++) {
            memcpy(modify, modify_direct, sizeof(modify)); modify[5].value = writes[i];
            execute(AXD_PHY_MODIFY, 0x1234, masks[i], values[i], &c, modify, N(modify), AXD_PHY_TRACE_COMPLETE);
            sweep(AXD_PHY_MODIFY, 0x1234, masks[i], values[i], &c, modify, N(modify));
        }
    }
    {
        struct event asserted[N(pulse)];
        memcpy(asserted, pulse, sizeof(asserted)); asserted[0].reply = 0xfeed1234u;
        job = execute(AXD_PHY_RESET_PULSE, 0, 0, 0, &c, asserted, N(asserted), AXD_PHY_TRACE_COMPLETE);
        assert(job.pulse_original == 0x1234); /* Set then clear bit80 from original A. */
    }
    c = context_for(1); c.snapshot_valid = 1; c.saved_tssi[0] = 0xfd55;
    execute(AXD_PHY_TSSI_RESTORE, 0, 0, 0, &c, restore_one, N(restore_one), AXD_PHY_TRACE_COMPLETE);
    for (cores = 3; cores <= 4; cores++) {
        c = context_for(cores);
        execute(AXD_PHY_TSSI_SNAPSHOT, 0, 0, 0, &c, NULL, 0, AXD_PHY_DEPENDENCY);
        execute(AXD_PHY_TSSI_RESTORE, 0, 0, 0, &c, NULL, 0, AXD_PHY_DEPENDENCY);
    }
    c = context_for(2);
    job = execute(AXD_PHY_TSSI_SNAPSHOT, 0, 0, 0, &c, snapshot_two, N(snapshot_two), AXD_PHY_TRACE_COMPLETE);
    assert(axd_phy_register_begin(&job, AXD_PHY_TSSI_RESTORE, 0, 0, 0, &job.context) == 0);
    assert(job.context.phy_core_count == 2 && job.context.snapshot_valid == 3);
    assert(job.context.saved_tssi[0] == 0x278 && job.context.saved_tssi[1] == 0x3cd && job.completed_ops == 0);
    {
        static const struct event restored[] = {
            SB, W(0x3fc, 0x845), SB, R(0x3fe, 0xffff0000u), FB, WB(0x3fe, 0x3cd),
            SB, W(0x3fc, 0x645), SB, R(0x3fe, 0x1234f000u), FB, WB(0x3fe, 0xf278)
        };
        t = trace_for(restored, N(restored), &job.context, AXD_PHY_TSSI_RESTORE);
        run(&job, &t);
        assert(job.phase == AXD_PHY_TRACE_COMPLETE && job.completed_ops == N(restored));
        terminal(&job, &t, AXD_PHY_TRACE_COMPLETE);
    }
}
static void validation(void)
{
    struct axd_phy_context c = context_for(1);
    struct axd_phy_register job, empty = {0};
    struct trace t = trace_for(NULL, 0, &c, AXD_PHY_READ);
    unsigned i;
    assert(axd_phy_register_begin(NULL, AXD_PHY_READ, 0, 0, 0, &c) != 0);
    assert(axd_phy_register_begin(&job, AXD_PHY_READ, 0, 0, 0, NULL) != 0);
    terminal(&job, &t, AXD_PHY_FAILED);
    for (i = 0; i < 6; i++) {
        c = context_for(1);
        if (i == 0) c.core = 129;
        if (i == 1) c.minor = 2;
        if (i == 2) c.phy_type = 0xc;
        if (i == 3) c.phy_revision = 50;
        if (i == 4) c.phy_core_count = 0;
        if (i == 5) c.phy_core_count = 5;
        assert(axd_phy_register_begin(&job, AXD_PHY_READ, 0, 0, 0, &c) != 0);
        assert(job.attempted_ops == 0);
        terminal(&job, &t, AXD_PHY_FAILED);
    }
    c = context_for(1); c.snapshot_valid = 4;
    assert(axd_phy_register_begin(&job, AXD_PHY_READ, 0, 0, 0, &c) != 0);
    assert(job.error == AXD_PHY_BAD_ARGUMENT && job.attempted_ops == 0);
    terminal(&job, &t, AXD_PHY_FAILED);
    c = context_for(1);
    assert(axd_phy_register_begin(&job, (enum axd_phy_command)0, 0, 0, 0, &c) != 0);
    assert(axd_phy_register_begin(&job, (enum axd_phy_command)7, 0, 0, 0, &c) != 0);
    assert(axd_phy_register_begin(&job, AXD_PHY_READ, 0, 1, 0, &c) != 0);
    assert(axd_phy_register_begin(&job, AXD_PHY_READ, 0, 0, 1, &c) != 0);
    for (i = AXD_PHY_ANALOG_RESET; i <= AXD_PHY_TSSI_RESTORE; i++) {
        assert(axd_phy_register_begin(&job, (enum axd_phy_command)i, 1, 0, 0, &c) != 0);
        assert(axd_phy_register_begin(&job, (enum axd_phy_command)i, 0, 1, 0, &c) != 0);
        assert(axd_phy_register_begin(&job, (enum axd_phy_command)i, 0, 0, 1, &c) != 0);
    }
    for (i = 0; i < 3; i++) {
        c = context_for(i == 0 ? 1 : 2);
        c.snapshot_valid = i == 0 ? 2 : (i == 1 ? 1 : 2);
        assert(axd_phy_register_begin(&job, AXD_PHY_TSSI_RESTORE, 0, 0, 0, &c) != 0);
        assert(job.error == AXD_PHY_MISSING_SNAPSHOT && job.attempted_ops == 0);
        terminal(&job, &t, AXD_PHY_FAILED);
    }
    c = context_for(1); c.minor = 0;
    assert(axd_phy_register_begin(&job, AXD_PHY_READ, 0, 0, 0, &c) == 0);
    c.bookkeeping = 0; assert(job.context.bookkeeping == 0x5aa5);
    for (i = 0; i < 8 && job.phase == AXD_PHY_RUNNING; i++) axd_phy_register_step(&job, NULL, NULL);
    assert(job.phase == AXD_PHY_FAILED && job.error == AXD_PHY_BAD_ARGUMENT && job.completed_ops == 0);
    terminal(&job, &t, AXD_PHY_FAILED);
    c = context_for(2);
    assert(axd_phy_register_begin(&job, AXD_PHY_TSSI_SNAPSHOT, 0, 0, 0, &c) == 0);
    assert(axd_phy_register_cancel(&job) == AXD_PHY_CANCELLED);
    assert(job.context.snapshot_valid == 3); /* ENTRY has not run yet. */
    terminal(&job, &t, AXD_PHY_CANCELLED);
    assert(axd_phy_register_step(NULL, io, &t) == AXD_PHY_INVALID);
    assert(axd_phy_register_cancel(NULL) == AXD_PHY_INVALID);
    terminal(&empty, &t, AXD_PHY_INVALID);
}
int main(void)
{
    literal_paths(); boundaries_and_alias(); validation();
    puts("PHY register: independent read/modify/reset/TSSI traces, software commit boundaries and callback exits passed");
    return 0;
}
