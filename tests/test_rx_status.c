/* SPDX-License-Identifier: MIT */
#include "../driver/rx_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define NODE 0x5000u
#define DATA 0x600000u
#define POOL 0x7000u
struct event { unsigned kind, child; uint32_t identity,offset,value,reply,count; };
struct fixture {
    struct axd_rx_status *job; struct event events[512]; size_t count,seen,fault;
    unsigned error,cancel,delays,reads;
};
static struct axd_rx_status_context context(void)
{
    struct axd_rx_status_context c;memset(&c,0,sizeof(c));
    c.list.ring=(struct axd_dma64_ring){8,0x1fff,0x100,0,1};
    c.list.core=130;c.list.minor=1;c.list.cached_current=1;
    c.list.head=c.list.tail=NODE;c.list.available=99;c.list.descriptor_identity=0x44;
    c.list.descriptor_bytes=128;c.list.descriptor_owned=c.list.node_owned=1;
    c.pool=(struct axd_wake_u32){POOL,1};c.error_count=UINT32_MAX;c.work_limit=100;
    c.nodes_owned=c.data_owned=c.pool_owned=c.os_owned=1;return c;
}
static void add(struct fixture *f,unsigned kind,unsigned child,uint32_t identity,
                uint32_t offset,uint32_t value,uint32_t reply,uint32_t count)
{
    const struct event e={kind,child,identity,offset,value,reply,count};
    assert(f->count<sizeof(f->events)/sizeof(f->events[0]));f->events[f->count++]=e;
}
#define E(k,id,off,v,r,c) add(f,AXD_RS_##k,0,id,off,v,r,c)
#define L(k,id,off,v,r) add(f,AXD_RS_LIST,AXD_RX_STATUS_LIST_##k,id,off,v,r,5)
static void prefix(struct fixture *f)
{
    unsigned i;
    L(NODE_NEXT_READ32,NODE,0x1c,0,0);
    L(DESCRIPTOR_READ32,0x44,8,0,0x76543210);
    for(i=0;i<4;i++)L(NODE_NEXT_WRITE8,NODE,0x1c+i,0,0);
    E(NODE_READ32,NODE,0,0,DATA,5);
}
static void expected(struct fixture *f,struct axd_rx_status *j,unsigned timeout,
                      unsigned callback_mode,uint32_t old_count,uint32_t old_highwater)
{
    unsigned i;uint32_t new_count=old_count+1u;
    memset(f,0,sizeof(*f));f->job=j;prefix(f);
    if(!timeout){E(DATA_READ16,DATA,0,0,0xabcd0008,5);E(NODE_READ32,NODE,0x1c,0,0,6);return;}
    for(i=0;i<200;i++){E(DATA_READ16,DATA,0,0,0,5);E(DELAY_US,0,0,1,0,5);}
    E(NODE_READ32,NODE,0x1c,0,0,5);
    for(i=0;i<4;i++)E(NODE_WRITE8,NODE,0x1c+i,0,0,5);
    E(POOL_READ32,POOL,0x14,0,callback_mode!=0?0x1234:0,5);
    if(callback_mode!=0)E(INDEX_READ16,NODE,0x18,0,callback_mode==1?0xffff:0,5);
    if(callback_mode==2){E(POOL_READ32,POOL,0x10,0,0x9988,5);return;}
    E(POOL_READ32,POOL,4,0,old_count,5);E(POOL_WRITE32,POOL,4,new_count,0,5);
    E(NODE_READ32,NODE,0x1c,0,0,5);E(POOL_READ32,POOL,0xc,0,old_highwater,5);
    E(POOL_READ32,POOL,0,0,0x7777,5);
    /* The observed comparison is SIGNED, including count wrap. */
    if((old_highwater^0x80000000u)>(new_count^0x80000000u))new_count=old_highwater;
    E(POOL_WRITE32,POOL,0xc,new_count,0,5);
    E(NODE_WRITE32,NODE,0x1c,0x7777,0,5);E(POOL_WRITE32,POOL,0,NODE,0,5);
}
static int io(void *opaque,const struct axd_rx_status_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;
    if(f->seen>=f->count)fprintf(stderr,"unexpected status callback kind%u stage%u after%zu\n",op->kind,f->job->stage,f->seen);
    assert(f->seen<f->count);e=&f->events[f->seen++];
    assert((unsigned)op->kind==e->kind);
    if(op->kind==AXD_RS_LIST){
        assert((unsigned)op->list.kind==e->child && op->list.identity==e->identity);
        assert(op->list.offset==e->offset && op->list.value==e->value);
    }else assert(op->identity==e->identity && op->offset==e->offset && op->value==e->value);
    assert(f->job->output.count==e->count);
    assert(f->job->attempted_ops==f->seen && f->job->completed_ops==f->seen-1);
    if(op->kind==AXD_RS_NODE_WRITE8)assert(f->job->head==0 && f->job->context.error_count==UINT32_MAX);
    if(f->seen==f->fault && f->cancel)assert(axd_rx_status_cancel(f->job)==AXD_RS_CANCELLED);
    if(f->seen==f->fault && f->error)return -1;
    *reply=e->reply;
    f->delays+=op->kind==AXD_RS_DELAY_US;f->reads+=op->kind==AXD_RS_DATA_READ16;return 0;
}
static void run(struct fixture *f)
{
    unsigned steps=0;
    while(f->job->phase==AXD_RS_RUNNING){
        size_t n=f->seen;assert(++steps<10000);
        axd_rx_status_step(f->job,io,f);assert(f->seen-n<=1);
        assert(f->job->attempted_ops==f->seen);
        assert(f->job->completed_ops==f->seen-(f->error && f->seen==f->fault?1u:0u));
        assert(f->job->length_reads==f->reads && f->job->delays==f->delays);
    }
    {
        struct axd_rx_status before=*f->job;size_t seen=f->seen;
        assert(axd_rx_status_step(f->job,io,f)==before.phase);
        assert(axd_rx_status_step(f->job,NULL,NULL)==before.phase);
        assert(axd_rx_status_cancel(f->job)==before.phase);
        assert(f->seen==seen && memcmp(&before,f->job,sizeof(before))==0);
    }
}
static void cases_and_faults(unsigned timeout,unsigned callback_mode,uint32_t count,uint32_t high)
{
    struct axd_rx_status j;struct axd_rx_status_context c=context();
    const struct axd_rx_status_output out={0xaaaa,0xbbbb,5};struct fixture f;
    size_t n,p;unsigned mode;
    expected(&f,&j,timeout,callback_mode,count,high);n=f.count;
    assert(axd_rx_status_begin(&j,&out,&c)==0);run(&f);assert(f.seen==n);
    if(callback_mode==2){
        assert(j.phase==AXD_RS_DEPENDENCY && j.dependency==AXD_RS_REGISTERED_POOL_CALLBACK);
        assert(j.callback==0x1234 && j.callback_argument==0x9988 && j.pool_index==0);
        assert(j.context.error_count==UINT32_MAX && j.output.head==out.head && j.output.tail==out.tail);
        return;
    }
    assert(j.phase==AXD_RS_TRACE_COMPLETE && j.result_stores==3);
    assert(j.output.head==(timeout?0u:NODE) && j.output.tail==(timeout?0u:NODE));
    assert(j.output.count==(timeout?5u:6u) && j.context.error_count==(timeout?0u:UINT32_MAX));
    assert(j.context.list.ring.in==1 && j.context.list.available==7 && j.context.list.tail==0);
    assert(j.length_reads==(timeout?200u:1u) && j.delays==(timeout?200u:0u));
    for(p=1;p<=n;p++)for(mode=1;mode<=3;mode++){
        expected(&f,&j,timeout,callback_mode,count,high);f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(axd_rx_status_begin(&j,&out,&c)==0);run(&f);
        assert(j.phase==(f.error?AXD_RS_FAILED:AXD_RS_CANCELLED));
        assert(j.error==(f.error?AXD_RS_IO_ERROR:AXD_RS_NO_ERROR) && f.seen==p);
        assert(j.context.error_count==UINT32_MAX && j.output.head==out.head && j.output.tail==out.tail);
        assert(j.result_stores==0);
    }
}
static void unused_and_partial(void)
{
    struct axd_rx_status j;struct axd_rx_status_context c=context();
    const struct axd_rx_status_output out={0xaaaa,0xbbbb,5};struct fixture f;
    c.pool.known=0;c.pool_owned=c.os_owned=0;
    expected(&f,&j,0,0,0,0);assert(axd_rx_status_begin(&j,&out,&c)==0);run(&f);
    assert(j.phase==AXD_RS_TRACE_COMPLETE);
    assert(axd_rx_status_begin(&j,&j.output,&j.context)==0);
    assert(j.context.list.ring.in==1 && j.output.head==NODE && j.output.count==6);
    c=context();c.pool.known=0;expected(&f,&j,1,0,0,0);
    assert(axd_rx_status_begin(&j,&out,&c)==0);run(&f);
    assert(j.phase==AXD_RS_DEPENDENCY && j.dependency==AXD_RS_POOL_BINDING && j.byte_cursor==4);
    assert(j.length_reads==200 && j.delays==200 && j.result_stores==0);
    c=context();c.list.ring.count=0;c.list.node_owned=c.list.descriptor_owned=0;
    c.nodes_owned=c.data_owned=c.pool_owned=c.os_owned=0;c.pool.known=0;
    memset(&f,0,sizeof(f));f.job=&j;assert(axd_rx_status_begin(&j,&out,&c)==0);run(&f);
    assert(j.phase==AXD_RS_TRACE_COMPLETE && f.seen==0 && j.output.head==0 && j.output.tail==0 && j.output.count==5);
    c=context();expected(&f,&j,0,0,0,0);assert(axd_rx_status_begin(&j,&out,&c)==0);
    while(j.phase==AXD_RS_RUNNING && j.result_stores!=1)axd_rx_status_step(&j,io,&f);
    assert(j.output.head==NODE && j.output.tail==out.tail && j.output.count==6);
    assert(axd_rx_status_cancel(&j)==AXD_RS_CANCELLED);run(&f);
    c.work_limit=0;assert(axd_rx_status_begin(&j,&out,&c)==-1 && j.error==AXD_RS_BOUNDS);
    assert(axd_rx_status_begin(NULL,&out,&c)==-1 && axd_rx_status_step(NULL,NULL,NULL)==AXD_RS_INVALID);
}
int main(void)
{
    cases_and_faults(0,0,0,0);cases_and_faults(1,0,UINT32_MAX,0x80000000);
    cases_and_faults(1,1,0x7fffffff,0);cases_and_faults(1,2,0,0);unused_and_partial();
    puts("RX status: real status-list/200-read timeout/byte clears/pool stores and callback prefixes passed");return 0;
}
