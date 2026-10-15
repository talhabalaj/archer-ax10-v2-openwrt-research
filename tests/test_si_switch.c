/* SPDX-License-Identifier: MIT */
#include "../driver/si_switch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const struct axd_si_core cores[]={
    {0x812,0x10000,0x20000,0},{0x844,0x30000,0x40000,0},{0x812,0x50000,0x60000,0}
};
struct fixture {struct axd_si_switch *j;unsigned calls,fault,error,cancel,known,change;};
static struct axd_si_switch_context context(void)
{
    struct axd_si_switch_context c={0};unsigned i;
    c.si=(struct axd_si_context){1,0,0x83c,3,cores,3,1};
    c.state.current_map=0x100000;c.state.primary_wrapper=0x200000;
    for(i=0;i<3;i++)c.state.cache[i]=(struct axd_si_cache){0x100000+i*0x10000,0x200000+i*0x10000,0};
    c.irq_core_id=0x812;c.disable_callback=0x111;c.restore_callback=0x222;c.irq_context=0x333;
    c.state_known=1;
    return c;
}
static int io(void *opaque,const struct axd_si_switch_operation *op,struct axd_si_switch_reply *reply)
{
    struct fixture *f=opaque;f->calls++;
    assert(f->j->attempted_ops==f->calls && f->j->completed_ops==f->calls-1);
    if(op->kind==AXD_SIS_SELECT){
        assert(op->select.kind==AXD_SI_PCI_WRITE32);
        assert(op->select.address==(f->calls==2?0x80u:0x70u));
        assert(op->select.value==(f->calls==2?0x30000u:0x40000u));
    }else{
        assert(op->callback==(op->kind==AXD_SIS_IRQ_DISABLE?0x111u:0x222u));
        assert(op->callback_context==0x333 && op->state==&f->j->context.state);
        assert(op->value==(op->kind==AXD_SIS_IRQ_DISABLE?0u:0xabcdu));
        reply->value=0xabcd;reply->irq_state_known=(uint8_t)f->known;
        reply->irq_state=*op->state;
        if(f->change){reply->irq_state.current_index=2;reply->irq_state.current_map=0x120000;}
    }
    if(f->calls==f->fault && f->cancel)axd_si_switch_cancel(f->j);
    if(f->calls==f->fault && f->error)return -1;
    return 0;
}
static void run(struct fixture *f)
{
    unsigned n=0;
    while(f->j->phase==AXD_SIS_RUNNING){unsigned before=f->calls;assert(++n<500);axd_si_switch_step(f->j,io,f);assert(f->calls-before<=1);}
    {struct axd_si_switch before=*f->j;assert(axd_si_switch_step(f->j,NULL,NULL)==before.phase);assert(axd_si_switch_cancel(f->j)==before.phase);assert(memcmp(&before,f->j,sizeof(before))==0);}
}
static void ordinary(void)
{
    struct axd_si_switch_context c=context();struct axd_si_switch j;struct fixture f={&j,0,0,0,0,1,1};
    assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,0xaaaa,0xbbbb,&c)==0);run(&f);
    assert(j.phase==AXD_SIS_TRACE_COMPLETE && j.return_value==0x110000 && j.saved_index==2 && j.irq_token==0xabcd);
    assert(j.disable_called && j.saved_stored && j.token_stored && j.context.state.current_index==1);
    c=j.context;f.calls=0;f.change=0;
    assert(axd_si_switch_begin(&j,AXD_SIS_RESTORE,2,2,0xabcd,&c)==0);run(&f);
    assert(j.phase==AXD_SIS_TRACE_COMPLETE && j.restore_called && j.context.state.current_index==2 && f.calls==1);
    assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,0,0,&j.context)==0 && j.context.state.current_index==2);
    c=context();c.disable_callback=0;
    assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,8,9,&c)==0);f.calls=0;run(&f);
    assert(j.phase==AXD_SIS_TRACE_COMPLETE && j.saved_index==0 && j.irq_token==9 && !j.token_stored && f.calls==0);
    c=context();c.si.bus=1;
    assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,0,0,&c)==0);f.calls=0;f.change=1;run(&f);
    assert(j.phase==AXD_SIS_TRACE_COMPLETE && f.calls==3 && j.saved_index==2 && j.return_value==0x120000);
}
static void faults(void)
{
    unsigned mode,known;struct axd_si_switch_context c=context();struct axd_si_switch j;
    for(mode=1;mode<4;mode++){
        struct fixture f={&j,0,1,mode&1,mode>>1,1,1};
        assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,7,8,&c)==0);run(&f);
        assert(j.phase==(f.error?AXD_SIS_FAILED:AXD_SIS_CANCELLED));
        assert(j.saved_index==7 && j.saved_stored==0);
        assert(j.state_valid==!f.error && j.token_stored==!f.error);
        assert(j.context.state.current_index==(f.error?0u:2u));
    }
    for(known=0;known<=2;known+=2){
        struct fixture f={&j,0,0,0,0,known,0};
        assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,7,8,&c)==0);run(&f);
        assert(j.phase==(known==0?AXD_SIS_DEPENDENCY:AXD_SIS_FAILED) && !j.state_valid && j.token_stored);
        assert(j.saved_index==7);
        assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,7,8,&j.context)==0);f.calls=0;run(&f);
        assert(j.phase==AXD_SIS_DEPENDENCY && j.dependency==AXD_SIS_IRQ_STATE && f.calls==0 && !j.state_valid);
    }
    c.si.catalog=NULL;
    assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,0,0,&c)==0);
    {struct fixture f={&j,0,0,0,0,1,0};run(&f);assert(j.phase==AXD_SIS_DEPENDENCY && !f.calls);}
    c=context();c.si.bus=1;
    for(known=1;known<=3;known++)for(mode=1;mode<4;mode++){
        struct fixture f={&j,0,known,mode&1,mode>>1,1,1};
        assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,7,8,&c)==0);run(&f);
        assert(j.phase==(f.error?AXD_SIS_FAILED:AXD_SIS_CANCELLED) && f.calls==known);
        assert(j.context.state.current_index==(known==1 && f.error?0u:2u));
    }
    c=context();c.state.current_index=1;
    for(mode=1;mode<4;mode++){
        struct fixture f={&j,0,1,mode&1,mode>>1,1,0};
        assert(axd_si_switch_begin(&j,AXD_SIS_RESTORE,2,2,0xabcd,&c)==0);run(&f);
        assert(j.phase==(f.error?AXD_SIS_FAILED:AXD_SIS_CANCELLED) && j.context.state.current_index==2);
        assert(j.context.state_known==!f.error);
    }
}
struct map_fixture {struct axd_si_switch *j;unsigned calls,mode;};
static int map_io(void *opaque,const struct axd_si_switch_operation *op,struct axd_si_switch_reply *reply)
{
    struct map_fixture *f=opaque;f->calls++;
    assert(op->kind==AXD_SIS_SELECT && op->select.kind==AXD_SI_MAP && op->select.size==0x1000);
    assert(op->select.address==(f->calls==1?0x30000u:0x40000u));
    if(f->calls==2){
        assert(f->j->context.state.cache[1].core==0xabc000 && f->j->context.state.current_map==0xabc000);
        assert(f->j->context.state.current_index==0);
        if(f->mode&2)axd_si_switch_cancel(f->j);
        if(f->mode&1)return -1;
    }
    reply->value=f->calls==1?0xabc000:0xdef000;return 0;
}
static void map_coherence(void)
{
    struct axd_si_switch_context c=context();struct axd_si_switch j;unsigned mode,n;
    c.disable_callback=c.restore_callback=0;memset(c.state.cache,0,sizeof(c.state.cache));
    for(mode=0;mode<4;mode++){
        struct map_fixture f={&j,0,mode};
        assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x844,0,0,&c)==0);n=0;
        while(j.phase==AXD_SIS_RUNNING){assert(++n<100);axd_si_switch_step(&j,map_io,&f);}
        assert(f.calls==2 && j.phase==(mode&1?AXD_SIS_FAILED:mode&2?AXD_SIS_CANCELLED:AXD_SIS_TRACE_COMPLETE));
        assert(j.context.state.cache[1].core==0xabc000 && j.context.state.current_map==0xabc000);
        assert(j.context.state.cache[1].primary==(mode&1?0u:0xdef000u));
        assert(j.context.state.current_index==(mode==0?1u:0u));
    }
}
static void fast_paths(void)
{
    static const uint32_t ids[]={0x83c,0x820,0x804};
    struct axd_si_switch j;unsigned i,k;struct axd_si_switch_context c=context();
    c.si.bus=1;c.si.catalog=NULL;c.bus_core_revision=13;
    for(i=0;i<3;i++)for(k=0;k<3;k++){
        struct fixture f={&j,0,0,0,0,1,0};uint32_t map=k==0?0u:k==1?0xffffd000u:0x100000u;
        c.si.bus_core_id=ids[i];c.state.current_map=map;
        assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,0x800,8,9,&c)==0);run(&f);
        assert(j.phase==AXD_SIS_TRACE_COMPLETE && j.return_value==(map==0?0:map+0x3000u));
        assert(j.saved_index==0x800 && j.irq_token==9 && !f.calls);
        assert(axd_si_switch_begin(&j,AXD_SIS_SWITCH,ids[i],8,9,&c)==0);run(&f);
        assert(j.return_value==map+0x2000u && j.saved_index==ids[i] && !f.calls);
        assert(axd_si_switch_begin(&j,AXD_SIS_RESTORE,0x800,8,9,&c)==0);run(&f);
        assert(j.phase==AXD_SIS_TRACE_COMPLETE && !j.selection_completed && !f.calls);
        assert(axd_si_switch_begin(&j,AXD_SIS_RESTORE,ids[i],8,9,&c)==0);run(&f);
        assert(j.phase==AXD_SIS_TRACE_COMPLETE && !j.selection_completed && !f.calls);
    }
    assert(axd_si_switch_begin(NULL,AXD_SIS_SWITCH,0,0,0,&c)==-1);
    assert(axd_si_switch_step(NULL,NULL,NULL)==AXD_SIS_INVALID);
}
int main(void){ordinary();faults();fast_paths();map_coherence();puts("SI switch/restore: actual windows, post-IRQ index, selectors, independent restore and terminal effects passed");return 0;}
