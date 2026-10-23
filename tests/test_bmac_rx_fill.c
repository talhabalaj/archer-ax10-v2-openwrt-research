/* SPDX-License-Identifier: MIT */
#include "../driver/bmac_rx_fill.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a)/sizeof((a)[0]))
struct event {enum axd_bmac_rx_fill_kind path;unsigned kind,resource;uint32_t identity,offset,value,reply;};
#define PUB(v) {AXD_BRF_PUBLIC_READ8,0,0,0x30000,0x1c0,0,v}
#define S(k,r,id,o,v,x) {AXD_BRF_STATUS,AXD_RSF_##k,AXD_RSF_##r,id,o,v,x}
#define B(b) S(WRITE8,NODE,0x1000,0x1c+(b),0,0)
#define D(o,v) S(WRITE32,DESCRIPTOR,0x3333,o,v,0)
/* A real status fill child, including inline pool removal and both byte-clear
 * groups. Its primary head/tail stores must be visible before descriptor I/O. */
static const struct event status_one[]={PUB(1),S(READ32,POOL,0x8000,0,0,0x1000),S(READ32,NODE,0x1000,0x1c,0,0),S(WRITE32,POOL,0x8000,0,0,0),B(0),B(1),B(2),B(3),S(READ32,POOL,0x8000,4,0,1),S(WRITE32,POOL,0x8000,4,0,0),
    S(READ32,NODE,0x1000,0,0,0x100000),S(WRITE32,DATA,0x100000,0,0,0),S(READ32,NODE,0x1000,0,0,0x100000),S(TRANSLATE,DATA,0x100000,0,0,0x2000),S(WRITE16,NODE,0x1000,0x18,7,0),B(0),B(1),B(2),B(3),
    D(0x78,0x3000),D(0x7c,0x55),D(0x70,0x90000000),D(0x74,64),S(STORE_BARRIER,CHANNEL,0x4444,0,0,0),S(WRITE32,CHANNEL,0x4444,4,0x100,0)};
