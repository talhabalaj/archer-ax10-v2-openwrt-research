/* SPDX-License-Identifier: MIT */
#include "../driver/phy_table_read.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a)/sizeof((a)[0]))
struct event {enum axd_d11_kind kind;uint32_t offset,value,reply;};
#define ST {AXD_D11_STORE_BARRIER,0,0,0}
#define SY {AXD_D11_FULL_BARRIER,0,0,0}
#define W(o,v) {AXD_D11_WRITE32,o,v,0}
#define H(o,v) {AXD_D11_WRITE16,o,v,0}
#define R(o,v) {AXD_D11_READ32,o,0,v}
#define Q(o,v) {AXD_D11_READ16,o,0,v}
#define P(s,v) ST,W(0x3fc,((uint32_t)(v)<<16)|(s))
#define READ(s,v) ST,H(0x3fc,s),Q(0x3fe,v),SY
#define MOD(s,v,w) ST,H(0x3fc,s),ST,Q(0x3fe,v),SY,H(0x3fe,w)
#define HEAD P(0xd,0x144),P(0xe,8)
static const struct event normal[]={R(0x120,0),SY,READ(0x19e,2),HEAD,READ(0xf,0xfeedaa11),READ(0xf,0xfacebb22)};
static const struct event guard[]={R(0x120,0),SY,READ(0x19e,0),MOD(0x19e,0x5540,0x5542),HEAD,
    READ(0xf,0xfeedaa11),READ(0xf,0xfacebb22),MOD(0x19e,0x6703,0x6701)};
