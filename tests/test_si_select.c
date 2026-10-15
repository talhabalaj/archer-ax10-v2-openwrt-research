/* SPDX-License-Identifier: MIT */
#include "../driver/si_select.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a)/sizeof((a)[0]))
#define E(k,r,a,s,v,reply) {{k,r,a,s,v,0},reply}
struct event { struct axd_si_operation op; uint32_t reply; };
static const struct axd_si_core catalog[] = {
    {0x800,0x100000,0x200000,0x300000},
    {0x812,0x110000,0x210000,0x310000},
    {0x50b,0x120000,0x220000,0x320000},
    {0x812,0x130000,0x230000,0x330000}
};
static const struct event maps[] = {
    E(AXD_SI_MAP,AXD_SI_CORE,0x110000,0x1000,0,0xc1000000),
    E(AXD_SI_MAP,AXD_SI_PRIMARY_WRAPPER,0x210000,0x1000,0,0xc2000000),
    E(AXD_SI_MAP,AXD_SI_SECONDARY_WRAPPER,0x310000,0x1000,0,0xc3000000)
};
static const struct event pci_ac[] = {
    E(AXD_SI_PCI_WRITE32,AXD_SI_CORE,0x80,4,0x110000,0xdeadbeef),
    E(AXD_SI_PCI_WRITE32,AXD_SI_PRIMARY_WRAPPER,0xac,4,0x210000,0xabcdef01)
};
static const struct event pci_70[] = {
    E(AXD_SI_PCI_WRITE32,AXD_SI_CORE,0x80,4,0x110000,0xdeadbeef),
    E(AXD_SI_PCI_WRITE32,AXD_SI_PRIMARY_WRAPPER,0x70,4,0x210000,0xabcdef01)
};
struct trace {
    struct axd_si_select *job;
    const struct event *events;
    size_t count, seen, fault;
    unsigned cancel, error, check_commits;
};
static struct axd_si_context context(void)
{
    const struct axd_si_context c={1,0,0x804,4,catalog,N(catalog),1}; return c;
}
static struct axd_si_state state(void)
{
    struct axd_si_state s={0};
    s.current_map=0xc0000000; s.current_index=0; s.primary_wrapper=0xc0001000;
    s.cache[0]=(struct axd_si_cache){0xc0000000,0xc0001000,0xc0002000};
    return s;
}
static struct trace trace(struct axd_si_select *j,const struct event *e,size_t n)
{
    const struct trace t={j,e,n,0,(size_t)-1,0,0,0}; return t;
}
static int io(void *opaque,const struct axd_si_operation *op,uint32_t *reply)
{
    struct trace *t=opaque;
    const struct event *e;
    size_t index=t->seen++;
    assert(index<t->count); e=&t->events[index];
    assert(op->kind==e->op.kind && op->resource==e->op.resource && op->address==e->op.address);
    assert(op->size==e->op.size && op->value==e->op.value && op->map_type==0);
    if(t->check_commits) {
        const struct axd_si_select *j=t->job;
        assert(j->state.current_index==0 && j->state.primary_wrapper==0xc0001000);
        if(op->kind==AXD_SI_MAP) {
            assert(j->state.current_map==(index==0?0xc0000000u:0xc1000000u));
            assert(j->state.cache[1].core==(index==0?0u:0xc1000000u));
            assert(j->state.cache[1].primary==(index<2?0u:0xc2000000u));
            assert(j->state.cache[1].secondary==0);
        } else assert(j->state.current_map==0xc0000000);
    }
    *reply=e->reply;
    if(index==t->fault) {
        if(t->cancel) assert(axd_si_select_cancel(t->job)==AXD_SI_CANCELLED);
        if(t->error) return -1;
    }
    return 0;
}
static void run(struct axd_si_select *j,struct trace *t)
{
    unsigned steps=0;
    while(j->phase==AXD_SI_RUNNING) {
        size_t before=t->seen;
        assert(++steps<256); axd_si_select_step(j,io,t); assert(t->seen-before<=1);
        assert(j->attempted_ops==t->seen);
    }
}
static void silent(struct axd_si_select *j,struct trace *t)
{
    struct axd_si_select copy=*j;
    size_t seen=t->seen;
    assert(axd_si_select_step(j,io,t)==copy.phase && axd_si_select_cancel(j)==copy.phase);
    assert(memcmp(&copy,j,sizeof(copy))==0 && t->seen==seen);
}
static void literal_and_faults(void)
{
    struct axd_si_context c;
    struct axd_si_state s=state();
    struct axd_si_select j;
    struct trace t;
    unsigned which,mode;
    size_t i,n;
    for(which=0;which<3;which++) {
        const struct event *e=which==0?maps:(which==1?pci_ac:pci_70);
        n=which==0?N(maps):N(pci_ac); c=context(); c.bus=which==0?0:1;
        c.bus_core_id=which==2?0x83c:0x804;
        assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0x812,0,&c,&s)==0);
        t=trace(&j,e,n); t.check_commits=1; run(&j,&t);
        assert(j.phase==AXD_SI_TRACE_COMPLETE && j.result==AXD_SI_SELECTED && t.seen==n);
        assert(j.selected_index_valid && j.selected_index==1 && j.state.current_index==1);
        assert(j.return_value==(which==0?0xc1000000u:s.current_map));
        assert(j.state.primary_wrapper==(which==0?0xc2000000u:s.primary_wrapper));
        assert(j.current_map_stores==(which==0?2u:1u) && j.wrapper_stores==(which==0));
        assert(j.index_stores==1 && !j.zero_map_results && !j.zero_map_mask);
        assert(memcmp(&j.state.cache[0],&s.cache[0],sizeof(s.cache[0]))==0); silent(&j,&t);
        for(i=0;i<n;i++) for(mode=0;mode<3;mode++) {
            assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0);
            t=trace(&j,e,n); t.fault=i; t.cancel=mode!=0; t.error=mode!=1; t.check_commits=1;
            run(&j,&t);
            assert(j.phase==(mode==1?AXD_SI_CANCELLED:AXD_SI_FAILED));
            assert(j.error==(mode==1?AXD_SI_NO_ERROR:AXD_SI_IO_ERROR));
            assert(t.seen==i+1 && j.completed_ops==i+(mode==1));
            assert(j.state.current_index==0 && j.state.primary_wrapper==s.primary_wrapper);
            assert(j.index_stores==0 && j.wrapper_stores==0 && j.result==AXD_SI_NO_RESULT);
            if(which==0) {
                const size_t committed=i+(mode==1);
                assert(j.state.cache[1].core==(committed>=1?maps[0].reply:0));
                assert(j.state.cache[1].primary==(committed>=2?maps[1].reply:0));
                assert(j.state.cache[1].secondary==(committed>=3?maps[2].reply:0));
                assert(j.state.current_map==(i>=1?maps[0].reply:s.current_map));
                assert(j.current_map_stores==(i>=1));
            } else assert(memcmp(&j.state,&s,sizeof(s))==0);
            silent(&j,&t);
        }
    }
    /* Every internal transition is independently cancellable, including stores. */
    c=context();
    for(i=0;i<12;i++) {
        unsigned steps=0;
        assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0); t=trace(&j,maps,N(maps));
        while(j.phase==AXD_SI_RUNNING && steps++<i) axd_si_select_step(&j,io,&t);
        if(j.phase==AXD_SI_RUNNING) assert(axd_si_select_cancel(&j)==AXD_SI_CANCELLED);
        silent(&j,&t);
    }
}
static void mapping_matrix(void)
{
    struct axd_si_core entries[4];
    struct axd_si_context c=context();
    struct axd_si_state s;
    struct axd_si_select j;
    struct event e[3];
    struct trace t;
    unsigned mask,k,n;
    for(mask=0;mask<64;mask++) {
        memcpy(entries,catalog,sizeof(entries)); c.catalog=entries; s=state(); n=0;
        entries[1].core_physical=(mask&1)?catalog[1].core_physical:0;
        entries[1].primary_physical=(mask&2)?catalog[1].primary_physical:0;
        entries[1].secondary_physical=(mask&4)?catalog[1].secondary_physical:0;
        s.cache[1]=(struct axd_si_cache){(mask&8)?0xd1000000u:0,(mask&16)?0xd2000000u:0,(mask&32)?0xd3000000u:0};
        for(k=0;k<3;k++) if((mask&(1u<<k))!=0 && (mask&(8u<<k))==0) e[n++]=maps[k];
        assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0); t=trace(&j,e,n); run(&j,&t);
        assert(j.phase==AXD_SI_TRACE_COMPLETE && t.seen==n);
        assert(j.state.cache[1].core==((mask&8)?s.cache[1].core:((mask&1)?maps[0].reply:0)));
        assert(j.state.cache[1].primary==((mask&16)?s.cache[1].primary:((mask&2)?maps[1].reply:0)));
        assert(j.state.cache[1].secondary==((mask&32)?s.cache[1].secondary:((mask&4)?maps[2].reply:0)));
        assert(j.return_value==j.state.cache[1].core && j.state.primary_wrapper==j.state.cache[1].primary);
    }
    for(mask=0;mask<8;mask++) {
        s=state(); c=context(); memcpy(e,maps,sizeof(e));
        for(k=0;k<3;k++) if(mask&(1u<<k)) e[k].reply=0;
        assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0); t=trace(&j,e,3); run(&j,&t);
        assert(j.phase==AXD_SI_TRACE_COMPLETE && j.result==AXD_SI_SELECTED && j.state.current_index==1);
        assert(j.zero_map_mask==mask && j.zero_map_results==((mask&1)!=0)+((mask&2)!=0)+((mask&4)!=0));
        assert(j.return_value==e[0].reply && j.state.primary_wrapper==e[1].reply);
    }
    {
        const struct event big[]={
            E(AXD_SI_MAP,AXD_SI_CORE,0x120000,0xf000,0,0xc1000000),
            E(AXD_SI_MAP,AXD_SI_PRIMARY_WRAPPER,0x220000,0x1000,0,0xc2000000),
            E(AXD_SI_MAP,AXD_SI_SECONDARY_WRAPPER,0x320000,0x1000,0,0xc3000000)};
        s=state(); c=context(); assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0x50b,0,&c,&s)==0);
        t=trace(&j,big,N(big)); run(&j,&t); assert(j.state.current_index==2 && t.seen==3);
    }
    {
        const struct event zero_pci[]={
            E(AXD_SI_PCI_WRITE32,AXD_SI_CORE,0x80,4,0,0),
            E(AXD_SI_PCI_WRITE32,AXD_SI_PRIMARY_WRAPPER,0xac,4,0,0)};
        memcpy(entries,catalog,sizeof(entries)); entries[1]=(struct axd_si_core){0x812,0,0,0};
        c=context(); c.bus=1; c.catalog=entries; s=state();
        assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0);
        t=trace(&j,zero_pci,N(zero_pci)); run(&j,&t);
        assert(j.phase==AXD_SI_TRACE_COMPLETE && t.seen==2 && j.return_value==s.current_map);
        assert(j.state.primary_wrapper==s.primary_wrapper && !j.zero_map_results);
    }
}
static void search_dispatch_and_bounds(void)
{
    struct axd_si_context c;
    struct axd_si_state s=state();
    struct axd_si_select j;
    struct trace t;
    unsigned k;
    const uint32_t interconnects[]={0,1,2,3,4,5,0xffffffffu};
    for(k=0;k<N(interconnects);k++) {
        c=context(); c.interconnect=interconnects[k]; c.bus=7;
        assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0); t=trace(&j,NULL,0); run(&j,&t);
        if(c.interconnect==0) assert(j.phase==AXD_SI_DEPENDENCY && j.dependency==AXD_SI_SB_SELECTOR);
        else if(c.interconnect==1 || c.interconnect==3 || c.interconnect==4) {
            assert(j.phase==AXD_SI_TRACE_COMPLETE && j.result==AXD_SI_SELECTED && j.return_value==0);
            assert(j.state.current_index==1 && j.state.current_map==0 && j.state.primary_wrapper==s.primary_wrapper);
            assert(j.current_map_stores==1 && j.wrapper_stores==0 && j.index_stores==1);
        } else assert(j.phase==AXD_SI_TRACE_COMPLETE && j.result==AXD_SI_INTERCONNECT_SKIPPED);
        if(j.result!=AXD_SI_SELECTED) assert(memcmp(&j.state,&s,sizeof(s))==0);
        assert(t.seen==0); silent(&j,&t);
    }
    for(k=0;k<4;k++) {
        c=context(); c.bus=2;
        assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0x812,k,&c,&s)==0); t=trace(&j,NULL,0); run(&j,&t);
        assert(j.phase==AXD_SI_TRACE_COMPLETE && !t.seen);
        if(k<2) assert(j.result==AXD_SI_SELECTED && j.state.current_index==(k==0?1u:3u));
        else assert(j.result==AXD_SI_NO_MATCH && memcmp(&j.state,&s,sizeof(s))==0);
    }
    c=context(); assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0x812,UINT32_MAX,&c,&s)==0);
    t=trace(&j,NULL,0); run(&j,&t); assert(j.result==AXD_SI_NO_MATCH);
    c.interconnect=0; assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0x999,0,&c,&s)==0);
    t=trace(&j,NULL,0); run(&j,&t); assert(j.result==AXD_SI_NO_MATCH && j.dependency==AXD_SI_NO_DEPENDENCY);
    c=context(); c.catalog=NULL;
    assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0x800,0,&c,&s)==0); t=trace(&j,NULL,0); run(&j,&t);
    assert(j.phase==AXD_SI_DEPENDENCY && j.dependency==AXD_SI_CATALOG);
    c.core_count=0; assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0x800,0,&c,&s)==0);
    t=trace(&j,NULL,0); run(&j,&t); assert(j.result==AXD_SI_NO_MATCH);
    assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,0,0,&c,&s)==0);
    t=trace(&j,NULL,0); run(&j,&t); assert(j.result==AXD_SI_OUT_OF_RANGE);
    c=context(); assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,UINT32_MAX,0,&c,&s)==0);
    t=trace(&j,NULL,0); run(&j,&t); assert(j.result==AXD_SI_OUT_OF_RANGE && memcmp(&j.state,&s,sizeof(s))==0);
    c.catalog_length=1; assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0);
    t=trace(&j,NULL,0); run(&j,&t); assert(j.phase==AXD_SI_FAILED && j.error==AXD_SI_BOUNDS);
    c=context(); c.core_count=33; assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,0,0,&c,&s)!=0 && j.error==AXD_SI_BOUNDS);
    {
        struct axd_si_core many[32]={{0}};
        c=context(); c.core_count=32; c.catalog=many; c.catalog_length=32; c.bus=2; many[31].core_id=0xabc;
        assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0xabc,0,&c,&s)==0); t=trace(&j,NULL,0); run(&j,&t);
        assert(j.result==AXD_SI_SELECTED && j.selected_index==31 && j.state.current_index==31);
    }
}
static void owners_alias_and_validation(void)
{
    struct axd_si_context c=context();
    struct axd_si_state s=state();
    struct axd_si_select j;
    struct trace t;
    c.bus=1; c.pci_config_owned=0;
    assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0); t=trace(&j,NULL,0); run(&j,&t);
    assert(j.phase==AXD_SI_DEPENDENCY && j.dependency==AXD_SI_PCI_OWNER && t.seen==0);
    assert(memcmp(&j.state,&s,sizeof(s))==0);
    c=context(); assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0);
    while(j.phase==AXD_SI_RUNNING) axd_si_select_step(&j,NULL,NULL);
    assert(j.error==AXD_SI_BAD_ARGUMENT && j.attempted_ops==0);
    c=context(); c.bus=2; assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0);
    t=trace(&j,NULL,0); run(&j,&t);
    assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,0,0,&j.context,&j.state)==0);
    t=trace(&j,NULL,0); run(&j,&t); assert(j.state.current_index==0 && j.context.catalog==catalog);
    assert(memcmp(&j.state.cache[0],&s.cache[0],sizeof(s.cache[0]))==0);
    c=context(); assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,1,0,&c,&s)==0);
    assert(axd_si_select_cancel(&j)==AXD_SI_CANCELLED); t=trace(&j,NULL,0); silent(&j,&t);
    c.pci_config_owned=2; assert(axd_si_select_begin(&j,AXD_SI_BY_INDEX,0,0,&c,&s)!=0);
    assert(axd_si_select_begin(&j,(enum axd_si_command)0,0,0,&c,&s)!=0);
    assert(axd_si_select_begin(NULL,AXD_SI_BY_ID,0,0,&c,&s)!=0);
    assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0,0,NULL,&s)!=0);
    assert(axd_si_select_begin(&j,AXD_SI_BY_ID,0,0,&c,NULL)!=0);
    assert(axd_si_select_step(NULL,NULL,NULL)==AXD_SI_INVALID && axd_si_select_cancel(NULL)==AXD_SI_INVALID);
    memset(&j,0,sizeof(j)); t=trace(&j,NULL,0); silent(&j,&t);
}
int main(void)
{
    literal_and_faults(); mapping_matrix(); search_dispatch_and_bounds(); owners_alias_and_validation();
    puts("SI selector: literal mapping/PCI order, partial commits, bounded search and callback exits passed");
    return 0;
}
