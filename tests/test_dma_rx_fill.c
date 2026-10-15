/* SPDX-License-Identifier: MIT */
#include "../driver/dma_rx_fill.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a)/sizeof((a)[0]))
struct event {enum axd_rx_fill_kind kind;enum axd_rx_fill_resource resource;uint32_t identity,offset,value,argument,reply;};
#define E(k,r,id,o,v,a,x) {AXD_RXF_##k,AXD_RXF_##r,id,o,v,a,x}
#define R(p,o,v) E(READ32,PACKET,p,o,0,0,v)
#define W(p,o,v) E(WRITE32,PACKET,p,o,v,0,0)
#define DW(o,v) E(WRITE32,DESCRIPTOR,0x3333,o,v,0,0)
#define DR(o,v) E(READ32,DESCRIPTOR,0x3333,o,0,0,v)
#define ST E(STORE_BARRIER,CHANNEL,0x4444,0,0,0,0)
#define POST(v) E(WRITE32,CHANNEL,0x4444,4,v,0,0)
#define ALLOC(n,v) E(ALLOCATE,ALLOCATOR,0x5555,0,n,0,v)
#define ACCOUNT E(ACCOUNT,OS,0x77,0,1,1,0)
#define INIT(n) ALLOC(n,0x1000),R(0x1000,0x160,0x6000),R(0x1000,0x1c,0xf0),W(0x1000,0x1c,0xd0),E(WRITE32,AUX,0x6000,0,0,0,0),ACCOUNT
/* This literal parity fixture deliberately returns descriptor observations
 * unlike the preceding stores: four fresh reads XOR to7, forcing parity bit18.
 * Source out stays7 throughout producer I/O, then commits0 only afterwards. */
