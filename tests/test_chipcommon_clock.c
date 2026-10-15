/* SPDX-License-Identifier: MIT */
#include "../driver/chipcommon_clock.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

struct event { struct axd_cc_clock_operation op; uint32_t reply; };
#define E(k,r,o,v,a) {{k,r,o,v},a}
#define ST E(AXD_CC_STORE_BARRIER,AXD_CC_CHIPCOMMON,0,0,0)
#define SY E(AXD_CC_FULL_BARRIER,AXD_CC_CHIPCOMMON,0,0,0)
#define R(o,a) E(AXD_CC_READ32,AXD_CC_CHIPCOMMON,o,0,a)
#define W(o,v) E(AXD_CC_WRITE32,AXD_CC_CHIPCOMMON,o,v,0)
#define PR(o,a) E(AXD_CC_PCI_READ32,AXD_CC_PCI_CONFIG,o,0,a)
#define PW(o,v) E(AXD_CC_PCI_WRITE32,AXD_CC_PCI_CONFIG,o,v,0)
#define D(v) E(AXD_CC_DELAY_US,AXD_CC_NONE,0,v,0)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
struct fixture {
    struct axd_cc_clock *job;
    const struct event *events;
    size_t count, seen, fault;
    unsigned error, cancel;
};
static struct axd_cc_clock_context context(int32_t revision, uint32_t capabilities)
{
    const struct axd_cc_clock_context c = {revision, capabilities, 1, 0x804, 13, 1, 1};
    return c;
}
static int io(void *opaque, const struct axd_cc_clock_operation *op, uint32_t *reply)
{
    struct fixture *f = opaque;
    const struct event *e;
    assert(f->seen < f->count);
    e = &f->events[f->seen++];
    assert(op->kind == e->op.kind && op->region == e->op.region);
    assert(op->offset == e->op.offset && op->value == e->op.value);
    *reply = e->reply;
    if (f->seen == f->fault) {
        if (f->cancel)
            assert(axd_cc_clock_cancel(f->job) == AXD_CC_CANCELLED);
        if (f->error)
            return -1;
    }
    return 0;
}
/* Independent successful-prefix oracle, also checked after cancellation. */
static void telemetry(const struct axd_cc_clock *job, const struct event *e, size_t n)
{
    uint32_t last = 0, cc = 0, off = 0, status = 0, in = 0, out = 0, en = 0;
    unsigned cc_seen = 0, cc_newer = 0, status_seen = 0, status_newer = 0, delays = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        const struct axd_cc_clock_operation *op = &e[i].op;
        if (op->kind == AXD_CC_READ32 || op->kind == AXD_CC_PCI_READ32)
            last = e[i].reply;
        if (op->kind == AXD_CC_READ32) {
            cc = e[i].reply; off = op->offset; cc_seen = 1; cc_newer = 0;
            if (op->offset == 0x1e0) {
                status = e[i].reply; status_seen = 1; status_newer = 0;
            }
        }
        if (op->kind == AXD_CC_WRITE32) {
            cc_newer = 1;
            if (op->offset == 0x1e0) status_newer = 1;
        }
        if (op->kind == AXD_CC_PCI_READ32) {
            if (op->offset == 0xb0) in = e[i].reply;
            if (op->offset == 0xb4) out = e[i].reply;
            if (op->offset == 0xb8) en = e[i].reply;
        }
        if (op->kind == AXD_CC_DELAY_US && op->value == 10) delays++;
    }
    assert(job->last_read == last && job->last_cc_sample == cc && job->last_cc_offset == off);
    assert(job->cc_sample_observed == cc_seen && job->cc_writes_since_sample == cc_newer);
    assert(job->last_status == status && job->status_observed == status_seen);
    assert(job->clock_writes_since_status == status_newer && job->poll_delays == delays);
    assert(job->crystal_input == in && job->crystal_output == out && job->crystal_enable == en);
}
static void run(struct fixture *f)
{
    size_t steps = 0;
    while (f->job->phase == AXD_CC_RUNNING) {
        uint64_t before = f->job->attempted_ops;
        assert(++steps < f->count * 5 + 80);
        axd_cc_clock_step(f->job, io, f);
        assert(f->job->attempted_ops - before <= 1);
        assert(f->job->attempted_ops == f->seen);
        telemetry(f->job, f->events, (size_t)f->job->completed_ops);
    }
    {
        size_t seen = f->seen;
        enum axd_cc_clock_phase phase = f->job->phase;
        assert(axd_cc_clock_step(f->job, io, f) == phase);
        assert(axd_cc_clock_step(f->job, NULL, NULL) == phase);
        assert(axd_cc_clock_cancel(f->job) == phase && f->seen == seen);
    }
}
static void check(const struct axd_cc_clock_context *c, uint32_t mode,
                  const struct event *events, size_t n, unsigned result,
                  unsigned crystal_called, int32_t crystal_result, unsigned exhausted,
                  unsigned faults)
{
    struct axd_cc_clock job;
    struct fixture f = {&job, events, n, 0, 0, 0, 0};
    size_t p;
    unsigned type;
    assert(axd_cc_clock_begin(&job, mode, c) == 0);
    run(&f);
    assert(job.phase == AXD_CC_TRACE_COMPLETE && job.return_value == result);
    assert(job.error == AXD_CC_NO_ERROR && f.seen == n && job.completed_ops == n);
    assert(job.crystal_called == crystal_called && job.crystal_return_valid == crystal_called);
    if (crystal_called) assert(job.crystal_return == crystal_result);
    assert(job.poll_exhausted == exhausted);
    if (!faults) return;
    for (p = 1; p <= n; p++) {
        for (type = 1; type <= 3; type++) {
            assert(axd_cc_clock_begin(&job, mode, c) == 0);
            f.seen = 0; f.fault = p; f.error = type & 1u; f.cancel = (type >> 1) & 1u;
            run(&f);
            assert(f.seen == p && job.attempted_ops == p);
            assert(job.completed_ops == p - f.error);
            assert(job.phase == (f.error ? AXD_CC_FAILED : AXD_CC_CANCELLED));
            assert(job.error == (f.error ? AXD_CC_IO_ERROR : AXD_CC_NO_ERROR));
        }
    }
}
static void literal_traces(void)
{
    struct axd_cc_clock_context c;
    static const struct event fast[] = {ST,R(0x1e0,0x80000001),SY,W(0x1e0,0x80000003),D(150)};
    static const struct event dynamic[] = {ST,R(0x1e0,0xa5a5a5a7),SY,W(0x1e0,0xa5a5a5a5),D(150)};
    static const struct event middle_fast[] = {ST,R(0xc0,0xabc00001),SY,W(0xc0,0xabc00011),D(150)};
    static const struct event middle_dynamic[] = {ST,R(0xc0,0xabcdef10),SY,W(0xc0,0xabcdef00),D(150)};
    static const struct event old_fast[] = {
        PR(0xb0,0x110),PR(0xb4,0x201),PR(0xb8,0x302),PW(0xb4,0x241),PW(0xb8,0x342),D(1000),
        ST,R(0xb8,0xdeadffff),SY,W(0xb8,0xdeadd7ff),D(150)
    };
    static const struct event old_already_on[] = {
        PR(0xb0,0x40),PR(0xb4,0x201),PR(0xb8,0x302),
        ST,R(0xb8,0xdeadffff),SY,W(0xb8,0xdeadd7ff),D(150)
    };
    static const struct event old_off[] = {
        R(0xb8,0xdeadf804),SY,ST,W(0xb8,0xdeade004),
        PR(0xb0,0xffffffff),PR(0xb4,0xa5a5a5ff),PR(0xb8,0xc0000002),
        PW(0xb4,0xa5a5a5bf),PW(0xb8,0xc0000042),D(150)
    };
    static const struct event old_xtal_source[] = {R(0xb8,0xdeadf801),SY,ST,W(0xb8,0xdeadc001),D(150)};
    static const struct event old_ignored[] = {ST,R(0xb8,0xdeadffff),SY,W(0xb8,0xdeadd7ff),D(150)};
    static const struct event poll_fast[] = {
        ST,R(0x1e0,0x8000),SY,W(0x1e0,0x8002),R(0x1e0,0x4),SY,D(10),R(0x1e0,0x20001),SY
    };
    static const struct event poll_dynamic[] = {
        ST,R(0x1e0,0x8002),SY,W(0x1e0,0x8000),R(0x1e0,0x20001),SY,D(10),R(0x1e0,0x1),SY
    };
    static const struct event old_pmu[] = {
        ST,R(0xb8,0xdeadffff),SY,W(0xb8,0xdeadd7ff),R(0x1e0,0x20000),SY
    };
    static const struct event middle_pmu[] = {
        ST,R(0xc0,0xabc00001),SY,W(0xc0,0xabc00011),R(0x1e0,0x20000),SY
    };
    uint32_t buses[] = {0,2,3,UINT32_MAX};
    size_t i;
    c = context(20,0); check(&c,0,fast,COUNT(fast),1,0,0,0,1);
    c = context(INT32_MAX,0); check(&c,2,dynamic,COUNT(dynamic),0,0,0,0,1);
    c = context(10,0x40000); check(&c,0,middle_fast,COUNT(middle_fast),1,0,0,0,1);
    c = context(19,0x40000); check(&c,2,middle_dynamic,COUNT(middle_dynamic),0,0,0,0,1);
    c = context(6,0x40000); check(&c,0,old_fast,COUNT(old_fast),1,1,0,0,1);
    c = context(9,0x40000); check(&c,0,old_already_on,COUNT(old_already_on),1,1,0,0,1);
    check(&c,2,old_off,COUNT(old_off),0,1,0,0,1);
    c.pci_config_owned = 0; check(&c,2,old_xtal_source,COUNT(old_xtal_source),0,0,0,0,1);
    for (i = 0; i < COUNT(buses); i++) {
        c.bus = buses[i];
        check(&c,0,old_ignored,COUNT(old_ignored),1,1,buses[i] == 2 ? 0 : -1,0,1);
    }
    c.bus = 1; c.bus_core_id = 0x820;
    check(&c,0,old_ignored,COUNT(old_ignored),1,1,-1,0,1);
    c.bus_core_id = 0x83c;
    check(&c,0,old_ignored,COUNT(old_ignored),1,1,-1,0,1);
    c = context(20,0x10000000); check(&c,0,poll_fast,COUNT(poll_fast),1,0,0,0,1);
    check(&c,2,poll_dynamic,COUNT(poll_dynamic),0,0,0,0,1);
    c = context(9,0x10040000); c.bus = 2; c.pci_config_owned = 0;
    check(&c,0,old_pmu,COUNT(old_pmu),1,1,0,0,1);
    c = context(10,0x10040000);
    check(&c,0,middle_pmu,COUNT(middle_pmu),1,0,0,0,1);
}
static void boundaries(void)
{
    static struct event events[4510];
    unsigned mode, finish_ready;
    size_t n, i;
    for (mode = 0; mode <= 2; mode += 2) {
        for (finish_ready = 0; finish_ready < 2; finish_ready++) {
            struct axd_cc_clock_context c = context(20,0x10000000);
            struct event st = ST, sy = SY, r = R(0x1e0,0x8002), w = W(0x1e0,0), d = D(10);
            n = 0; events[n++] = st; events[n++] = r; events[n++] = sy;
            w.op.value = mode == 0 ? 0x8002 : 0x8000; events[n++] = w;
            r.reply = mode == 0 ? 0xa0000001 : 0xa0020001;
            events[n++] = r; events[n++] = sy;
            for (i = 0; i < 1500; i++) {
                events[n++] = d;
                if (finish_ready && i == 1499) r.reply ^= 0x20000;
                events[n++] = r; events[n++] = sy;
            }
            assert(n == 4506);
            check(&c,mode,events,n,mode == 0,0,0,!finish_ready,0);
            /* Fault/cancel/both at the last delay, read and final barrier. */
            for (i = n-2; i <= n; i++) {
                unsigned t;
                for (t = 1; t <= 3; t++) {
                    struct axd_cc_clock job;
                    struct fixture f = {&job,events,n,0,i,t&1u,(t>>1)&1u};
                    assert(axd_cc_clock_begin(&job,mode,&c) == 0); run(&f);
                    assert(f.seen == i && job.completed_ops == i - (t&1u));
                    assert(job.phase == ((t&1u) ? AXD_CC_FAILED : AXD_CC_CANCELLED));
                }
            }
            /* Ready on first post-write sample: no delay or final confirmation. */
            events[4].reply = mode == 0 ? 0x20000 : 0x80000000;
            check(&c,mode,events,6,mode == 0,0,0,0,1);
        }
    }
}
static void guards_and_alias(void)
{
    struct axd_cc_clock job;
    struct axd_cc_clock_context c;
    int32_t revisions[] = {INT32_MIN,-1,0,5};
    uint32_t modes[] = {0,1,2,3,UINT32_MAX};
    size_t i,j;
    for (i=0;i<COUNT(revisions);i++) {
        c = context(revisions[i],UINT32_MAX); c.chipcommon_owned = c.pci_config_owned = 0;
        for (j=0;j<COUNT(modes);j++) check(&c,modes[j],NULL,0,0,0,0,0,0);
    }
    for (i=6;i<=19;i++) {
        c = context((int32_t)i,0); c.pci_config_owned = 0;
        for (j=0;j<COUNT(modes);j++) check(&c,modes[j],NULL,0,modes[j]==0,0,0,0,0);
    }
    c = context(20,UINT32_MAX); c.pci_config_owned = 0;
    for (j=0;j<COUNT(modes);j++) if (modes[j]!=0 && modes[j]!=2)
        check(&c,modes[j],NULL,0,0,0,0,0,0);
    c.chipcommon_owned=0;
    assert(axd_cc_clock_begin(&job,0,&c)==0);
    assert(axd_cc_clock_step(&job,NULL,NULL)==AXD_CC_DEPENDENCY);
    assert(job.dependency==AXD_CC_OWNER && job.completed_ops==0);
    c=context(9,0x40000);c.pci_config_owned=0;
    assert(axd_cc_clock_begin(&job,0,&c)==0);
    while(job.phase==AXD_CC_RUNNING) axd_cc_clock_step(&job,NULL,NULL);
    assert(job.phase==AXD_CC_DEPENDENCY && job.dependency==AXD_CC_PCI_OWNER);
    {
        static const struct event before_dependency[] = {
            R(0xb8,0xdeadf804),SY,ST,W(0xb8,0xdeade004)
        };
        struct fixture f = {&job,before_dependency,COUNT(before_dependency),0,0,0,0};
        assert(axd_cc_clock_begin(&job,2,&c)==0); run(&f);
        assert(job.phase==AXD_CC_DEPENDENCY && job.dependency==AXD_CC_PCI_OWNER);
        assert(f.seen==4 && job.completed_ops==4 && job.crystal_called==1);
        assert(!job.crystal_return_valid && job.cc_writes_since_sample==1);
    }
    c=context(20,0);
    assert(axd_cc_clock_begin(&job,0,&c)==0);
    assert(axd_cc_clock_begin(&job,2,&job.context)==0);
    assert(job.context.revision==20 && job.context.capabilities==0 && job.context.bus==1);
    assert(job.context.bus_core_id==0x804 && job.context.bus_core_revision==13);
    assert(job.context.chipcommon_owned==1 && job.context.pci_config_owned==1 && job.mode==2);
    while(job.phase==AXD_CC_RUNNING) axd_cc_clock_step(&job,NULL,NULL);
    assert(job.phase==AXD_CC_FAILED && job.error==AXD_CC_BAD_ARGUMENT && job.attempted_ops==0);
    assert(axd_cc_clock_begin(&job,0,&c)==0);
    assert(axd_cc_clock_cancel(&job)==AXD_CC_CANCELLED);
    assert(axd_cc_clock_step(&job,NULL,NULL)==AXD_CC_CANCELLED);
    c.chipcommon_owned=2;assert(axd_cc_clock_begin(&job,0,&c)==-1);
    c.chipcommon_owned=1;c.pci_config_owned=2;assert(axd_cc_clock_begin(&job,0,&c)==-1);
    assert(axd_cc_clock_begin(&job,0,NULL)==-1);
    assert(axd_cc_clock_begin(NULL,0,&c)==-1);
    assert(axd_cc_clock_step(NULL,NULL,NULL)==AXD_CC_INVALID);
    assert(axd_cc_clock_cancel(NULL)==AXD_CC_INVALID);
}
int main(void)
{
    literal_traces(); boundaries(); guards_and_alias();
    puts("chipcommon clock tests passed");
    return 0;
}
