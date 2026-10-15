/* SPDX-License-Identifier: MIT */
#include "../driver/dma_rx_take.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))
/* Independently derived from the closed current RX leaf. Slot values are
 * opaque synthetic ARM32 labels, not CPU pointers, buffers or ownership bits. */
struct event {enum axd_rx_take_kind kind;uint32_t identity,offset,value,reply;};
#define SR(v) {AXD_RX_TAKE_STATUS_READ32,0x1111,0x10,0,v}
#define SY {AXD_RX_TAKE_FULL_BARRIER,0x1111,0,0,0}
#define R(i,v) {AXD_RX_TAKE_SLOT_READ32,0x2222,(i)*4,0,v}
#define W(i) {AXD_RX_TAKE_SLOT_WRITE32,0x2222,(i)*4,0,0}
static const struct event refreshed[]={SR(0xf0000110),SY,R(6,0xabcdef12),W(6)};
static const struct event shortcut[]={R(6,0x12345678),W(6)};
static struct axd_rx_take_context context(void)
{
    struct axd_rx_take_context c;memset(&c,0,sizeof(c));
    c.ring.count=8;c.ring.pointer_mask=0x1fff;c.ring.pointer_base=0x100;c.ring.in=6;c.ring.out=2;
    c.core=130;c.minor=1;c.cached_current=6;
    c.status_owned=c.slots_owned=1;c.status_identity=0x1111;c.slots_identity=0x2222;
    c.available=99;c.slot_count=8;return c;
}
struct fixture {
    struct axd_dma_rx_take *job;const struct event *events;size_t count,seen,fault;
    unsigned error,cancel,clears;uint32_t status_sample,slot_sample;
};
static struct fixture fixture(struct axd_dma_rx_take *j,const struct event *e,size_t count)
{
    struct fixture f;memset(&f,0,sizeof(f));f.job=j;f.events=e;f.count=count;return f;
}
static int io(void *opaque,const struct axd_rx_take_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;
    if(f->seen>=f->count)fprintf(stderr,"RX unexpected callback kind%u id%x offset%x in%u out%u cached%u expected%zu\n",(unsigned)op->kind,op->identity,op->offset,f->job->context.ring.in,f->job->context.ring.out,f->job->context.cached_current,f->count);
    assert(f->seen<f->count);e=&f->events[f->seen++];
    assert(op->kind==e->kind && op->identity==e->identity && op->offset==e->offset && op->value==e->value);
    assert(f->job->attempted_ops==f->seen && f->job->completed_ops==f->seen-1);
    if(op->kind==AXD_RX_TAKE_SLOT_READ32 || op->kind==AXD_RX_TAKE_SLOT_WRITE32)
        assert(op->offset==f->job->context.ring.in*4u);
    else assert(f->job->context.cached_current==f->job->context.ring.in);
    if(f->seen==f->fault && f->cancel)assert(axd_dma_rx_take_cancel(f->job)==AXD_RX_TAKE_CANCELLED);
    if(f->seen==f->fault && f->error)return -1;
    *reply=e->reply;
    if(op->kind==AXD_RX_TAKE_STATUS_READ32)f->status_sample=e->reply;
    if(op->kind==AXD_RX_TAKE_SLOT_READ32)f->slot_sample=e->reply;
    if(op->kind==AXD_RX_TAKE_SLOT_WRITE32)f->clears++;
    return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_dma_rx_take before=*f->job;size_t seen=f->seen;
    assert(axd_dma_rx_take_step(f->job,io,f)==before.phase);
    assert(axd_dma_rx_take_step(f->job,NULL,NULL)==before.phase);
    assert(axd_dma_rx_take_cancel(f->job)==before.phase);
    assert(seen==f->seen && !memcmp(&before,f->job,sizeof(before)));
}
static void run(struct fixture *f)
{
    size_t steps=0;
    while(f->job->phase==AXD_RX_TAKE_RUNNING){
        size_t before=f->seen;assert(++steps<f->count*8+100);
        axd_dma_rx_take_step(f->job,io,f);assert(f->seen-before<=1);
        assert(f->job->attempted_ops==f->seen);
        assert(f->job->completed_ops==f->seen-(f->error && f->seen==f->fault ? 1u:0u));
        assert(f->job->status_sample==f->status_sample && f->job->slot_sample==f->slot_sample);
        assert(f->job->removed_slots==f->clears);
    }
    terminal(f);
}
static void execute(struct axd_dma_rx_take *j,uint32_t force,struct axd_rx_take_context c,
    const struct event *events,size_t count,uint32_t result)
{
    struct fixture f=fixture(j,events,count);assert(axd_dma_rx_take_begin(j,force,&c)==0);run(&f);
    assert(j->phase==AXD_RX_TAKE_DONE && j->error==AXD_RX_TAKE_NO_ERROR && j->return_valid);
    assert(j->return_value==result && f.seen==count);
}
static void normal_and_faults(void)
{
    struct axd_dma_rx_take j;struct axd_rx_take_context c=context();size_t p;unsigned mode;
    execute(&j,0,c,refreshed,N(refreshed),0xabcdef12);
    assert(j.context.ring.in==7 && j.context.available==4 && j.context.cached_current==1);
    for(p=1;p<=N(refreshed);p++)for(mode=1;mode<=3;mode++){
        struct fixture f=fixture(&j,refreshed,N(refreshed));f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(axd_dma_rx_take_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_RX_TAKE_FAILED : AXD_RX_TAKE_CANCELLED));
        assert(j.error==(f.error ? AXD_RX_TAKE_IO_ERROR : AXD_RX_TAKE_NO_ERROR) && f.seen==p);
        assert(j.context.ring.in==6 && j.context.available==99 && !j.return_valid);
        assert(j.context.cached_current==(p>2 ? 1u:6u));
    }
    c.cached_current=1;c.status_owned=0;c.status_identity=0;
    execute(&j,0,c,shortcut,N(shortcut),0x12345678);
    assert(j.context.ring.in==7 && j.context.available==4);
    {
        const struct event zero[]={R(6,0),W(6)};
        execute(&j,0,c,zero,N(zero),0);assert(j.removed_slots==1 && j.context.ring.in==7);
    }
}
static void force_and_modes(void)
{
    struct axd_dma_rx_take j;struct axd_rx_take_context c=context();unsigned mode;
    const struct event none[]={SR(0xa0000160),SY};
    const struct event forced[]={SR(0xa0000160),SY,R(6,0x87654321),W(6)};
    execute(&j,0,c,none,N(none),0);assert(j.context.ring.in==6 && j.context.available==99 && !j.removed_slots);
    execute(&j,1,c,forced,N(forced),0x87654321);
    execute(&j,UINT32_MAX,c,forced,N(forced),0x87654321);
    for(mode=0;mode<4;mode++){
        const struct event one[]={R(6,0x80000000),W(6)};
        const struct event two[]={R(6,0x80000000),W(6),R(7,0x1234),W(7)};
        c=context();c.cached_current=1;
        c.sentinel_mode_a=(uint8_t)(mode&1 ? 0x80:0);
        c.sentinel_mode_b=(uint8_t)(mode&2 ? 0xff:0);
        execute(&j,0,c,mode ? two:one,mode ? N(two):N(one),mode ? 0x1234:0x80000000);
        assert(j.context.ring.in==(mode ? 0u:7u) && j.context.available==(mode ? 5u:4u));
        assert(j.skipped_sentinels==!!mode);
    }
}
static void sentinel_loops_and_faults(void)
{
    struct axd_dma_rx_take j;struct axd_rx_take_context c=context();size_t p;unsigned mode;
    const struct event stopped[]={R(6,0x80000000),W(6),R(7,0x80000000),W(7),
        R(0,0x80000000),W(0),SR(0xf0000110),SY};
    const struct event force[]={R(6,0x80000000),W(6),R(7,0x80000000),W(7),
        R(0,0x80000000),W(0),SR(0xf0000110),SY,R(1,0x9876),W(1)};
    c.cached_current=1;c.sentinel_mode_a=1;
    execute(&j,0,c,stopped,N(stopped),0);
    assert(j.context.ring.in==1 && j.context.available==6 && j.removed_slots==3 && j.skipped_sentinels==3);
    execute(&j,UINT32_MAX,c,force,N(force),0x9876);
    assert(j.context.ring.in==2 && j.context.available==7 && j.removed_slots==4);
    for(p=1;p<=N(force);p++)for(mode=1;mode<=3;mode++){
        struct fixture f=fixture(&j,force,N(force));f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(axd_dma_rx_take_begin(&j,1,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_RX_TAKE_FAILED : AXD_RX_TAKE_CANCELLED));
        assert(!j.return_valid && f.seen==p);
        if(p<=2)assert(j.context.ring.in==6 && j.context.available==99);
        else if(p<=4)assert(j.context.ring.in==7 && j.context.available==4);
        else if(p<=6)assert(j.context.ring.in==0 && j.context.available==5);
        else assert(j.context.ring.in==1 && j.context.available==6);
    }
    {
        const struct event empty[]={R(6,0x80000000),W(6),R(7,0x80000000),W(7)};
        c.ring.out=0;c.cached_current=0;
        execute(&j,0,c,empty,N(empty),0);assert(j.context.ring.in==0 && j.context.available==7);
    }
}
static void bindings_bounds_and_alias(void)
{
    struct axd_dma_rx_take j;struct axd_rx_take_context c=context();struct fixture f;unsigned kind;
    c.ring.in=c.ring.out;c.status_owned=c.slots_owned=0;c.status_identity=c.slots_identity=0;
    execute(&j,0,c,NULL,0,0);assert(j.context.available==99);
    for(kind=0;kind<3;kind++){
        const struct event status[]={SR(0xf0000110),SY};
        c=context();f=fixture(&j,status,kind==0 ? 0:N(status));
        if(kind==0)c.status_owned=0;
        if(kind==1)c.slots_owned=0;
        if(kind==2)c.slot_count=6;
        assert(axd_dma_rx_take_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==(kind<2 ? AXD_RX_TAKE_DEPENDENCY : AXD_RX_TAKE_FAILED));
        assert(j.context.ring.in==6 && j.context.available==99);
    }
    for(kind=0;kind<2;kind++){
        struct event e[]={SR(0),SY};c=context();e[0].reply=kind ? 0xf0000180:0xf0000111;
        f=fixture(&j,e,N(e));assert(axd_dma_rx_take_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==AXD_RX_TAKE_FAILED && j.error==AXD_RX_TAKE_BOUNDS && j.context.cached_current==6);
    }
    c=context();execute(&j,0,c,refreshed,N(refreshed),0xabcdef12);
    {
        const struct event e[]={R(7,0x1357),W(7)};f=fixture(&j,e,N(e));
        assert(axd_dma_rx_take_begin(&j,0,&j.context)==0);run(&f);
        assert(j.phase==AXD_RX_TAKE_DONE && j.return_value==0x1357 && j.context.ring.in==0 && j.context.available==5);
    }
    c=context();c.core=129;assert(axd_dma_rx_take_begin(&j,0,&c)<0 && j.error==AXD_RX_TAKE_UNSUPPORTED);
    assert(axd_dma_rx_take_begin(NULL,0,&c)<0);
    assert(axd_dma_rx_take_step(NULL,NULL,NULL)==AXD_RX_TAKE_INVALID);
    assert(axd_dma_rx_take_cancel(NULL)==AXD_RX_TAKE_INVALID);
}
int main(void)
{
    normal_and_faults();force_and_modes();sentinel_loops_and_faults();bindings_bounds_and_alias();
    puts("current DMA RX take: independent status/slot traces, sentinel loops and callback prefixes passed");return 0;
}
