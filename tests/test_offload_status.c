/* SPDX-License-Identifier: MIT */
#include "../driver/offload_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct event {unsigned kind,si_kind;uint32_t offset,value,reply,consumer;};
static const struct event wrapped[]={
    {AXD_OS_BITMAP_READ8,0,0,0,0x44,6},{AXD_OS_BITMAP_WRITE8,0,0,4,0,6},
    {AXD_OS_BITMAP_READ8,0,0,0,4,6},{AXD_OS_BITMAP_READ8,0,0,0,4,7},
    {AXD_OS_BITMAP_READ8,0,0,0,4,0},{AXD_OS_BITMAP_READ8,0,0,0,4,1},
    {AXD_OS_BITMAP_READ8,0,0,0,4,2},
    {AXD_OS_STORE_BARRIER,0,0,0,0,2},{AXD_OS_WRITE32,0,0x20,2,0,2}
};
static const struct axd_si_core cores[]={{0x812,0x10000,0x20000,0},{0x844,0x30000,0x40000,0}};
static const struct event pci[]={
    {AXD_OS_BITMAP_READ8,0,0,0,0x44,6},{AXD_OS_BITMAP_WRITE8,0,0,4,0,6},
    {AXD_OS_BITMAP_READ8,0,0,0,4,6},{AXD_OS_BITMAP_READ8,0,0,0,4,7},
    {AXD_OS_BITMAP_READ8,0,0,0,4,0},{AXD_OS_BITMAP_READ8,0,0,0,4,1},
    {AXD_OS_BITMAP_READ8,0,0,0,4,2},
    {AXD_OS_SI,AXD_SIS_IRQ_DISABLE,0,0,0xabcd,2},
    {AXD_OS_SI,AXD_SIS_SELECT,0x80,0x30000,0,2},{AXD_OS_SI,AXD_SIS_SELECT,0x70,0x40000,0,2},
    {AXD_OS_STORE_BARRIER,0,0,0,0,2},{AXD_OS_WRITE32,0,0x20,2,0,2},
    {AXD_OS_SI,AXD_SIS_SELECT,0x80,0x10000,0,2},{AXD_OS_SI,AXD_SIS_SELECT,0x70,0x20000,0,2},
    {AXD_OS_SI,AXD_SIS_IRQ_RESTORE,0,0xabcd,0,2}
};
struct fixture {struct axd_offload_status *j;const struct event *e;size_t count,seen,fault;unsigned error,cancel;};
static struct axd_offload_status_context context(unsigned bus)
{
    struct axd_offload_status_context c={0};
    c.count=8;c.consumer=6;c.producer=3;c.bitmap=0x5000;c.bitmap_bytes=1;c.registers=0x6000;
    c.bitmap_owned=c.registers_owned=c.os_owned=c.si_known=1;
    c.si.si=(struct axd_si_context){1,bus,0x83c,2,cores,2,1};
    c.si.state.current_map=0x8000;c.si.irq_core_id=0x812;
    c.si.state_known=1;
    c.si.disable_callback=0x111;c.si.restore_callback=0x222;c.si.irq_context=0x333;
    return c;
}
static int io(void *opaque,const struct axd_offload_status_operation *op,struct axd_offload_status_reply *reply)
{
    struct fixture *f=opaque;const struct event *e;
    assert(f->seen<f->count);e=&f->e[f->seen++];
    assert(op->kind==e->kind && f->j->context.consumer==e->consumer);
    assert(f->j->attempted_ops==f->seen && f->j->completed_ops==f->seen-1);
    if(op->kind==AXD_OS_SI){
        assert(op->si.kind==e->si_kind);
        if(op->si.kind==AXD_SIS_SELECT){
            assert(op->si.select.kind==AXD_SI_PCI_WRITE32 && op->si.select.size==4);
            assert(op->si.select.address==e->offset && op->si.select.value==e->value);
        }else{
            assert(op->si.callback==(op->si.kind==AXD_SIS_IRQ_DISABLE?0x111u:0x222u));
            assert(op->si.callback_context==0x333 && op->si.value==e->value);
            reply->si.irq_state_known=1;reply->si.irq_state=*op->si.state;
        }
        reply->si.value=e->reply;
    }else{
        assert(op->offset==e->offset && op->value==e->value);
        assert(op->identity==(op->kind==AXD_OS_STORE_BARRIER?0u:op->kind==AXD_OS_WRITE32?0x6000u:0x5000u));
        reply->value=e->reply;
    }
    if(f->seen==f->fault && f->cancel)axd_offload_status_cancel(f->j);
    if(f->seen==f->fault && f->error)return -1;
    return 0;
}
static void run(struct fixture *f)
{
    unsigned n=0;
    while(f->j->phase==AXD_OS_RUNNING){size_t seen=f->seen;assert(++n<1000);axd_offload_status_step(f->j,io,f);assert(f->seen-seen<=1);}
    {struct axd_offload_status before=*f->j;assert(axd_offload_status_step(f->j,NULL,NULL)==before.phase);assert(axd_offload_status_cancel(f->j)==before.phase);assert(memcmp(f->j,&before,sizeof(before))==0);}
}
static void traces(unsigned bus)
{
    struct axd_offload_status_context c=context(bus);struct axd_offload_status j;size_t p;unsigned mode;
    struct fixture base={&j,bus?pci:wrapped,bus?sizeof(pci)/sizeof(pci[0]):sizeof(wrapped)/sizeof(wrapped[0]),0,0,0,0},f=base;
    assert(axd_offload_status_begin(&j,6,&c)==0);run(&f);
    assert(j.phase==AXD_OS_TRACE_COMPLETE && f.seen==f.count && j.consumer_stores==4 && j.register_writes==1 && j.context.consumer==2);
    if(bus)assert(j.context.si.state.current_index==0 && j.saved_index==0 && j.irq_token==0xabcd && j.si_state_valid);
    assert(axd_offload_status_begin(&j,6,&j.context)==0 && j.context.consumer==2);
    for(p=1;p<=base.count;p++)for(mode=1;mode<4;mode++){
        f=base;f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(axd_offload_status_begin(&j,6,&c)==0);run(&f);
        assert(j.phase==(f.error?AXD_OS_FAILED:AXD_OS_CANCELLED) && f.seen==p);
        assert(j.error==(f.error?AXD_OS_IO_ERROR:AXD_OS_NO_ERROR));
        assert(j.attempted_ops==p && j.completed_ops==p-(f.error!=0));
        assert(j.context.consumer==f.e[p-1].consumer);
        if(bus && (p==8 || p==15))assert(j.si_state_valid==!f.error);
    }
}
static void gates(void)
{
    struct axd_offload_status_context c=context(0);struct axd_offload_status j;struct fixture f={&j,wrapped,9,0,0,0,0};
    c.si_known=c.registers_owned=c.os_owned=0;c.producer=6;
    assert(axd_offload_status_begin(&j,6,&c)==0);run(&f);
    assert(j.phase==AXD_OS_TRACE_COMPLETE && f.seen==3 && j.consumer_stores==0);
    c=context(0);c.si_known=0;f.seen=0;
    assert(axd_offload_status_begin(&j,6,&c)==0);run(&f);
    assert(j.phase==AXD_OS_DEPENDENCY && j.dependency==AXD_OS_SI_BINDING && f.seen==7 && j.context.consumer==2);
    c=context(0);c.bitmap_bytes=0;f.seen=0;
    assert(axd_offload_status_begin(&j,6,&c)==0);run(&f);
    assert(j.phase==AXD_OS_FAILED && j.error==AXD_OS_BOUNDS && !f.seen);
    c=context(0);c.registers_owned=0;f.seen=0;
    assert(axd_offload_status_begin(&j,6,&c)==0);run(&f);
    assert(j.phase==AXD_OS_DEPENDENCY && j.dependency==AXD_OS_REGISTER_OWNER && f.seen==8);
    c=context(0);assert(axd_offload_status_begin(&j,-1,&c)==-1 && j.error==AXD_OS_BOUNDS);
    c.count=7;assert(axd_offload_status_begin(&j,6,&c)==-1 && j.error==AXD_OS_BOUNDS);
    assert(axd_offload_status_begin(NULL,0,&c)==-1 && axd_offload_status_step(NULL,NULL,NULL)==AXD_OS_INVALID);
}
int main(void){traces(0);traces(1);gates();puts("Offload status: bitmap RMW/wrap/cursor publication, real SI children and every-event terminal effects passed");return 0;}
