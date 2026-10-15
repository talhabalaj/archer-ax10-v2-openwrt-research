/* SPDX-License-Identifier: MIT */
#include "../driver/phy_tssi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Original table event builders retained from the independently derived
 * table-wrapper oracle. TSSI slot commit rules below are a separate oracle. */
enum effect { BOOK_ZERO = 1, PRE_COUNT = 2, LOW = 4, WORD = 8, GUARD_SET = 16 };
struct event {
    enum axd_d11_kind kind;
    uint32_t offset, value, reply;
    unsigned effect;
    uint16_t counter;
};
#define SB {AXD_D11_STORE_BARRIER, 0, 0, 0, 0, 0}
#define FB {AXD_D11_FULL_BARRIER, 0, 0, 0, 0, 0}
#define W16(o,v) {AXD_D11_WRITE16, (o), (v), 0, 0, 0}
#define WB(o,v) {AXD_D11_WRITE16, (o), (v), 0, BOOK_ZERO, 0}
#define W32(o,v) {AXD_D11_WRITE32, (o), (v), 0, 0, 0}
#define R16(o,v) {AXD_D11_READ16, (o), 0, (v), 0, 0}
#define R32(o,v) {AXD_D11_READ32, (o), 0, (v), 0, 0}
#define N(a) (sizeof(a) / sizeof((a)[0]))
static const struct event inactive[] = {R32(0x120, 2), FB}; /* Bit1 is not the wrapper's bit0. */
static const struct event active[] = {R32(0x120, 3), FB};
static const struct event guard_set[] = {
    SB, WB(0x3fc, 0x19e), R16(0x3fe, 0xfeed0002u), FB
};
static const struct event guard_clear[] = {
    SB, WB(0x3fc, 0x19e), R16(0x3fe, 0xdead0040u), FB
};
static const struct event guard_enable[] = {
    SB, W16(0x3fc, 0x19e), SB, R16(0x3fe, 0xabcd00a0u), FB,
    {AXD_D11_WRITE16, 0x3fe, 0xa2, 0, BOOK_ZERO | GUARD_SET, 0}
};
static const struct event guard_disable[] = {
    SB, W16(0x3fc, 0x19e), SB, R16(0x3fe, 0xfeed005bu), FB, WB(0x3fe, 0x59)
}; /* Fresh sample5b, not original40 or set samplea0. */
static const struct event halves[] = {
    SB, WB(0x3fc, 0xf), R16(0x3fe, 0xfeedbeefu),
    {AXD_D11_FULL_BARRIER, 0, 0, 0, LOW, 0},
    SB, WB(0x3fc, 0x10), R16(0x3fe, 0xdead1234u),
    {AXD_D11_FULL_BARRIER, 0, 0, 0, WORD, 0}
};
static const struct event suspend_main[] = {
    R32(0x120, 3), FB, R32(0x120, 3), FB, R32(0x128, 0), FB,
    SB, W32(0x120, 0x04000002), R32(0x128, 1), FB, R32(0x128, 1), FB,
    R32(0x120, 2), FB
};
static const struct event enable_main[] = {
    R32(0x120, 2), FB, SB, W32(0x120, 0x04000003), SB, W32(0x128, 1),
    R32(0x120, 3), FB, R32(0x128, 0), FB,
    SB, W32(0x160, 0x10010), R32(0x160, 0xdeadbeef), FB, R16(0x164, 2), FB
};
struct script { struct event events[192]; size_t count, suspend_start, enable_start; unsigned mac_active; };
static void append(struct script *s, const struct event *e, size_t count)
{
    assert(s->count + count <= N(s->events));
    memcpy(s->events + s->count, e, count * sizeof(*e)); s->count += count;
}
static void packed(struct script *s, uint32_t word, unsigned prelude,
                    uint16_t counter, unsigned flush)
{
    const struct event version[] = {R16(0x3e0, 0xabcd), FB};
    struct event write[] = {SB, W32(0x3fc, 0)};
    size_t first = s->count;
    if (flush) append(s, version, N(version));
    write[1].value = word; append(s, write, N(write));
    if (prelude) { s->events[first].effect |= PRE_COUNT; s->events[first].counter = counter; }
}
static struct script script_for(uint32_t bus, uint16_t threshold, unsigned clear_guard,
                                 unsigned mac_active, uint32_t packed_index)
{
    struct script s;
    memset(&s, 0, sizeof(s));
    append(&s, mac_active ? active : inactive, 2);
    if (mac_active) { s.mac_active = 1; s.suspend_start = s.count; append(&s, suspend_main, N(suspend_main)); }
    append(&s, clear_guard ? guard_clear : guard_set, 4);
    if (clear_guard) append(&s, guard_enable, N(guard_enable));
    /* Mandatory guard READ resets the counter. Literal two-write consequences:
     * thresholds0/1:0,0; threshold2:1,0; thresholds>=3:1,2.
     */
    packed(&s, 0x0029000d, bus == 1, threshold <= 1 ? 0 : 1, bus == 1 && threshold <= 1);
    packed(&s, packed_index, bus == 1, threshold <= 2 ? 0 : 2, bus == 1 && threshold <= 2);
    append(&s, halves, N(halves));
    if (clear_guard) append(&s, guard_disable, N(guard_disable));
    if (mac_active) { s.enable_start = s.count; append(&s, enable_main, N(enable_main)); }
    return s;
}
/* Direct snapshot SY commits; table slots commit only at wrapper completion. */
#define DIRECT0 32u
#define DIRECT1 64u
static const struct event direct_one[] = {
    SB, WB(0x3fc, 0x645), R16(0x3fe, 0xdeadf123u),
    {AXD_D11_FULL_BARRIER, 0, 0, 0, DIRECT0, 0x123}
};
static const struct event direct_two[] = {
    SB, WB(0x3fc, 0x845), R16(0x3fe, 0xfeed0a55u),
    {AXD_D11_FULL_BARRIER, 0, 0, 0, DIRECT1, 0x255}
};
static const struct event restore_one[] = {
    SB, W16(0x3fc, 0x645), SB, R16(0x3fe, 0xdead1234u), FB, WB(0x3fe, 0x1111)
};
static const struct event restore_two[] = {
    SB, W16(0x3fc, 0x845), SB, R16(0x3fe, 0xfeedabcdu), FB, WB(0x3fe, 0xaa22)
};
struct segment { size_t end; unsigned slot; uint16_t saved; };
struct plan { struct script trace; struct segment tables[2]; unsigned table_count; };
static struct plan plan_for(enum axd_tssi_command command, unsigned cores,
                             uint32_t bus, uint16_t threshold, unsigned clear, unsigned mac_active)
{
    struct plan p;
    unsigned slot;
    memset(&p, 0, sizeof(p));
    if (command == AXD_TSSI_SNAPSHOT && cores >= 3) {
        for (slot = cores - 1; slot >= 2; slot--) {
            struct script part = script_for(bus, threshold, clear, mac_active, 0xffff000e);
            unsigned second = p.table_count;
            size_t i;
            if (second) {
                for (i = 0; i < part.count; i++) {
                    if (part.events[i].effect & LOW) part.events[i - 1].reply = 0xfeed5678u;
                    if (part.events[i].effect & WORD) part.events[i - 1].reply = 0xdeadfedcu;
                }
            }
            append(&p.trace, part.events, part.count);
            p.tables[p.table_count].end = p.trace.count;
            p.tables[p.table_count].slot = slot;
            p.tables[p.table_count].saved = second ? 0x278 : 0x2ef; /* beef &3ff=2ef */
            p.table_count++;
        }
    }
    if (cores >= 2) append(&p.trace, command == AXD_TSSI_SNAPSHOT ? direct_two : restore_two,
                           command == AXD_TSSI_SNAPSHOT ? N(direct_two) : N(restore_two));
    append(&p.trace, command == AXD_TSSI_SNAPSHOT ? direct_one : restore_one,
           command == AXD_TSSI_SNAPSHOT ? N(direct_one) : N(restore_one));
    return p;
}
struct trace {
    const struct plan *plan;
    size_t cursor, fail_at, cancel_at;
    struct axd_phy_tssi *cancel_job;
    uint16_t counter, saved[4];
    uint8_t valid;
    unsigned invalidation, committed_tables;
};
static struct axd_tssi_context context_for(unsigned cores, uint32_t bus, uint16_t threshold)
{
    struct axd_tssi_context c;
    memset(&c, 0, sizeof(c));
    c.hardware.phy.core = 130; c.hardware.phy.minor = 1;
    c.hardware.phy.phy_type = 0xb; c.hardware.phy.phy_revision = 51;
    c.hardware.phy.bookkeeping = 0x5aa5; c.hardware.phy.phy_core_count = (uint8_t)cores;
    c.hardware.phy.saved_tssi[0] = 0x155; c.hardware.phy.saved_tssi[1] = 0x2aa;
    c.hardware.phy.snapshot_valid = 3;
    c.hardware.mac.core = 130; c.hardware.mac.minor = 1; c.hardware.mac.chip_id = 63178;
    c.hardware.mac.main_control = 3; c.hardware.mac.wake_reasons = 8; c.hardware.mac.band_present = 1;
    c.hardware.shared_bus_word = bus; c.hardware.write_threshold = threshold;
    c.saved[0] = 0x111; c.saved[1] = 0x222; c.saved[2] = 0x333; c.saved[3] = 0x44; c.valid = 15;
    return c;
}
static struct trace trace_for(const struct plan *p, const struct axd_tssi_context *c,
                              enum axd_tssi_command cmd)
{
    struct trace t;
    memset(&t, 0, sizeof(t)); t.plan = p; t.fail_at = t.cancel_at = (size_t)-1;
    t.counter = c->hardware.phy.bookkeeping; memcpy(t.saved, c->saved, sizeof(t.saved)); t.valid = c->valid;
    t.invalidation = cmd == AXD_TSSI_SNAPSHOT ? (1u << c->hardware.phy.phy_core_count) - 1u : 0u;
    return t;
}
static int io(void *opaque, const struct axd_d11_operation *op, uint32_t *reply)
{
    struct trace *t = opaque;
    size_t i = t->cursor++;
    const struct event *e;
    assert(i < t->plan->trace.count); e = &t->plan->trace.events[i];
    assert(op->kind == e->kind && op->offset == e->offset && op->value == e->value);
    if (e->effect & PRE_COUNT) t->counter = e->counter;
    if (t->cancel_job != NULL && i == t->cancel_at)
        assert(axd_phy_tssi_cancel(t->cancel_job) == AXD_TSSI_CANCELLED);
    if (i == t->fail_at) return -1;
    *reply = e->reply; return 0;
}
static void terminal(struct axd_phy_tssi *job, struct trace *t, enum axd_tssi_phase phase)
{
    struct axd_phy_tssi before = *job;
    size_t cursor = t->cursor;
    assert(axd_phy_tssi_step(job, io, t) == phase);
    assert(axd_phy_tssi_step(job, NULL, NULL) == phase);
    assert(axd_phy_tssi_cancel(job) == phase);
    assert(t->cursor == cursor && memcmp(&before, job, sizeof(before)) == 0);
}
static void run(struct axd_phy_tssi *job, struct trace *t)
{
    size_t i;
    for (i = 0; i < t->plan->trace.count * 12 + 128; i++) {
        size_t before = t->cursor;
        enum axd_tssi_phase phase = axd_phy_tssi_step(job, io, t);
        assert(t->cursor - before <= 1);
        if (i == 0) t->valid = (uint8_t)(t->valid & ~t->invalidation);
        if (t->cursor != before && before != t->fail_at) {
            const struct event *e = &t->plan->trace.events[before];
            if (e->effect & BOOK_ZERO) t->counter = 0;
            if (e->effect & DIRECT0) { t->saved[0] = e->counter; t->valid |= 1; }
            if (e->effect & DIRECT1) { t->saved[1] = e->counter; t->valid |= 2; }
        }
        if (before == t->cursor && t->cursor < t->plan->trace.count &&
            (t->plan->trace.events[t->cursor].effect & PRE_COUNT) &&
            job->context.hardware.phy.bookkeeping == t->plan->trace.events[t->cursor].counter)
            t->counter = t->plan->trace.events[t->cursor].counter;
        if (t->committed_tables < t->plan->table_count) {
            const struct segment *segment = &t->plan->tables[t->committed_tables];
            if (t->cursor == segment->end && job->table_job.phase == AXD_TABLE_TRACE_COMPLETE &&
                phase != AXD_TSSI_FAILED && phase != AXD_TSSI_CANCELLED && phase != AXD_TSSI_DEPENDENCY) {
                t->saved[segment->slot] = segment->saved;
                t->valid |= (uint8_t)(1u << segment->slot); t->committed_tables++;
            }
        }
        assert(job->context.hardware.phy.bookkeeping == t->counter);
        assert(job->context.valid == t->valid && memcmp(job->context.saved, t->saved, sizeof(t->saved)) == 0);
        assert(job->context.hardware.phy.saved_tssi[0] == 0x155 && job->context.hardware.phy.saved_tssi[1] == 0x2aa);
        assert(job->context.hardware.phy.snapshot_valid == 3); /* Separate callback buffer. */
        if (phase != AXD_TSSI_RUNNING) return;
    }
    assert(0 && "TSSI exceeded independent trace bound");
}
static struct axd_phy_tssi execute(enum axd_tssi_command cmd,
    const struct axd_tssi_context *c, const struct plan *p, enum axd_tssi_phase phase)
{
    struct axd_phy_tssi job;
    struct trace t = trace_for(p, c, cmd);
    assert(axd_phy_tssi_begin(&job, cmd, c) == 0); run(&job, &t);
    assert(job.phase == phase && job.error == AXD_TSSI_NO_ERROR);
    assert(t.cursor == p->trace.count && job.attempted_ops == t.cursor && job.completed_ops == t.cursor);
    terminal(&job, &t, phase); return job;
}
static void sweep(enum axd_tssi_command cmd, const struct axd_tssi_context *c, const struct plan *p)
{
    size_t i; unsigned mode;
    for (i = 0; i < p->trace.count; i++) for (mode = 0; mode < 3; mode++) {
        struct axd_phy_tssi job;
        struct trace t = trace_for(p, c, cmd);
        if (mode != 1) t.fail_at = i;
        if (mode != 0) { t.cancel_at = i; t.cancel_job = &job; }
        assert(axd_phy_tssi_begin(&job, cmd, c) == 0); run(&job, &t);
        assert(job.phase == (mode == 1 ? AXD_TSSI_CANCELLED : AXD_TSSI_FAILED));
        assert(job.error == (mode == 1 ? AXD_TSSI_NO_ERROR : AXD_TSSI_IO_ERROR));
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1 && job.completed_ops == i + (mode == 1));
        terminal(&job, &t, job.phase);
    }
}
static void normal_and_partial(void)
{
    unsigned cores, clear;
    for (cores = 1; cores <= 4; cores++) for (clear = 0; clear < 2; clear++) {
        uint32_t bus = clear ? 1u : 0u;
        struct axd_tssi_context c = context_for(cores, bus, 1);
        struct plan p = plan_for(AXD_TSSI_SNAPSHOT, cores, bus, 1, clear, 0);
        struct axd_phy_tssi job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_TSSI_TRACE_COMPLETE);
        assert(job.context.saved[0] == 0x123 && job.context.valid == 15);
        if (cores >= 2) assert(job.context.saved[1] == 0x255);
        if (cores == 3) assert(job.context.saved[2] == 0x2ef && job.context.saved[3] == 0x44);
        if (cores == 4) assert(job.context.saved[3] == 0x2ef && job.context.saved[2] == 0x278);
        sweep(AXD_TSSI_SNAPSHOT, &c, &p);
        p = plan_for(AXD_TSSI_RESTORE, cores, bus, 1, clear, 0);
        job = execute(AXD_TSSI_RESTORE, &c, &p, AXD_TSSI_TRACE_COMPLETE);
        assert(memcmp(job.context.saved, c.saved, sizeof(c.saved)) == 0 && job.context.valid == 15);
        assert(p.trace.count == (cores == 1 ? 6u : 12u)); /* Extra ffff modifies generate no events. */
        sweep(AXD_TSSI_RESTORE, &c, &p);
    }
}
static void table_mac_dependencies(void)
{
    struct axd_tssi_context c = context_for(3, 1, 2);
    struct plan p = plan_for(AXD_TSSI_SNAPSHOT, 3, 1, 2, 1, 1);
    struct axd_phy_tssi job;
    c.hardware.mac_context_valid = 1;
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_TSSI_TRACE_COMPLETE);
    assert(job.context.hardware.mac.main_depth == 0 && job.context.hardware.mac.wake_reasons == 8);
    sweep(AXD_TSSI_SNAPSHOT, &c, &p);
    /* Table's complete data word cannot commit slot2 before MAC enable finishes. */
    {
        struct axd_tssi_context four = context_for(4, 1, 2);
        struct plan repeated = plan_for(AXD_TSSI_SNAPSHOT, 4, 1, 2, 1, 1);
        four.hardware.mac_context_valid = 1;
        job = execute(AXD_TSSI_SNAPSHOT, &four, &repeated, AXD_TSSI_TRACE_COMPLETE);
        assert(job.context.saved[3] == 0x2ef && job.context.saved[2] == 0x278);
        assert(job.context.hardware.mac.main_depth == 0 && job.context.hardware.mac.wake_reasons == 8);
        sweep(AXD_TSSI_SNAPSHOT, &four, &repeated);
    }
    c.hardware.mac.wake_reasons = 4;
    p.trace.count = p.tables[0].end; /* No following direct slots on dependency. */
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_TSSI_DEPENDENCY);
    assert(job.table_job.word_valid && !(job.context.valid & 4) && job.context.saved[2] == 0x333);
    assert(job.pending_callouts == AXD_PSM_WAKE_CONTROL && job.context.hardware.mac.wake_reasons == 0);
    c.hardware.mac_context_valid = 0;
    memset(&p, 0, sizeof(p)); append(&p.trace, active, N(active));
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_TSSI_DEPENDENCY);
    assert(job.table_job.dependency == AXD_TABLE_MAC_CONTEXT && !(job.context.valid & 7));
}
static void zero_slots_and_semantic_failure(void)
{
    struct axd_tssi_context c = context_for(1, 0, 0);
    struct plan p = plan_for(AXD_TSSI_SNAPSHOT, 1, 0, 0, 0, 0);
    struct axd_phy_tssi job;
    struct trace t;
    unsigned cores;
    size_t i;
    c.valid = 0;
    p.trace.events[2].reply = 0; p.trace.events[3].counter = 0;
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_TSSI_TRACE_COMPLETE);
    assert(job.context.valid == 1 && job.context.saved[0] == 0 && job.completed_ops == 4);
    sweep(AXD_TSSI_SNAPSHOT, &c, &p);
    c = context_for(3, 0, 0); p = plan_for(AXD_TSSI_SNAPSHOT, 3, 0, 0, 1, 0);
    for (i = 0; i < p.tables[0].end; i++)
        if ((p.trace.events[i].effect & LOW) || (p.trace.events[i].effect & WORD)) p.trace.events[i - 1].reply = 0;
    p.tables[0].saved = 0;
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_TSSI_TRACE_COMPLETE);
    assert((job.context.valid & 4) && job.context.saved[2] == 0);
    sweep(AXD_TSSI_SNAPSHOT, &c, &p);
    for (cores = 1; cores <= 4; cores++) {
        c = context_for(cores, 0, 0); c.valid = (uint8_t)((1u << cores) - 1u);
        p = plan_for(AXD_TSSI_RESTORE, cores, 0, 0, 0, 0);
        job = execute(AXD_TSSI_RESTORE, &c, &p, AXD_TSSI_TRACE_COMPLETE);
        assert(job.context.valid == c.valid); /* Inactive bits need not be valid. */
    }
    c = context_for(3, 0, 0); c.hardware.mac_context_valid = 1; c.hardware.mac.main_depth = 1;
    memset(&p, 0, sizeof(p)); append(&p.trace, active, N(active)); append(&p.trace, suspend_main, 2);
    t = trace_for(&p, &c, AXD_TSSI_SNAPSHOT);
    assert(axd_phy_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) == 0); run(&job, &t);
    assert(job.phase == AXD_TSSI_FAILED && job.error == AXD_TSSI_TABLE_ERROR);
    assert(job.table_job.error == AXD_TABLE_MAC_ERROR && job.pending_callouts == AXD_PSM_FATAL);
    assert(job.context.hardware.mac.main_depth == 2 && job.context.valid == 8);
    assert(job.context.saved[2] == 0x333 && !job.table_job.word_valid && job.completed_ops == 4);
    terminal(&job, &t, AXD_TSSI_FAILED);
}

