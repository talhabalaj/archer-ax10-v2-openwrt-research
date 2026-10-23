/* SPDX-License-Identifier: MIT */
#include "../driver/dma_rx_status_fill.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a)/sizeof((a)[0]))
struct event {enum axd_rx_status_fill_kind kind;enum axd_rx_status_fill_resource resource;uint32_t identity,offset,value,reply;};
#define E(k,r,id,o,v,x) {AXD_RSF_##k,AXD_RSF_##r,id,o,v,x}
#define PR(o,v) E(READ32,POOL,0x8000,o,0,v)
#define PW(o,v) E(WRITE32,POOL,0x8000,o,v,0)
#define NR(n,o,v) E(READ32,NODE,n,o,0,v)
#define NW(n,o,v) E(WRITE32,NODE,n,o,v,0)
#define NB(n,b) E(WRITE8,NODE,n,0x1c+(b),0,0)
#define DW(o,v) E(WRITE32,DESCRIPTOR,0x3333,o,v,0)
#define DR(o,v) E(READ32,DESCRIPTOR,0x3333,o,0,v)
#define ST E(STORE_BARRIER,CHANNEL,0x4444,0,0,0)
#define POST(v) E(WRITE32,CHANNEL,0x4444,4,v,0)
/* Literal current inline pop. Two separate groups of FOUR byte clears are
 * mandatory. Fresh descriptor parity replies intentionally differ from stores. */
