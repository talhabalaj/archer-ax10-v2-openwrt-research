/* SPDX-License-Identifier: MIT */
#include "../driver/rx_packets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))
#define P1 0x1000u
#define P2 0x2000u
#define P3 0x3000u
#define D1 0x100000u
#define D2 0x200000u
struct event {
    enum axd_rx_packets_kind kind;
    unsigned list_kind, field;
    uint32_t packet, data, value, reply, count;
};
#define L(k,id,off,val,r,c) {AXD_RP_LIST,k,off,id,0,val,r,c}
#define R(p,f,r,c) {AXD_RP_PACKET_READ32,0,AXD_RP_##f,p,0,0,r,c}
#define W(p,f,v,c) {AXD_RP_PACKET_WRITE32,0,AXD_RP_##f,p,0,v,0,c}
#define H(d,v,c) {AXD_RP_DATA_READ16,0,0,0,d,0,v,c}
#define Q(d,v,c) {AXD_RP_DATA_READ32,0,0,0,d,0,v,c}
#define O(k,p,v,c) {AXD_RP_##k,0,0,p,0,v,0,c}
#define LIST2 \
 L(AXD_RX_LIST_STATUS_READ32,0x11,0x10,0,0x120,7), \
 L(AXD_RX_LIST_FULL_BARRIER,0x11,0,0,0,7), \
 L(AXD_RX_LIST_PACKET_NEXT_READ32,P1,4,0,P2,7), \
 L(AXD_RX_LIST_DESCRIPTOR_READ32,0x22,8,0,0xabcdef01,7), \
 L(AXD_RX_LIST_PACKET_NEXT_READ32,P2,4,0,P3,7), \
 L(AXD_RX_LIST_DESCRIPTOR_READ32,0x22,24,0,0xfedcba10,7), \
 L(AXD_RX_LIST_PACKET_NEXT_WRITE32,P2,4,0,0,7)
#define LIST1 \
 L(AXD_RX_LIST_STATUS_READ32,0x11,0x10,0,0x120,7), \
 L(AXD_RX_LIST_FULL_BARRIER,0x11,0,0,0,7), \
 L(AXD_RX_LIST_PACKET_NEXT_READ32,P1,4,0,P2,7), \
 L(AXD_RX_LIST_DESCRIPTOR_READ32,0x22,8,0,0xabcdef01,7), \
 L(AXD_RX_LIST_PACKET_NEXT_WRITE32,P1,4,0,0,7)
/* Literal current bulk child followed by normal frame processing. Count starts
 * at7 and increments BEFORE the actual final NEXT load for each accepted skb. */
