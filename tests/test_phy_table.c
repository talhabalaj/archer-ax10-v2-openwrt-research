/* SPDX-License-Identifier: MIT */
#include "../driver/phy_table.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Original synthetic event oracle derived before inspecting production code.
 * PRE_COUNT is deliberately pre-I/O; other effects commit only on success.
 */
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
struct script { struct event events[96]; size_t count, suspend_start, enable_start; unsigned mac_active; };
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
struct trace {
    const struct event *events;
    size_t count, cursor, fail_at, cancel_at;
    struct axd_phy_table *cancel_job;
    uint16_t counter, low, high;
    uint32_t last_read, result;
    unsigned low_valid, word_valid, flush_reads, changed_guard, mac_active;
    size_t suspend_start, enable_start;
    uint32_t original_wake_reasons;
};
static struct axd_phy_table_context context_for(uint32_t bus, uint16_t threshold)
{
    struct axd_phy_table_context c;
    memset(&c, 0, sizeof(c));
    c.phy.core = 130; c.phy.minor = 1; c.phy.phy_type = 0xb; c.phy.phy_revision = 51;
    c.phy.bookkeeping = 0x5aa5; c.phy.phy_core_count = 2;
    c.mac.core = 130; c.mac.minor = 1; c.mac.chip_id = 63178;
    c.mac.main_control = 3; c.mac.wake_reasons = 8; c.mac.band_present = 1;
    c.shared_bus_word = bus; c.write_threshold = threshold;
    return c;
}
static struct trace trace_for(const struct script *s, const struct axd_phy_table_context *c)
{
    struct trace t;
    memset(&t, 0, sizeof(t)); t.events = s->events; t.count = s->count;
    t.fail_at = t.cancel_at = (size_t)-1; t.counter = c->phy.bookkeeping;
    t.mac_active = s->mac_active; t.suspend_start = s->suspend_start; t.enable_start = s->enable_start;
    t.original_wake_reasons = c->mac.wake_reasons;
    return t;
}
static int io(void *opaque, const struct axd_d11_operation *op, uint32_t *reply)
{
    struct trace *t = opaque;
    size_t i = t->cursor++;
    const struct event *e;
    assert(i < t->count); e = &t->events[i];
    assert(op->kind == e->kind && op->offset == e->offset && op->value == e->value);
    if (e->effect & PRE_COUNT) t->counter = e->counter;
    if (t->cancel_job != NULL && i == t->cancel_at)
        assert(axd_phy_table_cancel(t->cancel_job) == AXD_TABLE_CANCELLED);
    if (i == t->fail_at) return -1;
    *reply = e->reply;
    return 0;
}
static void terminal(struct axd_phy_table *job, struct trace *t, enum axd_phy_table_phase phase)
{
    struct axd_phy_table before = *job;
    size_t cursor = t->cursor;
    assert(axd_phy_table_step(job, io, t) == phase);
    assert(axd_phy_table_step(job, NULL, NULL) == phase);
    assert(axd_phy_table_cancel(job) == phase);
    assert(t->cursor == cursor && memcmp(&before, job, sizeof(before)) == 0);
}
static void run(struct axd_phy_table *job, struct trace *t)
{
    size_t i;
    for (i = 0; i < t->count * 8 + 128; i++) {
        size_t before = t->cursor;
        enum axd_phy_table_phase phase = axd_phy_table_step(job, io, t);
        assert(t->cursor - before <= 1);
        if (t->cursor != before && before != t->fail_at) {
            const struct event *e = &t->events[before];
            if (e->kind == AXD_D11_READ16) t->last_read = e->reply & 0xffffu;
            if (e->kind == AXD_D11_READ32) t->last_read = e->reply;
            if (e->kind == AXD_D11_READ16 && e->offset == 0x3e0) t->flush_reads++;
            if (e->effect & BOOK_ZERO) t->counter = 0;
            if (e->effect & GUARD_SET) t->changed_guard = 1;
            if (e->effect & LOW) { t->low = (uint16_t)t->last_read; t->low_valid = 1; }
            if (e->effect & WORD) {
                t->high = (uint16_t)t->last_read;
                t->result = (uint32_t)t->low | ((uint32_t)t->high << 16); t->word_valid = 1;
            }
        }
        /* A pure prelude transition may perform its documented software update
         * before its first queued callback. Its only allowed value is explicit.
         */
        if (before == t->cursor && t->cursor < t->count &&
            (t->events[t->cursor].effect & PRE_COUNT) &&
            job->context.phy.bookkeeping == t->events[t->cursor].counter)
            t->counter = t->events[t->cursor].counter;
        assert(job->context.phy.bookkeeping == t->counter);
        assert(job->last_read == t->last_read && job->flush_reads == t->flush_reads);
        assert(job->low_valid == t->low_valid && job->word_valid == t->word_valid);
        assert(job->changed_guard == t->changed_guard);
        assert(job->low_half == t->low && job->high_half == t->high && job->result == t->result);
        if (t->mac_active && before != t->cursor) {
            unsigned depth = before >= t->suspend_start + 2 && before < t->enable_start + 2;
            uint32_t cache = before >= t->suspend_start + 6 && before < t->enable_start + 2 ? 2u : 3u;
            uint32_t reasons = before >= t->suspend_start + 2 ? t->original_wake_reasons | 4u : t->original_wake_reasons;
            assert(job->context.mac.main_depth == depth && job->context.mac.main_control == cache);
            assert(job->context.mac.wake_reasons == reasons);
        }
        if (phase != AXD_TABLE_RUNNING) return;
    }
    assert(0 && "table wrapper exceeded independent trace bound");
}
static struct axd_phy_table execute(uint32_t index, const struct axd_phy_table_context *c,
    const struct script *s, enum axd_phy_table_phase phase, enum axd_phy_table_dependency dependency)
{
    struct axd_phy_table job;
    struct trace t = trace_for(s, c);
    assert(axd_phy_table_begin(&job, index, c) == 0);
    run(&job, &t);
    assert(job.phase == phase && job.error == AXD_TABLE_NO_ERROR && job.dependency == dependency);
    assert(t.cursor == s->count && job.attempted_ops == t.cursor && job.completed_ops == t.cursor);
    terminal(&job, &t, phase);
    return job;
}
static void sweep(uint32_t index, const struct axd_phy_table_context *c, const struct script *s)
{
    size_t i; unsigned mode;
    for (i = 0; i < s->count; i++) for (mode = 0; mode < 3; mode++) {
        struct axd_phy_table job;
        struct trace t = trace_for(s, c);
        if (mode != 1) t.fail_at = i;
        if (mode != 0) { t.cancel_at = i; t.cancel_job = &job; }
        assert(axd_phy_table_begin(&job, index, c) == 0);
        run(&job, &t);
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1 && job.completed_ops == i + (mode == 1));
        assert(job.phase == (mode == 1 ? AXD_TABLE_CANCELLED : AXD_TABLE_FAILED));
        if (mode == 1) assert(job.error == AXD_TABLE_NO_ERROR);
        else assert(job.error == AXD_TABLE_IO_ERROR);
        terminal(&job, &t, job.phase);
    }
}
static void normal_and_fault_paths(void)
{
    const uint32_t buses[] = {0, 1, 2, UINT32_MAX};
    const uint16_t thresholds[] = {0, 1, 2, 3, 0xffff};
    size_t b, h; unsigned clear;
    for (b = 0; b < N(buses); b++) for (h = 0; h < N(thresholds); h++) for (clear = 0; clear < 2; clear++) {
        struct axd_phy_table_context c = context_for(buses[b], thresholds[h]);
        struct script s = script_for(buses[b], thresholds[h], clear, 0, 0xffff000e);
        struct axd_phy_table job = execute(0x1234ffff, &c, &s, AXD_TABLE_TRACE_COMPLETE, AXD_TABLE_NO_DEPENDENCY);
        assert(job.index == 0x1234ffff && job.result == 0x1234beef && job.low_valid && job.word_valid);
        assert(job.changed_guard == clear && job.context.phy.bookkeeping == 0);
        assert(job.flush_reads == (buses[b] != 1 || thresholds[h] >= 3 ? 0u : (thresholds[h] <= 1 ? 2u : 1u)));
        sweep(0x1234ffff, &c, &s);
    }
}
static void active_mac_and_dependencies(void)
{
    struct axd_phy_table_context c = context_for(1, 2);
    struct script s = script_for(1, 2, 1, 1, 0xc000000e);
    struct axd_phy_table job;
    c.mac_context_valid = 1;
    job = execute(0xc000, &c, &s, AXD_TABLE_TRACE_COMPLETE, AXD_TABLE_NO_DEPENDENCY);
    assert(job.context.mac.main_depth == 0 && job.context.mac.main_control == 3);
    assert(job.context.mac.wake_reasons == 8 && job.word_valid == 1 && job.pending_callouts == 0);
    sweep(0xc000, &c, &s);
    c.mac.wake_reasons = 4;
    job = execute(0xc000, &c, &s, AXD_TABLE_DEPENDENCY, AXD_TABLE_MAC_CALLOUT);
    assert(job.word_valid && job.result == 0x1234beef && job.context.mac.wake_reasons == 0);
    assert(job.pending_callouts == AXD_PSM_WAKE_CONTROL);
    {
        struct script missing;
        memset(&missing, 0, sizeof(missing)); append(&missing, active, N(active));
        c.mac_context_valid = 0;
        job = execute(0xc000, &c, &missing, AXD_TABLE_DEPENDENCY, AXD_TABLE_MAC_CONTEXT);
        assert(job.low_valid == 0 && job.word_valid == 0 && job.context.phy.bookkeeping == 0x5aa5);
    }
}
static void zero_word_and_semantic_failure(void)
{
    struct axd_phy_table_context c = context_for(0, 0);
    struct script s = script_for(0, 0, 0, 0, 0x0000000e);
    struct axd_phy_table job;
    struct trace t;
    size_t i;
    for (i = 0; i < s.count; i++)
        if (s.events[i].kind == AXD_D11_READ16 && s.events[i].offset == 0x3fe &&
            (s.events[i].reply == 0xfeedbeefu || s.events[i].reply == 0xdead1234u)) s.events[i].reply = 0;
    job = execute(0, &c, &s, AXD_TABLE_TRACE_COMPLETE, AXD_TABLE_NO_DEPENDENCY);
    assert(job.low_valid && job.word_valid && job.result == 0);
    sweep(0, &c, &s);
    /* Initial control's bit0 alone triggers real MAC context, irrespective bit1. */
    s = script_for(0, 0, 0, 0, 0x0000000e); s.count = 2; s.events[0].reply = 1;
    execute(0, &c, &s, AXD_TABLE_DEPENDENCY, AXD_TABLE_MAC_CONTEXT);
    c.mac_context_valid = 1; c.mac.main_depth = 1;
    memset(&s, 0, sizeof(s)); append(&s, active, N(active)); append(&s, suspend_main, 2);
    t = trace_for(&s, &c);
    assert(axd_phy_table_begin(&job, 0, &c) == 0); run(&job, &t);
    assert(job.phase == AXD_TABLE_FAILED && job.error == AXD_TABLE_MAC_ERROR);
    assert(job.mac_job.error == AXD_PSM_BAD_NESTING && job.pending_callouts == AXD_PSM_FATAL);
    assert(job.context.mac.main_depth == 2 && job.context.phy.bookkeeping == 0x5aa5);
    assert(job.attempted_ops == 4 && job.completed_ops == 4 && !job.low_valid && !job.word_valid);
    terminal(&job, &t, AXD_TABLE_FAILED);
}