static struct axd_bmac_rx_fill_context context(uint32_t fifo)
{
    struct axd_bmac_rx_fill_context c;memset(&c,0,sizeof(c));
    c.hardware=(struct axd_wake_u32){0x10000,1};c.wlc=(struct axd_wake_u32){0x20000,1};c.pub=(struct axd_wake_u32){0x30000,1};
    c.selected_fifo=fifo;c.selected_di=c.normal_binding=c.status_binding=(struct axd_wake_u32){0x9000,1};c.public_owned=c.normal_known=c.status_known=1;
    c.normal.disabled=c.status.disabled=0xff;return c;
}
struct fixture {struct axd_bmac_rx_fill *j;const struct event *e;size_t count,seen,fault;unsigned error,cancel;};
static struct fixture fixture(struct axd_bmac_rx_fill *j,const struct event *e,size_t n)
{struct fixture f;memset(&f,0,sizeof(f));f.j=j;f.e=e;f.count=n;return f;}
static int io(void *opaque,const struct axd_bmac_rx_fill_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;assert(f->seen<f->count);e=&f->e[f->seen++];assert(op->kind==e->path);
    assert(f->j->attempted_ops==f->seen && f->j->completed_ops==f->seen-1);
    if(op->kind==AXD_BRF_PUBLIC_READ8)assert(op->identity==e->identity && op->offset==e->offset && f->seen==1);
    else if(op->kind==AXD_BRF_STATUS){
        assert((unsigned)op->status.kind==e->kind && (unsigned)op->status.resource==e->resource && op->status.identity==e->identity && op->status.offset==e->offset && op->status.value==e->value);
        assert(!memcmp(&f->j->context.status,&f->j->status_job.context,sizeof(f->j->context.status)));
    }else{
        assert((unsigned)op->normal.kind==e->kind && (unsigned)op->normal.resource==e->resource && op->normal.identity==e->identity && op->normal.offset==e->offset && op->normal.value==e->value);
        assert(!memcmp(&f->j->context.normal,&f->j->normal_job.context,sizeof(f->j->context.normal)));
    }
    if(f->seen==f->fault && f->cancel)assert(axd_bmac_rx_fill_cancel(f->j)==AXD_BRF_CANCELLED);
    if(f->seen==f->fault && f->error)return -1;*reply=e->reply;return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_bmac_rx_fill old=*f->j;size_t n=f->seen;
    assert(axd_bmac_rx_fill_step(f->j,io,f)==old.phase && axd_bmac_rx_fill_step(f->j,NULL,NULL)==old.phase);
    assert(axd_bmac_rx_fill_cancel(f->j)==old.phase && n==f->seen && !memcmp(&old,f->j,sizeof(old)));
}
static void run(struct fixture *f)
{
    unsigned steps=0;while(f->j->phase==AXD_BRF_RUNNING){size_t n=f->seen;assert(++steps<1000);axd_bmac_rx_fill_step(f->j,io,f);assert(f->seen-n<=1);}
    assert(f->j->attempted_ops==f->seen && f->j->completed_ops==f->seen-(f->error && f->seen==f->fault?1u:0u));terminal(f);
}
static void guards_and_bindings(void)
{
    struct axd_bmac_rx_fill j;unsigned byte;uint32_t fifos[]={0,2,3,71};size_t i;
    for(i=0;i<N(fifos);i++)for(byte=0;byte<256;byte++){
        const struct event e[]={PUB(byte)};struct axd_bmac_rx_fill_context c=context(fifos[i]);struct fixture f=fixture(&j,e,N(e));
        if(fifos[i]==3 && (byte&1))c.normal_known=0;else c.status_known=0;
        assert(!axd_bmac_rx_fill_begin(&j,fifos[i],&c));run(&f);
        assert(j.phase==AXD_BRF_TRACE_COMPLETE && j.selected_status==(fifos[i]==3 && (byte&1)) && j.eligibility_sample==byte);
        assert(j.child_return_valid && j.child_return_value==0 && f.seen==1);
    }
    for(i=0;i<8;i++){
        const struct event e[]={PUB(0)};struct axd_bmac_rx_fill_context c=context(2);struct fixture f=fixture(&j,e,N(e));
        if(i==0)c.hardware.known=0;if(i==1)c.pub.value=0;if(i==2)c.public_owned=0;if(i==3)c.selected_fifo=3;
        if(i==4)c.selected_di.known=0;if(i==5)c.normal_binding.value=0x9001;if(i==6)c.normal_known=0;if(i==7)c.normal_known=2;
        if(i<3)f.count=0;assert(!axd_bmac_rx_fill_begin(&j,2,&c));run(&f);
        assert(j.phase==(i==0||i==2||i==4||i==6?AXD_BRF_DEPENDENCY:AXD_BRF_FAILED));
        assert(!j.child_return_valid && f.seen==(i<3?0u:1u));
    }
    {
        const struct event e[]={PUB(1)};struct axd_bmac_rx_fill_context c=context(72);struct fixture f=fixture(&j,e,N(e));
        assert(!axd_bmac_rx_fill_begin(&j,72,&c));run(&f);assert(j.error==AXD_BRF_BOUNDS && f.seen==1);
    }
}
static void status_and_faults(void)
{
    struct axd_bmac_rx_fill j;struct axd_bmac_rx_fill_context c=context(3);struct fixture f;size_t p;unsigned mode;
    c.status.disabled=0;c.status.rx.core=130;c.status.rx.minor=1;c.status.rx.ring=(struct axd_dma64_ring){8,0x1fff,0x100,6,7};
    c.status.rx.available=88;c.status.rx.cached_current=5;c.status.rx.status_identity=0x4444;c.status.rx.descriptor_identity=0x3333;c.status.rx.descriptor_bytes=128;
    c.status.rx.status_owned=c.status.rx.descriptor_owned=c.status.rx.node_owned=1;c.status.pool=(struct axd_wake_u32){0x8000,1};
    c.status.receive_posts=2;c.status.buffer_size=64;c.status.data_offset_low=0x1000;c.status.data_offset_high=0x55;c.status.no_buffer_count=99;
    c.status.pool_owned=c.status.os_owned=c.status.data_owned=1;
    f=fixture(&j,status_one,N(status_one));assert(!axd_bmac_rx_fill_begin(&j,3,&c));run(&f);
    assert(j.phase==AXD_BRF_TRACE_COMPLETE && j.child_return_valid && !j.child_return_value);
    assert(j.context.status.rx.ring.out==0 && j.context.status.rx.head==0x1000 && j.context.status.rx.tail==0x1000);
    for(p=1;p<=N(status_one);p++)for(mode=1;mode<=3;mode++){
        f=fixture(&j,status_one,N(status_one));f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(!axd_bmac_rx_fill_begin(&j,3,&c));run(&f);
        assert(j.phase==(f.error?AXD_BRF_FAILED:AXD_BRF_CANCELLED) && !j.child_return_valid && f.seen==p);
        assert(j.error==(f.error?AXD_BRF_IO_ERROR:AXD_BRF_NO_ERROR));
        assert(j.context.status.rx.ring.out==7 && j.context.status.rx.head==(p<=19?0u:0x1000u) && j.context.status.rx.tail==(p<=19?0u:0x1000u));
    }
    f=fixture(&j,status_one,N(status_one));assert(!axd_bmac_rx_fill_begin(&j,3,&c));run(&f);
    assert(!axd_bmac_rx_fill_begin(&j,3,&j.context));assert(j.context.status.rx.ring.out==0 && j.context.status.rx.head==0x1000);axd_bmac_rx_fill_cancel(&j);
}
static void normal_composition(void)
{
    const struct event e[]={PUB(0x80),{AXD_BRF_NORMAL,AXD_RXF_STORE_BARRIER,AXD_RXF_CHANNEL,0x1111,0,0,0},{AXD_BRF_NORMAL,AXD_RXF_WRITE32,AXD_RXF_CHANNEL,0x1111,4,0x170,0}};
    struct axd_bmac_rx_fill j;struct axd_bmac_rx_fill_context c=context(3);struct fixture f;size_t p;unsigned mode;
    c.normal.disabled=0;c.normal.rx.core=130;c.normal.rx.minor=1;c.normal.rx.ring=(struct axd_dma64_ring){8,0x1fff,0x100,6,7};
    c.normal.receive_posts=1;c.normal.rx.status_identity=0x1111;c.normal.rx.status_owned=1;
    f=fixture(&j,e,N(e));assert(!axd_bmac_rx_fill_begin(&j,3,&c));run(&f);
    assert(j.phase==AXD_BRF_TRACE_COMPLETE && !j.selected_status && j.child_return_value==1 && j.normal_job.producer_written && j.context.normal.rx.ring.out==7);
    for(p=1;p<=N(e);p++)for(mode=1;mode<=3;mode++){
        f=fixture(&j,e,N(e));f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(!axd_bmac_rx_fill_begin(&j,3,&c));run(&f);assert(j.phase==(f.error?AXD_BRF_FAILED:AXD_BRF_CANCELLED));
    }
    assert(axd_bmac_rx_fill_begin(NULL,0,&c)<0 && axd_bmac_rx_fill_begin(&j,0,NULL)<0);
    assert(axd_bmac_rx_fill_step(NULL,NULL,NULL)==AXD_BRF_INVALID && axd_bmac_rx_fill_cancel(NULL)==AXD_BRF_INVALID);
}
int main(void){guards_and_bindings();status_and_faults();normal_composition();puts("BMAC RX fill: all256 eligibility bytes, real child compositions and callback prefixes passed");return 0;}