static const struct event normal[] = {
    LIST2,
    R(P1,DATA,D1,7), H(D1,0xabcd0014,7), R(P1,NONLINEAR,0,7),
    R(P1,DATA,D1,7), W(P1,LENGTH,24,7), W(P1,TAIL,D1+24,7), R(P1,NEXT,P2,8),
    R(P2,DATA,D2,8), H(D2,0,8), O(DELAY_US,0,1,8),
    H(D2,0,8), O(DELAY_US,0,1,8), H(D2,10,8),
    R(P2,NONLINEAR,0x80000000,8), O(TRIM,P2,14,8), R(P2,NEXT,0,9)
};
static const struct event fragment[] = {
    LIST1,
    R(P1,DATA,D1,7), H(D1,100,7), R(P1,NONLINEAR,0,7),
    R(P1,DATA,D1,7), W(P1,LENGTH,64,7), W(P1,TAIL,D1+64,7),
    R(P1,DATA,D1,7), Q(D1,0x01000064,7), R(P1,NEXT,0,7),
    L(AXD_RX_LIST_STATUS_READ32,0x11,0x10,0,0x120,7),
    L(AXD_RX_LIST_FULL_BARRIER,0x11,0,0,0,7),
    L(AXD_RX_LIST_PACKET_NEXT_READ32,P2,4,0,0,7),
    L(AXD_RX_LIST_DESCRIPTOR_READ32,0x22,24,0,0x76543210,7),
    L(AXD_RX_LIST_PACKET_NEXT_WRITE32,P2,4,0,0,7),
    W(P1,FRAGMENT,P2,7), R(P2,NONLINEAR,0,7), R(P2,DATA,D2,7),
    W(P2,LENGTH,40,7), W(P2,TAIL,D2+40,7), W(P1,NEXT,0,7),
    R(P2,NEXT,0,7), W(P1,NEXT,0,7), W(P2,NEXT,0,7), R(P1,NEXT,0,8)
};
static const struct event drop[] = {
    LIST1,
    R(P1,DATA,D1,7), H(D1,100,7), R(P1,NONLINEAR,0,7),
    R(P1,DATA,D1,7), W(P1,LENGTH,64,7), W(P1,TAIL,D1+64,7),
    R(P1,NEXT,0,7), W(P1,NEXT,0,7), O(FREE,P1,0,7)
};
struct fixture {
    struct axd_rx_packets *job; const struct event *events; size_t count, seen, fault;
    unsigned error, cancel, frees, trims, delays;
};
static void run(struct fixture *);
static struct axd_rx_packets_context context(void)
{
    struct axd_rx_packets_context c; memset(&c,0,sizeof(c));
    c.list.ring=(struct axd_dma64_ring){8,0x1fff,0x100,0,3};
    c.list.core=130;c.list.minor=1;c.list.head=P1;c.list.tail=P3;c.list.available=99;
    c.list.status_identity=0x11;c.list.descriptor_identity=0x22;c.list.descriptor_bytes=128;
    c.list.status_owned=c.list.descriptor_owned=c.list.packet_owned=1;
    c.receive_offset=(struct axd_wake_u32){4,1};c.buffer_size=(struct axd_wake_u32){64,1};
    c.flags=(struct axd_wake_u32){4,1};c.os_context=(struct axd_wake_u32){0x77,1};
    c.status_context=(struct axd_wake_u32){0,1};c.work_limit=100;c.error_count=UINT32_MAX;
    c.packets_owned=c.data_owned=c.os_owned=1;return c;
}
static struct axd_rx_packets_request request(void)
{ const struct axd_rx_packets_request r={2,0,{0xaaaa,0xbbbb,7}};return r; }
static struct fixture fixture(struct axd_rx_packets *j,const struct event *e,size_t n)
{ struct fixture f;memset(&f,0,sizeof(f));f.job=j;f.events=e;f.count=n;return f; }
static int io(void *opaque,const struct axd_rx_packets_operation *op,struct axd_rx_packets_reply *reply)
{
    struct fixture *f=opaque;const struct event *e;
    if(f->seen>=f->count)fprintf(stderr,"unexpected RX event kind%u stage%u after%zu\n",op->kind,f->job->stage,f->seen);
    assert(f->seen<f->count);e=&f->events[f->seen++];
    if(op->kind!=e->kind)fprintf(stderr,"RX kind%u expected%u at%zu stage%u\n",op->kind,e->kind,f->seen,f->job->stage);
    assert(op->kind==e->kind);
    if(op->kind==AXD_RP_LIST){
        assert((unsigned)op->list.kind==e->list_kind && op->list.identity==e->packet);
        assert(op->list.offset==e->field && op->list.value==e->value);
    }else if(op->kind==AXD_RP_STATUS){
        assert((unsigned)op->status.kind==e->list_kind);
        if(op->status.kind==AXD_RS_LIST){
            assert((unsigned)op->status.list.kind==e->field && op->status.list.identity==e->packet);
            assert(op->status.list.offset==e->data && op->status.list.value==e->value);
        }else assert(op->status.identity==e->packet && op->status.offset==e->field && op->status.value==e->value);
    }else {
        assert((unsigned)op->field==e->field && op->packet==e->packet && op->data==e->data && op->value==e->value);
        assert(op->os_context==(op->kind==AXD_RP_FREE?0x77u:0u));
    }
    assert(f->job->output.count==e->count);
    assert(f->job->attempted_ops==f->seen && f->job->completed_ops==f->seen-1);
    if(f->seen==f->fault && f->cancel)assert(axd_rx_packets_cancel(f->job)==AXD_RP_CANCELLED);
    if(f->seen==f->fault && f->error)return -1;
    if(op->kind==AXD_RP_STATUS)reply->status.value=e->reply;
    else reply->value=e->reply;
    f->frees+=op->kind==AXD_RP_FREE;f->trims+=op->kind==AXD_RP_TRIM;f->delays+=op->kind==AXD_RP_DELAY_US;
    return 0;
}
static void actual_status_child(void)
{
#define SL(k,id,off,v,r) {AXD_RP_STATUS,AXD_RS_LIST,k,id,off,v,r,9}
#define SN(k,id,off,v,r) {AXD_RP_STATUS,k,off,id,0,v,r,9}
    const struct event status[]={
        SL(AXD_RX_STATUS_LIST_STATUS_READ32,0x33,0x10,0,0x110),
        SL(AXD_RX_STATUS_LIST_FULL_BARRIER,0x33,0,0,0),
        SL(AXD_RX_STATUS_LIST_NODE_NEXT_READ32,0x5000,0x1c,0,0),
        SL(AXD_RX_STATUS_LIST_DESCRIPTOR_READ32,0x44,8,0,0x76543210),
        SL(AXD_RX_STATUS_LIST_NODE_NEXT_WRITE8,0x5000,0x1c,0,0),
        SL(AXD_RX_STATUS_LIST_NODE_NEXT_WRITE8,0x5000,0x1d,0,0),
        SL(AXD_RX_STATUS_LIST_NODE_NEXT_WRITE8,0x5000,0x1e,0,0),
        SL(AXD_RX_STATUS_LIST_NODE_NEXT_WRITE8,0x5000,0x1f,0,0),
        SN(AXD_RS_NODE_READ32,0x5000,0,0,0x600000),
        SN(AXD_RS_DATA_READ16,0x600000,0,0,8),
        SN(AXD_RS_NODE_READ32,0x5000,0x1c,0,0)
    };
    struct event events[N(normal)+N(status)];struct axd_rx_packets j;
    struct axd_rx_packets_context c=context();struct axd_rx_packets_request r=request();
    struct fixture f;size_t p;unsigned mode;
    memcpy(events,normal,sizeof(normal));memcpy(events+N(normal),status,sizeof(status));
    c.status_context.value=0x8800;c.status_binding=(struct axd_wake_u32){0x8800,1};
    c.status_output_known=1;c.status_output=(struct axd_rx_status_output){0x111,0x222,3};r.status_output=0x9900;
    c.status.work_limit=100;c.status.list.core=130;c.status.list.minor=1;
    f=fixture(&j,normal,N(normal));assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_TRACE_COMPLETE && j.context.status_output.head==0 && j.context.status_output.tail==0);
    assert(j.context.status_output.count==3 && j.context.status.list.ring.count==0);
    c.status.list.ring=(struct axd_dma64_ring){8,0x1fff,0x100,0,1};
    c.status.list.head=c.status.list.tail=0x5000;c.status.list.status_identity=0x33;
    c.status.list.descriptor_identity=0x44;c.status.list.descriptor_bytes=128;
    c.status.list.status_owned=c.status.list.descriptor_owned=c.status.list.node_owned=1;
    c.status.nodes_owned=c.status.data_owned=1;
    f=fixture(&j,events,N(events));assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_TRACE_COMPLETE && f.seen==N(events));
    assert(j.output.head==P1 && j.output.tail==P2 && j.output.count==9);
    assert(j.context.status_output.head==0x5000 && j.context.status_output.tail==0x5000);
    assert(j.context.status_output.count==4 && j.context.status.list.ring.in==1);
    for(p=N(normal)+1;p<=N(events);p++)for(mode=1;mode<=3;mode++){
        f=fixture(&j,events,N(events));f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==(f.error?AXD_RP_FAILED:AXD_RP_CANCELLED) && j.result_stores==0);
        assert(j.output.head==0xaaaa && j.output.tail==0xbbbb && j.output.count==9 && f.seen==p);
        assert(j.context.status_output.count==(p==N(events)?4u:3u));
    }