static void alias_and_validation(void)
{
    struct axd_phy_table_context c = context_for(0, 2);
    struct script s = script_for(0, 2, 0, 0, 0x0000000e);
    struct axd_phy_table job, empty = {0};
    struct trace t;
    unsigned i;
    job = execute(0x10000, &c, &s, AXD_TABLE_TRACE_COMPLETE, AXD_TABLE_NO_DEPENDENCY);
    assert(axd_phy_table_begin(&job, 0x10000, &job.context) == 0);
    assert(job.context.phy.core == 130 && job.context.phy.bookkeeping == 0 && !job.word_valid);
    t = trace_for(&s, &job.context); run(&job, &t);
    assert(job.phase == AXD_TABLE_TRACE_COMPLETE && job.word_valid && job.completed_ops == s.count);
    terminal(&job, &t, AXD_TABLE_TRACE_COMPLETE);
    c = context_for(0, 0); t = trace_for(&s, &c);
    assert(axd_phy_table_begin(NULL, 0, &c) != 0);
    assert(axd_phy_table_begin(&job, 0, NULL) != 0);
    terminal(&job, &t, AXD_TABLE_FAILED);
    for (i = 0; i < 7; i++) {
        c = context_for(0, 0);
        if (i == 0) c.phy.core = 129;
        if (i == 1) c.phy.minor = 2;
        if (i == 2) c.phy.phy_type = 0xc;
        if (i == 3) c.phy.phy_revision = 50;
        if (i == 4) c.phy.phy_core_count = 0;
        if (i == 5) c.phy.phy_core_count = 5;
        if (i == 6) c.mac_context_valid = 2;
        assert(axd_phy_table_begin(&job, 0, &c) != 0 && job.attempted_ops == 0);
        terminal(&job, &t, AXD_TABLE_FAILED);
    }
    for (i = 0; i < 7; i++) {
        c = context_for(0, 0); c.mac_context_valid = 1;
        if (i == 0) c.mac.core = 129;
        if (i == 1) c.mac.minor = 0; /* Must match PHY minor1. */
        if (i == 2) c.mac.band_present = 2;
        if (i == 3) c.mac.mute_override = 2;
        if (i == 4) c.mac.mirror_enabled = 2;
        if (i == 5) c.mac.wake_hint = 2;
        if (i == 6) c.mac.mbss_enabled = 2;
        assert(axd_phy_table_begin(&job, 0, &c) != 0 && job.attempted_ops == 0);
        terminal(&job, &t, AXD_TABLE_FAILED);
    }
    c = context_for(1, 1); c.phy.minor = 0;
    assert(axd_phy_table_begin(&job, 0, &c) == 0);
    for (i = 0; i < 8 && job.phase == AXD_TABLE_RUNNING; i++) axd_phy_table_step(&job, NULL, NULL);
    assert(job.phase == AXD_TABLE_FAILED && job.error == AXD_TABLE_BAD_ARGUMENT && job.attempted_ops == 0);
    terminal(&job, &t, AXD_TABLE_FAILED);
    c = context_for(1, 1); s = script_for(1, 1, 0, 0, 0x0000000e);
    assert(axd_phy_table_begin(&job, 0, &c) == 0); t = trace_for(&s, &c);
    assert(axd_phy_table_step(&job, io, &t) == AXD_TABLE_RUNNING);
    assert(axd_phy_table_step(&job, io, &t) == AXD_TABLE_RUNNING);
    for (i = 0; i < 16 && job.phase == AXD_TABLE_RUNNING; i++) axd_phy_table_step(&job, NULL, NULL);
    assert(job.phase == AXD_TABLE_FAILED && job.error == AXD_TABLE_BAD_ARGUMENT);
    assert(job.attempted_ops == 2 && job.completed_ops == 2 && job.context.phy.bookkeeping == 0x5aa5);
    terminal(&job, &t, AXD_TABLE_FAILED);
    assert(axd_phy_table_begin(&job, 0, &c) == 0);
    assert(axd_phy_table_cancel(&job) == AXD_TABLE_CANCELLED);
    terminal(&job, &t, AXD_TABLE_CANCELLED);
    assert(axd_phy_table_step(NULL, io, &t) == AXD_TABLE_INVALID);
    assert(axd_phy_table_cancel(NULL) == AXD_TABLE_INVALID);
    terminal(&empty, &t, AXD_TABLE_INVALID);
}
int main(void)
{
    normal_and_fault_paths(); active_mac_and_dependencies(); zero_word_and_semantic_failure(); alias_and_validation();
    puts("PHY table: independent packed/flush/guard/half-word/MAC traces and every-event exits passed");
    return 0;
}