static const struct event one[]={
    PR(0,0x1000),NR(0x1000,0x1c,0x2000),PW(0,0x2000),NB(0x1000,0),NB(0x1000,1),NB(0x1000,2),NB(0x1000,3),PR(4,5),PW(4,4),
    NR(0x1000,0,0x100000),E(WRITE32,DATA,0x100000,0,0,0),NR(0x1000,0,0x100000),E(TRANSLATE,DATA,0x100000,0,0,0xc0000400),
    E(WRITE16,NODE,0x1000,0x18,7,0),NB(0x1000,0),NB(0x1000,1),NB(0x1000,2),NB(0x1000,3),NW(0x5000,0x1c,0x1000),
    DW(0x78,0x1400),DW(0x7c,0x55),DW(0x70,0x90000000),DW(0x74,0x30040),DR(0x78,1),DR(0x7c,2),DR(0x70,4),DR(0x74,0),DW(0x74,0x70040),ST,POST(0x100)
};
static const struct event empty_pool[]={PR(0,0),PR(8,9),PW(8,10),E(READ32,CHANNEL,0x4444,0x10,0,0xa0000170),E(FULL_BARRIER,CHANNEL,0x4444,0,0,0),E(READ32,CHANNEL,0x4444,4,0,0xf0000170),E(FULL_BARRIER,CHANNEL,0x4444,0,0,0),ST,POST(0x170)};
static struct axd_rx_status_fill_context context(void)
{
    struct axd_rx_status_fill_context c;memset(&c,0,sizeof(c));
    c.rx.ring=(struct axd_dma64_ring){8,0x1fff,0x100,6,7};c.rx.core=130;c.rx.minor=1;c.rx.cached_current=5;
    c.rx.head=c.rx.tail=0x5000;c.rx.available=88;c.rx.status_identity=0x4444;c.rx.descriptor_identity=0x3333;c.rx.descriptor_bytes=128;
    c.rx.status_owned=c.rx.descriptor_owned=c.rx.node_owned=1;c.pool=(struct axd_wake_u32){0x8000,1};
    c.flags=1;c.receive_posts=2;c.no_buffer_count=99;c.data_offset_low=0x1000;c.data_offset_high=0x55;c.buffer_size=64;
    c.pool_owned=c.data_owned=c.os_owned=1;return c;
}
struct fixture {struct axd_dma_rx_status_fill *j;const struct event *events;size_t count,seen,fault;unsigned error,cancel;uint32_t old_out;};
static struct fixture fixture(struct axd_dma_rx_status_fill *j,const struct event *e,size_t n)
{struct fixture f;memset(&f,0,sizeof(f));f.j=j;f.events=e;f.count=n;f.old_out=7;return f;}
static int io(void *opaque,const struct axd_rx_status_fill_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;
    if(f->seen>=f->count)fprintf(stderr,"unexpected status fill operation kind%u resource%u offset%x event%zu\n",op->kind,op->resource,op->offset,f->seen);
    assert(f->seen<f->count);e=&f->events[f->seen++];
    assert(op->kind==e->kind && op->resource==e->resource && op->identity==e->identity && op->offset==e->offset && op->value==e->value);
    assert(f->j->context.rx.ring.out==f->old_out && f->j->context.rx.available==88 && f->j->context.rx.cached_current==5);
    assert(f->j->attempted_ops==f->seen && f->j->completed_ops==f->seen-1);
    if(f->seen==f->fault && f->cancel)assert(axd_dma_rx_status_fill_cancel(f->j)==AXD_RSF_CANCELLED);
    if(f->seen==f->fault && f->error)return -1;*reply=e->reply;return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_dma_rx_status_fill old=*f->j;size_t n=f->seen;
    assert(axd_dma_rx_status_fill_step(f->j,io,f)==old.phase && axd_dma_rx_status_fill_step(f->j,NULL,NULL)==old.phase);
    assert(axd_dma_rx_status_fill_cancel(f->j)==old.phase && n==f->seen && !memcmp(&old,f->j,sizeof(old)));
}
static void run(struct fixture *f)
{
    unsigned steps=0;while(f->j->phase==AXD_RSF_RUNNING){size_t n=f->seen;assert(++steps<1000);axd_dma_rx_status_fill_step(f->j,io,f);assert(f->seen-n<=1);}
    assert(f->j->attempted_ops==f->seen && f->j->completed_ops==f->seen-(f->error && f->seen==f->fault?1u:0u));terminal(f);
}
static void execute(struct axd_dma_rx_status_fill *j,struct axd_rx_status_fill_context c,const struct event *e,size_t n,uint32_t value)
{
    struct fixture f=fixture(j,e,n);f.old_out=c.rx.ring.out;assert(!axd_dma_rx_status_fill_begin(j,&c));run(&f);
    assert(j->phase==AXD_RSF_TRACE_COMPLETE && j->return_valid && j->return_value==value && f.seen==n);
}
static void faults(struct axd_rx_status_fill_context c,const struct event *e,size_t n,unsigned branch)
{
    struct axd_dma_rx_status_fill j;size_t p;unsigned mode;
    for(p=1;p<=n;p++)for(mode=1;mode<=3;mode++){
        struct fixture f=fixture(&j,e,n);f.old_out=c.rx.ring.out;f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(!axd_dma_rx_status_fill_begin(&j,&c));run(&f);
        assert(j.phase==(f.error?AXD_RSF_FAILED:AXD_RSF_CANCELLED) && !j.return_valid && !j.out_committed);
        assert(j.error==(f.error?AXD_RSF_IO_ERROR:AXD_RSF_NO_ERROR) && f.seen==p && j.context.rx.ring.out==c.rx.ring.out);
        assert(j.producer_written==(p==n && !f.error));
        if(branch==0){
            assert(j.context.rx.head==0x5000 && j.context.rx.tail==(p<=19?0x5000u:0x1000u) && j.context.no_buffer_count==99);
            assert(j.taken_nodes==(p<3 ||(p==3 && f.error)?0u:1u));
            assert(j.pop_bytes==(p<4?0u:(unsigned)(p>7?4:p-3)-(f.error && p<=7?1u:0u)));
            assert(j.post_bytes==(p<15?0u:(unsigned)(p>18?4:p-14)-(f.error && p<=18?1u:0u)));
            assert(j.prepared_nodes==(p<=19?0u:1u) && j.completed_nodes==(p>n-2?1u:0u));
        }else if(branch==1){
            assert(j.context.no_buffer_count==(p<=7?99u:100u) && !j.taken_nodes && !j.prepared_nodes);
            assert(j.context.rx.tail==0x5000);
        }else if(branch==2){
            assert(j.context.no_buffer_count==(p<=31?99u:100u));
            assert(j.context.rx.tail==(p<=19?0x5000u:0x1000u) && j.completed_nodes==(p<=28?0u:1u));
        }else if(branch==3){
            assert(j.context.rx.head==(p<=18?0u:0x1000u) && j.context.rx.tail==(p<=18?0u:0x1000u));
        }else if(branch==4){
            assert(j.context.rx.tail==(p<=19?0x5000u:p<=47?0x1000u:0x2000u));
            assert(j.completed_nodes==(p<=28?0u:p<=56?1u:2u) && j.context.no_buffer_count==99);
        }else assert(!j.taken_nodes && j.context.no_buffer_count==99);
    }
}
static void normal_and_failures(void)
{
    struct axd_dma_rx_status_fill j;struct axd_rx_status_fill_context c=context();
    execute(&j,c,one,N(one),0);assert(j.context.rx.ring.out==0 && j.context.rx.tail==0x1000 && j.context.no_buffer_count==99);
    assert(j.taken_nodes==1 && j.prepared_nodes==1 && j.completed_nodes==1 && j.pop_bytes==4 && j.post_bytes==4 && j.index_writes==1 && j.parity_reads==4 && j.descriptor_writes==5);
    faults(c,one,N(one),0);execute(&j,c,empty_pool,N(empty_pool),0);assert(j.idle_observed && j.context.no_buffer_count==100);
    faults(c,empty_pool,N(empty_pool),1);
    {
        struct event busy[N(empty_pool)];memcpy(busy,empty_pool,sizeof(busy));busy[5].reply=0xf0000180;
        execute(&j,c,busy,N(busy),0);assert(j.idle_observed && j.context.no_buffer_count==100);
    }
    {
        struct event partial[33];memcpy(partial,one,28*sizeof(*partial));partial[1].reply=0;partial[2].value=0;
        partial[28]=(struct event)PR(0,0);partial[29]=(struct event)PR(8,9);partial[30]=(struct event)PW(8,10);partial[31]=(struct event)ST;partial[32]=(struct event)POST(0x100);
        c.receive_posts=3;execute(&j,c,partial,N(partial),0);assert(j.completed_nodes==1 && !j.idle_observed && j.context.no_buffer_count==100);faults(c,partial,N(partial),2);
    }
}
static void multiple_and_alternatives(void)
{
    const struct event second[]={PR(0,0x2000),NR(0x2000,0x1c,0),PW(0,0),NB(0x2000,0),NB(0x2000,1),NB(0x2000,2),NB(0x2000,3),PR(4,4),PW(4,3),
        NR(0x2000,0,0x200000),E(WRITE32,DATA,0x200000,0,0,0),NR(0x2000,0,0x200000),E(TRANSLATE,DATA,0x200000,0,0,0x12345678),
        E(WRITE16,NODE,0x2000,0x18,0,0),NB(0x2000,0),NB(0x2000,1),NB(0x2000,2),NB(0x2000,3),NW(0x1000,0x1c,0x2000),
        DW(8,0x12346678),DW(12,0x55),DW(0,0x80000000),DW(4,64),DR(8,1),DR(12,0),DR(0,0),DR(4,0),DW(4,0x40040),ST,POST(0x110)};
    struct axd_dma_rx_status_fill j;struct axd_rx_status_fill_context c=context();struct event both[58];
    memcpy(both,one,28*sizeof(*both));memcpy(both+28,second,sizeof(second));c.receive_posts=3;
    execute(&j,c,both,N(both),0);assert(j.completed_nodes==2 && j.context.rx.tail==0x2000 && j.context.rx.ring.out==1 && j.pop_bytes==8 && j.post_bytes==8);faults(c,both,N(both),4);
    {
        struct event full[N(both)];memcpy(full,both,sizeof(full));
        for(size_t k=0;k<N(full);k++){if(full[k].resource==AXD_RSF_DESCRIPTOR && full[k].offset>=0x70)full[k].offset-=0x60;if(full[k].kind==AXD_RSF_WRITE16 && full[k].value==7)full[k].value=1;}
        c.rx.ring.count=2;c.rx.ring.in=c.rx.ring.out=1;c.receive_posts=UINT32_MAX;c.rx.descriptor_bytes=32;
        execute(&j,c,full,N(full),0);assert(j.completed_nodes==2 && j.needed==2 && j.context.rx.ring.out==1);
    }
    {
        struct event e[N(one)-1];memcpy(e,one,18*sizeof(*e));memcpy(e+18,one+19,(N(one)-19)*sizeof(*e));c=context();c.rx.head=c.rx.tail=0;
        execute(&j,c,e,N(e),0);assert(j.head_stores==1 && j.context.rx.head==0x1000);faults(c,e,N(e),3);
    }
    {
        struct event e[25];memcpy(e,one,23*sizeof(*e));e[23]=(struct event)ST;e[24]=(struct event)POST(0x100);c=context();c.flags=0;
        execute(&j,c,e,N(e),0);assert(j.parity_reads==0 && j.descriptor_writes==4);faults(c,e,N(e),0);
    }
    {
        struct event e[29];memcpy(e,one,27*sizeof(*e));e[26].reply=8;e[27]=(struct event)ST;e[28]=(struct event)POST(0x100);c=context();
        execute(&j,c,e,N(e),0);assert(j.parity_sample==15 && j.descriptor_writes==4);faults(c,e,N(e),0);
    }
}
static void gates_and_bindings(void)
{
    struct axd_dma_rx_status_fill j;struct axd_rx_status_fill_context c=context();unsigned k;
    c.disabled=0xff;c.rx.core=0;c.rx.ring.count=1;execute(&j,c,NULL,0,0);
    c=context();c.receive_posts=0;c.rx.status_owned=c.pool_owned=0;c.pool.known=0;execute(&j,c,NULL,0,1);
    {
        const struct event e[]={ST,POST(0x170)};c=context();c.receive_posts=1;c.pool.known=0;c.pool_owned=c.rx.node_owned=c.rx.descriptor_owned=c.os_owned=c.data_owned=0;
        execute(&j,c,e,N(e),0);faults(c,e,N(e),5);
    }
    {
        const struct event e[]={ST,POST(0x100)};c=context();c.rx.ring.count=0;c.rx.ring.in=c.rx.ring.out=0;c.pool.known=0;
        execute(&j,c,e,N(e),0);faults(c,e,N(e),5);
    }
    for(k=0;k<7;k++){
        size_t n=k<2?0:k==2?1:k==3?10:k==4?12:19;struct fixture f=fixture(&j,one,n);c=context();
        if(k==0)c.pool.known=0;if(k==1)c.pool_owned=0;if(k==2)c.rx.node_owned=0;if(k==3)c.data_owned=0;if(k==4)c.os_owned=0;if(k==5)c.rx.descriptor_owned=0;if(k==6)c.rx.descriptor_bytes=127;
        assert(!axd_dma_rx_status_fill_begin(&j,&c));run(&f);assert(j.phase==(k<6?AXD_RSF_DEPENDENCY:AXD_RSF_FAILED));
        assert(j.context.rx.ring.out==7 && j.context.rx.tail==(k>=5?0x1000u:0x5000u));
    }
    c=context();execute(&j,c,one,N(one),0);assert(!axd_dma_rx_status_fill_begin(&j,&j.context));assert(j.context.rx.ring.out==0 && j.context.rx.tail==0x1000);axd_dma_rx_status_fill_cancel(&j);
    assert(axd_dma_rx_status_fill_begin(NULL,&c)<0 && axd_dma_rx_status_fill_begin(&j,NULL)<0);
    assert(axd_dma_rx_status_fill_step(NULL,NULL,NULL)==AXD_RSF_INVALID && axd_dma_rx_status_fill_cancel(NULL)==AXD_RSF_INVALID);
}
int main(void){normal_and_failures();multiple_and_alternatives();gates_and_bindings();puts("current status RX fill: literal inline pool/byte/index/descriptor/publication traces and all callback prefixes passed");return 0;}
