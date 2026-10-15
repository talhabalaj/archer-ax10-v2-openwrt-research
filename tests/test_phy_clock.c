/* SPDX-License-Identifier: MIT */
#include "../driver/phy_clock.h"
#include <assert.h>
#include <stdio.h>

struct event { struct axd_phy_clock_operation op; uint32_t reply; };
#define E(k,r,o,v,a) {{k,r,o,v},a}
#define C AXD_PHY_CLOCK_CURRENT_WRAPPER
#define U AXD_PHY_CLOCK_UNIT1_WRAPPER
#define R(r,a) E(AXD_D11_READ32,r,0x408,0,a)
#define W(r,v) E(AXD_D11_WRITE32,r,0x408,v,0)
#define ST(r) E(AXD_D11_STORE_BARRIER,r,0,0,0)
#define SY(r) E(AXD_D11_FULL_BARRIER,r,0,0,0)
#define D(v) E(AXD_D11_DELAY_US,AXD_PHY_CLOCK_NO_REGION,0,v,0)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static const struct event enable[] = {
    R(C,0xa5a5ffff),SY(C),ST(C),W(C,0xa5a5fdff),R(C,0x12345678),SY(C),
    R(C,0xdeadbeef),SY(C),ST(C),W(C,0xdeadbee1),R(C,0x87654321),SY(C),D(1),
    R(C,0x80000001),SY(C),ST(C),W(C,0x80000005),R(C,0xf0000000),SY(C),D(1)
};
static const struct event enable_rsdb[] = {
    R(C,0xa5a5ffff),SY(C),ST(C),W(C,0xa5a5fdff),R(C,0x12345678),SY(C),
    R(C,0xdeadbeef),SY(C),ST(C),W(C,0xdeadbee1),R(C,0x87654321),SY(C),D(1),
    R(U,0xcafeffff),SY(U),ST(U),W(U,0xcafeffef),R(U,0x13579bdf),SY(U),
    R(C,0x80000001),SY(C),ST(C),W(C,0x80000005),R(C,0xf0000000),SY(C),D(1)
};
static const struct event disable[] = {
    R(C,0xa5a5ffff),SY(C),ST(C),W(C,0xa5a5dfff),R(C,0x11111111),SY(C),D(1),
    R(C,0xcafefefe),SY(C),ST(C),W(C,0xcafefefc),R(C,0x22222222),SY(C),D(1)
};
struct fixture {
    struct axd_phy_clock *job;
    const struct event *events;
    size_t count, seen, fault;
    unsigned error, cancel, initial_done, initial_clock;
};
static struct axd_phy_clock_context context(void)
{
    const struct axd_phy_clock_context c = {130,1,0xb,51,0x2836,0x5555,0xf6ca,1,0,0,0,1,1};
    return c;
}
static int io(void *opaque, const struct axd_phy_clock_operation *op, uint32_t *reply)
{
    struct fixture *f = opaque;
    const struct event *e;
    assert(f->seen < f->count);
    e = &f->events[f->seen++];
    assert(op->kind==e->op.kind && op->region==e->op.region);
    assert(op->offset==e->op.offset && op->value==e->op.value);
    *reply=e->reply;
    if (f->seen==f->fault) {
        if (f->cancel) assert(axd_phy_clock_cancel(f->job)==AXD_PHY_CLOCK_CANCELLED);
        if (f->error) return -1;
    }
    return 0;
}
static void observations(const struct fixture *f)
{
    const struct axd_phy_clock *j=f->job;
    uint32_t sample[2]={0,0},last=0;
    unsigned seen[2]={0,0},newer[2]={0,0},state[2]={0,0},updates[2]={0,0};
    size_t i,n=(size_t)j->completed_ops;
    for(i=0;i<n;i++) {
        const struct axd_phy_clock_operation *o=&f->events[i].op;
        unsigned r;
        if(o->region==AXD_PHY_CLOCK_NO_REGION) continue;
        r=o->region==C ? 0 : 1;
        if(o->kind==AXD_D11_READ32) {
            sample[r]=last=f->events[i].reply;seen[r]=1;newer[r]=0;
            state[r]=state[r]==2 ? 3 : 1;
        } else if(o->kind==AXD_D11_WRITE32) {
            newer[r]=1;state[r]=2;
        } else if(o->kind==AXD_D11_FULL_BARRIER && state[r]==3) {
            updates[r]++;state[r]=0;
        }
    }
    assert(j->last_read==last && j->current_sample==sample[0] && j->unit1_sample==sample[1]);
    assert(j->current_observed==seen[0] && j->unit1_observed==seen[1]);
    assert(j->current_writes_since_sample==newer[0] && j->unit1_writes_since_sample==newer[1]);
    assert(j->current_updates==updates[0] && j->unit1_updates==updates[1]);
    assert(j->context.workaround_done==(f->initial_done || updates[1]!=0));
    if(j->command==AXD_PHY_CLOCK_ENABLE && f->seen) assert(j->context.phy_clock==1);
    if(j->command==AXD_PHY_CLOCK_DISABLE && f->seen) assert(j->context.phy_clock==0);
    if(j->command==AXD_PHY_CLOCK_RESET_PREPARE && f->seen) {
        /* Prep six events + delay2 return before entering clock-on. */
        if(f->seen<7) assert(j->context.phy_clock==f->initial_clock);
        if(f->seen>7) assert(j->context.phy_clock==1);
    }
}
static void run(struct fixture *f)
{
    size_t steps=0;
    while(f->job->phase==AXD_PHY_CLOCK_RUNNING) {
        uint64_t before=f->job->attempted_ops;
        assert(++steps < f->count*6+80);
        axd_phy_clock_step(f->job,io,f);
        assert(f->job->attempted_ops-before<=1 && f->job->attempted_ops==f->seen);
        observations(f);
    }
    {
        enum axd_phy_clock_phase p=f->job->phase;
        size_t n=f->seen;
        assert(axd_phy_clock_step(f->job,io,f)==p);
        assert(axd_phy_clock_step(f->job,NULL,NULL)==p);
        assert(axd_phy_clock_cancel(f->job)==p && n==f->seen);
    }
}
static void check(struct axd_phy_clock_context c,enum axd_phy_clock_command cmd,
                  const struct event *e,size_t n,unsigned current,unsigned unit1)
{
    struct axd_phy_clock job;
    struct fixture f={&job,e,n,0,0,0,0,c.workaround_done,c.phy_clock};
    size_t p;
    unsigned t;
    assert(axd_phy_clock_begin(&job,cmd,&c)==0);run(&f);
    assert(job.phase==AXD_PHY_CLOCK_TRACE_COMPLETE && f.seen==n);
    assert(job.error==AXD_PHY_CLOCK_NO_ERROR && job.completed_ops==n);
    assert(job.current_updates==current && job.unit1_updates==unit1);
    assert(job.context.bandwidth_state==c.bandwidth_state); /* No early bandwidth commit. */
    assert(job.context.phy_clock==(cmd!=AXD_PHY_CLOCK_DISABLE));
    for(p=1;p<=n;p++) for(t=1;t<=3;t++) {
        assert(axd_phy_clock_begin(&job,cmd,&c)==0);
        f.seen=0;f.fault=p;f.error=t&1u;f.cancel=(t>>1)&1u;run(&f);
        assert(job.phase==(f.error ? AXD_PHY_CLOCK_FAILED : AXD_PHY_CLOCK_CANCELLED));
        assert(job.error==(f.error ? AXD_PHY_CLOCK_IO_ERROR : AXD_PHY_CLOCK_NO_ERROR));
        assert(f.seen==p && job.completed_ops==p-f.error);
        assert(job.context.bandwidth_state==c.bandwidth_state);
        if(cmd==AXD_PHY_CLOCK_RESET_PREPARE)
            assert(job.context.phy_clock==(p>7 ? 1 : c.phy_clock));
    }
}
static void traces_and_bandwidth(void)
{
    struct axd_phy_clock_context c=context();
    struct event prep[40];
    uint16_t bits[]={0,0x800,0x1000,0x1800,0x2000,0x2800,0x3000,0x3800};
    uint32_t values[]={0,0,0x40,0x80,0xc0,0x800,0xc0,0};
    size_t i,k,n;
    check(c,AXD_PHY_CLOCK_ENABLE,enable,COUNT(enable),3,0);
    c.rsdb=1;check(c,AXD_PHY_CLOCK_ENABLE,enable_rsdb,COUNT(enable_rsdb),3,1);
    c.workaround_done=1;c.unit1_owned=0;
    check(c,AXD_PHY_CLOCK_ENABLE,enable,COUNT(enable),3,0);
    c.workaround_done=0;c.phy_clock=1;
    check(c,AXD_PHY_CLOCK_DISABLE,disable,COUNT(disable),2,0); /* RSDB never called off. */
    for(i=0;i<COUNT(bits);i++) {
        struct event initial[]={R(C,0xffff0000),SY(C),ST(C),W(C,0),R(C,0xbadc0ffe),SY(C),D(2)};
        struct axd_phy_clock job;
        c=context();c.chanspec=(uint16_t)(0xc037u|bits[i]);
        initial[3].op.value=0xffff0000u|values[i]|0xeu;
        n=0;for(k=0;k<COUNT(initial);k++) prep[n++]=initial[k];
        for(k=0;k<COUNT(enable);k++) prep[n++]=enable[k];
        check(c,AXD_PHY_CLOCK_RESET_PREPARE,prep,n,4,0);
        assert(axd_phy_clock_begin(&job,AXD_PHY_CLOCK_RESET_PREPARE,&c)==0);
        assert(job.bandwidth_flags==values[i]);
        c.current_owned=c.unit1_owned=0;
        assert(axd_phy_clock_begin(&job,AXD_PHY_CLOCK_RECORD_BANDWIDTH,&c)==0);
        assert(axd_phy_clock_step(&job,NULL,NULL)==AXD_PHY_CLOCK_TRACE_COMPLETE);
        assert(job.context.bandwidth_state==bits[i] && job.context.phy_clock==c.phy_clock);
        assert(job.completed_ops==0 && job.attempted_ops==0 && job.context.workaround_done==0);
    }
    /* Prep plus real unit1 branch; neither duplicated pulse nor extra delay. */
    c=context();c.rsdb=1;n=7;
    prep[3].op.value=0xffff080e;
    for(k=0;k<COUNT(enable_rsdb);k++) prep[n++]=enable_rsdb[k];
    check(c,AXD_PHY_CLOCK_RESET_PREPARE,prep,n,4,1);
}
static void dependencies(void)
{
    struct axd_phy_clock_context c=context();
    struct axd_phy_clock job;
    struct fixture f={&job,enable_rsdb,13,0,0,0,0,0,0};
    unsigned cmd;
    c.rsdb=1;c.unit1_owned=0;
    assert(axd_phy_clock_begin(&job,AXD_PHY_CLOCK_ENABLE,&c)==0);run(&f);
    assert(job.phase==AXD_PHY_CLOCK_DEPENDENCY && job.dependency==AXD_PHY_CLOCK_UNIT1_OWNER);
    assert(f.seen==13 && job.context.phy_clock==1 && !job.context.workaround_done);
    assert(job.current_updates==2 && job.unit1_updates==0);
    c.current_owned=0;
    for(cmd=AXD_PHY_CLOCK_ENABLE;cmd<=AXD_PHY_CLOCK_RESET_PREPARE;cmd++) {
        c.phy_clock=cmd==AXD_PHY_CLOCK_DISABLE;
        assert(axd_phy_clock_begin(&job,(enum axd_phy_clock_command)cmd,&c)==0);
        while(job.phase==AXD_PHY_CLOCK_RUNNING) axd_phy_clock_step(&job,NULL,NULL);
        assert(job.phase==AXD_PHY_CLOCK_DEPENDENCY && job.dependency==AXD_PHY_CLOCK_CURRENT_OWNER);
        assert(job.attempted_ops==0 && job.completed_ops==0);
        assert(job.context.phy_clock==(cmd==AXD_PHY_CLOCK_ENABLE));
    }
}
static void validation_and_alias(void)
{
    struct axd_phy_clock_context c=context(),bad;
    struct axd_phy_clock job;
    unsigned v;
    uint8_t interconnect[]={1,3,4};
    for(v=0;v<COUNT(interconnect);v++) {
        c.interconnect=interconnect[v];c.minor=(uint16_t)(v&1u);
        check(c,AXD_PHY_CLOCK_ENABLE,enable,COUNT(enable),3,0);
    }
#define BAD(field,val) do {bad=c;bad.field=val;assert(axd_phy_clock_begin(&job,AXD_PHY_CLOCK_ENABLE,&bad)==-1);assert(job.attempted_ops==0);}while(0)
    BAD(core,129);BAD(minor,2);BAD(phy_type,4);BAD(phy_revision,50);BAD(chip_id,0x4352);
    BAD(interconnect,0);BAD(interconnect,2);BAD(interconnect,5);
    BAD(rsdb,2);BAD(workaround_done,2);BAD(phy_clock,2);BAD(current_owned,2);BAD(unit1_owned,2);
#undef BAD
    assert(axd_phy_clock_begin(&job,0,&c)==-1);
    assert(axd_phy_clock_begin(&job,5,&c)==-1);
    assert(axd_phy_clock_begin(&job,AXD_PHY_CLOCK_ENABLE,NULL)==-1);
    assert(axd_phy_clock_begin(NULL,AXD_PHY_CLOCK_ENABLE,&c)==-1);
    assert(axd_phy_clock_step(NULL,NULL,NULL)==AXD_PHY_CLOCK_INVALID);
    assert(axd_phy_clock_cancel(NULL)==AXD_PHY_CLOCK_INVALID);
    assert(axd_phy_clock_begin(&job,AXD_PHY_CLOCK_ENABLE,&c)==0);
    assert(axd_phy_clock_begin(&job,AXD_PHY_CLOCK_RECORD_BANDWIDTH,&job.context)==0);
    assert(job.context.core==130 && job.context.phy_revision==51 && job.context.chip_id==0xf6ca);
    assert(job.context.chanspec==c.chanspec && job.context.interconnect==c.interconnect);
    assert(axd_phy_clock_step(&job,NULL,NULL)==AXD_PHY_CLOCK_TRACE_COMPLETE);
    assert(job.context.bandwidth_state==0x2800);
    assert(axd_phy_clock_begin(&job,AXD_PHY_CLOCK_ENABLE,&c)==0);
    assert(axd_phy_clock_cancel(&job)==AXD_PHY_CLOCK_CANCELLED);
    assert(axd_phy_clock_step(&job,NULL,NULL)==AXD_PHY_CLOCK_CANCELLED);
    assert(axd_phy_clock_begin(&job,AXD_PHY_CLOCK_ENABLE,&c)==0);
    while(job.phase==AXD_PHY_CLOCK_RUNNING) axd_phy_clock_step(&job,NULL,NULL);
    assert(job.phase==AXD_PHY_CLOCK_FAILED && job.error==AXD_PHY_CLOCK_BAD_ARGUMENT);
    assert(job.context.phy_clock==1 && job.attempted_ops==0);
}
int main(void)
{
    traces_and_bandwidth();dependencies();validation_and_alias();
    puts("PHY clock: literal flags, reset bandwidth, RSDB, owners and callback exits passed");
    return 0;
}
