/* SPDX-License-Identifier: MIT */
#include "../driver/phy_table_write.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Original synthetic literals. Read samples are supplied, never default-zero
 * or write echoes. Packed writes differ deliberately from continuation writes.
 */
struct event { enum axd_d11_kind kind; uint32_t offset, value, reply; };
#define N(a) (sizeof(a) / sizeof((a)[0]))
#define ST {AXD_D11_STORE_BARRIER,0,0,0}
#define SY {AXD_D11_FULL_BARRIER,0,0,0}
#define W(o,v) {AXD_D11_WRITE32,(o),(v),0}
#define H(o,v) {AXD_D11_WRITE16,(o),(v),0}
#define R(o,v) {AXD_D11_READ32,(o),0,(v)}
#define Q(o,v) {AXD_D11_READ16,(o),0,(v)}
#define P(s,v) ST,W(0x3fc,((uint32_t)(v)<<16)|(s))
#define C(v) ST,H(0x3fe,(v))
#define GUARD(v) ST,H(0x3fc,0x19e),Q(0x3fe,(v)),SY
#define MOD(v,w) ST,H(0x3fc,0x19e),ST,Q(0x3fe,(v)),SY,H(0x3fe,(w))
#define PREFIX R(0x120,0),SY,GUARD(2)
#define HEADERS P(0xd,0x2f),P(0xe,0x5678)
#define FLUSH Q(0x3e0,0xfeedbeef),SY
#define SHM(v) ST,W(0x160,0x10010),R(0x160,0xdeadbeef),SY,Q(0x164,(v)),SY
static const uint8_t payload[] = {1,0xa1,2,0xb2,3,0xc3,4,0xd4,5,0xe5,6,0xf6,7,0x87,8,0x98};
static const struct event width48[] = {
    PREFIX,HEADERS,P(0x11,0xa101),C(0xb202),C(0xc303),
    P(0x11,0xd404),C(0xe505),C(0xf606)
};
static const struct event width32[] = {PREFIX,HEADERS,P(0x10,0xb202),P(0xf,0xa101),P(0x10,0xd404),P(0xf,0xc303)};
static const struct event width16[] = {PREFIX,HEADERS,P(0xf,0xa101),P(0xf,0xb202)};
static const struct event width8[] = {PREFIX,HEADERS,P(0xf,1),P(0xf,0xa1)};
static const struct event width64[] = {
    PREFIX,HEADERS,P(0x11,0xa101),C(0xb202),C(0xc303),C(0xd404),
    P(0x11,0xe505),C(0xf606),C(0x8707),C(0x9808)
};
static const struct event zero_count[] = {PREFIX,HEADERS};
static const struct event threshold3[] = {
    PREFIX,HEADERS,FLUSH,P(0x11,0xa101),C(0xb202),C(0xc303),
    P(0x11,0xd404),C(0xe505),C(0xf606)
};
static const struct event threshold1[] = {
    PREFIX,FLUSH,P(0xd,0x2f),FLUSH,P(0xe,0x5678),
    FLUSH,P(0x11,0xa101),C(0xb202),C(0xc303),
    FLUSH,P(0x11,0xd404),C(0xe505),C(0xf606)
};
static const struct event guard_clear[] = {
    R(0x120,0),SY,GUARD(0),MOD(0x4400,0x4402),HEADERS,
    P(0x11,0xa101),C(0xb202),C(0xc303),MOD(0x9903,0x9901)
};
static const struct event active_mac[] = {
    R(0x120,1),SY,
    R(0x120,3),SY,R(0x120,3),SY,R(0x128,0),SY,
    ST,W(0x120,0x04000002),R(0x128,1),SY,R(0x128,1),SY,R(0x120,2),SY,
    GUARD(2),HEADERS,
    R(0x120,2),SY,ST,W(0x120,0x04000003),ST,W(0x128,1),
    R(0x120,3),SY,R(0x128,0),SY,SHM(2)
};
struct trace {
    struct axd_phy_table_write *job;
    const struct event *events;
    size_t count,cursor,fail_at,cancel_at;
    unsigned inspect_mac;
};
static struct axd_phy_table_context context(void)
{
    struct axd_phy_table_context c = {0};
    c.phy.core=130;c.phy.minor=1;c.phy.phy_type=0xb;c.phy.phy_revision=51;
    c.phy.phy_core_count=1;c.phy.bookkeeping=0x1234;
    c.mac.core=130;c.mac.minor=1;c.mac.main_control=3;c.mac.wake_reasons=8;
    c.mac.chip_id=63178;c.mac.band_present=1;c.mac_context_valid=1;
    c.write_threshold=32;
    return c;
}
static struct axd_phy_table_write_request request(uint32_t width,uint32_t count)
{
    const struct axd_phy_table_write_request q={0xbeef002f,count,0x12345678,width,payload,sizeof(payload)};
    return q;
}
static struct trace trace(struct axd_phy_table_write *j,const struct event *e,size_t count)
{
    struct trace t={0};t.job=j;t.events=e;t.count=count;t.fail_at=t.cancel_at=(size_t)-1;return t;
}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *value)
{
    struct trace *t=opaque;size_t i=t->cursor++;const struct event *e;
    assert(i<t->count);e=&t->events[i];
    if(op->kind!=e->kind||op->offset!=e->offset||op->value!=e->value)
        fprintf(stderr,"event%zu width%u: actual %u/%x/%x expected %u/%x/%x\n",i,
                t->job->request.width,(unsigned)op->kind,op->offset,op->value,
                (unsigned)e->kind,e->offset,e->value);
    assert(op->kind==e->kind&&op->offset==e->offset&&op->value==e->value);
    if(t->inspect_mac){
        assert(t->job->context.mac.main_control==(i<8||i>=26?3u:2u));
        assert(t->job->context.mac.main_depth==(i<4||i>=26?0u:1u));
        assert(t->job->context.mac.wake_reasons==(i<4?8u:12u));
    }
    if(i==t->cancel_at)assert(axd_phy_table_write_cancel(t->job)==AXD_TW_CANCELLED);
    if(i==t->fail_at)return -1;
    *value=e->reply;return 0;
}
static void terminal(struct axd_phy_table_write *j,struct trace *t)
{
    struct axd_phy_table_write before=*j;size_t cursor=t->cursor;
    assert(j->phase!=AXD_TW_RUNNING);
    assert(axd_phy_table_write_step(j,io,t)==before.phase);
    assert(axd_phy_table_write_step(j,NULL,NULL)==before.phase);
    assert(axd_phy_table_write_cancel(j)==before.phase);
    assert(t->cursor==cursor&&memcmp(j,&before,sizeof(before))==0);
}
static void run(struct axd_phy_table_write *j,struct trace *t)
{
    unsigned steps=0;
    while(j->phase==AXD_TW_RUNNING){size_t cursor=t->cursor;
        axd_phy_table_write_step(j,io,t);assert(t->cursor-cursor<=1);assert(++steps<400);}
}
static void execute(struct axd_phy_table_context c,struct axd_phy_table_write_request q,
                    const struct event *events,size_t count,unsigned words,unsigned flushes)
{
    struct axd_phy_table_write j;struct trace t=trace(&j,events,count);
    t.inspect_mac=events==active_mac;
    assert(axd_phy_table_write_begin(&j,&q,&c)==0);run(&j,&t);
    assert(j.phase==AXD_TW_TRACE_COMPLETE&&j.error==AXD_TW_NO_ERROR);
    assert(j.attempted_ops==count&&j.completed_ops==count&&t.cursor==count);
    if(j.data_writes!=words||j.flush_reads!=flushes||j.index!=q.count)
        fprintf(stderr,"width%u: data=%llu/%u flush=%u/%u index=%u/%u\n",
                q.width,(unsigned long long)j.data_writes,words,j.flush_reads,flushes,j.index,q.count);
    assert(j.data_writes==words&&j.flush_reads==flushes&&j.index==q.count);
    if(events==active_mac)assert(j.context.mac.main_depth==0&&j.context.mac.wake_reasons==8);
    terminal(&j,&t);
}
static void widths_and_flushes(void)
{
    struct axd_phy_table_context c=context();
    execute(c,request(8,2),width8,N(width8),2,0);
    execute(c,request(16,2),width16,N(width16),2,0);
    execute(c,request(32,2),width32,N(width32),4,0);
    execute(c,request(42,2),width48,N(width48),6,0);
    execute(c,request(48,2),width48,N(width48),6,0);
    execute(c,request(60,2),width64,N(width64),8,0);
    execute(c,request(64,2),width64,N(width64),8,0);
    {struct axd_phy_table_write_request q=request(48,0);q.data=NULL;q.data_length=0;
     execute(c,q,zero_count,N(zero_count),0,0);}
    c.shared_bus_word=1;c.write_threshold=3;
    execute(c,request(48,2),threshold3,N(threshold3),6,1);
    c.write_threshold=1;
    execute(c,request(48,2),threshold1,N(threshold1),6,4);
    c.write_threshold=0;
    execute(c,request(48,2),threshold1,N(threshold1),6,4);
    c.shared_bus_word=2;c.write_threshold=1;
    execute(c,request(48,2),width48,N(width48),6,0);
    execute(context(),request(48,1),guard_clear,N(guard_clear),3,0);
    execute(context(),request(48,0),active_mac,N(active_mac),0,0);
}
static void sweep(struct axd_phy_table_context c,struct axd_phy_table_write_request q,
                   const struct event *events,size_t count)
{
    size_t cut;unsigned mode;
    for(cut=0;cut<count;cut++)for(mode=0;mode<3;mode++){
        struct axd_phy_table_write j;struct trace t=trace(&j,events,count);
        size_t k;uint64_t data_writes=0;size_t start=10,end=count;
        t.inspect_mac=events==active_mac;
        if(mode!=1)t.fail_at=cut;if(mode!=0)t.cancel_at=cut;
        assert(axd_phy_table_write_begin(&j,&q,&c)==0);run(&j,&t);
        assert(t.cursor==cut+1&&j.attempted_ops==cut+1&&j.completed_ops==cut+(mode==1));
        assert(j.phase==(mode==1?AXD_TW_CANCELLED:AXD_TW_FAILED));
        assert(j.error==(mode==1?AXD_TW_NO_ERROR:AXD_TW_IO_ERROR));
        if(events==threshold3)start=12;
        if(events==guard_clear){start=16;end=22;}
        if(events==active_mac)start=end;
        for(k=start;k<end&&k<=cut;k++)
            if((k<cut||mode==1)&&(events[k].kind==AXD_D11_WRITE32||events[k].kind==AXD_D11_WRITE16))
                data_writes++;
        assert(j.data_writes==data_writes);
        if(events==guard_clear)assert(j.changed_guard==(cut>11||(cut==11&&mode==1)));
        if(events==threshold3){
            uint16_t expected=cut<3||(cut==3&&mode!=1)?0x1234:
                cut<6?0:cut<8?1:cut<10?2:cut<18?0:1;
            assert(j.context.phy.bookkeeping==expected);
        }
        terminal(&j,&t);
    }
}
static void packed_direct(void)
{
    const struct event plain[]={P(0x140b,0xbeef)};
    const struct event flushed[]={FLUSH,P(0x140b,0xbeef)};
    struct axd_phy_table_context c=context();struct axd_phy_table_write j;
    struct trace t;size_t cut;unsigned mode;
    c.shared_bus_word=1;c.phy.bookkeeping=UINT16_MAX;c.write_threshold=1;
    assert(axd_phy_packed_write_begin(&j,0x140b,0xbeef,&c)==0);
    t=trace(&j,plain,N(plain));run(&j,&t);
    assert(j.phase==AXD_TW_TRACE_COMPLETE&&j.context.phy.bookkeeping==0&&!j.flush_reads);
    terminal(&j,&t);
    c.phy.bookkeeping=3;c.write_threshold=4;
    for(cut=0;cut<N(flushed);cut++)for(mode=0;mode<3;mode++){
        assert(axd_phy_packed_write_begin(&j,0x140b,0xbeef,&c)==0);
        t=trace(&j,flushed,N(flushed));if(mode!=1)t.fail_at=cut;if(mode!=0)t.cancel_at=cut;
        run(&j,&t);
        assert(j.context.phy.bookkeeping==0); /* Pre-I/O increment/reset. */
        assert(j.flush_reads==(cut==0&&mode!=1?0u:1u));
        assert(j.phase==(mode==1?AXD_TW_CANCELLED:AXD_TW_FAILED));
        assert(j.completed_ops==cut+(mode==1));terminal(&j,&t);
    }
    assert(axd_phy_packed_write_begin(&j,0x140b,0xbeef,&c)==0);
    t=trace(&j,flushed,N(flushed));run(&j,&t);assert(j.phase==AXD_TW_TRACE_COMPLETE);
    c=j.context;
    assert(axd_phy_packed_write_begin(&j,0x140b,0xbeef,&j.context)==0);
    assert(memcmp(&j.context,&c,sizeof(c))==0);
    assert(axd_phy_table_write_cancel(&j)==AXD_TW_CANCELLED);terminal(&j,&t);
}
static void arguments_dependencies_alias(void)
{
    struct axd_phy_table_context c=context();struct axd_phy_table_write j;
    struct axd_phy_table_write_request q=request(48,2);struct trace t;
    assert(axd_phy_table_write_begin(NULL,&q,&c)==-1);
    assert(axd_phy_table_write_begin(&j,NULL,&c)==-1);
    assert(axd_phy_table_write_begin(&j,&q,NULL)==-1);
    assert(axd_phy_table_write_step(NULL,NULL,NULL)==AXD_TW_INVALID);
    assert(axd_phy_table_write_cancel(NULL)==AXD_TW_INVALID);
    q.width=24;assert(axd_phy_table_write_begin(&j,&q,&c)==-1&&j.error==AXD_TW_UNSUPPORTED);
    q=request(48,2);q.data_length=11;
    assert(axd_phy_table_write_begin(&j,&q,&c)==-1&&j.error==AXD_TW_BAD_DATA);
    q=request(48,2);q.data=NULL;
    assert(axd_phy_table_write_begin(&j,&q,&c)==-1&&j.error==AXD_TW_BAD_DATA);
    assert(axd_phy_packed_write_begin(&j,0xc000,1,&c)==-1&&j.error==AXD_TW_UNSUPPORTED);
    c.phy.phy_revision=50;q=request(48,2);
    assert(axd_phy_table_write_begin(&j,&q,&c)==-1&&j.error==AXD_TW_PHY_ERROR);
    c=context();c.mac_context_valid=0;
    assert(axd_phy_table_write_begin(&j,&q,&c)==0);
    t=trace(&j,active_mac,2);run(&j,&t);
    assert(j.phase==AXD_TW_DEPENDENCY&&j.dependency==AXD_TW_MAC_CONTEXT&&t.cursor==2);
    terminal(&j,&t);
    c=context();c.mac.main_control=0x04000003;c.mac.wake_reasons=0;
    q=request(48,0);assert(axd_phy_table_write_begin(&j,&q,&c)==0);
    t=trace(&j,active_mac,N(active_mac));run(&j,&t);
    assert(j.phase==AXD_TW_DEPENDENCY&&j.dependency==AXD_TW_MAC_CALLOUT);
    assert(j.pending_callouts==AXD_PSM_WAKE_CONTROL&&j.context.mac.wake_reasons==0);
    assert(j.context.mac.main_depth==0&&j.context.mac.main_control==0x04000003);
    terminal(&j,&t);q=request(48,2);
    c=context();assert(axd_phy_table_write_begin(&j,&q,&c)==0);
    assert(axd_phy_table_write_begin(&j,&j.request,&j.context)==0);
    assert(j.request.data==payload&&j.request.width==48&&j.context.phy.bookkeeping==0x1234);
    t=trace(&j,width48,N(width48));run(&j,&t);assert(j.phase==AXD_TW_TRACE_COMPLETE);
    assert(axd_phy_table_write_begin(&j,&q,&c)==0);
    assert(axd_phy_table_write_cancel(&j)==AXD_TW_CANCELLED);
    assert(j.context.phy.bookkeeping==0x1234);terminal(&j,&t);
}
int main(void)
{
    struct axd_phy_table_context c=context();
    widths_and_flushes();packed_direct();arguments_dependencies_alias();
    sweep(c,request(48,2),width48,N(width48));
    c.shared_bus_word=1;c.write_threshold=3;sweep(c,request(48,2),threshold3,N(threshold3));
    sweep(context(),request(48,1),guard_clear,N(guard_clear));
    sweep(context(),request(48,0),active_mac,N(active_mac));
    puts("AC table write: literal widths/counters/wrappers and all callback exits passed");
    return 0;
}