static void alias_and_validation(void)
{
    struct axd_tssi_context c = context_for(4, 0, 3);
    struct plan p = plan_for(AXD_TSSI_SNAPSHOT, 4, 0, 3, 0, 0);
    struct axd_phy_tssi job, empty = {0};
    struct trace t;
    unsigned i;
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_TSSI_TRACE_COMPLETE);
    assert(axd_phy_tssi_begin(&job, AXD_TSSI_RESTORE, &job.context) == 0);
    assert(job.context.saved[3] == 0x2ef && job.context.saved[2] == 0x278 && job.context.valid == 15);
    c = job.context; p = plan_for(AXD_TSSI_RESTORE, 4, 0, 3, 0, 0);
    p.trace.events[5].value = 0xaa55; p.trace.events[11].value = 0x1123;
    t = trace_for(&p, &c, AXD_TSSI_RESTORE); run(&job, &t);
    assert(job.phase == AXD_TSSI_TRACE_COMPLETE && job.completed_ops == 12);
    terminal(&job, &t, AXD_TSSI_TRACE_COMPLETE);
    c = context_for(4, 0, 0); t = trace_for(&p, &c, AXD_TSSI_RESTORE);
    assert(axd_phy_tssi_begin(NULL, AXD_TSSI_SNAPSHOT, &c) != 0);
    assert(axd_phy_tssi_begin(&job, AXD_TSSI_SNAPSHOT, NULL) != 0);
    terminal(&job, &t, AXD_TSSI_FAILED);
    c.valid = 16;
    assert(axd_phy_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) != 0);
    c = context_for(4, 0, 0);
    assert(axd_phy_tssi_begin(&job, (enum axd_tssi_command)0, &c) != 0);
    assert(axd_phy_tssi_begin(&job, (enum axd_tssi_command)3, &c) != 0);
    for (i = 1; i <= 4; i++) {
        c = context_for(i, 0, 0); c.valid = (uint8_t)(((1u << i) - 1u) & ~(1u << (i - 1u)));
        assert(axd_phy_tssi_begin(&job, AXD_TSSI_RESTORE, &c) != 0);
        assert(job.error == AXD_TSSI_MISSING_SNAPSHOT && job.attempted_ops == 0);
        terminal(&job, &t, AXD_TSSI_FAILED);
    }
    c = context_for(2, 0, 0); c.hardware.phy.phy_revision = 50;
    assert(axd_phy_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) != 0 && job.attempted_ops == 0);
    terminal(&job, &t, AXD_TSSI_FAILED);
    c = context_for(4, 0, 0);
    assert(axd_phy_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) == 0);
    assert(axd_phy_tssi_cancel(&job) == AXD_TSSI_CANCELLED && job.context.valid == 15);
    terminal(&job, &t, AXD_TSSI_CANCELLED);
    c = context_for(1, 0, 0);
    assert(axd_phy_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) == 0);
    for (i = 0; i < 16 && job.phase == AXD_TSSI_RUNNING; i++) axd_phy_tssi_step(&job, NULL, NULL);
    assert(job.phase == AXD_TSSI_FAILED && job.error == AXD_TSSI_BAD_ARGUMENT && job.attempted_ops == 0);
    assert(job.context.valid == 14); /* ENTRY invalidation remains on later failure. */
    terminal(&job, &t, AXD_TSSI_FAILED);
    assert(axd_phy_tssi_step(NULL, io, &t) == AXD_TSSI_INVALID);
    assert(axd_phy_tssi_cancel(NULL) == AXD_TSSI_INVALID);
    terminal(&empty, &t, AXD_TSSI_INVALID);
}
int main(void)
{
    normal_and_partial(); table_mac_dependencies(); zero_slots_and_semantic_failure(); alias_and_validation();
    puts("PHY TSSI: independent four-slot/table/direct/restore traces and partial-commit exits passed");
    return 0;
}
