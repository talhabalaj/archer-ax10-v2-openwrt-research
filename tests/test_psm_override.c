/* SPDX-License-Identifier: MIT */
#include "../driver/psm_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independent synthetic expectations from retained override/mctrl/wake assembly.
 * Policy facts are explicit; no queue read is inferred from a preceding write.
 */
struct event { enum axd_d11_kind kind; uint32_t offset, value, reply; unsigned sample; };
#define SB {AXD_D11_STORE_BARRIER, 0, 0, 0, 0}
#define FB {AXD_D11_FULL_BARRIER, 0, 0, 0, 0}
#define W(o,v) {AXD_D11_WRITE32, (o), (v), 0, 0}
#define R(o,v) {AXD_D11_READ32, (o), 0, (v), 0}
#define P(v) {AXD_D11_READ32, 0x120, 0, (v), 1}
#define H(o,v) {AXD_D11_READ16, (o), 0, (v), 0}
#define D(v) {AXD_D11_DELAY_US, 0, (v), 0, 0}
#define SHM(v) SB, W(0x160, 0x10010), R(0x160, 0xdeadbeefu), FB, H(0x164, (v)), FB
#define N(a) (sizeof(a) / sizeof((a)[0]))
static const struct event forced_set[] = {SB, W(0x120, 0x04000003), D(40), SHM(2), SHM(2)};
static const struct event forced_mirror[] = {
    SB, W(0x120, 0x04020003), SB, W(0x324, 0x04000800), D(40), SHM(2), SHM(2)
};
static const struct event set_no_band[] = {SB, W(0x120, 0x04000003)};
static const struct event policy_read[] = {P(3), FB};
static const struct event set_and_wait[] = {
    P(3), FB, SB, W(0x120, 0x04000003), D(40), SHM(2), SHM(2)
};
static const struct event cached_but_wait[] = {P(3), FB, D(40), SHM(2), SHM(2)};
static const struct event original_wake[] = {P(0x04000003), FB, SB, W(0x120, 0x04000003)};
static const struct event original_and_cached[] = {P(0x04000003), FB};
static const struct event clear_wake[] = {P(0x04000003), FB, SB, W(0x120, 3)};
static const struct event clear_with_overrides[] = {
    P(0x04000003), FB, SB, W(0x120, 0x04020003), SB, W(0x324, 0x800)
};
static const struct event enable_main[] = {
    R(0x120, 2), FB, SB, W(0x120, 0x04000003), SB, W(0x128, 1),
    R(0x120, 3), FB, R(0x128, 0), FB, SHM(2)
};
enum api { SET, CLEAR, WAKE, ENABLE_POLICY, ENABLE_LEGACY };
struct request {
    enum api api;
    uint32_t reason;
    struct axd_psm_context context;
    struct axd_psm_policy policy;
};
struct trace {
    const struct event *events;
    size_t count, cursor, fail_at, cancel_at;
    struct axd_psm_control *cancel_job;
    uint32_t last_read, sample;
    unsigned observed;
};
static struct axd_psm_context context_for(void)
{
    struct axd_psm_context c;
    memset(&c, 0, sizeof(c)); c.core = 130; c.minor = 1; c.chip_id = 63178;
    c.main_control = 3; c.wake_reasons = 8; c.band_present = 1;
    return c;
}
static struct axd_psm_policy policy(unsigned up_known, unsigned up, unsigned stay_known, unsigned stay)
{
    const struct axd_psm_policy p = {(uint8_t)up_known, (uint8_t)up, (uint8_t)stay_known, (uint8_t)stay, NULL};
    return p;
}
static struct request request(enum api api)
{
    struct request r;
    memset(&r, 0, sizeof(r)); r.api = api; r.context = context_for();
    return r;
}
static int begin(struct axd_psm_control *job, const struct request *r)
{
    switch (r->api) {
    case SET: case CLEAR: return axd_psm_override_begin(job, r->api == SET, r->reason, &r->context, &r->policy);
    case WAKE: return axd_psm_wake_control_begin(job, &r->context, &r->policy);
    case ENABLE_POLICY: return axd_psm_control_begin_policy(job, AXD_PSM_ENABLE_MAIN, &r->context, &r->policy);
    case ENABLE_LEGACY: return axd_psm_control_begin(job, AXD_PSM_ENABLE_MAIN, &r->context);
    }
    return -1;
}
static struct trace trace_for(const struct event *events, size_t count)
{
    struct trace t;
    memset(&t, 0, sizeof(t)); t.events = events; t.count = count;
    t.fail_at = t.cancel_at = (size_t)-1; return t;
}
static int io(void *opaque, const struct axd_d11_operation *op, uint32_t *reply)
{
    struct trace *t = opaque;
    size_t i = t->cursor++;
    const struct event *e;
    assert(i < t->count); e = &t->events[i];
    assert(op->kind == e->kind && op->offset == e->offset && op->value == e->value);
    if (t->cancel_job != NULL && i == t->cancel_at)
        assert(axd_psm_control_cancel(t->cancel_job) == AXD_PSM_CANCELLED);
    if (i == t->fail_at) return -1;
    *reply = e->reply; return 0;
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
    for (i = 0; i < t->count * 4 + 64; i++) {
        size_t before = t->cursor;
        enum axd_psm_phase phase = axd_psm_control_step(job, io, t);
        assert(t->cursor - before <= 1);
        if (t->cursor != before && before != t->fail_at) {
            const struct event *e = &t->events[before];
            if (e->kind == AXD_D11_READ32) t->last_read = e->reply;
            if (e->kind == AXD_D11_READ16) t->last_read = e->reply & 0xffffu;
            if (e->sample) { t->observed = 1; t->sample = e->reply; }
        }
        assert(job->last_read == t->last_read);
        assert(job->wake_control_observed == t->observed && job->wake_control_sample == t->sample);
        if (phase != AXD_PSM_RUNNING) return;
    }
    assert(0 && "override/policy exceeded independent trace bound");
}
static struct axd_psm_control execute(const struct request *r,
    const struct event *events, size_t count, enum axd_psm_phase phase,
    enum axd_psm_result result, unsigned callouts)
{
    struct axd_psm_control job;
    struct trace t = trace_for(events, count);
    assert(begin(&job, r) == 0); run(&job, &t);
    assert(job.phase == phase && job.result == result && job.error == AXD_PSM_NO_ERROR);
    assert(job.pending_callouts == callouts);
    assert(t.cursor == count && job.attempted_ops == count && job.completed_ops == count);
    terminal(&job, &t, phase); return job;
}
/* The mirror software store precedes its own ST, after primary ST/W return.
 * Even failing/cancelling mirror ST sees the new cache; primary failure/cancel
 * must retain the old mirror, including successful cancellation of primary W.
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
static void sweep(const struct request *r, const struct event *events, size_t count)
{
    size_t i; unsigned mode;
    for (i = 0; i < count; i++) for (mode = 0; mode < 3; mode++) {
        struct axd_psm_control job;
        struct trace t = trace_for(events, count);
        if (mode != 1) t.fail_at = i;
        if (mode != 0) { t.cancel_at = i; t.cancel_job = &job; }
        assert(begin(&job, r) == 0); run(&job, &t);
        assert(t.cursor == i + 1 && job.attempted_ops == i + 1 && job.completed_ops == i + (mode == 1));
        assert(job.phase == (mode == 1 ? AXD_PSM_CANCELLED : AXD_PSM_FAILED));
        assert(job.error == (mode == 1 ? AXD_PSM_NO_ERROR : AXD_PSM_IO_ERROR));
        /* Cache/reason changes are source software stores before their I/O.
         * They remain explicit when that I/O fails or is cancelled.
         */
        if (r->api == SET && r->context.wake_reasons == 0 && !(r->context.main_control & 0x04000000u)) {
            assert(job.context.wake_reasons == r->reason);
            assert(job.context.main_control == (r->context.main_control | 0x04000000u));
        }
        if (r->api == CLEAR) assert(job.context.wake_reasons == (r->context.wake_reasons & ~r->reason));
        if (r->api == WAKE || r->api == CLEAR) {
            uint32_t cache = r->context.main_control;
            if (i >= 2 && r->policy.stay_awake_known) {
                cache = r->policy.stay_awake ? cache | 0x04000000u : cache & ~0x04000000u;
            }
            assert(job.context.main_control == cache);
        }
        if (r->api == ENABLE_POLICY || r->api == ENABLE_LEGACY) {
            assert(job.context.main_depth == (i < 2 ? 1u : 0u));
            assert(job.context.main_control == (i < 2 ? 2u : 3u));
            assert(job.context.wake_reasons == (i < N(enable_main) ? 4u : 0u));
            if (i >= N(enable_main) && r->context.mbss_enabled)
                assert(job.pending_callouts & AXD_PSM_MBSS_PRQ);
        }
        assert(job.context.mirror_control == mirror_prefix(events, count, i, r->context.mirror_control));
        terminal(&job, &t, job.phase);
    }
}
static void override_paths(void)
{
    struct request r = request(SET);
    struct axd_psm_control job;
    const uint32_t reasons[] = {0, 4, 0x80000000u, UINT32_MAX};
    size_t i;
    for (i = 0; i < N(reasons); i++) {
        r.context = context_for(); r.context.wake_reasons = 0; r.reason = reasons[i];
        job = execute(&r, forced_set, N(forced_set), AXD_PSM_DONE, AXD_PSM_OVERRIDE_UPDATED, 0);
        assert(job.context.wake_reasons == reasons[i] && job.context.main_control == 0x04000003);
        assert(job.context.wake_hint == 1 && job.wake_control_observed == 0);
        sweep(&r, forced_set, N(forced_set));
    }
    r = request(SET); r.reason = 0x80000000u;
    job = execute(&r, NULL, 0, AXD_PSM_DONE, AXD_PSM_OVERRIDE_UPDATED, 0);
    assert(job.context.wake_reasons == 0x80000008u && job.context.main_control == 3);
    r.context.wake_reasons = 0; r.context.main_control = 0x04000003;
    job = execute(&r, NULL, 0, AXD_PSM_DONE, AXD_PSM_OVERRIDE_UPDATED, 0);
    assert(job.context.wake_reasons == 0x80000000u && job.context.wake_hint == 0);
    r.context.main_control = 3; r.context.band_present = 0;
    job = execute(&r, set_no_band, N(set_no_band), AXD_PSM_DONE, AXD_PSM_OVERRIDE_UPDATED, 0);
    assert(job.context.wake_hint == 0 && job.context.main_control == 0x04000003);
    sweep(&r, set_no_band, N(set_no_band));
    r.context = context_for(); r.context.wake_reasons = 0; r.context.main_control = 0x40003;
    r.context.mute_override = r.context.mirror_enabled = 1; r.context.mirror_control = 0x800;
    job = execute(&r, forced_mirror, N(forced_mirror), AXD_PSM_DONE, AXD_PSM_OVERRIDE_UPDATED, 0);
    assert(job.context.main_control == 0x04040003 && job.context.mirror_control == 0x04000800);
    sweep(&r, forced_mirror, N(forced_mirror));
    r = request(CLEAR); r.context.wake_reasons = 0x80000005u; r.reason = 0x80000000u;
    job = execute(&r, NULL, 0, AXD_PSM_DONE, AXD_PSM_OVERRIDE_UPDATED, 0);
    assert(job.context.wake_reasons == 5);
    r.reason = 0; r.context.wake_reasons = 0;
    execute(&r, NULL, 0, AXD_PSM_DEPENDENCY, AXD_PSM_NO_RESULT, AXD_PSM_WAKE_CONTROL);
    r.reason = UINT32_MAX; r.context.wake_reasons = UINT32_MAX; r.policy = policy(1, 0, 0, 1);
    job = execute(&r, NULL, 0, AXD_PSM_DONE, AXD_PSM_OVERRIDE_UPDATED, 0);
    assert(job.context.wake_reasons == 0 && !job.wake_control_observed);
    r.policy = policy(1, 1, 1, 0); r.context.main_control = 0x04000003;
    job = execute(&r, clear_wake, N(clear_wake), AXD_PSM_DONE, AXD_PSM_OVERRIDE_UPDATED, 0);
    assert(job.context.main_control == 3 && job.context.wake_reasons == 0);
    sweep(&r, clear_wake, N(clear_wake));
}
static void policy_paths(void)
{
    struct request r = request(WAKE);
    struct axd_psm_control job;
    r.policy = policy(0, 1, 1, 1);
    execute(&r, NULL, 0, AXD_PSM_DEPENDENCY, AXD_PSM_NO_RESULT, AXD_PSM_WAKE_CONTROL);
    r.policy = policy(1, 0, 0, 1);
    execute(&r, NULL, 0, AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    r.policy = policy(1, 1, 0, 1);
    job = execute(&r, policy_read, N(policy_read), AXD_PSM_DEPENDENCY, AXD_PSM_NO_RESULT, AXD_PSM_WAKE_CONTROL);
    assert(job.context.main_control == 3 && job.wake_control_sample == 3);
    sweep(&r, policy_read, N(policy_read));
    r.policy = policy(1, 1, 1, 1);
    job = execute(&r, set_and_wait, N(set_and_wait), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.main_control == 0x04000003 && job.wake_control_sample == 3 && job.last_read == 2);
    sweep(&r, set_and_wait, N(set_and_wait));
    r.context.main_control = 0x04000003;
    job = execute(&r, cached_but_wait, N(cached_but_wait), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.wake_hint == 1);
    sweep(&r, cached_but_wait, N(cached_but_wait));
    r.context.main_control = 3;
    job = execute(&r, original_wake, N(original_wake), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.wake_hint == 0 && job.context.main_control == 0x04000003);
    sweep(&r, original_wake, N(original_wake));
    r.context.main_control = 0x04000003;
    execute(&r, original_and_cached, N(original_and_cached), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    sweep(&r, original_and_cached, N(original_and_cached));
    r.context.mirror_enabled = 1; r.context.mirror_control = 0x800;
    job = execute(&r, original_and_cached, N(original_and_cached), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.mirror_control == 0x800); /* Unchanged cache never repairs stale mirror. */
    sweep(&r, original_and_cached, N(original_and_cached));
    r.policy = policy(1, 1, 1, 0); r.context.main_control = 0x04040003;
    r.context.mute_override = r.context.mirror_enabled = 1; r.context.mirror_control = 0x04000800;
    job = execute(&r, clear_with_overrides, N(clear_with_overrides), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.main_control == 0x40003 && job.context.mirror_control == 0x800 && job.context.wake_reasons == 8);
    sweep(&r, clear_with_overrides, N(clear_with_overrides));
    r = request(WAKE); r.policy = policy(1, 1, 1, 0);
    r.context.mirror_enabled = 1; r.context.mirror_control = 0x04000800;
    job = execute(&r, policy_read, N(policy_read), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.mirror_control == 0x04000800);
    sweep(&r, policy_read, N(policy_read));
    r = request(WAKE); r.policy = policy(1, 1, 1, 1); r.context.band_present = 0;
    { const struct event no_band[] = {P(3), FB, SB, W(0x120, 0x04000003)};
      execute(&r, no_band, N(no_band), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
      sweep(&r, no_band, N(no_band)); }
}
static void enable_policy_paths(void)
{
    struct request r = request(ENABLE_LEGACY);
    struct axd_psm_control job;
    r.context.main_control = 2; r.context.main_depth = 1; r.context.wake_reasons = 4;
    execute(&r, enable_main, N(enable_main), AXD_PSM_DEPENDENCY, AXD_PSM_NO_RESULT, AXD_PSM_WAKE_CONTROL);
    sweep(&r, enable_main, N(enable_main));
    r.api = ENABLE_POLICY; r.policy = policy(1, 0, 0, 1);
    job = execute(&r, enable_main, N(enable_main), AXD_PSM_DONE, AXD_PSM_ENABLED, 0);
    assert(job.context.main_depth == 0 && job.context.wake_reasons == 0);
    sweep(&r, enable_main, N(enable_main));
    r.context.mbss_enabled = 1;
    execute(&r, enable_main, N(enable_main), AXD_PSM_DEPENDENCY, AXD_PSM_NO_RESULT, AXD_PSM_MBSS_PRQ);
    r.api = ENABLE_LEGACY;
    execute(&r, enable_main, N(enable_main), AXD_PSM_DEPENDENCY, AXD_PSM_NO_RESULT,
            AXD_PSM_WAKE_CONTROL | AXD_PSM_MBSS_PRQ);
    r.api = ENABLE_POLICY; r.policy = policy(1, 1, 0, 1);
    {
        struct event combined[N(enable_main) + N(policy_read)];
        memcpy(combined, enable_main, sizeof(enable_main));
        memcpy(combined + N(enable_main), policy_read, sizeof(policy_read));
        execute(&r, combined, N(combined), AXD_PSM_DEPENDENCY, AXD_PSM_NO_RESULT,
                AXD_PSM_WAKE_CONTROL | AXD_PSM_MBSS_PRQ);
        sweep(&r, combined, N(combined));
        r.policy = policy(1, 1, 1, 0);
        execute(&r, combined, N(combined), AXD_PSM_DEPENDENCY, AXD_PSM_NO_RESULT, AXD_PSM_MBSS_PRQ);
        sweep(&r, combined, N(combined));
        r.context.mbss_enabled = 0;
        execute(&r, combined, N(combined), AXD_PSM_DONE, AXD_PSM_ENABLED, 0);
        sweep(&r, combined, N(combined));
    }
}
/* Explicit known values for a source-derived SLEEP route. Guarded nulls skip
 * optional objects; unguarded pointers remain distinct synthetic ARM32 IDs.
 * Unreached fields remain unknown rather than receiving pretend zero samples.
 */
static struct axd_wake_inputs sleep_inputs(void)
{
    struct axd_wake_inputs in = {0};
    const struct axd_wake_u8 zero = {1, 0};
    const struct axd_wake_u32 word_zero = {0, 1};
    unsigned i;
    in.w19c = in.w19b = in.public58 = zero;
    in.w199 = in.w19a = in.w198 = in.w300 = in.w336 = zero;
    in.wb4 = in.w2fc = word_zero;
    in.public_pointer = (struct axd_wake_u32){0x1000, 1};
    in.w22c = (struct axd_wake_u32){0x2000, 1};
    in.assoc4 = (struct axd_wake_u32){0x2100, 1}; in.assoc_word = word_zero;
    in.w568 = in.w188 = in.w1a0 = word_zero;
    in.w18 = (struct axd_wake_u32){0x3000, 1};
    in.w1e0 = (struct axd_wake_u32){0x4000, 1};
    for (i = 0; i < AXD_WAKE_BSS_COUNT; i++) in.bss[i].pointer = word_zero;
    return in;
}
static void actual_policy_inputs(void)
{
    struct axd_wake_inputs in = {0}, before;
    struct request r = request(WAKE);
    struct axd_psm_control job;
    struct trace t;
    in.w19c = (struct axd_wake_u8){1, 0x80};
    before = in;
    r.policy = policy(1, 1, 1, 0); r.policy.inputs = &in; /* Actual inputs outrank supplied false. */
    job = execute(&r, set_and_wait, N(set_and_wait), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.policy_evaluated && job.policy_decision == AXD_WAKE_STAY);
    assert(job.policy_reason == AXD_WAKE_W19C && job.policy_slot == 255 && job.policy_pass == 0 && job.policy_accesses == 1);
    assert(memcmp(&in, &before, sizeof(in)) == 0 && job.policy.inputs == &in);
    r.policy.stay_awake = 1; sweep(&r, set_and_wait, N(set_and_wait));
    /* Unknown reached input stops after the real hardware read/SY, without
     * falling back to the contradictory supplied STAY value. */
    memset(&in, 0, sizeof(in)); in.w19c = (struct axd_wake_u8){1, 0};
    job = execute(&r, policy_read, N(policy_read), AXD_PSM_DEPENDENCY, AXD_PSM_NO_RESULT, AXD_PSM_WAKE_CONTROL);
    assert(job.policy_evaluated && job.policy_decision == AXD_WAKE_DEPENDENCY);
    assert(job.policy_reason == AXD_WAKE_W19B && job.policy_accesses == 2 && job.policy_slot == 255 && !job.policy_pass);
    sweep(&r, policy_read, N(policy_read));
    /* The unguarded public object is a known null, not an unknown fact. */
    in = sleep_inputs(); in.public_pointer.value = 0; before = in;
    t = trace_for(policy_read, N(policy_read)); assert(begin(&job, &r) == 0); run(&job, &t);
    assert(job.phase == AXD_PSM_FAILED && job.error == AXD_PSM_POLICY_STATE && job.pending_callouts == 0);
    assert(job.policy_decision == AXD_WAKE_INVALID && job.policy_reason == AXD_WAKE_PUBLIC && job.policy_accesses == 5);
    assert(job.policy_slot == 255 && job.policy_pass == 0 && job.completed_ops == 2 && job.wake_control_observed);
    assert(memcmp(&in, &before, sizeof(in)) == 0); terminal(&job, &t, AXD_PSM_FAILED);
    /* CLEAR reason2 must evaluate current zero, not its pre-clear value2. */
    in = sleep_inputs(); before = in; r = request(CLEAR); r.reason = 2;
    r.context.wake_reasons = 2; r.context.main_control = 0x04000003;
    r.policy = policy(1, 1, 1, 1); r.policy.inputs = &in;
    job = execute(&r, clear_wake, N(clear_wake), AXD_PSM_DONE, AXD_PSM_OVERRIDE_UPDATED, 0);
    assert(job.context.wake_reasons == 0 && job.context.main_control == 3);
    assert(job.policy_evaluated && job.policy_decision == AXD_WAKE_SLEEP && job.policy_reason == AXD_WAKE_NONE);
    assert(job.policy_slot == 255 && job.policy_pass == 0 && job.policy_accesses == 68);
    assert(memcmp(&in, &before, sizeof(in)) == 0);
    r.policy.stay_awake = 0; sweep(&r, clear_wake, N(clear_wake));
    /* The same snapshot sees a still-present override on a direct policy call. */
    r = request(WAKE); r.policy = policy(1, 1, 1, 0); r.policy.inputs = &in;
    job = execute(&r, set_and_wait, N(set_and_wait), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.policy_decision == AXD_WAKE_STAY && job.policy_reason == AXD_WAKE_OVERRIDE && job.policy_accesses == 18);
    /* Main ENABLE clears reason4 before the same actual predicate. */
    r = request(ENABLE_POLICY); r.context.main_control = 2; r.context.main_depth = 1; r.context.wake_reasons = 4;
    r.policy = policy(1, 1, 1, 1); r.policy.inputs = &in;
    {
        struct event combined[N(enable_main) + N(policy_read)];
        memcpy(combined, enable_main, sizeof(enable_main));
        memcpy(combined + N(enable_main), policy_read, sizeof(policy_read));
        job = execute(&r, combined, N(combined), AXD_PSM_DONE, AXD_PSM_ENABLED, 0);
        assert(job.policy_decision == AXD_WAKE_SLEEP && job.policy_accesses == 68 && job.context.wake_reasons == 0);
        r.policy.stay_awake = 0; sweep(&r, combined, N(combined));
    }
    /* Public-down is an outer guard: no predicate access, even unknown inputs. */
    memset(&in, 0, sizeof(in)); r = request(WAKE);
    r.policy = policy(1, 0, 0, 0); r.policy.inputs = &in;
    job = execute(&r, NULL, 0, AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(!job.policy_evaluated && !job.policy_accesses && !job.wake_control_observed);
    /* R/SY cancellation/error prefixes cannot execute the later predicate. */
    r.policy = policy(1, 1, 1, 1); in = sleep_inputs(); r.policy.inputs = &in;
    for (unsigned i = 0; i < 2; i++) for (unsigned mode = 0; mode < 3; mode++) {
        t = trace_for(policy_read, N(policy_read));
        if (mode != 1) t.fail_at = i;
        if (mode != 0) { t.cancel_at = i; t.cancel_job = &job; }
        assert(begin(&job, &r) == 0); run(&job, &t);
        assert(!job.policy_evaluated && !job.policy_accesses);
        terminal(&job, &t, job.phase);
    }
}

static void alias_and_arguments(void)
{
    struct request r = request(WAKE);
    struct axd_psm_control job, empty = {0};
    struct trace t;
    unsigned i;
    r.policy = policy(1, 1, 1, 1);
    job = execute(&r, set_and_wait, N(set_and_wait), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(axd_psm_wake_control_begin(&job, &job.context, &job.policy) == 0);
    assert(job.context.main_control == 0x04000003 && job.policy.public_up_known && job.policy.stay_awake);
    assert(!job.wake_control_observed && job.attempted_ops == 0);
    t = trace_for(original_and_cached, N(original_and_cached)); run(&job, &t);
    assert(job.phase == AXD_PSM_DONE && job.result == AXD_PSM_WAKE_UPDATED && job.completed_ops == 2);
    terminal(&job, &t, AXD_PSM_DONE);
    assert(axd_psm_override_begin(&job, 0, 0, &job.context, &job.policy) == 0);
    /* Remaining reason8 means no policy access, while both aliases survive. */
    t = trace_for(NULL, 0); run(&job, &t);
    assert(job.phase == AXD_PSM_DONE && job.context.wake_reasons == 8 && job.policy.stay_awake);
    terminal(&job, &t, AXD_PSM_DONE);
    r = request(SET); t = trace_for(NULL, 0);
    assert(axd_psm_override_begin(NULL, 1, 0, &r.context, &r.policy) != 0);
    assert(axd_psm_wake_control_begin(NULL, &r.context, &r.policy) != 0);
    assert(axd_psm_override_begin(&job, 2, 0, &r.context, &r.policy) != 0);
    assert(axd_psm_override_begin(&job, 1, 0, NULL, &r.policy) != 0);
    assert(axd_psm_wake_control_begin(&job, NULL, &r.policy) != 0);
    for (i = 0; i < 4; i++) {
        struct axd_psm_policy p = policy(0, 0, 0, 0);
        if (i == 0) p.public_up_known = 2;
        if (i == 1) p.public_up = 2;
        if (i == 2) p.stay_awake_known = 2;
        if (i == 3) p.stay_awake = 2;
        assert(axd_psm_wake_control_begin(&job, &r.context, &p) != 0);
        assert(job.phase == AXD_PSM_FAILED && job.error == AXD_PSM_BAD_ARGUMENT && job.attempted_ops == 0);
        terminal(&job, &t, AXD_PSM_FAILED);
    }
    for (i = AXD_PSM_OVERRIDE_SET; i <= AXD_PSM_UPDATE_WAKE; i++) {
        assert(axd_psm_control_begin(&job, (enum axd_psm_command)i, &r.context) != 0);
        assert(axd_psm_control_begin_policy(&job, (enum axd_psm_command)i, &r.context, &r.policy) != 0);
    }
    assert(axd_psm_wake_control_begin(&job, &r.context, NULL) == 0);
    run(&job, &t); assert(job.phase == AXD_PSM_DEPENDENCY && job.pending_callouts == AXD_PSM_WAKE_CONTROL);
    terminal(&job, &t, AXD_PSM_DEPENDENCY);
    r = request(SET); r.context.wake_reasons = 0;
    assert(begin(&job, &r) == 0);
    for (i = 0; i < 16 && job.phase == AXD_PSM_RUNNING; i++) axd_psm_control_step(&job, NULL, NULL);
    assert(job.phase == AXD_PSM_FAILED && job.error == AXD_PSM_BAD_ARGUMENT && job.attempted_ops == 0);
    terminal(&job, &t, AXD_PSM_FAILED);
    assert(begin(&job, &r) == 0); assert(axd_psm_control_cancel(&job) == AXD_PSM_CANCELLED);
    terminal(&job, &t, AXD_PSM_CANCELLED);
    terminal(&empty, &t, AXD_PSM_INVALID);
}
static void band_binding(void)
{
    const struct event set_prefix[] = {P(3), FB, SB, W(0x120, 0x04000003)};
    struct request r = request(WAKE);
    struct axd_psm_control job;
    struct trace t;
    unsigned present;
    r.policy = policy(1, 1, 1, 1);
    r.context.band_binding_required = 1;
    /* Unknown is not a null band. The actual physical sample/policy/cache
     * update and primary write finish before the wait's band dependency.
     * Either placeholder predicate must produce the same dependency.
     */
    for (present = 0; present < 2; present++) {
        r.context.band_present = (uint8_t)present;
        job = execute(&r, set_prefix, N(set_prefix), AXD_PSM_DEPENDENCY,
                      AXD_PSM_NO_RESULT, AXD_PSM_BAND_STATE);
        assert(job.context.main_control == 0x04000003);
        assert(job.context.wake_reasons == 8 && job.context.wake_hint == 0);
        assert(job.wake_control_observed && job.wake_control_sample == 3);
        sweep(&r, set_prefix, N(set_prefix));
    }
    r.context.band_presence_known = 1;
    r.context.band_present = 0;
    job = execute(&r, set_prefix, N(set_prefix), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.main_control == 0x04000003 && job.context.wake_hint == 0);
    sweep(&r, set_prefix, N(set_prefix));
    r.context.band_present = 1;
    job = execute(&r, set_and_wait, N(set_and_wait), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.wake_hint == 1 && job.completed_ops == N(set_and_wait));
    sweep(&r, set_and_wait, N(set_and_wait));

    /* Unknown unused band does not reject at begin or any earlier guard. */
    r.context.band_presence_known = 0;
    r.policy = policy(1, 0, 0, 0);
    execute(&r, NULL, 0, AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    r.policy = policy(1, 1, 1, 0);
    execute(&r, policy_read, N(policy_read), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    r.policy = policy(1, 1, 1, 1);
    job = execute(&r, original_wake, N(original_wake), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.main_control == 0x04000003 && !job.context.wake_hint);

    /* Alias restart retains the opt-in and its explicit unknown status. */
    assert(axd_psm_wake_control_begin(&job, &job.context, &job.policy) == 0);
    assert(job.context.band_binding_required == 1 && !job.context.band_presence_known);
    t = trace_for(cached_but_wait, 2);
    run(&job, &t);
    assert(job.phase == AXD_PSM_DEPENDENCY && job.pending_callouts == AXD_PSM_BAND_STATE);
    assert(job.context.main_control == 0x04000003 && job.completed_ops == 2);
    terminal(&job, &t, AXD_PSM_DEPENDENCY);

    r.context.band_binding_required = 2;
    assert(begin(&job, &r) == -1 && job.error == AXD_PSM_BAD_ARGUMENT);
    r.context.band_binding_required = 1;
    r.context.band_presence_known = 2;
    assert(begin(&job, &r) == -1 && job.error == AXD_PSM_BAD_ARGUMENT);
    /* Legacy contexts ignore the unused known byte, retaining old behavior. */
    r.context.band_binding_required = 0;
    job = execute(&r, set_and_wait, N(set_and_wait), AXD_PSM_DONE, AXD_PSM_WAKE_UPDATED, 0);
    assert(job.context.band_presence_known == 2 && job.context.wake_hint == 1);
}
int main(void)
{
    override_paths(); policy_paths(); enable_policy_paths(); actual_policy_inputs(); alias_and_arguments(); band_binding();
    puts("PSM override: independent reason/policy/cache/mirror/enable traces and callback exits passed");
    return 0;
}
