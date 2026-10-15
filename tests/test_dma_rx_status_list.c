/* SPDX-License-Identifier: MIT */
#include "../driver/dma_rx_status_list.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a)/sizeof((a)[0]))
struct event {enum axd_rx_status_list_kind kind;uint32_t identity,offset,value,reply;};
#define SR(v) {AXD_RX_STATUS_LIST_STATUS_READ32,0x1111,0x10,0,v}
#define SY {AXD_RX_STATUS_LIST_FULL_BARRIER,0x1111,0,0,0}
#define R(p,n) {AXD_RX_STATUS_LIST_NODE_NEXT_READ32,p,0x1c,0,n}
#define D(i) {AXD_RX_STATUS_LIST_DESCRIPTOR_READ32,0x2222,(i)*16+8,0,0xdead0000+(i)}
#define W(p,b) {AXD_RX_STATUS_LIST_NODE_NEXT_WRITE8,p,0x1c+(b),0,0}
/* A status node NEXT has four independent byte stores. The normal packet list
 * WRITE32 and budget rules must not leak into this separate closed leaf. */
static const struct event three[]={SR(0xf0000110),SY,R(0x1000,0x2000),D(6),R(0x2000,0x3000),D(7),R(0x3000,0),D(0),W(0x3000,0),W(0x3000,1),W(0x3000,2),W(0x3000,3)};
static const struct event cached[]={R(0x1000,0x2000),D(6),W(0x1000,0),W(0x1000,1),W(0x1000,2),W(0x1000,3)};
static struct axd_rx_status_list_context context(void)
{
    struct axd_rx_status_list_context c;memset(&c,0,sizeof(c));
    c.ring.count=8;c.ring.in=6;c.ring.out=7;c.ring.pointer_mask=0x1fff;c.ring.pointer_base=0x100;
    c.core=130;c.minor=1;c.cached_current=6;c.head=0x1000;c.tail=0x3000;c.available=99;
    c.status_identity=0x1111;c.descriptor_identity=0x2222;c.descriptor_bytes=128;
    c.status_owned=c.descriptor_owned=c.node_owned=1;return c;
}
struct fixture {struct axd_dma_rx_status_list *j;const struct event *events;size_t count,seen,fault;unsigned error,cancel,bytes;uint32_t status,next,desc;};
static struct fixture fixture(struct axd_dma_rx_status_list *j,const struct event *e,size_t n)
{struct fixture f;memset(&f,0,sizeof(f));f.j=j;f.events=e;f.count=n;return f;}
static int io(void *opaque,const struct axd_rx_status_list_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;assert(f->seen<f->count);e=&f->events[f->seen++];
    assert(op->kind==e->kind && op->identity==e->identity && op->offset==e->offset && op->value==e->value);
    assert(f->j->attempted_ops==f->seen && f->j->completed_ops==f->seen-1);
    assert(f->j->context.ring.in==6 && f->j->context.available==99 && f->j->context.tail==0x3000);
    if(op->kind==AXD_RX_STATUS_LIST_DESCRIPTOR_READ32)assert(f->j->context.head==f->next);
    if(f->seen==f->fault && f->cancel)assert(axd_dma_rx_status_list_cancel(f->j)==AXD_RX_STATUS_LIST_CANCELLED);
    if(f->seen==f->fault && f->error)return -1;
    *reply=e->reply;
    if(op->kind==AXD_RX_STATUS_LIST_STATUS_READ32)f->status=e->reply;
    if(op->kind==AXD_RX_STATUS_LIST_NODE_NEXT_READ32)f->next=e->reply;
    if(op->kind==AXD_RX_STATUS_LIST_DESCRIPTOR_READ32)f->desc=e->reply;
    if(op->kind==AXD_RX_STATUS_LIST_NODE_NEXT_WRITE8)f->bytes++;
    return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_dma_rx_status_list before=*f->j;size_t n=f->seen;
    assert(axd_dma_rx_status_list_step(f->j,io,f)==before.phase && axd_dma_rx_status_list_step(f->j,NULL,NULL)==before.phase);
    assert(axd_dma_rx_status_list_cancel(f->j)==before.phase && f->seen==n && !memcmp(&before,f->j,sizeof(before)));
}
static void run(struct fixture *f)
{
    unsigned steps=0;
    while(f->j->phase==AXD_RX_STATUS_LIST_RUNNING){
        size_t n=f->seen;assert(++steps<1000);axd_dma_rx_status_list_step(f->j,io,f);assert(f->seen-n<=1);
        assert(f->j->status_sample==f->status && f->j->next_sample==f->next && f->j->descriptor_sample==f->desc);
        assert(f->j->tail_bytes_cleared==f->bytes);
    }
    assert(f->j->attempted_ops==f->seen && f->j->completed_ops==f->seen-(f->error && f->seen==f->fault?1u:0u));
    terminal(f);
}
static void execute(struct axd_dma_rx_status_list *j,uint32_t force,struct axd_rx_status_list_context c,const struct event *e,size_t n,uint32_t result)
{
    struct fixture f=fixture(j,e,n);assert(!axd_dma_rx_status_list_begin(j,force,&c));run(&f);
    assert(j->phase==AXD_RX_STATUS_LIST_DONE && j->return_valid && j->return_value==result && f.seen==n);
}
static void normal_and_faults(void)
{
    struct axd_dma_rx_status_list j;struct axd_rx_status_list_context c=context();size_t p;unsigned mode;
    execute(&j,0,c,three,N(three),0x1000);
    assert(j.context.ring.in==1 && j.context.available==1 && j.context.cached_current==1 && !j.context.head && !j.context.tail);
    assert(j.traversed_entries==3 && j.tail_bytes_cleared==4 && j.head_stores==3 && j.tail_cleared);
    for(p=1;p<=N(three);p++)for(mode=1;mode<=3;mode++){
        struct fixture f=fixture(&j,three,N(three));f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(!axd_dma_rx_status_list_begin(&j,0,&c));run(&f);
        assert(j.phase==(f.error?AXD_RX_STATUS_LIST_FAILED:AXD_RX_STATUS_LIST_CANCELLED));
        assert(j.error==(f.error?AXD_RX_STATUS_LIST_IO_ERROR:AXD_RX_STATUS_LIST_NO_ERROR) && f.seen==p && !j.return_valid);
        assert(j.context.ring.in==6 && j.context.available==99 && j.context.tail==0x3000);
        assert(j.context.cached_current==(p>2?1u:6u));
        assert(j.context.head==(p<=3?0x1000u:p<=5?0x2000u:p<=7?0x3000u:0u));
        assert(j.tail_bytes_cleared==(p<9?0u:(unsigned)(p-8)-(f.error?1u:0u)));
    }
    for(mode=0;mode<2;mode++){
        struct fixture f=fixture(&j,three,N(three));unsigned steps=0;
        assert(!axd_dma_rx_status_list_begin(&j,0,&c));
        while(!(mode?j.tail_cleared:j.index_committed)){assert(++steps<100);axd_dma_rx_status_list_step(&j,io,&f);}
        axd_dma_rx_status_list_cancel(&j);terminal(&f);
        assert(j.context.ring.in==1 && j.context.available==99 && j.context.tail==(mode?0u:0x3000u));
    }
    c.cached_current=7;c.status_owned=0;execute(&j,0,c,cached,N(cached),0x1000);
    assert(j.context.ring.in==7 && j.context.available==7 && j.context.head==0x2000 && j.context.tail==0x3000 && j.context.cached_current==7);
    c.cached_current=6;execute(&j,UINT32_MAX,c,cached,N(cached),0x1000);assert(j.context.cached_current==6);
}
static void gates_and_dependencies(void)
{
    struct axd_dma_rx_status_list j;struct axd_rx_status_list_context c=context();unsigned k;
    const struct event status[]={SR(0xf0000160),SY};
    execute(&j,0,c,status,N(status),0);assert(j.context.ring.in==6 && j.context.available==99);
    c.ring.out=6;c.status_owned=0;execute(&j,1,c,NULL,0,0);
    c=context();c.ring.count=0;c.ring.in=c.ring.out=0xffff;c.cached_current=0xffff;c.ring.pointer_mask=3;
    c.status_owned=c.node_owned=c.descriptor_owned=0;execute(&j,0,c,NULL,0,0);
    for(k=0;k<5;k++){
        const struct event prefix[]={SR(0xf0000110),SY,R(0x1000,0x2000)};
        struct fixture f=fixture(&j,prefix,k==0?0:k<3?2:3);c=context();
        if(k==0)c.status_owned=0;if(k==1)c.node_owned=0;if(k==2)c.head=0;
        if(k==3)c.descriptor_owned=0;if(k==4)c.descriptor_bytes=107;
        assert(!axd_dma_rx_status_list_begin(&j,0,&c));run(&f);
        assert(j.phase==(k==0||k==1||k==3?AXD_RX_STATUS_LIST_DEPENDENCY:AXD_RX_STATUS_LIST_FAILED));
        assert(j.context.ring.in==6 && j.context.available==99 && j.context.head==(k>=3?0x2000u:c.head));
    }
    for(k=0;k<2;k++){
        struct event e[]={SR(0),SY};struct fixture f=fixture(&j,e,N(e));c=context();e[0].reply=k?0xf0000180:0xf0000111;
        assert(!axd_dma_rx_status_list_begin(&j,0,&c));run(&f);assert(j.error==AXD_RX_STATUS_LIST_BOUNDS && j.context.cached_current==6);
    }
    {
        const struct event e[]={SR(0xf0000110),SY,R(0x1000,0),D(6)};
        struct fixture f=fixture(&j,e,N(e));c=context();
        assert(!axd_dma_rx_status_list_begin(&j,0,&c));run(&f);
        assert(j.phase==AXD_RX_STATUS_LIST_FAILED && j.error==AXD_RX_STATUS_LIST_BINDING);
        assert(!j.context.head && j.head_stores==1 && j.traversed_entries==1 && j.context.ring.in==6 && j.context.available==99);
    }
    c=context();execute(&j,0,c,three,N(three),0x1000);
    assert(!axd_dma_rx_status_list_begin(&j,1,&j.context));assert(j.context.ring.in==1 && !j.context.head && j.context.available==1);axd_dma_rx_status_list_cancel(&j);
    c.cached_current=8;assert(axd_dma_rx_status_list_begin(&j,0,&c)<0 && j.error==AXD_RX_STATUS_LIST_BOUNDS);
    c.core=129;assert(axd_dma_rx_status_list_begin(&j,0,&c)<0 && j.error==AXD_RX_STATUS_LIST_UNSUPPORTED);
    assert(axd_dma_rx_status_list_begin(NULL,0,&c)<0 && axd_dma_rx_status_list_begin(&j,0,NULL)<0);
    assert(axd_dma_rx_status_list_step(NULL,NULL,NULL)==AXD_RX_STATUS_LIST_INVALID && axd_dma_rx_status_list_cancel(NULL)==AXD_RX_STATUS_LIST_INVALID);
}
int main(void){normal_and_faults();gates_and_dependencies();puts("current RX status list: literal byte detach, cache gates and all callback prefixes passed");return 0;}