#undef SL
#undef SN
}
static void run(struct fixture *f)
{
    unsigned steps=0;
    while(f->job->phase==AXD_RP_RUNNING){
        size_t n=f->seen;assert(++steps<10000);
        axd_rx_packets_step(f->job,io,f);assert(f->seen-n<=1);
        assert(f->job->attempted_ops==f->seen);
        assert(f->job->completed_ops==f->seen-(f->error && f->seen==f->fault?1u:0u));
        assert(f->job->freed_packets==f->frees && f->job->trimmed_packets==f->trims && f->job->delay_count==f->delays);
    }
    {
        struct axd_rx_packets snapshot=*f->job;size_t seen=f->seen;
        assert(axd_rx_packets_step(f->job,io,f)==snapshot.phase);
        assert(axd_rx_packets_step(f->job,NULL,NULL)==snapshot.phase);
        assert(axd_rx_packets_cancel(f->job)==snapshot.phase);
        assert(f->seen==seen && memcmp(&snapshot,f->job,sizeof(snapshot))==0);
    }
}
static void full_and_faults(const struct event *events,size_t n,unsigned kind)
{
    struct axd_rx_packets j;struct axd_rx_packets_context c=context();
    struct axd_rx_packets_request r=request();struct fixture f;size_t p;unsigned mode;
    if(kind!=0)r.budget=1;
    if(kind==1){c.list.ring.out=2;c.list.tail=P2;}
    if(kind==2)c.flags.value=0x40;
    f=fixture(&j,events,n);assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_TRACE_COMPLETE && f.seen==n && j.result_stores==3);
    assert(j.output.head==(kind==2?0u:P1) && j.output.tail==(kind==0?P2:kind==1?P1:0));
    assert(j.output.count==(kind==0?9u:kind==1?8u:7u));
    assert(j.context.error_count==(kind==2?0u:UINT32_MAX));
    assert(j.context.list.ring.in==(kind==2?1u:2u));
    for(p=1;p<=n;p++)for(mode=1;mode<=3;mode++){
        f=fixture(&j,events,n);f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==(f.error?AXD_RP_FAILED:AXD_RP_CANCELLED));
        assert(j.error==(f.error?AXD_RP_IO_ERROR:AXD_RP_NO_ERROR));
        assert(f.seen==p && j.result_stores==0);
        assert(j.output.head==r.output.head && j.output.tail==r.output.tail);
        if(kind==2)assert(j.context.error_count==UINT32_MAX);
    }
}
static void dependencies_and_aliases(void)
{
    struct axd_rx_packets j;struct axd_rx_packets_context c=context();
    struct axd_rx_packets_request r=request();struct fixture f;
    c.flags.known=0;f=fixture(&j,normal,N(normal));
    assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);assert(j.phase==AXD_RP_TRACE_COMPLETE);
    assert(axd_rx_packets_begin(&j,&j.request,&j.context)==0);
    assert(j.context.list.ring.in==2 && j.request.output.count==7 && j.output.head==0xaaaa);
    c=context();c.receive_offset.known=0;f=fixture(&j,normal,N(normal));
    assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_DEPENDENCY && j.last_metadata==AXD_RP_RECEIVE_OFFSET && f.seen==7);
    assert(j.context.list.ring.in==2 && j.context.list.head==P3 && j.output.count==7);
    c=context();c.buffer_size.known=0;f=fixture(&j,normal,N(normal));
    assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_DEPENDENCY && j.last_metadata==AXD_RP_BUFFER_SIZE && f.seen==9);
    c=context();c.packets_owned=0;f=fixture(&j,normal,N(normal));
    assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_DEPENDENCY && j.dependency==AXD_RP_PACKET_OWNER && f.seen==7);
    c=context();c.data_owned=0;f=fixture(&j,normal,N(normal));
    assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_DEPENDENCY && j.dependency==AXD_RP_DATA_OWNER && f.seen==8);
    c=context();c.work_limit=1;f=fixture(&j,normal,N(normal));
    assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_FAILED && j.error==AXD_RP_BOUNDS && f.seen==14 && j.output.count==8);
    c=context();c.status_context.value=0x8800;r.status_output=0x9900;f=fixture(&j,normal,N(normal));
    assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_DEPENDENCY && j.dependency==AXD_RP_STATUS_PATH && f.seen==N(normal));
    assert(j.output.count==9 && j.output.head==0xaaaa && j.result_stores==0);
    r=request();c=context();c.status_context.known=0;f=fixture(&j,normal,N(normal));
    assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_DEPENDENCY && j.last_metadata==AXD_RP_STATUS_CONTEXT);
    c=context();f=fixture(&j,normal,N(normal));assert(axd_rx_packets_begin(&j,&r,&c)==0);
    while(j.phase==AXD_RP_RUNNING && j.result_stores!=1)axd_rx_packets_step(&j,io,&f);
    assert(j.output.head==P1 && j.output.tail==0xbbbb && j.output.count==9);
    assert(axd_rx_packets_cancel(&j)==AXD_RP_CANCELLED);run(&f);
    c.work_limit=0;assert(axd_rx_packets_begin(&j,&r,&c)==-1 && j.error==AXD_RP_BOUNDS);
    assert(axd_rx_packets_begin(NULL,&r,&c)==-1 && axd_rx_packets_step(NULL,NULL,NULL)==AXD_RP_INVALID);
}
static void timeout_and_middle_drop(void)
{
    const struct event prefix[]={LIST1,R(P1,DATA,D1,7)};
    const struct event suffix[]={R(P1,NEXT,0,7),W(P1,NEXT,0,7),O(FREE,P1,0,7)};
    struct event events[N(prefix)+400+N(suffix)];size_t i,p;unsigned mode;
    struct axd_rx_packets j;struct axd_rx_packets_context c=context();
    struct axd_rx_packets_request r=request();struct fixture f;
    memcpy(events,prefix,sizeof(prefix));
    for(i=0;i<200;i++){
        const struct event h=H(D1,0,7),d=O(DELAY_US,0,1,7);
        events[N(prefix)+i*2]=h;events[N(prefix)+i*2+1]=d;
    }
    memcpy(events+N(prefix)+400,suffix,sizeof(suffix));r.budget=1;
    f=fixture(&j,events,N(events));assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_RP_TRACE_COMPLETE && f.seen==N(events));
    assert(j.samples==200 && j.delay_count==200 && j.freed_packets==1 && j.trimmed_packets==0);
    assert(j.output.head==0 && j.output.tail==0 && j.output.count==7 && j.context.error_count==0);
    for(p=1;p<=N(events);p++)for(mode=1;mode<=3;mode++){
        f=fixture(&j,events,N(events));f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==(f.error?AXD_RP_FAILED:AXD_RP_CANCELLED));
        assert(j.result_stores==0 && j.output.count==7 && j.context.error_count==UINT32_MAX);
    }
    {
        const struct event middle[]={
            L(AXD_RX_LIST_STATUS_READ32,0x11,0x10,0,0x130,7),
            L(AXD_RX_LIST_FULL_BARRIER,0x11,0,0,0,7),
            L(AXD_RX_LIST_PACKET_NEXT_READ32,P1,4,0,P2,7),
            L(AXD_RX_LIST_DESCRIPTOR_READ32,0x22,8,0,0x11,7),
            L(AXD_RX_LIST_PACKET_NEXT_READ32,P2,4,0,P3,7),
            L(AXD_RX_LIST_DESCRIPTOR_READ32,0x22,24,0,0x22,7),
            L(AXD_RX_LIST_PACKET_NEXT_READ32,P3,4,0,0,7),
            L(AXD_RX_LIST_DESCRIPTOR_READ32,0x22,40,0,0x33,7),
            L(AXD_RX_LIST_PACKET_NEXT_WRITE32,P3,4,0,0,7),
            R(P1,DATA,D1,7),H(D1,20,7),R(P1,NONLINEAR,0,7),R(P1,DATA,D1,7),
            W(P1,LENGTH,24,7),W(P1,TAIL,D1+24,7),R(P1,NEXT,P2,8),
            R(P2,DATA,D2,8),H(D2,100,8),R(P2,NONLINEAR,0,8),R(P2,DATA,D2,8),
            W(P2,LENGTH,64,8),W(P2,TAIL,D2+64,8),R(P2,NEXT,P3,8),
            W(P1,NEXT,P3,8),W(P2,NEXT,0,8),O(FREE,P2,0,8),
            R(P3,DATA,D2,8),H(D2,10,8),R(P3,NONLINEAR,0,8),R(P3,DATA,D2,8),
            W(P3,LENGTH,14,8),W(P3,TAIL,D2+14,8),R(P3,NEXT,0,9)
        };
        c=context();c.flags.value=0x40;r=request();r.budget=3;
        f=fixture(&j,middle,N(middle));assert(axd_rx_packets_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==AXD_RP_TRACE_COMPLETE && f.seen==N(middle));
        assert(j.output.head==P1 && j.output.tail==P3 && j.output.count==9 && j.context.error_count==0);
    }
}
int main(void)
{
    full_and_faults(normal,N(normal),0);full_and_faults(fragment,N(fragment),1);full_and_faults(drop,N(drop),2);
    dependencies_and_aliases();actual_status_child();timeout_and_middle_drop();
    puts("RX packets: real bulk-list/length/trim/fragment/drop traces and callback prefixes passed");
    return 0;
}
