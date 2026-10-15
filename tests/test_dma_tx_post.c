/* SPDX-License-Identifier: MIT */
#include "../driver/dma_tx_post.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))
/* Literal current dma_txdesc fixtures. Parity read replies deliberately differ
 * from all descriptor writes. No implicit ring-memory echo or default read. */
struct event {enum axd_dma_tx_post_kind kind;enum axd_dma_tx_post_resource resource;uint32_t identity,offset,value,reply;};
#define M(o,v) {AXD_DTP_MEMORY_WRITE32,AXD_DTP_DESCRIPTOR,0x1111,o,v,0}
#define R(o,v) {AXD_DTP_MEMORY_READ32,AXD_DTP_DESCRIPTOR,0x1111,o,0,v}
#define ST {AXD_DTP_STORE_BARRIER,AXD_DTP_CHANNEL,0x2222,0,0,0}
#define W(v) {AXD_DTP_CHANNEL_WRITE32,AXD_DTP_CHANNEL,0x2222,4,v,0}
static const struct event odd_last[]={
    M(0x78,0x11223344),M(0x7c,0x55667788),M(0x70,0x90040001),M(0x74,0x89abcdef),
    R(0x78,0x11111111),R(0x7c,0x22222222),R(0x70,0x33333333),R(0x74,1),
    M(0x74,0x89afcdef),ST,W(0x12300100)
};
static const struct event even_mid[]={
    M(0x28,0x11223344),M(0x2c,0x55667788),M(0x20,0x80000001),M(0x24,0x89abcdef),
    R(0x28,0x11111111),R(0x2c,0x22222222),R(0x20,0x33333333),R(0x24,0),ST,W(0x12300130)
};
static const struct event no_parity[]={
    M(0x28,0x11223344),M(0x2c,0x55667788),M(0x20,0x80040001),M(0x24,0x89abcdef)
};
static struct axd_dma_tx_post_context context(void)
{
    const struct axd_dma_tx_post_context c={{8,0x1fff,0x12300100,4,7},1,99,0x12345678,
        0x1111,128,0x2222,0x80,1,1};return c;
}
static struct axd_dma_tx_post_request request(void)
{
    const struct axd_dma_tx_post_request r={{0x80000001,0x89abcdef,0x11223344,0x55667788},0x80};return r;
}
struct fixture {
    struct axd_dma_tx_post *job;const struct event *events;size_t count,seen,fault;
    unsigned error,cancel,writes,reads,producer;uint32_t last_read;
};
static struct fixture fixture(struct axd_dma_tx_post *j,const struct event *e,size_t count)
{
    struct fixture f;memset(&f,0,sizeof(f));f.job=j;f.events=e;f.count=count;return f;
}
static int io(void *opaque,const struct axd_dma_tx_post_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;
    assert(f->seen<f->count);e=&f->events[f->seen++];
    assert(op->kind==e->kind && op->resource==e->resource && op->identity==e->identity);
    assert(op->offset==e->offset && op->value==e->value);
    assert(f->job->attempted_ops==f->seen && f->job->completed_ops==f->seen-1);
    /* Availability is committed only after the producer callback returns. */
    assert(f->job->context.available==99 && !f->job->availability_committed);
    if(op->resource==AXD_DTP_DESCRIPTOR)assert(!f->job->cursor_committed);
    else assert(f->job->cursor_committed && f->job->context.ring.out==f->job->next);
    if(f->seen==f->fault && f->cancel)assert(axd_dma_tx_post_cancel(f->job)==AXD_DTP_CANCELLED);
    if(f->seen==f->fault && f->error)return -1;
    *reply=e->reply;
    if(op->kind==AXD_DTP_MEMORY_WRITE32)f->writes++;
    if(op->kind==AXD_DTP_MEMORY_READ32){f->reads++;f->last_read=e->reply;}
    if(op->kind==AXD_DTP_CHANNEL_WRITE32)f->producer=1;
    return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_dma_tx_post before=*f->job;size_t seen=f->seen;
    assert(axd_dma_tx_post_step(f->job,io,f)==before.phase);
    assert(axd_dma_tx_post_step(f->job,NULL,NULL)==before.phase);
    assert(axd_dma_tx_post_cancel(f->job)==before.phase);
    assert(seen==f->seen && !memcmp(&before,f->job,sizeof(before)));
}
static void run(struct fixture *f)
{
    unsigned steps=0;
    while(f->job->phase==AXD_DTP_RUNNING){
        size_t before=f->seen;assert(++steps<f->count*4+50);
        axd_dma_tx_post_step(f->job,io,f);assert(f->seen-before<=1);
        assert(f->job->attempted_ops==f->seen);
        assert(f->job->completed_ops==f->seen-(f->error && f->seen==f->fault ? 1u:0u));
        assert(f->job->descriptor_writes==f->writes && f->job->parity_reads==f->reads);
        assert(f->job->last_read==f->last_read && f->job->producer_written==f->producer);
    }
    terminal(f);
}
static void execute(struct axd_dma_tx_post *j,struct axd_dma_tx_post_request r,
    struct axd_dma_tx_post_context c,const struct event *e,size_t count)
{
    struct fixture f=fixture(j,e,count);assert(axd_dma_tx_post_begin(j,&r,&c)==0);run(&f);
    assert(j->phase==AXD_DTP_DONE && j->return_valid && j->return_value==0 && f.seen==count);
}
static void literals_and_exits(void)
{
    struct axd_dma_tx_post_context c=context();struct axd_dma_tx_post_request r=request();
    struct axd_dma_tx_post j;size_t p;unsigned mode;
    execute(&j,r,c,odd_last,N(odd_last));
    assert(j.request.descriptor.control1==0x90040001 && j.request.descriptor.control2==0x89abcdef);
    assert(j.context.ring.out==0 && j.context.available==3 && j.context.no_descriptor_count==0x12345678);
    for(p=1;p<=N(odd_last);p++)for(mode=1;mode<=3;mode++){
        struct fixture f=fixture(&j,odd_last,N(odd_last));f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(axd_dma_tx_post_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_DTP_FAILED : AXD_DTP_CANCELLED));
        assert(j.error==(f.error ? AXD_DTP_IO_ERROR : AXD_DTP_NO_ERROR) && f.seen==p);
        assert(j.context.ring.out==(p<=9 ? 7u:0u) && j.cursor_committed==(p>9));
        assert(j.context.available==99 && !j.availability_committed && !j.return_valid);
        assert(j.request.descriptor.control1==0x90040001 && j.request.descriptor.control2==0x89abcdef);
    }
    c=context();c.ring.out=2;c.ring.in=0;c.burstsize_control=0;
    execute(&j,r,c,even_mid,N(even_mid));assert(j.context.ring.out==3 && j.context.available==4);
    for(p=1;p<=N(even_mid);p++)for(mode=1;mode<=3;mode++){
        struct fixture f=fixture(&j,even_mid,N(even_mid));f.fault=p;f.error=mode&1;f.cancel=mode>>1;
        assert(axd_dma_tx_post_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_DTP_FAILED : AXD_DTP_CANCELLED));
        assert(j.context.ring.out==(p<=8 ? 2u:3u) && j.context.available==99);
    }
    c.control_flags=0x8000;c.burstsize_control=2;r.commit=0;
    /* Unreached channel owner/mask are not validated for a noncommit post. */
    c.channel_owned=0;c.channel_identity=0;c.ring.pointer_mask=0;c.ring.pointer_base=1;
    execute(&j,r,c,no_parity,N(no_parity));assert(j.context.available==4 && !j.producer_written);
}
static void full_and_partial_software(void)
{
    struct axd_dma_tx_post j;struct axd_dma_tx_post_context c=context();
    struct axd_dma_tx_post_request r=request();struct fixture f=fixture(&j,NULL,0);unsigned steps;
    c.ring.in=0;c.descriptor_owned=c.channel_owned=0;c.no_descriptor_count=UINT32_MAX;
    c.descriptor_identity=c.channel_identity=c.descriptor_bytes=0;c.ring.pointer_mask=0;
    assert(axd_dma_tx_post_begin(&j,&r,&c)==0);run(&f);
    assert(j.phase==AXD_DTP_DONE && j.return_value==UINT32_MAX && j.return_valid);
    assert(!j.context.available && !j.context.no_descriptor_count && !j.cursor_committed);
    assert(!memcmp(&j.request.descriptor,&r.descriptor,sizeof(r.descriptor)));
    assert(axd_dma_tx_post_begin(&j,&r,&c)==0);
    for(steps=0;j.context.available==99;steps++){
        assert(steps<8);axd_dma_tx_post_step(&j,NULL,NULL);
    }
    assert(axd_dma_tx_post_cancel(&j)==AXD_DTP_CANCELLED);
    assert(j.context.no_descriptor_count==UINT32_MAX && !j.return_valid);
    c=context();assert(axd_dma_tx_post_begin(&j,&r,&c)==0);
    for(steps=0;!(j.request.descriptor.control1&AXD_DMA64_NOT_PCIE);steps++){
        assert(steps<8);axd_dma_tx_post_step(&j,NULL,NULL);
    }
    assert(!(j.request.descriptor.control1&AXD_DMA64_EOT));
    assert(axd_dma_tx_post_cancel(&j)==AXD_DTP_CANCELLED);
    assert(j.context.ring.out==7 && !j.attempted_ops);
}
static void reached_bindings_and_alias(void)
{
    struct axd_dma_tx_post j;struct axd_dma_tx_post_context c;
    struct axd_dma_tx_post_request r=request();unsigned kind;
    for(kind=0;kind<5;kind++){
        struct fixture f=fixture(&j,odd_last,kind>=3 ? 9 : 0);c=context();
        if(kind==0)c.descriptor_owned=0;
        if(kind==1)c.descriptor_identity=0;
        if(kind==2)c.descriptor_bytes=127;
        if(kind==3)c.channel_owned=0;
        if(kind==4)c.ring.pointer_mask=0x1ff0;
        assert(axd_dma_tx_post_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==((kind==0 || kind==3) ? AXD_DTP_DEPENDENCY : AXD_DTP_FAILED));
        assert(j.context.ring.out==(kind>=3 ? 0u:7u) && j.context.available==99);
        assert(j.request.descriptor.control1==0x90040001 && !j.return_valid);
    }
    c=context();execute(&j,r,c,odd_last,N(odd_last));
    /* Aliased restart keeps the updated input flags and current out0; use a
     * new literal slot0 trace, with no parity and no producer commit. */
    j.context.control_flags=0;j.request.commit=0;
    {
        const struct event e[]={M(8,0x11223344),M(12,0x55667788),M(0,0x90040001),M(4,0x89abcdef)};
        struct fixture f=fixture(&j,e,N(e));j.context.available=99;
        assert(axd_dma_tx_post_begin(&j,&j.request,&j.context)==0);run(&f);
        assert(j.phase==AXD_DTP_DONE && j.context.ring.out==1 && j.context.available==2);
    }
    c=context();c.ring.count=0;assert(axd_dma_tx_post_begin(&j,&r,&c)<0 && j.error==AXD_DTP_BOUNDS);
    c=context();c.ring.out=8;assert(axd_dma_tx_post_begin(&j,&r,&c)<0 && j.error==AXD_DTP_BOUNDS);
    assert(axd_dma_tx_post_begin(NULL,&r,&c)<0);
    assert(axd_dma_tx_post_step(NULL,NULL,NULL)==AXD_DTP_INVALID);
    assert(axd_dma_tx_post_cancel(NULL)==AXD_DTP_INVALID);
}
int main(void)
{
    literals_and_exits();full_and_partial_software();reached_bindings_and_alias();
    puts("current DMA TX descriptor posting: literal memory/commit traces and callback prefixes passed");return 0;
}