static const struct event normal[]={
    INIT(64),R(0x1000,0x28,0x100000),E(WRITE32,DATA,0x100000,0,0,0,0),
    R(0x1000,0x28,0x100000),E(TRANSLATE,DATA,0x100000,0,0,0,0xc0000400),
    W(0x1000,4,0),W(0x5000,4,0x1000),
    DW(0x78,0x1400),DW(0x7c,0x55),DW(0x70,0x90000000),DW(0x74,0x30040),
    DR(0x78,1),DR(0x7c,2),DR(0x70,4),DR(0x74,0),DW(0x74,0x70040),ST,POST(0x100)
};
static const struct event first_failure[]={
    ALLOC(64,0),E(READ32,CHANNEL,0x4444,0x10,0,0,0xa0000170),E(FULL_BARRIER,CHANNEL,0x4444,0,0,0,0),
    E(READ32,CHANNEL,0x4444,4,0,0,0xf0000170),E(FULL_BARRIER,CHANNEL,0x4444,0,0,0,0),ST,POST(0x170)
};
static struct axd_rx_fill_context context(void)
{
    struct axd_rx_fill_context c;memset(&c,0,sizeof(c));
    c.rx.ring=(struct axd_dma64_ring){8,0x1fff,0x100,6,7};c.rx.core=130;c.rx.minor=1;
    c.rx.head=c.rx.tail=0x5000;c.rx.available=88;c.rx.cached_current=5;
    c.rx.status_identity=0x4444;c.rx.descriptor_identity=0x3333;c.rx.descriptor_bytes=128;
    c.rx.status_owned=c.rx.descriptor_owned=c.rx.packet_owned=1;
    c.flags=0x10001;c.receive_posts=2;c.extra_headroom=0xdeadbeef;c.no_buffer_count=99;
    c.data_offset_low=0x1000;c.data_offset_high=0x55;c.os_context=0x77;c.allocator=(struct axd_wake_u32){0x5555,1};c.buffer_size=64;
    c.os_owned=c.data_owned=c.aux_owned=c.slots_owned=1;c.slots_identity=0x7777;c.slot_count=8;return c;
}
struct fixture {struct axd_dma_rx_fill *j;const struct event *events;size_t count,seen,fault;unsigned error,cancel,writes,barriers;uint32_t original_out;};
static struct fixture fixture(struct axd_dma_rx_fill *j,const struct event *events,size_t count)
{struct fixture f;memset(&f,0,sizeof(f));f.j=j;f.events=events;f.count=count;f.original_out=7;return f;}
static int io(void *opaque,const struct axd_rx_fill_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;
    if(f->seen>=f->count)fprintf(stderr,"unexpected RX fill callback kind%u resource%u off%x stage%u event%zu\n",op->kind,op->resource,op->offset,f->j->stage,f->seen);
    assert(f->seen<f->count);e=&f->events[f->seen++];
    if(op->kind!=e->kind || op->resource!=e->resource || op->identity!=e->identity || op->offset!=e->offset || op->value!=e->value || op->argument!=e->argument)
        fprintf(stderr,"RX fill mismatch event%zu kind%u/%u resource%u/%u id%x/%x off%x/%x value%x/%x arg%x/%x\n",f->seen,op->kind,e->kind,op->resource,e->resource,op->identity,e->identity,op->offset,e->offset,op->value,e->value,op->argument,e->argument);
    assert(op->kind==e->kind && op->resource==e->resource && op->identity==e->identity && op->offset==e->offset && op->value==e->value && op->argument==e->argument);
    assert(f->j->context.rx.ring.out==f->original_out && f->j->context.rx.available==88 && f->j->context.rx.cached_current==5);
    assert(f->j->attempted_ops==f->seen && f->j->completed_ops==f->seen-1);
    if(f->seen==f->fault && f->cancel)assert(axd_dma_rx_fill_cancel(f->j)==AXD_RXF_CANCELLED);
    if(f->seen==f->fault && f->error)return -1;
    *reply=e->reply;f->writes+=op->kind==AXD_RXF_WRITE32;
    f->barriers+=op->kind==AXD_RXF_STORE_BARRIER || op->kind==AXD_RXF_FULL_BARRIER;return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_dma_rx_fill before=*f->j;size_t n=f->seen;
    assert(axd_dma_rx_fill_step(f->j,io,f)==before.phase && axd_dma_rx_fill_step(f->j,NULL,NULL)==before.phase);
    assert(axd_dma_rx_fill_cancel(f->j)==before.phase && f->seen==n && !memcmp(&before,f->j,sizeof(before)));
}
static void run(struct fixture *f)
{
    unsigned steps=0;
    while(f->j->phase==AXD_RXF_RUNNING){size_t n=f->seen;assert(++steps<1000);axd_dma_rx_fill_step(f->j,io,f);assert(f->seen-n<=1);}
    assert(f->j->attempted_ops==f->seen && f->j->completed_ops==f->seen-(f->error && f->seen==f->fault?1u:0u));terminal(f);
}
static void execute(struct axd_dma_rx_fill *j,struct axd_rx_fill_context c,const struct event *e,size_t n,uint32_t result)
{
    struct fixture f=fixture(j,e,n);f.original_out=c.rx.ring.out;assert(!axd_dma_rx_fill_begin(j,&c));run(&f);
    assert(j->phase==AXD_RXF_TRACE_COMPLETE && j->return_valid && j->return_value==result && f.seen==n);
}
static void all_faults(struct axd_rx_fill_context c,const struct event *e,size_t n,unsigned failure)
{
    struct axd_dma_rx_fill j;size_t p;unsigned mode;
    for(p=1;p<=n;p++)for(mode=1;mode<=3;mode++){
        struct fixture f=fixture(&j,e,n);f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(!axd_dma_rx_fill_begin(&j,&c));run(&f);
        assert(j.phase==(f.error?AXD_RXF_FAILED:AXD_RXF_CANCELLED));
        assert(j.error==(f.error?AXD_RXF_IO_ERROR:AXD_RXF_NO_ERROR) && !j.return_valid && f.seen==p);
        assert(j.context.rx.ring.out==7 && !j.out_committed);
        if(!failure){
            assert(j.context.no_buffer_count==99 && j.context.rx.head==0x5000);
            assert(j.context.rx.tail==(p<=12?0x5000u:0x1000u));
            assert(j.allocated_buffers==(p==1 && f.error?0u:1u));
            assert(j.completed_buffers==(p>21?1u:0u));
            {
                unsigned reads=p<17?0u:(unsigned)(p-16>4?4:p-16)-(f.error && p<=20?1u:0u);
                assert(j.parity_reads==reads && j.parity_sample==(reads==0?0u:reads==1?1u:reads==2?3u:7u));
            }
            assert(j.list_links==(p<12 ||(p==12 && f.error)?0u:1u));
        }else{
            assert(j.context.no_buffer_count==(p<=5?99u:100u));
            assert(j.allocated_buffers==0 && j.completed_buffers==0 && j.context.rx.tail==0x5000);
        }
        assert(j.producer_written==(p==n && !f.error));
    }
}
static void branch_faults(struct axd_rx_fill_context c,const struct event *e,size_t n,unsigned branch)
{
    struct axd_dma_rx_fill j;size_t p;unsigned mode;
    for(p=1;p<=n;p++)for(mode=1;mode<=3;mode++){
        struct fixture f=fixture(&j,e,n);f.fault=p;f.error=mode&1;f.cancel=mode>>1;f.original_out=c.rx.ring.out;
        assert(!axd_dma_rx_fill_begin(&j,&c));run(&f);
        assert(j.phase==(f.error?AXD_RXF_FAILED:AXD_RXF_CANCELLED));
        assert(j.error==(f.error?AXD_RXF_IO_ERROR:AXD_RXF_NO_ERROR) && !j.return_valid && f.seen==p);
        assert(j.context.rx.ring.out==c.rx.ring.out && !j.out_committed);
        assert(j.producer_written==(p==n && !f.error));
        if(branch==0){ /* aligned slot path */
            assert(j.context.rx.head==0x5000 && j.context.rx.tail==0x5000 && j.context.no_buffer_count==99);
            assert(j.completed_buffers==(p>n-2?1u:0u));
            assert(j.slot_writes==(p<n-6 ||(p==n-6 && f.error)?0u:1u));
        }else if(branch==1){ /* partial second allocation failure */
            assert(j.context.rx.head==0x5000 && j.context.rx.tail==(p<=12?0x5000u:0x1000u));
            assert(j.completed_buffers==(p>=22?1u:0u));
            assert(j.context.no_buffer_count==(p<=22?99u:100u));
        }else if(branch==2){ /* first insertion into empty bulk list */
            assert(j.context.rx.head==(p<=11?0u:0x1000u) && j.context.rx.tail==(p<=11?0u:0x1000u));
            assert(j.head_stores==(p<=11?0u:1u));
        }else if(branch==5){
            assert(j.context.no_buffer_count==99 && j.context.rx.head==0x5000 && j.context.rx.tail==(p<=12?0x5000u:0x1000u));
            assert(j.completed_buffers==(p>n-2?1u:0u));
        }else if(branch==4){
            assert(j.context.no_buffer_count==99 && j.context.rx.head==0x5000);
            assert(j.context.rx.tail==(p<=12?0x5000u:p<=33?0x1000u:0x2000u));
            assert(j.completed_buffers==(p<=21?0u:p<=42?1u:2u));
            assert(j.allocated_buffers==(p==1 && f.error?0u:p<22 ||(p==22 && f.error)?1u:2u));
        }else{assert(!j.allocated_buffers && !j.completed_buffers && j.context.no_buffer_count==99);}
    }
}
static void normal_and_failures(void)
{
    struct axd_dma_rx_fill j;struct axd_rx_fill_context c=context();struct event partial[N(normal)+1];
    execute(&j,c,normal,N(normal),1);
    assert(j.context.rx.ring.out==0 && j.context.rx.head==0x5000 && j.context.rx.tail==0x1000 && j.context.no_buffer_count==99);
    assert(j.allocated_buffers==1 && j.completed_buffers==1 && j.descriptor_writes==5 && j.parity_reads==4 && j.list_links==1 && j.producer_written && j.out_committed);
    all_faults(c,normal,N(normal),0);
    execute(&j,c,first_failure,N(first_failure),0);assert(j.context.no_buffer_count==100 && j.context.rx.ring.out==7);
    all_faults(c,first_failure,N(first_failure),1);
    {
        struct event busy[N(first_failure)];memcpy(busy,first_failure,sizeof(busy));busy[3].reply=0xf0000180;
        execute(&j,c,busy,N(busy),1);assert(j.context.no_buffer_count==100);
    }
    {
        struct event threshold[N(first_failure)];memcpy(threshold,first_failure,sizeof(threshold));threshold[0].value=204;
        c.buffer_size=204;c.extra_headroom=UINT32_MAX;execute(&j,c,threshold,N(threshold),0);
        assert(j.extra==0 && j.allocation_size==204);
        c.buffer_size=205;{struct fixture f=fixture(&j,NULL,0);assert(!axd_dma_rx_fill_begin(&j,&c));run(&f);assert(j.error==AXD_RXF_BOUNDS);}
        c=context();c.no_buffer_count=UINT32_MAX;execute(&j,c,first_failure,N(first_failure),0);assert(j.context.no_buffer_count==0);c=context();
    }
    memcpy(partial,normal,(N(normal)-2)*sizeof(*partial));
    partial[N(normal)-2]=(struct event)ALLOC(64,0);partial[N(normal)-1]=(struct event)ST;partial[N(normal)]=(struct event)POST(0x100);
    c.receive_posts=3;execute(&j,c,partial,N(partial),1);
    assert(j.context.no_buffer_count==100 && j.completed_buffers==1 && j.allocated_buffers==1 && j.context.rx.ring.out==0);
    branch_faults(c,partial,N(partial),1);
}
static void multiple_buffers_and_even_parity(void)
{
    const struct event second[]={
        ALLOC(64,0x2000),R(0x2000,0x160,0x7000),R(0x2000,0x1c,0xf0),W(0x2000,0x1c,0xd0),E(WRITE32,AUX,0x7000,0,0,0,0),ACCOUNT,
        R(0x2000,0x28,0x200000),E(WRITE32,DATA,0x200000,0,0,0,0),R(0x2000,0x28,0x200000),E(TRANSLATE,DATA,0x200000,0,0,0,0x12345678),
        W(0x2000,4,0),W(0x1000,4,0x2000),DW(8,0x12346678),DW(12,0x55),DW(0,0x80000000),DW(4,64),
        DR(8,1),DR(12,0),DR(0,0),DR(4,0),DW(4,0x40040),ST,POST(0x110)
    };
    struct axd_dma_rx_fill j;struct axd_rx_fill_context c=context();struct event e[N(normal)-2+N(second)];
    memcpy(e,normal,(N(normal)-2)*sizeof(*e));memcpy(e+N(normal)-2,second,sizeof(second));
    c.receive_posts=3;execute(&j,c,e,N(e),1);
    assert(j.context.rx.ring.out==1 && j.context.rx.tail==0x2000 && j.completed_buffers==2 && j.allocated_buffers==2 && j.descriptor_writes==10 && j.parity_reads==8 && j.list_links==2);
    branch_faults(c,e,N(e),4);
    {
        struct event full[N(e)];memcpy(full,e,sizeof(full));
        for(size_t k=0;k<N(full);k++)if(full[k].resource==AXD_RXF_DESCRIPTOR && full[k].offset>=0x70)full[k].offset-=0x60;
        c.rx.ring.count=2;c.rx.ring.in=c.rx.ring.out=1;c.rx.descriptor_bytes=32;c.receive_posts=UINT32_MAX;
        execute(&j,c,full,N(full),1);
        assert(j.target==2 && j.needed==2 && j.completed_buffers==2 && j.context.rx.ring.out==1);
    }
    {
        struct event even[N(normal)-1];memcpy(even,normal,20*sizeof(*even));even[19].reply=8;
        memcpy(even+20,normal+21,2*sizeof(*even));c=context();execute(&j,c,even,N(even),1);
        assert(j.parity_sample==15 && j.descriptor_writes==4 && j.parity_reads==4);
        branch_faults(c,even,N(even),5);
    }
}
static void alignment_paths(void)
{
    struct axd_dma_rx_fill j;struct axd_rx_fill_context c=context();unsigned variant;
    const struct event prefix[]={
        INIT(275),E(PULL,PACKET,0x1000,0,12,0,0x1111),R(0x1000,0x28,0x100003),E(PULL,PACKET,0x1000,0,5,0,0x2222),
        R(0x1000,0xe8,0),R(0x1000,0x2c,259)
    };
    const struct event linear[]={R(0x1000,0x28,0x100008),W(0x1000,0x2c,256),W(0x1000,0x15c,0x100108)};
    const struct event trimmed[]={E(TRIM,PACKET,0x1000,0,256,0,0x3333),R(0x1000,0x28,0x100008)};
    const struct event suffix[]={E(WRITE32,DATA,0x100008,0,0,0,0),R(0x1000,0x28,0x100008),E(TRANSLATE,DATA,0x100008,0,0,0,0x2000),
        E(WRITE32,SLOT,0x7777,0x1c,0x1000,0,0),DW(0x78,0x3000),DW(0x7c,0x55),DW(0x70,0x90000000),DW(0x74,256),ST,POST(0x100)};
    for(variant=0;variant<3;variant++){
        unsigned nonlinear=variant==1;
        struct event e[N(prefix)+N(linear)+N(suffix)];size_t n=N(prefix);
        memcpy(e,prefix,sizeof(prefix));e[9].reply=nonlinear?0x80:0;
        memcpy(e+n,nonlinear?trimmed:linear,nonlinear?sizeof(trimmed):sizeof(linear));n+=nonlinear?N(trimmed):N(linear);
        memcpy(e+n,suffix,sizeof(suffix));n+=N(suffix);
        c.flags=variant==2?0x10:0x1010;c.buffer_size=256;c.extra_headroom=12;
        if(variant==2){
            e[0].value=283;e[8].value=13;
            for(size_t k=0;k<n;k++){
                if(e[k].kind==AXD_RXF_READ32 && e[k].resource==AXD_RXF_PACKET && e[k].offset==0x28 && e[k].reply==0x100008)e[k].reply=0x100010;
                if(e[k].resource==AXD_RXF_DATA && e[k].identity==0x100008)e[k].identity=0x100010;
                if(e[k].kind==AXD_RXF_WRITE32 && e[k].resource==AXD_RXF_PACKET && e[k].offset==0x15c)e[k].value=0x100110;
            }
        }
        execute(&j,c,e,n,1);
        assert(j.alignment==(variant==2?16u:8u) && j.allocation_size==(variant==2?283u:275u) && j.slot_writes==1 && !j.head_stores && !j.tail_stores && j.context.rx.tail==0x5000);
        branch_faults(c,e,n,0);
    }
}
static void gates_bindings_aliases(void)
{
    struct axd_dma_rx_fill j;struct axd_rx_fill_context c=context();struct fixture f;unsigned k;
    c.disabled=0xff;c.rx.core=0;c.rx.ring.count=1;execute(&j,c,NULL,0,0);
    for(k=0;k<2;k++){
        c=context();if(k)c.segment_mode=0x80;else c.split_receive=0xff;
        f=fixture(&j,NULL,0);assert(!axd_dma_rx_fill_begin(&j,&c));run(&f);
        assert(j.phase==AXD_RXF_DEPENDENCY && j.dependency==(k?AXD_RXF_SEGMENT_PATH:AXD_RXF_SPLIT_PATH));
    }
    c=context();c.receive_posts=0;c.rx.status_owned=0;execute(&j,c,NULL,0,1);
    c=context();execute(&j,c,normal,N(normal),1);
    assert(!axd_dma_rx_fill_begin(&j,&j.context));assert(j.context.rx.ring.out==0 && j.context.rx.tail==0x1000);axd_dma_rx_fill_cancel(&j);
    for(k=0;k<6;k++){
        c=context();f=fixture(&j,normal,k==0?0:k==1?1:k==2?4:k==3?7:12);
        if(k==0)c.allocator.known=0;if(k==1)c.rx.packet_owned=0;if(k==2)c.aux_owned=0;
        if(k==3)c.data_owned=0;if(k==4)c.rx.descriptor_owned=0;if(k==5)c.rx.descriptor_bytes=127;
        assert(!axd_dma_rx_fill_begin(&j,&c));run(&f);
        assert(j.phase==(k<5?AXD_RXF_DEPENDENCY:AXD_RXF_FAILED));
        assert(j.context.rx.ring.out==7 && j.context.no_buffer_count==99 && j.context.rx.tail==(k>=4?0x1000u:0x5000u));
    }
    {
        const struct event no_work[]={ST,POST(0x170)};
        c=context();c.receive_posts=1;c.os_owned=c.data_owned=c.aux_owned=c.rx.packet_owned=c.rx.descriptor_owned=0;
        execute(&j,c,no_work,N(no_work),1);assert(!j.allocated_buffers && j.producer_written && j.out_committed);
        branch_faults(c,no_work,N(no_work),3);
    }
    {
        const struct event empty[]={ST,POST(0x100)};
        c=context();c.rx.ring.count=0;c.rx.ring.in=c.rx.ring.out=0;
        c.os_owned=c.data_owned=c.aux_owned=c.rx.packet_owned=c.rx.descriptor_owned=0;
        execute(&j,c,empty,N(empty),1);assert(!j.allocated_buffers && j.producer_written && j.out_committed);
        branch_faults(c,empty,N(empty),3);
    }
    {
        struct event e[N(normal)-1];memcpy(e,normal,11*sizeof(*e));memcpy(e+11,normal+12,(N(normal)-12)*sizeof(*e));
        c=context();c.rx.head=c.rx.tail=0;execute(&j,c,e,N(e),1);
        assert(j.context.rx.head==0x1000 && j.context.rx.tail==0x1000 && j.head_stores==1 && !j.list_links);
        branch_faults(c,e,N(e),2);
    }
    assert(axd_dma_rx_fill_begin(NULL,&c)<0 && axd_dma_rx_fill_begin(&j,NULL)<0);
    assert(axd_dma_rx_fill_step(NULL,NULL,NULL)==AXD_RXF_INVALID && axd_dma_rx_fill_cancel(NULL)==AXD_RXF_INVALID);
}
int main(void){normal_and_failures();multiple_buffers_and_even_parity();alignment_paths();gates_bindings_aliases();puts("current RX fill: literal allocation/alignment/tracking/descriptor/publication traces and callback prefixes passed");return 0;}
