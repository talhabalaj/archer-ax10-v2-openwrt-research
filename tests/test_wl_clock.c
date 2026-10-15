/* SPDX-License-Identifier: MIT */
#include "../driver/wl_clock.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Synthetic literal events from retained WL/HND assembly, independent of
 * the coordinator. No target address, mapping, IRQ or readiness is assumed. */
#define N(a) (sizeof(a) / sizeof((a)[0]))
enum kind { D_ST, D_SY, D_READ, D_WRITE, D_DELAY,
            IRQ_OFF, IRQ_ON, MAP, S_ST, S_SY, S_READ, S_WRITE,
            PCI_READ, PCI_WRITE, S_DELAY };
struct event { enum kind kind; uint32_t address, offset, size, value, reply; };
#define DS {D_ST,0,0,0,0,0}
#define DF {D_SY,0,0,0,0,0}
#define DR(v) {D_READ,0,0x1e0,0,0,v}
#define DW(v) {D_WRITE,0,0x1e0,0,v,0}
#define DD(v) {D_DELAY,0,0,0,v,0}
#define OFF {IRQ_OFF,0,0,0,0,0x87654321}
#define ON {IRQ_ON,0,0,0,0x87654321,0}
#define M(r,p,m) {MAP,p,r,0x1000,0,m}
#define SS(b) {S_ST,b,0,0,0,0}
#define SF(b) {S_SY,b,0,0,0,0}
#define SR(b,o,v) {S_READ,b,o,4,0,v}
#define SW(b,o,v) {S_WRITE,b,o,4,v,0}
#define PR(o,v) {PCI_READ,0,o,4,0,v}
#define PW(o,v) {PCI_WRITE,0,o,4,v,0}
#define SD(v) {S_DELAY,0,0,0,v,0}
static const struct event direct_fast[] = {
    DS, DR(0x80000010), DF, DW(0x80000012), DD(64), DR(0x20000), DF
};
static const struct event direct_dynamic[] = {DS,DR(0xa5a50003),DF,DW(0xa5a50001)};
static const struct event mapped_fast[] = {
    OFF,M(1,0x18000000,0x10000000),M(2,0x18100000,0x20000000),
    M(3,0x18200000,0x30000000),SS(0x10000000),
    SR(0x10000000,0x1e0,0x80000001),SF(0x10000000),
    SW(0x10000000,0x1e0,0x80000003),SD(150),ON
};
static const struct event fast_mapped[] = {
    SS(0x40003000),SR(0x40003000,0x1e0,0x80000001),SF(0x40003000),
    SW(0x40003000,0x1e0,0x80000003),SD(150)
};
static const struct event ordinary_skip[] = {
    OFF,M(1,0x18000000,0x10000000),M(2,0x18100000,0x20000000),
    M(3,0x18200000,0x30000000),ON
};
static const struct event pci_old_fast[] = {
    OFF,PW(0x80,0x18000000),PW(0xac,0x18100000),
    PR(0xb0,0x110),PR(0xb4,0x201),PR(0xb8,0x302),
    PW(0xb4,0x241),PW(0xb8,0x342),SD(1000),
    SS(0x40000000),SR(0x40000000,0xb8,0xdeadffff),SF(0x40000000),
    SW(0x40000000,0xb8,0xdeadd7ff),SD(150),
    PW(0x80,0x19000000),PW(0xac,0x19100000),ON
};
static const struct axd_si_core cores[] = {
    {0x800,0x18000000,0x18100000,0x18200000},
    {0x812,0x19000000,0x19100000,0x19200000}
};
static struct axd_wl_clock_context context(void)
{
    struct axd_wl_clock_context c;
    memset(&c,0,sizeof(c));
    c.d11.core=130;c.d11.minor=1;c.d11.core_clock_valid=1;
    c.d11.wake_reasons=0x800000a5;
    c.fallback.si.interconnect=1;c.fallback.si.bus_core_id=0x804;
    c.fallback.si.core_count=N(cores);c.fallback.si.catalog=cores;
    c.fallback.si.catalog_length=N(cores);c.fallback.si.pci_config_owned=1;
    c.fallback.state.current_map=0x40000000;c.fallback.state.current_index=1;
    c.fallback.state.primary_wrapper=0x50000000;
    c.fallback.state.cache[1].core=0x40000000;
    c.fallback.state.cache[1].primary=0x50000000;
    c.fallback.state.cache[1].secondary=0x51000000;
    c.fallback.revision=20;c.fallback.chipcommon_owned=1;
    c.fallback.irq_core_id=0x812;c.fallback.disable_callback=0x11110000;
    c.fallback.restore_callback=0x22220000;c.fallback.irq_context=0x33330000;
    return c;
}
struct fixture {
    struct axd_wl_clock *job;
    const struct event *events;
    size_t count, seen, fault;
    unsigned error, cancel, irq_known;
    size_t unknown_at;
    const struct axd_si_state *irq_state;
};
static struct fixture fixture(struct axd_wl_clock *j,const struct event *e,size_t n)
{
    struct fixture f={j,e,n,0,0,0,0,1,0,NULL};return f;
}
static unsigned injected;
static int io(void *opaque,const struct axd_wl_clock_operation *op,struct axd_wl_clock_reply *reply)
{
    struct fixture *f=opaque;
    const struct event *e;
    assert(f->seen<f->count);e=&f->events[f->seen++];
    memset(reply,0,sizeof(*reply));reply->value=e->reply;
    if(e->kind<=D_DELAY) {
        const struct axd_clock_operation *d=&op->operation.d11;
        enum axd_d11_kind k=AXD_D11_STORE_BARRIER;
        assert(op->path==AXD_WLC_D11 && d->region==AXD_CLOCK_CURRENT_D11);
        if(e->kind==D_SY)k=AXD_D11_FULL_BARRIER;
        if(e->kind==D_READ)k=AXD_D11_READ32;
        if(e->kind==D_WRITE)k=AXD_D11_WRITE32;
        if(e->kind==D_DELAY)k=AXD_D11_DELAY_US;
        assert(d->kind==k && d->offset==e->offset && d->value==e->value);
    } else {
        const struct axd_si_clock_operation *s=&op->operation.si;
        enum axd_si_clock_kind k=AXD_SIC_MAP;
        enum axd_si_clock_resource r=AXD_SIC_NONE;
        assert(op->path==AXD_WLC_SI);
        switch(e->kind) {
        case IRQ_OFF:k=AXD_SIC_IRQ_DISABLE;break;
        case IRQ_ON:k=AXD_SIC_IRQ_RESTORE;break;
        case MAP:k=AXD_SIC_MAP;r=(enum axd_si_clock_resource)e->offset;break;
        case S_ST:k=AXD_SIC_STORE_BARRIER;r=AXD_SIC_CHIPCOMMON;break;
        case S_SY:k=AXD_SIC_FULL_BARRIER;r=AXD_SIC_CHIPCOMMON;break;
        case S_READ:k=AXD_SIC_READ32;r=AXD_SIC_CHIPCOMMON;break;
        case S_WRITE:k=AXD_SIC_WRITE32;r=AXD_SIC_CHIPCOMMON;break;
        case PCI_READ:k=AXD_SIC_PCI_READ32;r=AXD_SIC_PCI_CONFIG;break;
        case PCI_WRITE:k=AXD_SIC_PCI_WRITE32;r=AXD_SIC_PCI_CONFIG;break;
        case S_DELAY:k=AXD_SIC_DELAY_US;break;
        default:assert(0);break;
        }
        assert(s->kind==k && s->resource==r && s->address==e->address);
        assert(s->offset==(e->kind==MAP ? 0 : e->offset));
        assert(s->size==e->size && s->value==e->value && s->map_type==0);
        if(e->kind==IRQ_OFF || e->kind==IRQ_ON) {
            assert(s->state!=NULL);
            assert(memcmp(s->state,&f->job->context.fallback.state,sizeof(*s->state))==0);
            assert(s->callback==(e->kind==IRQ_OFF ? 0x11110000u : 0x22220000u));
            assert(s->callback_context==0x33330000);
            reply->irq_state_known=(uint8_t)(f->seen==f->unknown_at ? 0 : f->irq_known);
            reply->irq_state=f->irq_state!=NULL ? *f->irq_state : *s->state;
        } else assert(s->state==NULL && s->callback==0 && s->callback_context==0);
    }
    /* Neither software cache nor reason may commit during hardware callbacks. */
    assert(!f->job->cache_committed && !f->job->reason_committed);
    if(f->seen==f->fault) {
        if(f->cancel)assert(axd_wl_clock_cancel(f->job)==AXD_WLC_CANCELLED);
        if(f->error)return -1;
    }
    return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_wl_clock before=*f->job;
    size_t n=f->seen;
    assert(axd_wl_clock_step(f->job,io,f)==before.phase);
    assert(axd_wl_clock_step(f->job,NULL,NULL)==before.phase);
    assert(axd_wl_clock_cancel(f->job)==before.phase);
    assert(n==f->seen && memcmp(&before,f->job,sizeof(before))==0);
}
static void step(struct fixture *f)
{
    size_t seen=f->seen;
    axd_wl_clock_step(f->job,io,f);
    assert(f->seen-seen<=1 && f->job->attempted_ops==f->seen);
    assert(f->job->completed_ops==f->seen-(f->error && f->seen==f->fault ? 1u : 0u));
    if(f->job->path==AXD_WLC_D11 && f->job->d11_job.command==AXD_CLOCK_SELECT)
        assert(memcmp(&f->job->context.d11,&f->job->d11_job.context,sizeof(f->job->context.d11))==0);
    if(f->job->path==AXD_WLC_SI && f->job->si_job.phase!=AXD_SIC_INVALID)
        assert(memcmp(&f->job->context.fallback,&f->job->si_job.context,sizeof(f->job->context.fallback))==0);
}
static void run(struct fixture *f)
{
    size_t steps=0;
    while(f->job->phase==AXD_WLC_RUNNING) {
        assert(++steps<f->count*30+500);step(f);
    }
    terminal(f);
}
static void check(struct axd_wl_clock_context c,uint32_t mode,
                   const struct event *e,size_t n,unsigned result,unsigned faults)
{
    struct axd_wl_clock j;
    struct fixture f=fixture(&j,e,n);
    unsigned type;size_t p;
    assert(axd_wl_clock_begin(&j,mode,&c)==0);run(&f);
    assert(j.phase==AXD_WLC_TRACE_COMPLETE && j.error==AXD_WLC_NO_ERROR && f.seen==n);
    assert(j.cache_committed && j.context.d11.fast_clock==result);
    if((c.d11.si_capabilities&0x10000000u)!=0) {
        assert(j.path==AXD_WLC_D11 && !j.reason_committed && !j.return_valid);
        assert(j.context.d11.wake_reasons==c.d11.wake_reasons);
    } else {
        assert(j.path==AXD_WLC_SI && j.reason_committed && j.return_valid);
        assert(j.return_value==result && j.override_sample==c.d11.wake_reasons);
        assert(j.context.d11.wake_reasons==(result ? c.d11.wake_reasons|0x10u : c.d11.wake_reasons&~0x10u));
    }
    if(!faults)return;
    for(p=1;p<=n;p++)for(type=1;type<=3;type++) {
        assert(axd_wl_clock_begin(&j,mode,&c)==0);f=fixture(&j,e,n);
        f.fault=p;f.error=type&1u;f.cancel=(type>>1)&1u;
        run(&f);injected++;
        assert(f.seen==p && j.phase==(f.error ? AXD_WLC_FAILED : AXD_WLC_CANCELLED));
        assert(j.error==(f.error ? AXD_WLC_IO_ERROR : AXD_WLC_NO_ERROR));
        assert(!j.cache_committed && !j.reason_committed && !j.return_valid);
        assert(j.context.d11.fast_clock==c.d11.fast_clock);
        assert(j.context.d11.wake_reasons==c.d11.wake_reasons);
        if(j.path==AXD_WLC_SI) {
            unsigned ok=!f.error;
            if(e==mapped_fast) {
                assert(j.si_job.irq_token==(p>1 || (p==1 && ok) ? 0x87654321u : 0));
                assert(j.context.fallback.state.cache[0].core==(p>2 || (p==2 && ok) ? 0x10000000u : 0));
                assert(j.context.fallback.state.cache[0].primary==(p>3 || (p==3 && ok) ? 0x20000000u : 0));
                assert(j.context.fallback.state.cache[0].secondary==(p>4 || (p==4 && ok) ? 0x30000000u : 0));
            }
            if(e[p-1].kind==IRQ_OFF || e[p-1].kind==IRQ_ON)
                assert(j.si_job.state_valid==ok);
        } else {
            assert(j.d11_job.status_observed==(p>2 || (p==2 && !f.error)));
        }
    }
}
static void literal_paths(void)
{
    struct axd_wl_clock_context c=context();
    struct event dynamic[N(fast_mapped)];
    int32_t revs[]={INT32_MIN,-1,0,5};
    size_t i;
    c.d11.si_capabilities=0x10000000;
    /* Unused fallback input is deliberately invalid and never dereferenced. */
    memset(&c.fallback,0xff,sizeof(c.fallback));
    check(c,0,direct_fast,N(direct_fast),1,1);
    c.d11.fast_clock=1;c.d11.si_word20=1;
    check(c,UINT32_MAX,direct_dynamic,N(direct_dynamic),0,1);
    c.d11.core_clock_valid=0;
    check(c,0,NULL,0,1,0);check(c,99,NULL,0,0,0);
    for(i=0;i<N(revs);i++) {
        c=context();memset(&c.fallback,0xff,sizeof(c.fallback));
        c.fallback.revision=revs[i];c.fallback.capabilities=0;
        c.d11.core_clock_valid=255;c.d11.fast_clock=255;c.d11.rsdb=255;
        c.d11.wake_reasons=0xf1234578;
        check(c,0,NULL,0,0,0);check(c,2,NULL,0,0,0);
    }
    c=context();check(c,0,mapped_fast,N(mapped_fast),1,1);
    c=context();c.fallback.revision=19;c.fallback.chipcommon_owned=0;
    check(c,0,ordinary_skip,N(ordinary_skip),1,1);
    c.fallback.revision=20;check(c,UINT32_MAX,ordinary_skip,N(ordinary_skip),0,1);
    c=context();c.fallback.si.bus=1;c.fallback.si.bus_core_id=0x820;
    check(c,0,fast_mapped,N(fast_mapped),1,1);
    memcpy(dynamic,fast_mapped,sizeof(dynamic));dynamic[1].reply=0x80000003;
    dynamic[3].value=0x80000001;
    check(c,2,dynamic,N(dynamic),0,1);
    c.fallback.state.current_map=0;c.fallback.chipcommon_owned=0;
    check(c,0,NULL,0,1,0);check(c,2,NULL,0,0,0);
    c=context();c.fallback.si.bus=1;c.fallback.bus_core_revision=12;
    c.fallback.revision=6;c.d11.si_capabilities=c.fallback.capabilities=0x40000;
    check(c,0,pci_old_fast,N(pci_old_fast),1,1);
}
static void commit_boundaries(void)
{
    struct axd_wl_clock_context c;
    struct axd_wl_clock j;
    struct fixture f;
    unsigned point,result;
    for(result=0;result<2;result++)for(point=0;point<3;point++) {
        c=context();c.d11.fast_clock=(uint8_t)!result;
        c.d11.wake_reasons=result ? 0x800000a5 : 0x800000b5;
        if(!result)c.fallback.revision=5;
        assert(axd_wl_clock_begin(&j,result ? 0u : 2u,&c)==0);
        f=fixture(&j,result ? mapped_fast : NULL,result ? N(mapped_fast) : 0);
        while(!j.return_valid)step(&f);
        assert(!j.cache_committed && j.context.d11.fast_clock==!result);
        assert(j.context.d11.wake_reasons==c.d11.wake_reasons);
        if(point>0) {
            step(&f);assert(j.cache_committed && !j.reason_committed);
            assert(j.context.d11.fast_clock==result && j.context.d11.wake_reasons==c.d11.wake_reasons);
        }
        if(point>1) {
            step(&f);assert(j.reason_committed);
            assert(j.context.d11.wake_reasons==(result ? c.d11.wake_reasons|0x10u : c.d11.wake_reasons&~0x10u));
        }
        assert(axd_wl_clock_cancel(&j)==AXD_WLC_CANCELLED);terminal(&f);
        assert(j.cache_committed==(point>0) && j.reason_committed==(point>1));
    }
}
static unsigned internal_prefixes;
static void cancel_internal_prefixes(void)
{
    struct axd_wl_clock_context c=context();
    struct axd_wl_clock j;
    struct fixture f;
    unsigned stop,steps=0,i;
    assert(axd_wl_clock_begin(&j,0,&c)==0);f=fixture(&j,mapped_fast,N(mapped_fast));
    while(j.phase==AXD_WLC_RUNNING) {step(&f);steps++;}
    for(stop=0;stop<steps;stop++) {
        struct axd_wl_clock_context before;
        uint64_t completed,attempted;
        assert(axd_wl_clock_begin(&j,0,&c)==0);f=fixture(&j,mapped_fast,N(mapped_fast));
        for(i=0;i<stop;i++)step(&f);
        before=j.context;completed=j.completed_ops;attempted=j.attempted_ops;
        assert(axd_wl_clock_cancel(&j)==AXD_WLC_CANCELLED);terminal(&f);
        assert(memcmp(&before,&j.context,sizeof(before))==0);
        assert(j.completed_ops==completed && j.attempted_ops==attempted);
        internal_prefixes++;
    }
}
static void dependencies_and_irq_effects(void)
{
    struct axd_wl_clock_context c=context();
    struct axd_wl_clock j;
    struct fixture f;
    struct axd_si_state changed=c.fallback.state;
    unsigned error;
    assert(axd_wl_clock_begin(&j,0,&c)==0);f=fixture(&j,mapped_fast,N(mapped_fast));f.irq_known=0;
    run(&f);assert(j.phase==AXD_WLC_DEPENDENCY && j.si_job.dependency==AXD_SIC_IRQ_STATE);
    assert(f.seen==1 && !j.si_job.state_valid && j.si_job.irq_token==0x87654321);
    assert(!j.return_valid && !j.cache_committed && !j.reason_committed);
    assert(axd_wl_clock_begin(&j,0,&c)==0);f=fixture(&j,mapped_fast,N(mapped_fast));
    f.unknown_at=N(mapped_fast);run(&f);
    assert(j.phase==AXD_WLC_DEPENDENCY && j.si_job.dependency==AXD_SIC_IRQ_STATE);
    assert(!j.si_job.state_valid && j.si_job.restore_called && !j.return_valid);
    assert(j.context.fallback.state.current_index==1 && !j.cache_committed);
    assert(axd_wl_clock_begin(&j,0,&c)==0);f=fixture(&j,mapped_fast,N(mapped_fast));
    f.irq_known=2;run(&f);
    assert(j.phase==AXD_WLC_FAILED && j.error==AXD_WLC_SI_ERROR && !j.si_job.state_valid);
    changed.current_index=0;changed.current_map=0x98760000;
    for(error=0;error<2;error++) {
        assert(axd_wl_clock_begin(&j,0,&c)==0);f=fixture(&j,mapped_fast,N(mapped_fast));
        f.irq_state=&changed;f.fault=1;f.cancel=1;f.error=error;
        run(&f);assert(j.phase==(error ? AXD_WLC_FAILED : AXD_WLC_CANCELLED));
        assert(j.si_job.state_valid==!error);
        assert(j.context.fallback.state.current_index==(error ? 1u : 0u));
        assert(j.context.fallback.state.current_map==(error ? 0x40000000u : 0x98760000u));
        assert(j.si_job.irq_token==(error ? 0 : 0x87654321));
    }
    c.fallback.disable_callback=c.fallback.restore_callback=0;
    c.fallback.si.catalog=NULL;
    assert(axd_wl_clock_begin(&j,0,&c)==0);f=fixture(&j,NULL,0);run(&f);
    assert(j.phase==AXD_WLC_DEPENDENCY && !j.cache_committed);
    c=context();c.fallback.chipcommon_owned=0;
    assert(axd_wl_clock_begin(&j,0,&c)==0);f=fixture(&j,mapped_fast,4);run(&f);
    assert(j.phase==AXD_WLC_DEPENDENCY && j.si_job.dependency==AXD_SIC_CLOCK_OWNER);
    assert(j.context.fallback.state.current_index==0 && j.context.fallback.state.current_map==0x10000000);
    assert(!j.cache_committed && !j.return_valid);
}
static void arguments_and_alias(void)
{
    struct axd_wl_clock_context c=context();
    struct axd_wl_clock j,empty={0};
    struct fixture f=fixture(&j,NULL,0);
    unsigned i;
    assert(axd_wl_clock_begin(NULL,0,&c)!=0);
    assert(axd_wl_clock_begin(&j,0,NULL)!=0 && j.error==AXD_WLC_BAD_ARGUMENT);terminal(&f);
    assert(axd_wl_clock_step(NULL,NULL,NULL)==AXD_WLC_INVALID);
    assert(axd_wl_clock_cancel(NULL)==AXD_WLC_INVALID);
    f.job=&empty;terminal(&f);f.job=&j;
    c.d11.core=129;assert(axd_wl_clock_begin(&j,0,&c)!=0 && j.error==AXD_WLC_UNSUPPORTED);
    c.d11.core=130;c.d11.minor=2;
    assert(axd_wl_clock_begin(&j,0,&c)!=0 && j.error==AXD_WLC_UNSUPPORTED);
    c=context();c.fallback.capabilities=0x40000;
    assert(axd_wl_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_WLC_FAILED && j.error==AXD_WLC_BINDING && f.seen==0);
    c=context();c.d11.si_capabilities=0x10000000;c.d11.core_clock_valid=2;
    assert(axd_wl_clock_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_WLC_FAILED && j.error==AXD_WLC_D11_ERROR && f.seen==0);
    for(i=0;i<2;i++) {
        c=context();if(i)c.d11.si_capabilities=0x10000000;
        assert(axd_wl_clock_begin(&j,0,&c)==0);
        while(j.phase==AXD_WLC_RUNNING)axd_wl_clock_step(&j,NULL,NULL);
        assert(j.phase==AXD_WLC_FAILED && j.error==AXD_WLC_BAD_ARGUMENT && j.attempted_ops==0);
        terminal(&f);
    }
    c=context();assert(axd_wl_clock_begin(&j,0,&c)==0);
    f=fixture(&j,mapped_fast,N(mapped_fast));run(&f);
    assert(axd_wl_clock_begin(&j,2,&j.context)==0);
    assert(j.context.d11.fast_clock==1 && j.context.fallback.state.cache[0].core==0x10000000);
    assert(j.mode==2 && j.attempted_ops==0 && !j.cache_committed);
    {
        const struct event cached_dynamic[]={OFF,SS(0x10000000),
            SR(0x10000000,0x1e0,0x80000003),SF(0x10000000),
            SW(0x10000000,0x1e0,0x80000001),SD(150),ON};
        f=fixture(&j,cached_dynamic,N(cached_dynamic));run(&f);
        assert(j.phase==AXD_WLC_TRACE_COMPLETE && f.seen==N(cached_dynamic));
        assert(j.context.d11.fast_clock==0 && j.context.d11.wake_reasons==c.d11.wake_reasons);
        assert(j.context.fallback.state.cache[0].secondary==0x30000000);
    }
    assert(axd_wl_clock_begin(&j,0,&j.context)==0);
    assert(axd_wl_clock_cancel(&j)==AXD_WLC_CANCELLED);f=fixture(&j,NULL,0);terminal(&f);
}
int main(void)
{
    literal_paths();commit_boundaries();cancel_internal_prefixes();dependencies_and_irq_effects();arguments_and_alias();
    printf("WL clock: literal direct/SI/map/PCI/IRQ composition, cache/reason boundaries and %u callback exits/%u internal cancel prefixes passed\n",injected,internal_prefixes);
    return 0;
}
