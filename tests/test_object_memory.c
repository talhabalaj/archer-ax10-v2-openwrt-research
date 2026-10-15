/* SPDX-License-Identifier: MIT */
#include "../driver/object_memory.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))
/* Independent literal oracle from current four function bodies. Each read has
 * a supplied reply unrelated to the selector write, never an implicit echo. */
struct event {enum axd_d11_kind kind;uint32_t offset,value,reply;};
#define ST {AXD_D11_STORE_BARRIER,0,0,0}
#define SY {AXD_D11_FULL_BARRIER,0,0,0}
#define W(o,v) {AXD_D11_WRITE32,o,v,0}
#define H(o,v) {AXD_D11_WRITE16,o,v,0}
#define R(v) {AXD_D11_READ32,0x160,0,v}
static const uint8_t data[]={0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0};
static const struct event copy_words_tail[]={
    ST,W(0x160,0x80),R(0xf00d1234),SY,ST,W(0x164,0x78563412),
    ST,W(0x160,0x81),R(0xdeadbeef),SY,ST,H(0x164,0xbc9a)
};
static const struct event copy_half[]={
    ST,W(0x160,0x10080),R(0xfeedabcd),SY,ST,H(0x166,0x3412),
    ST,W(0x160,0x10081),R(0x87654321),SY,ST,H(0x164,0x7856),
    ST,W(0x160,0x10081),R(0xcafebabe),SY,ST,H(0x166,0xbc9a)
};
static const struct event auto_copy[]={
    ST,W(0x160,0x03020080),R(0xfeedface),SY,
    ST,W(0x164,0x78563412),ST,W(0x164,0xf0debc9a)
};
static const struct event auto_set[]={
    ST,W(0x160,0x03050080),R(0xcafef00d),SY,
    ST,W(0x164,0x12345678),ST,W(0x164,0x12345678)
};
static const struct event write_high[]={ST,W(0x160,0x3fffffff),R(0xfeedbeef),SY,ST,H(0x166,0xbeef)};
struct fixture {
    struct axd_object_memory *job;const struct event *events;size_t count,seen,fault;
    unsigned error,cancel;uint32_t bytes,addresses,last_read;
};
static struct fixture fixture(struct axd_object_memory *j,const struct event *e,size_t n)
{
    struct fixture f;memset(&f,0,sizeof(f));f.job=j;f.events=e;f.count=n;return f;
}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;
    assert(f->seen<f->count);e=&f->events[f->seen++];
    assert(op->kind==e->kind && op->offset==e->offset && op->value==e->value);
    assert(f->job->attempted_ops==f->seen && f->job->completed_ops==f->seen-1);
    if(f->seen==f->fault && f->cancel)assert(axd_object_memory_cancel(f->job)==AXD_OBJECT_CANCELLED);
    if(f->seen==f->fault && f->error)return -1;
    *reply=e->reply;
    if(e->kind==AXD_D11_READ32)f->last_read=e->reply;
    if(e->kind==AXD_D11_WRITE32 && e->offset==0x160)f->addresses++;
    else if(e->kind==AXD_D11_WRITE32)f->bytes+=4;
    else if(e->kind==AXD_D11_WRITE16)f->bytes+=2;
    return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_object_memory before=*f->job;size_t seen=f->seen;
    assert(axd_object_memory_step(f->job,io,f)==before.phase);
    assert(axd_object_memory_step(f->job,NULL,NULL)==before.phase);
    assert(axd_object_memory_cancel(f->job)==before.phase);
    assert(seen==f->seen && !memcmp(&before,f->job,sizeof(before)));
}
static void run(struct fixture *f)
{
    size_t steps=0;
    while(f->job->phase==AXD_OBJECT_RUNNING) {
        size_t before=f->seen;assert(++steps<f->count*4+30);
        axd_object_memory_step(f->job,io,f);assert(f->seen-before<=1);
        assert(f->job->attempted_ops==f->seen);
        assert(f->job->completed_ops==f->seen-(f->error && f->seen==f->fault ? 1u:0u));
        assert(f->job->completed_bytes==f->bytes && f->job->address_writes==f->addresses);
        assert(f->job->last_read==f->last_read);
    }
    terminal(f);
}
static struct axd_object_context context(void)
{const struct axd_object_context c={130,1,1};return c;}
static struct axd_object_request request(enum axd_object_command command)
{
    struct axd_object_request r;memset(&r,0,sizeof(r));r.command=command;r.address=0x200;
    r.byte_count=8;r.data=data;r.data_length=N(data);return r;
}
static void execute(struct axd_object_memory *j,struct axd_object_request r,
                    const struct event *events,size_t count)
{
    struct axd_object_context c=context();struct fixture f=fixture(j,events,count);
    assert(axd_object_memory_begin(j,&r,&c)==0);run(&f);
    assert(j->phase==AXD_OBJECT_DONE && j->error==AXD_OBJECT_NO_ERROR && f.seen==count);
}
static void sweep(struct axd_object_request r,const struct event *events,size_t count)
{
    struct axd_object_memory j;struct axd_object_context c=context();size_t p;unsigned mode;
    execute(&j,r,events,count);
    for(p=1;p<=count;p++)for(mode=1;mode<=3;mode++) {
        struct fixture f=fixture(&j,events,count);f.fault=p;f.error=mode&1u;f.cancel=mode>>1;
        assert(axd_object_memory_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_OBJECT_FAILED : AXD_OBJECT_CANCELLED));
        assert(j.error==(f.error ? AXD_OBJECT_IO_ERROR : AXD_OBJECT_NO_ERROR));
        assert(f.seen==p);
    }
}
static void literals(void)
{
    struct axd_object_request r=request(AXD_OBJECT_COPY);
    r.byte_count=6;sweep(r,copy_words_tail,N(copy_words_tail));
    r.address=0x202;r.selector=0x10000;sweep(r,copy_half,N(copy_half));
    r=request(AXD_OBJECT_COPY32);r.selector=0x20000;sweep(r,auto_copy,N(auto_copy));
    r=request(AXD_OBJECT_SET32);r.selector=0x50000;r.value=0x12345678;
    r.data=NULL;r.data_length=0;sweep(r,auto_set,N(auto_set));
    r=request(AXD_OBJECT_WRITE16);r.address=UINT32_MAX-1;r.selector=0x20000;r.value=0xdeadbeef;
    r.data=NULL;r.data_length=0;sweep(r,write_high,N(write_high));
}
static void selector_routes(void)
{
    unsigned bank,lane;struct axd_object_memory j;
    for(bank=0;bank<8;bank++)for(lane=0;lane<2;lane++) {
        const unsigned half=bank==1 || bank==2 || bank==3;
        struct axd_object_request r=request(AXD_OBJECT_COPY);
        struct event events[]={ST,W(0x160,0),R(0x12345678),SY,ST,W(0x164,0)};
        r.selector=0x80000|bank<<16;r.byte_count=half ? 2 : 4;
        r.address=0x1200+(half ? 2*lane : 0);
        events[1].value=r.selector|0x480;
        if(half) {events[5].kind=AXD_D11_WRITE16;events[5].offset=0x164+lane*2;events[5].value=0x3412;}
        else events[5].value=0x78563412;
        execute(&j,r,events,N(events));assert(j.halfword_route==half);
    }
}
static void nonpositive_and_ownership(void)
{
    struct axd_object_memory j;struct axd_object_context c=context();
    enum axd_object_command cmd;int32_t lengths[]={0,-1,INT32_MIN};size_t i;
    for(cmd=AXD_OBJECT_COPY;cmd<=AXD_OBJECT_SET32;cmd++)for(i=0;i<N(lengths);i++) {
        const struct event header[]={ST,W(0x160,0x03000080),R(0xc0decafe),SY};
        struct axd_object_request r=request(cmd);struct fixture f;
        r.byte_count=lengths[i];r.data=NULL;r.data_length=0;
        execute(&j,r,header,cmd==AXD_OBJECT_COPY ? 0 : N(header));
        c.owned=0;f=fixture(&j,header,0);
        assert(axd_object_memory_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==(cmd==AXD_OBJECT_COPY ? AXD_OBJECT_DONE : AXD_OBJECT_DEPENDENCY));c.owned=1;
    }
    {
        struct axd_object_request r=request(AXD_OBJECT_COPY);struct fixture f=fixture(&j,NULL,0);
        c.owned=0;assert(axd_object_memory_begin(&j,&r,&c)==0);run(&f);
        assert(j.phase==AXD_OBJECT_DEPENDENCY && !j.attempted_ops && !j.completed_bytes);
    }
}
static void input_bounds_and_aliases(void)
{
    struct axd_object_memory j;struct axd_object_context c=context();
    struct axd_object_request r;unsigned kind;
    for(kind=0;kind<8;kind++) {
        struct fixture f=fixture(&j,NULL,0);r=request(AXD_OBJECT_COPY);
        if(kind==0)r.byte_count=3;
        if(kind==1)r.address=2;
        if(kind==2){r.address=UINT32_MAX-3;r.byte_count=8;}
        if(kind==3)r.data=NULL;
        if(kind==4)r.data_length=7;
        if(kind==5){r.command=AXD_OBJECT_COPY32;r.byte_count=6;}
        if(kind==6){r.command=AXD_OBJECT_WRITE16;r.address=1;}
        if(kind==7){r.command=AXD_OBJECT_SET32;r.address=2;}
        assert(axd_object_memory_begin(&j,&r,&c)<0);run(&f);
        assert(j.phase==AXD_OBJECT_FAILED && !j.attempted_ops);
    }
    r=request(AXD_OBJECT_COPY);r.byte_count=6;execute(&j,r,copy_words_tail,N(copy_words_tail));
    {
        struct fixture f=fixture(&j,copy_words_tail,N(copy_words_tail));
        assert(axd_object_memory_begin(&j,&j.request,&j.context)==0);run(&f);
        assert(j.phase==AXD_OBJECT_DONE && f.seen==N(copy_words_tail));
    }
    r=request(AXD_OBJECT_COPY32);r.byte_count=0;r.data=NULL;r.address=UINT32_MAX-3;
    {const struct event e[]={ST,W(0x160,0x3fffffff),R(0xdeadbeef),SY};execute(&j,r,e,N(e));}
    c.core=129;assert(axd_object_memory_begin(&j,&r,&c)<0 && j.error==AXD_OBJECT_UNSUPPORTED);
    assert(axd_object_memory_begin(NULL,&r,&c)<0);
    assert(axd_object_memory_step(NULL,NULL,NULL)==AXD_OBJECT_INVALID);
    assert(axd_object_memory_cancel(NULL)==AXD_OBJECT_INVALID);
}
int main(void)
{
    literals();selector_routes();nonpositive_and_ownership();input_bounds_and_aliases();
    puts("object-memory independent word/halfword/auto traces and callback prefixes passed");return 0;
}
