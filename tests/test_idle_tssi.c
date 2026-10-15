/* SPDX-License-Identifier: MIT */
#include "../driver/idle_tssi.h"
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
static const struct event mac_inactive[] = {R32(0x120, 0), FB};
static const struct event enter_no_band[] = {SB, W32(0x120, 0x04000003)};
struct idle_plan {
    struct plan callback;
    size_t callback_start, callback_end, enable_start;
    unsigned active, force_enter;
};
static struct idle_plan idle_plan_for(enum axd_tssi_command cmd, unsigned cores,
    uint32_t bus, uint16_t threshold, unsigned clear, unsigned active_mac,
    unsigned force_enter, unsigned stop_at_exit)
{
    struct idle_plan p;
    struct plan callback = plan_for(cmd, cores, bus, threshold, clear, 0);
    unsigned i;
    memset(&p, 0, sizeof(p)); p.active = active_mac; p.force_enter = force_enter;
    append(&p.callback.trace, active_mac ? suspend_main : mac_inactive,
           active_mac ? N(suspend_main) : N(mac_inactive));
    if (force_enter) append(&p.callback.trace, enter_no_band, N(enter_no_band));
    p.callback_start = p.callback.trace.count;
    for (i = 0; i < callback.table_count; i++) {
        p.callback.tables[i] = callback.tables[i]; p.callback.tables[i].end += p.callback_start;
    }
    p.callback.table_count = callback.table_count;
    append(&p.callback.trace, callback.trace.events, callback.trace.count);
    p.callback_end = p.callback.trace.count; p.enable_start = p.callback.trace.count;
    if (!stop_at_exit)
        append(&p.callback.trace, active_mac ? enable_main : mac_inactive,
               active_mac ? N(enable_main) : N(mac_inactive));
    return p;
}
struct trace {
    const struct idle_plan *plan;
    size_t cursor, fail_at, cancel_at;
    struct axd_idle_tssi *cancel_job;
    uint16_t counter, saved[4];
    uint8_t valid;
    unsigned active_mask, committed_tables, entered, exited, callback_completed, invalidated;
};
static struct axd_idle_tssi_context context_for(unsigned cores, uint32_t bus, uint16_t threshold)
{
    struct axd_idle_tssi_context c;
    memset(&c, 0, sizeof(c)); c.callback_present = 1;
    c.tssi.hardware.phy.core = 130; c.tssi.hardware.phy.minor = 1;
    c.tssi.hardware.phy.phy_type = 0xb; c.tssi.hardware.phy.phy_revision = 51;
    c.tssi.hardware.phy.bookkeeping = 0x5aa5; c.tssi.hardware.phy.phy_core_count = (uint8_t)cores;
    c.tssi.hardware.phy.saved_tssi[0] = 0x155; c.tssi.hardware.phy.saved_tssi[1] = 0x2aa;
    c.tssi.hardware.phy.snapshot_valid = 3;
    c.tssi.hardware.mac.core = 130; c.tssi.hardware.mac.minor = 1; c.tssi.hardware.mac.chip_id = 63178;
    c.tssi.hardware.mac.main_control = 3; c.tssi.hardware.mac.wake_reasons = 8;
    c.tssi.hardware.mac.band_present = 1; c.tssi.hardware.mac_context_valid = 1;
    c.tssi.hardware.shared_bus_word = bus; c.tssi.hardware.write_threshold = threshold;
    c.tssi.saved[0] = 0x111; c.tssi.saved[1] = 0x222; c.tssi.saved[2] = 0x333; c.tssi.saved[3] = 0x44;
    c.tssi.valid = 15; return c;
}
static struct trace trace_for(const struct idle_plan *p, const struct axd_idle_tssi_context *c,
                              enum axd_tssi_command cmd)
{
    struct trace t;
    memset(&t, 0, sizeof(t)); t.plan = p; t.fail_at = t.cancel_at = (size_t)-1;
    t.counter = c->tssi.hardware.phy.bookkeeping; memcpy(t.saved, c->tssi.saved, sizeof(t.saved));
    t.valid = c->tssi.valid;
    t.active_mask = cmd == AXD_TSSI_SNAPSHOT ? (1u << c->tssi.hardware.phy.phy_core_count) - 1u : 0;
    return t;
}
static int io(void *opaque, const struct axd_d11_operation *op, uint32_t *reply)
{
    struct trace *t = opaque;
    size_t i = t->cursor++;
    const struct event *e;
    assert(i < t->plan->callback.trace.count); e = &t->plan->callback.trace.events[i];
    assert(op->kind == e->kind && op->offset == e->offset && op->value == e->value);
    if (e->effect & PRE_COUNT) t->counter = e->counter;
    if (t->cancel_job != NULL && i == t->cancel_at)
        assert(axd_idle_tssi_cancel(t->cancel_job) == AXD_IDLE_CANCELLED);
    if (i == t->fail_at) return -1;
    *reply = e->reply; return 0;
}
static void terminal(struct axd_idle_tssi *job, struct trace *t, enum axd_idle_phase phase)
{
    struct axd_idle_tssi before = *job;
    size_t cursor = t->cursor;
    assert(axd_idle_tssi_step(job, io, t) == phase);
    assert(axd_idle_tssi_step(job, NULL, NULL) == phase);
    assert(axd_idle_tssi_cancel(job) == phase);
    assert(t->cursor == cursor && memcmp(&before, job, sizeof(before)) == 0);
}
static void run(struct axd_idle_tssi *job, struct trace *t)
{
    size_t i;
    for (i = 0; i < t->plan->callback.trace.count * 16 + 128; i++) {
        size_t before = t->cursor;
        enum axd_idle_phase phase = axd_idle_tssi_step(job, io, t);
        assert(t->cursor - before <= 1);
        /* This layer checks child propagation against its separately tested
         * contracts; values and traces remain from independent literal fixtures.
         */
        if (job->guard_job.context.mac.core == 130 && job->guard_job.phase == AXD_GUARD_TRACE_COMPLETE) {
            if (job->guard_job.command == AXD_PHY_ENTER) t->entered = 1;
            if (job->guard_job.command == AXD_PHY_EXIT) t->exited = 1;
        }
        if (job->tssi_job.phase == AXD_TSSI_TRACE_COMPLETE) t->callback_completed = 1;
        if (job->entered && t->active_mask && !t->invalidated && t->cursor >= t->plan->callback_start &&
            ((job->tssi_job.context.valid & t->active_mask) == 0 || t->cursor > t->plan->callback_start)) {
            t->valid = (uint8_t)(t->valid & ~t->active_mask); t->invalidated = 1;
        }
        if (t->cursor != before && before != t->fail_at) {
            const struct event *e = &t->plan->callback.trace.events[before];
            if (e->effect & BOOK_ZERO) t->counter = 0;
            if (e->effect & DIRECT0) { t->saved[0] = e->counter; t->valid |= 1; }
            if (e->effect & DIRECT1) { t->saved[1] = e->counter; t->valid |= 2; }
        }
        if (before == t->cursor && t->cursor < t->plan->callback.trace.count &&
            (t->plan->callback.trace.events[t->cursor].effect & PRE_COUNT) &&
            job->context.tssi.hardware.phy.bookkeeping == t->plan->callback.trace.events[t->cursor].counter)
            t->counter = t->plan->callback.trace.events[t->cursor].counter;
        if (t->committed_tables < t->plan->callback.table_count) {
            const struct segment *seg = &t->plan->callback.tables[t->committed_tables];
            if (t->cursor == seg->end && job->tssi_job.table_job.phase == AXD_TABLE_TRACE_COMPLETE &&
                job->tssi_job.phase != AXD_TSSI_FAILED && job->tssi_job.phase != AXD_TSSI_CANCELLED &&
                phase != AXD_IDLE_FAILED && phase != AXD_IDLE_CANCELLED && phase != AXD_IDLE_DEPENDENCY) {
                t->saved[seg->slot] = seg->saved; t->valid |= (uint8_t)(1u << seg->slot); t->committed_tables++;
            }
        }
        assert(job->entered == t->entered && job->exited == t->exited && job->callback_completed == t->callback_completed);
        assert(job->context.tssi.valid == t->valid && memcmp(job->context.tssi.saved, t->saved, sizeof(t->saved)) == 0);
        assert(job->context.tssi.hardware.phy.bookkeeping == t->counter);
        assert(job->context.tssi.hardware.phy.saved_tssi[0] == 0x155 && job->context.tssi.hardware.phy.saved_tssi[1] == 0x2aa);
        assert(job->context.tssi.hardware.phy.snapshot_valid == 3);
        if (job->guard_job.context.mac.core == 130)
            assert(job->context.phy_depth == job->guard_job.context.depth);
        if (phase != AXD_IDLE_RUNNING) return;
    }
    assert(0 && "idle wrapper exceeded independent trace bound");
}
static struct axd_idle_tssi execute(enum axd_tssi_command command,
    const struct axd_idle_tssi_context *c, const struct idle_plan *p,
    enum axd_idle_phase phase, unsigned pending)
{
    struct axd_idle_tssi job;
    struct trace t = trace_for(p, c, command);
    assert(axd_idle_tssi_begin(&job, command, c) == 0); run(&job, &t);
    assert(job.phase == phase && job.error == AXD_IDLE_NO_ERROR && job.pending_callouts == pending);
    assert(t.cursor == p->callback.trace.count && job.attempted_ops == t.cursor && job.completed_ops == t.cursor);
    terminal(&job, &t, phase); return job;
}
static void sweep(enum axd_tssi_command command, const struct axd_idle_tssi_context *c,
                    const struct idle_plan *p)
{
    size_t i; unsigned mode;
    for (i = 0; i < p->callback.trace.count; i++) for (mode = 0; mode < 3; mode++) {
        struct axd_idle_tssi job;
        struct trace t = trace_for(p, c, command);
        if (mode != 1) t.fail_at = i;
        if (mode != 0) { t.cancel_at = i; t.cancel_job = &job; }
        assert(axd_idle_tssi_begin(&job, command, c) == 0); run(&job, &t);
        assert(job.phase == (mode == 1 ? AXD_IDLE_CANCELLED : AXD_IDLE_FAILED));
        assert(job.error == (mode == 1 ? AXD_IDLE_NO_ERROR : AXD_IDLE_IO_ERROR));
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1 && job.completed_ops == i + (mode == 1));
        terminal(&job, &t, job.phase);
    }
}
static void composed_paths(void)
{
    unsigned cores, active_mac;
    for (cores = 1; cores <= 4; cores++) for (active_mac = 0; active_mac < 2; active_mac++) {
        struct axd_idle_tssi_context c = context_for(cores, 1, 2);
        struct idle_plan p = idle_plan_for(AXD_TSSI_SNAPSHOT, cores, 1, 2, 1, active_mac, 0, 0);
        struct axd_idle_tssi job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_IDLE_TRACE_COMPLETE, 0);
        assert(job.entered && job.exited && job.callback_completed && job.context.phy_depth == 0);
        assert(job.context.tssi.hardware.mac.wake_reasons == 8 && job.context.tssi.hardware.mac.main_depth == 0);
        sweep(AXD_TSSI_SNAPSHOT, &c, &p);
        p = idle_plan_for(AXD_TSSI_RESTORE, cores, 1, 2, 1, active_mac, 0, 0);
        job = execute(AXD_TSSI_RESTORE, &c, &p, AXD_IDLE_TRACE_COMPLETE, 0);
        assert(memcmp(job.context.tssi.saved, c.tssi.saved, sizeof(c.tssi.saved)) == 0);
        sweep(AXD_TSSI_RESTORE, &c, &p);
    }
}
static void nested_and_semantic_failures(void)
{
    struct axd_idle_tssi_context c = context_for(1, 0, 0);
    struct idle_plan p = idle_plan_for(AXD_TSSI_SNAPSHOT, 1, 0, 0, 0, 0, 0, 0);
    struct axd_idle_tssi job;
    struct trace t;
    c.phy_depth = 2;
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_IDLE_TRACE_COMPLETE, 0);
    assert(job.context.phy_depth == 2 && job.context.tssi.hardware.mac.wake_reasons == 8);
    sweep(AXD_TSSI_SNAPSHOT, &c, &p);
    /* An initial MAC nesting failure stops before PHY enter or callback. */
    c = context_for(1, 0, 0); c.tssi.hardware.mac.main_depth = 1;
    memset(&p, 0, sizeof(p)); append(&p.callback.trace, suspend_main, 2);
    t = trace_for(&p, &c, AXD_TSSI_SNAPSHOT);
    assert(axd_idle_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) == 0); run(&job, &t);
    assert(job.phase == AXD_IDLE_FAILED && job.error == AXD_IDLE_MAC_ERROR);
    assert(job.pending_callouts == AXD_PSM_FATAL && job.context.tssi.hardware.mac.main_depth == 2);
    assert(!job.entered && !job.callback_completed && !job.exited && job.context.tssi.valid == 15);
    terminal(&job, &t, AXD_IDLE_FAILED);
    /* A callback-table failure retains entered depth and partial save validity. */
    c = context_for(3, 0, 0); c.tssi.hardware.mac.main_depth = 1;
    memset(&p, 0, sizeof(p)); append(&p.callback.trace, mac_inactive, 2);
    p.callback_start = p.callback.trace.count;
    append(&p.callback.trace, active, 2); append(&p.callback.trace, suspend_main, 2);
    p.callback_end = p.callback.trace.count;
    t = trace_for(&p, &c, AXD_TSSI_SNAPSHOT);
    assert(axd_idle_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) == 0); run(&job, &t);
    assert(job.phase == AXD_IDLE_FAILED && job.error == AXD_IDLE_TSSI_ERROR);
    assert(job.entered && !job.callback_completed && !job.exited && job.context.phy_depth == 1);
    assert(job.context.tssi.valid == 8 && job.context.tssi.hardware.mac.main_depth == 2);
    assert(job.pending_callouts == AXD_PSM_FATAL && job.completed_ops == 6);
    terminal(&job, &t, AXD_IDLE_FAILED);
}