static const struct event zero[]={R(0x120,0),SY,READ(0x19e,2),HEAD};
static struct axd_phy_table_context context(void)
{
    struct axd_phy_table_context c={0};c.phy.core=130;c.phy.minor=1;c.phy.phy_type=0xb;c.phy.phy_revision=51;
    c.phy.phy_core_count=1;c.phy.bookkeeping=0x1234;c.shared_bus_word=1;c.write_threshold=0xffff;return c;
}
struct trace {struct axd_phy_table_read *j;const struct event *events;size_t count,cursor,at;unsigned mode,book;};
static struct trace trace(struct axd_phy_table_read *j,const struct event *e,size_t n)
{struct trace t={j,e,n,0,(size_t)-1,0,0x1234};return t;}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct trace *t=opaque;size_t n=t->cursor++;const struct event *e;
    assert(n<t->count);e=&t->events[n];
    if(op->kind!=e->kind || op->offset!=e->offset || op->value!=e->value)
        fprintf(stderr,"reader event%zu actual%u/%x/%x expected%u/%x/%x\n",n,op->kind,op->offset,op->value,e->kind,e->offset,e->value);
    assert(op->kind==e->kind && op->offset==e->offset && op->value==e->value);
    if(op->kind==AXD_D11_STORE_BARRIER && n+1<t->count && t->events[n+1].kind==AXD_D11_WRITE32 && t->events[n+1].offset==0x3fc)t->book=(t->book+1)&65535u;
    assert(t->j->context.phy.bookkeeping==t->book);
    if(n==t->at && (t->mode&2))assert(axd_phy_table_read_cancel(t->j)==AXD_TR_CANCELLED);
    if(n==t->at && (t->mode&1))return -1;
    if(op->kind==AXD_D11_WRITE16 && (op->offset==0x3fe || (op->offset==0x3fc && n+1<t->count && t->events[n+1].kind==AXD_D11_READ16)))t->book=0;
    *reply=e->reply;return 0;
}
static void terminal(struct trace *t)
{
    struct axd_phy_table_read copy=*t->j;size_t n=t->cursor;
    assert(axd_phy_table_read_step(t->j,io,t)==copy.phase && axd_phy_table_read_step(t->j,NULL,NULL)==copy.phase);
    assert(axd_phy_table_read_cancel(t->j)==copy.phase && n==t->cursor && !memcmp(&copy,t->j,sizeof(copy)));
}
static void run(struct trace *t)
{unsigned steps=0;while(t->j->phase==AXD_TR_RUNNING){size_t n=t->cursor;assert(++steps<1000);axd_phy_table_read_step(t->j,io,t);assert(t->cursor-n<=1);}terminal(t);}
static void exercise(const struct event *events,size_t count,unsigned first_data,unsigned bytes)
{
    struct axd_phy_table_read j;struct axd_phy_table_context c=context();uint8_t data[4]={0xa5,0xa5,0xa5,0xa5};
    struct axd_phy_table_read_request q={0xbeef0144,bytes,0xcafe0008,8,data,sizeof(data)};
    struct trace t=trace(&j,events,count);size_t k;unsigned mode;
    assert(!axd_phy_table_read_begin(&j,&q,&c));run(&t);
    assert(j.phase==AXD_TR_TRACE_COMPLETE && t.cursor==count && j.bytes_completed==bytes && j.index==bytes);
    assert(data[0]==(bytes?0x11:0xa5) && data[1]==(bytes?0x22:0xa5) && data[2]==0xa5 && data[3]==0xa5);
    for(k=0;k<count;k++)for(mode=1;mode<=3;mode++){
        unsigned committed=bytes && k>=first_data+4?1:0;committed+=bytes && k>=first_data+8?1:0;
        memset(data,0xa5,sizeof(data));t=trace(&j,events,count);t.at=k;t.mode=mode;
        assert(!axd_phy_table_read_begin(&j,&q,&c));run(&t);
        assert(j.phase==(mode&1?AXD_TR_FAILED:AXD_TR_CANCELLED) && j.error==(mode&1?AXD_TR_IO_ERROR:AXD_TR_NO_ERROR));
        assert(t.cursor==k+1 && j.completed_ops==k+(mode==2));
        assert(j.bytes_completed==committed && data[0]==(committed?0x11:0xa5) && data[1]==(committed>1?0x22:0xa5));
        if(bytes && k==first_data+3)assert(j.sample_valid==(mode==2) && j.bytes_completed==0);
    }
}
static void gates(void)
{
    struct axd_phy_table_read j;struct axd_phy_table_context c=context();uint8_t data[2]={7,8};
    struct axd_phy_table_read_request q={0x144,2,8,8,data,sizeof(data)};struct trace t;
    assert(!axd_phy_table_read_begin(&j,&q,&c));t=trace(&j,normal,N(normal));
    while(t.cursor<14)axd_phy_table_read_step(&j,io,&t);
    assert(j.sample_valid && j.sample==0xaa11 && !j.bytes_completed && data[0]==7);
    axd_phy_table_read_cancel(&j);terminal(&t);assert(data[0]==7 && data[1]==8);
    q.count=0;q.data=NULL;q.data_length=0;assert(!axd_phy_table_read_begin(&j,&q,&c));
    assert(!axd_phy_table_read_begin(&j,&j.request,&j.context));axd_phy_table_read_cancel(&j);
    q.count=2;q.data=data;q.data_length=1;assert(axd_phy_table_read_begin(&j,&q,&c)<0 && j.error==AXD_TR_BAD_DATA);
    q.data_length=2;q.data=(uint8_t *)&j;assert(axd_phy_table_read_begin(&j,&q,&c)<0 && j.error==AXD_TR_BAD_DATA);
    q.data=data;q.width=16;assert(axd_phy_table_read_begin(&j,&q,&c)<0 && j.error==AXD_TR_UNSUPPORTED);
    q.width=8;{const struct event active[]={R(0x120,1),SY};t=trace(&j,active,N(active));assert(!axd_phy_table_read_begin(&j,&q,&c));run(&t);assert(j.phase==AXD_TR_DEPENDENCY && j.dependency==AXD_TR_MAC_CONTEXT && t.cursor==2);}
    assert(axd_phy_table_read_begin(NULL,&q,&c)<0 && axd_phy_table_read_begin(&j,NULL,&c)<0);
    assert(axd_phy_table_read_step(NULL,NULL,NULL)==AXD_TR_INVALID && axd_phy_table_read_cancel(NULL)==AXD_TR_INVALID);
}
int main(void){exercise(normal,N(normal),10,2);exercise(guard,N(guard),16,2);exercise(zero,N(zero),0,0);gates();puts("width8 AC table read: literal headers/guard/fresh samples, byte stores and callback exits passed");return 0;}
