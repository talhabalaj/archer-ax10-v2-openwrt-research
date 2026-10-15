/* SPDX-License-Identifier: MIT */
#include "../driver/si_clock.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Literal behavioral fixtures derived before reading the implementation.
 * ARM32 labels below are synthetic; mapping results never imply ownership. */
enum expected_kind {
    IRQ_OFF, IRQ_ON, MAP, CC_ST, CC_SY, CC_READ, CC_WRITE,
    PCI_READ, PCI_WRITE, DELAY
};
struct event {
    enum expected_kind kind;
    uint32_t base,offset,value,size,reply;
};
#define N(a) (sizeof(a)/sizeof((a)[0]))
#define OFF {IRQ_OFF,0,0,0,0,0x87654321}
#define ON {IRQ_ON,0,0,0x87654321,0,0}
#define MAP_TO(r,p,s,m) {MAP,p,r,0,s,m}
#define ST(b) {CC_ST,b,0,0,0,0}
#define SY(b) {CC_SY,b,0,0,0,0}
#define R(b,o,v) {CC_READ,b,o,0,4,v}
#define W(b,o,v) {CC_WRITE,b,o,v,4,0}
#define PR(o,v) {PCI_READ,0,o,0,4,v}
#define PW(o,v) {PCI_WRITE,0,o,v,4,0}
#define D(v) {DELAY,0,0,v,0,0}
static const struct event body_fast[] = {
    ST(0x10000000),R(0x10000000,0x1e0,0x80000001),SY(0x10000000),
    W(0x10000000,0x1e0,0x80000003),D(150)
};
static const struct event mapped_fast[] = {
    OFF,MAP_TO(1,0x18000000,0x1000,0x10000000),
    MAP_TO(2,0x18100000,0x1000,0x20000000),MAP_TO(3,0x18200000,0x1000,0x30000000),
    ST(0x10000000),R(0x10000000,0x1e0,0x80000001),SY(0x10000000),
    W(0x10000000,0x1e0,0x80000003),D(150),ON
};
static const struct event pci_old_fast[] = {
    OFF,PW(0x80,0x18000000),PW(0xac,0x18100000),
    PR(0xb0,0x110),PR(0xb4,0x201),PR(0xb8,0x302),
    PW(0xb4,0x241),PW(0xb8,0x342),D(1000),
    ST(0x40000000),R(0x40000000,0xb8,0xdeadffff),SY(0x40000000),
    W(0x40000000,0xb8,0xdeadd7ff),D(150),
    PW(0x80,0x19000000),PW(0xac,0x19100000),ON
};
static const struct axd_si_core cores[] = {
    {0x800,0x18000000,0x18100000,0x18200000},
    {0x812,0x19000000,0x19100000,0x19200000},
    {0x83a,0x1a000000,0x1a100000,0x1a200000}
};
static struct axd_si_state state(void)
{
    struct axd_si_state s;
    memset(&s,0,sizeof(s));s.current_map=0x40000000;s.current_index=1;
    s.primary_wrapper=0x50000000;
    s.cache[1].core=0x40000000;s.cache[1].primary=0x50000000;s.cache[1].secondary=0x51000000;
    s.cache[2].core=0x60000000;s.cache[2].primary=0x70000000;s.cache[2].secondary=0x71000000;
    return s;
}
static struct axd_si_context selector(void)
{
    const struct axd_si_context c={1,0,0x804,3,cores,N(cores),1};
    return c;
}
static struct axd_si_clock_context context(void)
{
    struct axd_si_clock_context c;
    memset(&c,0,sizeof(c));c.si=selector();c.state=state();c.revision=20;
    c.irq_core_id=0x812;c.disable_callback=0x11110000;c.restore_callback=0x22220000;
    c.irq_context=0x33330000;c.chipcommon_owned=1;return c;
}
struct fixture {
    struct axd_si_clock *job;
    const struct event *events;
    size_t count,seen,fault;
    unsigned error,cancel,irq_known;
    const struct axd_si_state *disable_state,*restore_state;
    unsigned restore_missing;
};
static int io(void *opaque,const struct axd_si_clock_operation *op,struct axd_si_clock_reply *reply)
{
    struct fixture *f=opaque;
    const struct event *e;
    enum expected_kind kind;
    assert(f->seen<f->count);e=&f->events[f->seen++];
    switch(op->kind) {
    case AXD_SIC_IRQ_DISABLE:kind=IRQ_OFF;break;
    case AXD_SIC_IRQ_RESTORE:kind=IRQ_ON;break;
    case AXD_SIC_MAP:kind=MAP;break;
    case AXD_SIC_STORE_BARRIER:kind=CC_ST;break;
    case AXD_SIC_FULL_BARRIER:kind=CC_SY;break;
    case AXD_SIC_READ32:kind=CC_READ;break;
    case AXD_SIC_WRITE32:kind=CC_WRITE;break;
    case AXD_SIC_PCI_READ32:kind=PCI_READ;break;
    case AXD_SIC_PCI_WRITE32:kind=PCI_WRITE;break;
    case AXD_SIC_DELAY_US:kind=DELAY;break;
    default:assert(0);kind=IRQ_OFF;break;
    }
    assert(kind==e->kind && op->map_type==0);
    assert(op->address==e->base && op->size==e->size && op->value==e->value);
    if(kind==MAP) {
        assert((unsigned)op->resource==e->offset && op->offset==0);
    } else {
        assert(op->offset==e->offset);
        if(kind==CC_ST || kind==CC_SY || kind==CC_READ || kind==CC_WRITE)
            assert(op->resource==AXD_SIC_CHIPCOMMON);
        else if(kind==PCI_READ || kind==PCI_WRITE) assert(op->resource==AXD_SIC_PCI_CONFIG);
        else assert(op->resource==AXD_SIC_NONE);
    }
    memset(reply,0,sizeof(*reply));reply->value=e->reply;
    if(kind==IRQ_OFF || kind==IRQ_ON) {
        const struct axd_si_state *s=kind==IRQ_OFF ? f->disable_state : f->restore_state;
        assert(op->state!=NULL && memcmp(op->state,&f->job->context.state,sizeof(*op->state))==0);
        assert(op->callback==(kind==IRQ_OFF ? 0x11110000u : 0x22220000u));
        assert(op->callback_context==0x33330000);
        reply->irq_state_known=(uint8_t)(kind==IRQ_ON && f->restore_missing ? 0 : f->irq_known);
        reply->irq_state=s!=NULL ? *s : *op->state;
    } else assert(op->callback==0 && op->callback_context==0 && op->state==NULL);
    if(f->seen==f->fault) {
        if(f->cancel) assert(axd_si_clock_cancel(f->job)==AXD_SIC_CANCELLED);
        if(f->error) return -1;
    }
    return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_si_clock before=*f->job;
    size_t seen=f->seen;
    enum axd_si_clock_phase p=f->job->phase;
    assert(axd_si_clock_step(f->job,io,f)==p);
    assert(axd_si_clock_step(f->job,NULL,NULL)==p);
    assert(axd_si_clock_cancel(f->job)==p);
    assert(f->seen==seen && memcmp(&before,f->job,sizeof(before))==0);
}
static void run(struct fixture *f)
{
    size_t steps=0;
    while(f->job->phase==AXD_SIC_RUNNING) {
        size_t seen=f->seen;
        struct axd_si_select child=f->job->selector;
        assert(++steps<f->count*20+400);
        axd_si_clock_step(f->job,io,f);
        assert(f->seen-seen<=1 && f->job->attempted_ops==f->seen);
        assert(f->job->completed_ops==f->seen-(f->error && f->seen==f->fault ? 1u : 0u));
        if(memcmp(&child,&f->job->selector,sizeof(child))!=0)
            assert(memcmp(&f->job->context.state,&f->job->selector.state,sizeof(child.state))==0);
    }
    terminal(f);
}
static void check(struct axd_si_clock_context c,uint32_t mode,const struct event *e,size_t n,
                   unsigned result,unsigned fast,unsigned skipped,unsigned faults)
{
    struct axd_si_clock j;
    struct fixture f={&j,e,n,0,0,0,0,1,NULL,NULL,0};
    size_t p;
    unsigned type;
    assert(axd_si_clock_begin(&j,mode,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_TRACE_COMPLETE && j.error==AXD_SIC_NO_ERROR);
    assert(j.return_value==result && j.fast_mapping==fast && j.map_skipped==skipped);
    assert(f.seen==n && j.completed_ops==n && j.state_valid);
    if(!faults) return;
    for(p=1;p<=n;p++) for(type=1;type<=3;type++) {
        assert(axd_si_clock_begin(&j,mode,&c)==0);f.seen=0;f.fault=p;
        f.error=type&1u;f.cancel=(type>>1)&1u;run(&f);
        assert(j.phase==(f.error ? AXD_SIC_FAILED : AXD_SIC_CANCELLED));
        assert(j.error==(f.error ? AXD_SIC_IO_ERROR : AXD_SIC_NO_ERROR));
        assert(f.seen==p && j.completed_ops==p-f.error);
    }
}
static void mapped_and_partial(void)
{
    struct axd_si_clock_context c=context();
    struct axd_si_clock j;
    struct fixture f={&j,mapped_fast,N(mapped_fast),0,0,0,0,1,NULL,NULL,0};
    size_t p;
    unsigned type;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_TRACE_COMPLETE && j.return_value==1 && f.seen==N(mapped_fast));
    assert(j.disable_called && j.restore_called && j.irq_token==0x87654321);
    assert(j.saved_index==1 && j.mapping==0x10000000);
    assert(j.selection_completed && j.body_completed && j.restoration_completed);
    assert(j.context.state.current_map==0x40000000 && j.context.state.current_index==1);
    assert(j.context.state.primary_wrapper==0x50000000 && j.context.state.cache[0].core==0x10000000);
    for(p=1;p<=N(mapped_fast);p++) for(type=1;type<=3;type++) {
        unsigned ok=!(type&1u);
        assert(axd_si_clock_begin(&j,0,&c)==0);f.seen=0;f.fault=p;
        f.error=type&1u;f.cancel=(type>>1)&1u;run(&f);
        assert(j.phase==(f.error ? AXD_SIC_FAILED : AXD_SIC_CANCELLED));
        assert(j.error==(f.error ? AXD_SIC_IO_ERROR : AXD_SIC_NO_ERROR));
        assert(j.disable_called==(p>1 || (p==1 && ok)));
        assert(j.restore_called==(p==10 && ok));
        assert(j.irq_token==(p>1 || (p==1 && ok) ? 0x87654321u : 0));
        assert(j.context.state.cache[0].core==(p>2 || (p==2 && ok) ? 0x10000000u : 0));
        assert(j.context.state.cache[0].primary==(p>3 || (p==3 && ok) ? 0x20000000u : 0));
        assert(j.context.state.cache[0].secondary==(p>4 || (p==4 && ok) ? 0x30000000u : 0));
        assert(j.context.state.current_map==(p<=2 || p==10 ? 0x40000000u : 0x10000000u));
        assert(j.context.state.current_index==(p<=4 || p==10 ? 1u : 0u));
        assert(j.context.state.primary_wrapper==(p<=4 || p==10 ? 0x50000000u : 0x20000000u));
        assert(j.selection_completed==(p>4) && j.restoration_completed==(p==10));
        assert(j.body_completed==(p==10));
        assert(j.clock_job.cc_sample_observed==(p>6 || (p==6 && ok)));
        assert(j.clock_job.cc_writes_since_sample==(p>8 || (p==8 && ok)));
    }
}
static void early_and_fast(void)
{
    struct axd_si_clock_context c;
    struct event e[N(body_fast)];
    int32_t revisions[]={INT32_MIN,-1,0,5};
    uint32_t ids[]={0x83c,0x820,0x804,0x804};
    uint32_t brevs[]={0,0,13,UINT32_MAX};
    size_t i,k;
    for(i=0;i<N(revisions);i++) {
        memset(&c,0xff,sizeof(c));c.revision=revisions[i];
        check(c,0,NULL,0,0,0,0,0);check(c,2,NULL,0,0,0,0,0);
    }
    for(i=0;i<N(ids);i++) {
        c=context();c.si.bus=1;c.si.bus_core_id=ids[i];c.bus_core_revision=brevs[i];
        /* Fast path does not consult catalog/index/IRQ or selector ownership. */
        c.si.catalog=NULL;c.si.catalog_length=0;c.si.core_count=UINT32_MAX;
        c.state.current_index=UINT32_MAX;c.si.pci_config_owned=0;
        for(k=0;k<N(e);k++) {e[k]=body_fast[k];if(e[k].kind!=DELAY)e[k].base=0x40003000;}
        check(c,0,e,N(e),1,1,0,1);
        e[1].reply=0xa5a5a5a7;e[3].value=0xa5a5a5a5;
        check(c,2,e,N(e),0,1,0,1);
        c.state.current_map=0;c.chipcommon_owned=0;
        check(c,0,NULL,0,1,1,1,0);check(c,2,NULL,0,0,1,1,0);
        c.state.current_map=0xffffd000;
        check(c,0,NULL,0,1,1,1,0);
    }
    c=context();c.si.bus=1;c.si.bus_core_id=0x820;c.state.current_map=0xffffe000;
    for(k=0;k<N(e);k++) {e[k]=body_fast[k];if(e[k].kind!=DELAY)e[k].base=0x1000;}
    check(c,0,e,N(e),1,1,0,1);
    c.revision=19;c.capabilities=0;c.chipcommon_owned=0;
    check(c,0,NULL,0,1,1,0,0);
    c.revision=20;check(c,UINT32_MAX,NULL,0,0,1,0,0);
}
static void body_skips_and_pci(void)
{
    const struct event no_body[]={OFF,MAP_TO(1,0x18000000,0x1000,0x10000000),
        MAP_TO(2,0x18100000,0x1000,0x20000000),MAP_TO(3,0x18200000,0x1000,0x30000000),ON};
    const struct event cached[]={OFF,
        ST(0x10000000),R(0x10000000,0x1e0,0x80000001),SY(0x10000000),
        W(0x10000000,0x1e0,0x80000003),D(150),ON};
    const struct event pci_skip[]={OFF,PW(0x80,0x18000000),PW(0xac,0x18100000),
        PW(0x80,0x19000000),PW(0xac,0x19100000),ON};
    const struct event poll_dynamic[]={OFF,
        ST(0x10000000),R(0x10000000,0x1e0,0x8002),SY(0x10000000),
        W(0x10000000,0x1e0,0x8000),R(0x10000000,0x1e0,0x20001),SY(0x10000000),
        D(10),R(0x10000000,0x1e0,1),SY(0x10000000),ON};
    struct axd_si_clock_context c=context();
    c.revision=19;c.chipcommon_owned=0;
    check(c,0,no_body,N(no_body),1,0,0,1);
    c.revision=6;check(c,2,no_body,N(no_body),0,0,0,1);
    c.revision=20;check(c,3,no_body,N(no_body),0,0,0,1);
    c=context();c.state.cache[0].core=0x10000000;c.state.cache[0].primary=0x20000000;
    c.state.cache[0].secondary=0x30000000;check(c,0,cached,N(cached),1,0,0,1);
    c.capabilities=0x10000000;check(c,2,poll_dynamic,N(poll_dynamic),0,0,0,1);
    c=context();c.si.bus=1;c.bus_core_revision=12;c.revision=6;c.capabilities=0x40000;
    check(c,0,pci_old_fast,N(pci_old_fast),1,0,0,1);
    c.revision=19;c.capabilities=0;c.chipcommon_owned=0;
    check(c,0,pci_skip,N(pci_skip),1,0,0,1);
    c.revision=20;check(c,UINT32_MAX,pci_skip,N(pci_skip),0,0,0,1);
}
static void irq_reports(void)
{
    struct axd_si_clock_context c=context();
    struct axd_si_clock j;
    struct axd_si_state changed=state();
    struct event e[N(mapped_fast)];
    struct fixture f={&j,e,0,0,0,0,0,1,NULL,NULL,0};
    unsigned type;
    size_t i;
    memcpy(e,mapped_fast,sizeof(e));e[0].reply=0;e[N(e)-1].value=0;
    check(c,0,e,N(e),1,0,0,1); /* Actual disable may legitimately return token0. */
    /* Restore is independently present even if disable callback is absent. */
    c.disable_callback=0;
    for(i=1;i<N(mapped_fast);i++)e[i-1]=mapped_fast[i];
    e[N(mapped_fast)-2].value=0;f.count=N(mapped_fast)-1;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_TRACE_COMPLETE && !j.disable_called && j.restore_called && !j.irq_token);
    c=context();c.restore_callback=0;check(c,0,mapped_fast,N(mapped_fast)-1,1,0,0,1);
    /* Disable callback changes actual SI index: save AFTER callback, restore
     * index2, then test current-ID guard independently (no IRQ restore). */
    c=context();changed.current_index=2;changed.current_map=0x60000000;
    changed.primary_wrapper=0x70000000;
    memcpy(e,mapped_fast,sizeof(e));f.count=N(mapped_fast)-1;f.seen=0;f.disable_state=&changed;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_TRACE_COMPLETE && j.saved_index==2 && !j.restore_called);
    assert(j.context.state.current_index==2 && j.context.state.current_map==0x60000000);
    assert(j.context.state.primary_wrapper==0x70000000 && j.irq_token==0x87654321);
    /* Both IRQ callbacks explicitly report actual state. A restore may itself
     * change SI state, and successful cancellation keeps that reported effect. */
    f.count=N(mapped_fast);f.disable_state=NULL;f.restore_state=&changed;f.seen=0;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_TRACE_COMPLETE && j.restore_called && j.state_valid);
    assert(j.context.state.current_index==2 && j.context.state.current_map==0x60000000);
    f.restore_state=NULL;
    for(type=0;type<4;type++) {
        f.count=1;f.seen=0;f.irq_known=0;f.fault=1;f.error=type&1u;f.cancel=(type>>1)&1u;
        assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_SIC_FAILED : f.cancel ? AXD_SIC_CANCELLED : AXD_SIC_DEPENDENCY));
        assert(j.error==(f.error ? AXD_SIC_IO_ERROR : AXD_SIC_NO_ERROR));
        assert(j.disable_called==!f.error && j.irq_token==(f.error ? 0 : 0x87654321));
        assert(!j.state_valid && !j.selection_completed && !j.restore_called);
        if(!f.error && !f.cancel) assert(j.dependency==AXD_SIC_IRQ_STATE);
    }
    f.irq_known=2;f.seen=f.fault=f.error=f.cancel=0;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_FAILED && j.error==AXD_SIC_BAD_ARGUMENT);
    assert(!j.state_valid && j.disable_called && j.irq_token==0x87654321);
    f.count=1;f.irq_known=1;f.disable_state=&changed;
    for(type=1;type<=3;type++) {
        f.seen=0;f.fault=1;f.error=type&1u;f.cancel=(type>>1)&1u;
        assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_SIC_FAILED : AXD_SIC_CANCELLED));
        assert(j.context.state.current_index==(f.error ? 1u : 2u));
        assert(!j.selection_completed && j.state_valid==!f.error);
        assert(j.saved_index==1); /* No post-cancel saved-index reload. */
    }
    /* A successful cancelling restore callback also commits its explicit
     * reported SI state. Error+cancel instead keeps the pre-callback restore. */
    f.count=N(mapped_fast);f.disable_state=NULL;f.restore_state=&changed;
    for(type=1;type<=3;type++) {
        f.seen=0;f.fault=N(mapped_fast);f.error=type&1u;f.cancel=(type>>1)&1u;
        assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_SIC_FAILED : AXD_SIC_CANCELLED));
        assert(j.restore_called==!f.error && j.restoration_completed && j.body_completed);
        assert(j.context.state.current_index==(f.error ? 1u : 2u));
        assert(j.context.state.current_map==(f.error ? 0x40000000u : 0x60000000u));
        assert(j.irq_token==0x87654321 && j.state_valid==!f.error);
    }
    /* A missing restore snapshot invalidates observed SI state; selection
     * restoration already happened, but wrapper cannot invent its final state. */
    f.restore_state=NULL;f.irq_known=1;f.restore_missing=1;
    for(type=0;type<4;type++) {
        f.seen=0;f.fault=N(mapped_fast);f.error=type&1u;f.cancel=(type>>1)&1u;
        assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_SIC_FAILED : f.cancel ? AXD_SIC_CANCELLED : AXD_SIC_DEPENDENCY));
        assert(j.restore_called==!f.error && !j.state_valid);
        assert(j.restoration_completed && j.body_completed && j.irq_token==0x87654321);
        assert(j.context.state.current_index==1);
    }
}
static void null_mapping_and_dependencies(void)
{
    struct axd_si_clock_context c=context();
    struct axd_si_clock j;
    struct event e[N(mapped_fast)];
    struct fixture f={&j,e,0,0,0,0,0,1,NULL,NULL,0};
    size_t i;
    memcpy(e,mapped_fast,sizeof(e));e[1].reply=0;e[4].base=0;f.count=5;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_FAILED && j.error==AXD_SIC_BINDING && f.seen==5);
    assert(j.selection_completed && !j.body_completed && !j.restoration_completed);
    assert(j.context.state.current_index==0 && j.context.state.current_map==0);
    assert(j.context.state.cache[0].core==0 && j.selector.zero_map_results==1);
    assert(j.disable_called && !j.restore_called && j.irq_token==0x87654321);
    /* Zero map does not prevent gate/mode skip or actual index/IRQ restore. */
    e[4]=mapped_fast[N(mapped_fast)-1];f.count=5;f.seen=0;c.revision=19;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_TRACE_COMPLETE && j.return_value && j.body_completed);
    assert(j.context.state.current_map==0x40000000 && j.context.state.current_index==1);
    assert(j.selection_completed && j.restoration_completed && j.restore_called);
    c.revision=20;f.seen=0;
    assert(axd_si_clock_begin(&j,3,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_TRACE_COMPLETE && !j.return_value && j.body_completed);
    /* Ordinary old revision has crystal config work before first CC read;
     * only its reached dereference of zero is rejected, with no fake restore. */
    {
        struct event old[N(pci_old_fast)];
        memcpy(old,pci_old_fast,sizeof(old));old[9].base=0;
        c=context();c.si.bus=1;c.bus_core_revision=12;c.revision=6;
        c.capabilities=0x40000;c.state.current_map=0;
        f.events=old;f.count=10;f.seen=0;
        assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==AXD_SIC_FAILED && j.error==AXD_SIC_BINDING && f.seen==10);
        assert(j.clock_job.crystal_called && j.clock_job.crystal_return_valid);
        assert(j.selection_completed && !j.restoration_completed && !j.restore_called);
        assert(j.context.state.current_index==0 && j.context.state.current_map==0);
    }
    c=context();c.chipcommon_owned=0;f.events=mapped_fast;f.count=4;f.seen=0;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_DEPENDENCY && j.dependency==AXD_SIC_CLOCK_OWNER && f.seen==4);
    assert(j.context.state.current_index==0 && !j.restoration_completed && !j.restore_called);
    c=context();c.si.bus=1;c.bus_core_revision=12;c.si.pci_config_owned=0;f.count=1;f.seen=0;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_DEPENDENCY && j.dependency==AXD_SIC_SELECTION && f.seen==1);
    assert(j.context.state.current_index==1 && !j.selection_completed && !j.restore_called);
    c=context();c.si.interconnect=0;f.seen=0;
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_DEPENDENCY && j.selector.dependency==AXD_SI_SB_SELECTOR);
    assert(f.seen==1 && j.disable_called && !j.restore_called && !j.selection_completed);
    /* Different initial runtime ID skips both independently guarded callbacks. */
    c=context();c.irq_core_id=0x83a;
    for(i=1;i<N(mapped_fast)-1;i++)e[i-1]=mapped_fast[i];
    check(c,0,e,N(mapped_fast)-2,1,0,0,1);
    /* Actual selector returns zero for a missing ID/unknown interconnect.
     * No-body calls still restore and apply the independent IRQ guard. */
    {
        struct axd_si_core no_cc[N(cores)];
        const struct event only_irq[]={OFF,ON};
        memcpy(no_cc,cores,sizeof(no_cc));no_cc[0].core_id=0x999;
        c=context();c.si.catalog=no_cc;
        check(c,3,only_irq,N(only_irq),0,0,0,1);
        c=context();c.si.interconnect=5;
        check(c,3,only_irq,N(only_irq),0,0,0,1);
    }
}
static void validation_and_alias(void)
{
    struct axd_si_clock_context c=context();
    struct axd_si_clock j;
    const struct event restart[]={OFF,
        ST(0x10000000),R(0x10000000,0x1e0,0x80000001),SY(0x10000000),
        W(0x10000000,0x1e0,0x80000003),D(150),ON};
    struct fixture f={&j,mapped_fast,N(mapped_fast),0,0,0,0,1,NULL,NULL,0};
    assert(axd_si_clock_begin(NULL,0,&c)==-1 && axd_si_clock_begin(&j,0,NULL)==-1);
    assert(axd_si_clock_step(NULL,NULL,NULL)==AXD_SIC_INVALID);
    assert(axd_si_clock_cancel(NULL)==AXD_SIC_INVALID);
    assert(axd_si_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_SIC_TRACE_COMPLETE);
    f.events=restart;f.count=N(restart);f.seen=0;
    assert(axd_si_clock_begin(&j,0,&j.context)==0);run(&f);
    assert(j.phase==AXD_SIC_TRACE_COMPLETE && j.context.state.cache[0].core==0x10000000);
    assert(j.context.state.current_index==1 && j.irq_token==0x87654321);
    assert(axd_si_clock_begin(&j,0,&c)==0);
    assert(axd_si_clock_cancel(&j)==AXD_SIC_CANCELLED);terminal(&f);
    assert(!j.disable_called && !j.attempted_ops && j.context.state.current_index==1);
    assert(axd_si_clock_begin(&j,0,&c)==0);
    while(j.phase==AXD_SIC_RUNNING) axd_si_clock_step(&j,NULL,NULL);
    assert(j.phase==AXD_SIC_FAILED && j.error==AXD_SIC_BAD_ARGUMENT && !j.attempted_ops && j.state_valid);
}
int main(void)
{
    mapped_and_partial();early_and_fast();body_skips_and_pci();irq_reports();
    null_mapping_and_dependencies();validation_and_alias();
    puts("SI clock: literal selection/IRQ/CC traces, partial states and callback exits passed");
    return 0;
}