static void zero_reason_and_dependencies(void)
{
    struct axd_idle_tssi_context c = context_for(1, 0, 0);
    struct idle_plan p = idle_plan_for(AXD_TSSI_SNAPSHOT, 1, 0, 0, 0, 0, 1, 0);
    struct axd_idle_tssi job;
    c.tssi.hardware.mac.wake_reasons = 0; c.tssi.hardware.mac.band_present = 0;
    c.tssi.hardware.policy.public_up_known = 1; /* Known down at reason-clear policy calls. */
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_IDLE_TRACE_COMPLETE, 0);
    assert(job.context.tssi.hardware.mac.wake_reasons == 0 && job.context.phy_depth == 0);
    sweep(AXD_TSSI_SNAPSHOT, &c, &p);
    c.tssi.hardware.policy.public_up_known = 0;
    p = idle_plan_for(AXD_TSSI_SNAPSHOT, 1, 0, 0, 0, 0, 1, 1);
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_IDLE_DEPENDENCY, AXD_PSM_WAKE_CONTROL);
    assert(job.entered && job.callback_completed && !job.exited && job.context.phy_depth == 0);
    assert(job.context.tssi.valid == 15 && job.context.tssi.saved[0] == 0x123);
    c = context_for(1, 0, 0); c.tssi.hardware.mac.wake_reasons = 0;
    c.tssi.hardware.mac.main_control = 0x04000003;
    c.tssi.hardware.policy.public_up_known = 1;
    p = idle_plan_for(AXD_TSSI_SNAPSHOT, 1, 0, 0, 0, 1, 0, 0);
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_IDLE_TRACE_COMPLETE, 0);
    assert(job.context.tssi.hardware.mac.main_control == 0x04000003 && job.context.tssi.hardware.mac.wake_reasons == 0);
    c.tssi.hardware.policy.public_up_known = 0;
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_IDLE_DEPENDENCY, AXD_PSM_WAKE_CONTROL);
    assert(job.entered && job.exited && job.callback_completed && job.context.phy_depth == 0);
}
static void absent_alias_and_badargs(void)
{
    struct axd_idle_tssi_context c = context_for(2, 0, 0);
    struct idle_plan p = idle_plan_for(AXD_TSSI_SNAPSHOT, 2, 0, 0, 0, 0, 0, 0);
    struct axd_idle_tssi job, empty = {0};
    struct trace t;
    unsigned i;
    job = execute(AXD_TSSI_SNAPSHOT, &c, &p, AXD_IDLE_TRACE_COMPLETE, 0);
    assert(axd_idle_tssi_begin(&job, AXD_TSSI_RESTORE, &job.context) == 0);
    c = job.context; p = idle_plan_for(AXD_TSSI_RESTORE, 2, 0, 0, 0, 0, 0, 0);
    p.callback.trace.events[7].value = 0xaa55; p.callback.trace.events[13].value = 0x1123;
    t = trace_for(&p, &c, AXD_TSSI_RESTORE); run(&job, &t);
    assert(job.phase == AXD_IDLE_TRACE_COMPLETE && job.entered && job.exited && job.callback_completed);
    terminal(&job, &t, AXD_IDLE_TRACE_COMPLETE);
    /* An absent callback returns before validating unused hardware or save buffer. */
    memset(&c, 0xff, sizeof(c)); c.callback_present = 0;
    for (i = AXD_TSSI_SNAPSHOT; i <= AXD_TSSI_RESTORE; i++) {
        struct idle_plan absent;
        memset(&absent, 0, sizeof(absent));
        assert(axd_idle_tssi_begin(&job, (enum axd_tssi_command)i, &c) == 0);
        { struct axd_idle_tssi_context dummy = context_for(1, 0, 0);
          t = trace_for(&absent, &dummy, AXD_TSSI_RESTORE); }
        for (unsigned step = 0; step < 8 && job.phase == AXD_IDLE_RUNNING; step++) axd_idle_tssi_step(&job, NULL, NULL);
        assert(job.phase == AXD_IDLE_TRACE_COMPLETE && job.attempted_ops == 0 && job.completed_ops == 0);
        assert(!job.entered && !job.exited && !job.callback_completed && !job.pending_callouts);
        assert(job.context.phy_depth == 255 && job.context.tssi.valid == 255);
        assert(job.context.tssi.hardware.mac_context_valid == 255 && job.context.tssi.hardware.phy.phy_core_count == 255);
        assert(job.context.tssi.hardware.mac.wake_reasons == UINT32_MAX && job.context.tssi.saved[3] == 0xffff);
        terminal(&job, &t, AXD_IDLE_TRACE_COMPLETE);
    }
    c = context_for(1, 0, 0); memset(&p, 0, sizeof(p)); t = trace_for(&p, &c, AXD_TSSI_RESTORE);
    assert(axd_idle_tssi_begin(NULL, AXD_TSSI_SNAPSHOT, &c) != 0);
    assert(axd_idle_tssi_begin(&job, AXD_TSSI_SNAPSHOT, NULL) != 0);
    c.callback_present = 2; assert(axd_idle_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) != 0);
    c = context_for(1, 0, 0); c.tssi.hardware.mac_context_valid = 0;
    assert(axd_idle_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) != 0 && job.error == AXD_IDLE_MAC_CONTEXT);
    c = context_for(4, 0, 0); c.tssi.valid = 3;
    assert(axd_idle_tssi_begin(&job, AXD_TSSI_RESTORE, &c) != 0 && job.attempted_ops == 0);
    c = context_for(1, 0, 0); c.tssi.hardware.phy.phy_revision = 50;
    assert(axd_idle_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) != 0 && job.attempted_ops == 0);
    c = context_for(1, 0, 0); c.phy_depth = 255;
    memset(&p, 0, sizeof(p)); append(&p.callback.trace, mac_inactive, N(mac_inactive));
    t = trace_for(&p, &c, AXD_TSSI_SNAPSHOT);
    assert(axd_idle_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) == 0); run(&job, &t);
    assert(job.phase == AXD_IDLE_FAILED && job.error == AXD_IDLE_GUARD_ERROR && job.context.phy_depth == 255);
    assert(!job.entered && !job.callback_completed && !job.exited && job.completed_ops == 2);
    terminal(&job, &t, AXD_IDLE_FAILED);
    c = context_for(1, 0, 0); assert(axd_idle_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) == 0);
    for (i = 0; i < 16 && job.phase == AXD_IDLE_RUNNING; i++) axd_idle_tssi_step(&job, NULL, NULL);
    assert(job.phase == AXD_IDLE_FAILED && job.error == AXD_IDLE_BAD_ARGUMENT && job.attempted_ops == 0);
    terminal(&job, &t, AXD_IDLE_FAILED);
    assert(axd_idle_tssi_begin(&job, AXD_TSSI_SNAPSHOT, &c) == 0);
    assert(axd_idle_tssi_cancel(&job) == AXD_IDLE_CANCELLED); terminal(&job, &t, AXD_IDLE_CANCELLED);
    assert(axd_idle_tssi_step(NULL, io, &t) == AXD_IDLE_INVALID);
    assert(axd_idle_tssi_cancel(NULL) == AXD_IDLE_INVALID); terminal(&empty, &t, AXD_IDLE_INVALID);
}
int main(void)
{
    composed_paths(); nested_and_semantic_failures(); zero_reason_and_dependencies(); absent_alias_and_badargs();
    puts("idle TSSI: independent outer-order/child/context/policy traces and partial exits passed");
    return 0;
}
