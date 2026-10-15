/* SPDX-License-Identifier: MIT */
#include "../driver/phy_channel_init.h"
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
#define SH(s,a,v) ST,W(0x160,(s)|((a)>>2)),R(0x160,0xdeadbeef),SY,ST,H(0x164+((a)&2),(v))
/* Original synthetic runtime thresholds; no board defaults or read echoes. */
static const struct event first20[]={P(0x255,0x3100),P(0x256,0x4200),P(0x266,0x3100),P(0x267,0x4200),
    P(0x25d,0),P(0x259,0x5678),P(0x25f,0x5f88),P(0x25a,1),SH(0x10000,0x262,0x4200),
    P(0x25c,1),P(0x260,0x3206),P(0x264,0x141)};
static const struct event disabled[]={P(0x25c,0)};
static const struct event tail20[]={P(0x25e,0x34a3),P(0x260,0x3237),P(0x261,0x14),P(0x25b,8),P(0x262,200),P(0x263,25),P(0x262,600),P(0x250,0xa419)};
static const struct event tail40[]={P(0x25e,0x34a3),P(0x260,0x3237),P(0x261,0x14),P(0x25b,16),P(0x262,400),P(0x263,50),P(0x262,1200),P(0x250,0xac32)};
static const struct event tail80[]={P(0x25e,0x34a3),P(0x260,0x3237),P(0x261,0x14),P(0x25b,31),P(0x262,800),P(0x263,100),P(0x250,0xac64),P(0x262,2400)};
static const struct event tailother[]={P(0x25e,0x34a3),P(0x260,0x32f7),P(0x261,0x14),P(0x25b,0),P(0x262,1600),P(0x263,200),P(0x250,0xacc8),P(0x262,4800)};
static const struct event mhf_on[]={SH(0x10000,0x1a4,0xa55a),SH(0x110000,0x1a4,0xa55a)};
static const struct event mhf_off[]={SH(0x10000,0x1a4,0x855a),SH(0x110000,0x1a4,0x855a)};
static const struct event hw_on[]={P(0x1100,0x7800),P(0x358,0xb0),P(0x359,0x37f),P(0x35a,0x3340),P(0x13a,0x48),
    ST,H(0x3fc,0x16c),ST,Q(0x3fe,0xfedcbabe),SY,H(0x3fe,0x3abe),P(0x1101,0x200),P(0x110f,0x280),
    ST,H(0x3fc,0x13a),Q(0x3fe,0xface1234),SY,SH(0x10000,0x3b2,0x1234)};
static const struct event hw_off[]={P(0x1100,0x7000),P(0x13a,0x3f),P(0x358,0xc07f),P(0x359,100),
    ST,H(0x3fc,0x35a),ST,Q(0x3fe,0xabcdba98),SY,H(0x3fe,0xb064),
    ST,H(0x3fc,0x13a),Q(0x3fe,0xface1234),SY,SH(0x10000,0x3b2,0x1234)};
static struct axd_wake_u32 known(uint32_t v){const struct axd_wake_u32 k={v,1};return k;}
static struct axd_phy_channel_context context(void)
{
    struct axd_phy_channel_context c={0};unsigned k;
    c.hardware.phy.core=130;c.hardware.phy.minor=1;c.hardware.phy.phy_type=0xb;c.hardware.phy.phy_revision=51;
    c.hardware.phy.phy_core_count=1;c.hardware.phy.bookkeeping=0x1234;c.hardware.write_threshold=0xffff;
    c.hardware.mac.core=130;c.hardware.mac.minor=1;c.hardware.mac_context_valid=1;
    c.hardware.shared_bus_word=1;c.object=(struct axd_object_context){130,1,1};
    c.chanspec=known(0x5024);c.radar_state_pointer=known(0x1111);
    for(k=0;k<8;k++){c.thresholds[k].first=known(0x3100+k);c.thresholds[k].second=known(0x4200+k);}
    c.state176=known(0x5678);c.state1b0=known(0x789a);c.radar_shm_offset=known(0x262);c.hwobss_shm_offset=known(0x3b2);
    c.current_band=known(0x2222);c.mhf_cache_pointer=known(0x3333);c.mhf0=known(0x855a);c.mhf0_shm_offset=known(0x1a4);
    c.mhf_clock_up=(struct axd_wake_u8){1,0x80};c.mhf_extended=(struct axd_wake_u8){1,0xff};
    c.state170=0xaaaa;c.state172=0xbbbb;c.pi47c=0xcccc;return c;
}
struct trace {struct axd_phy_channel_init *j;const struct event *events;size_t count,cursor,fault;unsigned mode,bookkeeping;};
static struct trace trace(struct axd_phy_channel_init *j,const struct event *e,size_t n)
{struct trace t={j,e,n,0,(size_t)-1,0,0x1234};return t;}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct trace *t=opaque;size_t i=t->cursor++;const struct event *e;
    assert(i<t->count);e=&t->events[i];
    if(op->kind!=e->kind || op->offset!=e->offset || op->value!=e->value)
        fprintf(stderr,"channel event%zu actual%u/%x/%x expected%u/%x/%x\n",i,op->kind,op->offset,op->value,e->kind,e->offset,e->value);
    assert(op->kind==e->kind && op->offset==e->offset && op->value==e->value);
    /* Packed write increments before its ST; direct PHY read/modify data
     * write clears the same shared canonical u16 after success. */
    if(op->kind==AXD_D11_STORE_BARRIER && i+1<t->count && t->events[i+1].kind==AXD_D11_WRITE32 && t->events[i+1].offset==0x3fc)t->bookkeeping=(t->bookkeeping+1)&65535u;
    if(t->j->context.hardware.phy.bookkeeping!=t->bookkeeping)fprintf(stderr,"book event%zu actual%x expected%x command%u\n",i,t->j->context.hardware.phy.bookkeeping,t->bookkeeping,t->j->command);
    assert(t->j->context.hardware.phy.bookkeeping==t->bookkeeping);
    assert(t->j->attempted_ops==t->cursor && t->j->completed_ops==i);
    if(i==t->fault && (t->mode&2))assert(axd_phy_channel_init_cancel(t->j)==AXD_CHANNEL_CANCELLED);
    if(i==t->fault && (t->mode&1))return -1;
    if(op->kind==AXD_D11_WRITE16 && op->offset==0x3fc && i+1<t->count && t->events[i+1].kind==AXD_D11_READ16)t->bookkeeping=0;
    if(op->kind==AXD_D11_WRITE16 && op->offset==0x3fe)t->bookkeeping=0;
    *reply=e->reply;return 0;
}
static void terminal(struct trace *t)
{
    struct axd_phy_channel_init copy=*t->j;size_t n=t->cursor;
    assert(axd_phy_channel_init_step(t->j,io,t)==copy.phase && axd_phy_channel_init_step(t->j,NULL,NULL)==copy.phase);
    assert(axd_phy_channel_init_cancel(t->j)==copy.phase && n==t->cursor && !memcmp(&copy,t->j,sizeof(copy)));
}
static void run(struct trace *t)
{unsigned steps=0;while(t->j->phase==AXD_CHANNEL_RUNNING){size_t n=t->cursor;assert(++steps<3000);axd_phy_channel_init_step(t->j,io,t);assert(t->cursor-n<=1);}terminal(t);}
static size_t radar_events(struct event *e,unsigned enabled,unsigned group,unsigned high)
{
    const struct event *tail[]={tail20,tail40,tail80,tailother};size_t n=0;unsigned i,idx=group+4*high;
    if(enabled){memcpy(e,first20,sizeof(first20));n=N(first20);
        for(i=0;i<4;i++)e[2*i+1].value=((uint32_t)((i&1?0x4200:0x3100)+idx)<<16)|(i==0?0x255:i==1?0x256:i==2?0x266:0x267);
        if(group==2 && high)e[13].value=0x5e88025f;
        e[21].value=0x4200+idx;
    }else {memcpy(e,disabled,sizeof(disabled));n=N(disabled);}
    memcpy(e+n,tail[group],sizeof(tail20));
    if(group==0 && high)e[n+1].value=0xb4a3025e;
    n+=N(tail20);memcpy(e+n,enabled?mhf_on:mhf_off,sizeof(mhf_on));return n+N(mhf_on);
}
static void faults(struct axd_phy_channel_context c,enum axd_phy_channel_command cmd,unsigned enabled,const struct event *e,size_t n)
{
    struct axd_phy_channel_init j;size_t k;unsigned mode;
    for(k=0;k<n;k++)for(mode=1;mode<=3;mode++){
        struct trace t=trace(&j,e,n);t.fault=k;t.mode=mode;
        assert(!axd_phy_channel_init_begin(&j,cmd,enabled,&c));run(&t);
        assert(j.phase==(mode&1?AXD_CHANNEL_FAILED:AXD_CHANNEL_CANCELLED) && t.cursor==k+1);
        assert(j.error==(mode&1?AXD_CHANNEL_IO_ERROR:AXD_CHANNEL_NO_ERROR));
        assert(j.completed_ops==k+(mode==2));
        if(cmd==AXD_CHANNEL_RADAR_INIT){
            assert(j.context.state170==0x3100 && j.context.state172==0x4200);
            assert(j.context.pi47c==(enabled && k>=22?0x789a:0xcccc));
            assert(j.context.mhf0.value==(k>=n-12?(enabled?0xa55a:0x855a):c.mhf0.value));
        }else {assert(j.context.pi47c==0xcccc);assert(j.read_valid==(k>n-7 || (k==n-7 && mode==2)));}
    }
}
static void radar(void)
{
    static const uint16_t chanspecs[8]={0x5024,0x5824,0x6024,0x7024,0x5047,0x5847,0x6047,0x7047};
    struct axd_phy_channel_init j;struct event e[80];unsigned enabled,k;
    for(enabled=0;enabled<2;enabled++)for(k=0;k<8;k++){
        struct axd_phy_channel_context c=context();size_t n=radar_events(e,enabled,k%4,k/4);struct trace t=trace(&j,e,n);
        c.chanspec=known(chanspecs[k]);if(!enabled)c.mhf0=known(0xa55a);
        assert(!axd_phy_channel_init_begin(&j,AXD_CHANNEL_RADAR_INIT,enabled?0xff:0,&c));run(&t);
        if(j.phase!=AXD_CHANNEL_TRACE_COMPLETE || t.cursor!=n || j.packed_completed!=(enabled?19u:9u))fprintf(stderr,"radar enabled%u k%u phase%u error%u field%u seen%zu/%zu packed%u child%u/%u stage%u\n",enabled,k,j.phase,j.error,j.last_field,t.cursor,n,j.packed_completed,j.packed_job.phase,j.packed_job.error,j.stage);
        assert(j.phase==AXD_CHANNEL_TRACE_COMPLETE && t.cursor==n && j.packed_completed==(enabled?19u:9u));
        assert(j.context.state170==0x3100+k && j.context.state172==0x4200+k && j.context.pi47c==(enabled?0x789a:0xcccc));
        assert(j.shm_completed==(enabled?3u:2u) && j.mhf_completed && j.software_stores==(enabled?4u:3u));
        if(!k)faults(c,AXD_CHANNEL_RADAR_INIT,enabled,e,n);
    }
}
static void hwobss(void)
{
    struct axd_phy_channel_init j;struct axd_phy_channel_context c=context();unsigned enabled;
    c.chanspec.known=0;
    for(enabled=0;enabled<2;enabled++){
        const struct event *e=enabled?hw_on:hw_off;size_t n=enabled?N(hw_on):N(hw_off);struct trace t=trace(&j,e,n);
        assert(!axd_phy_channel_init_begin(&j,AXD_CHANNEL_HWOBSS,enabled,&c));run(&t);
        assert(j.phase==AXD_CHANNEL_TRACE_COMPLETE && t.cursor==n && j.shm_completed==1 && j.modify_completed==1 && j.read_valid && j.read_sample==0x1234);
        faults(c,AXD_CHANNEL_HWOBSS,enabled,e,n);
    }
}
static void reached_bindings(void)
{
    struct axd_phy_channel_init j;struct axd_phy_channel_context c=context();struct event e[80];size_t n=radar_events(e,1,0,0);struct trace t;
    c.current_band=known(0);c.mhf_cache_pointer.known=0;c.mhf_clock_up.known=0;c.mhf_extended.known=0;
    t=trace(&j,e,n-12);assert(!axd_phy_channel_init_begin(&j,AXD_CHANNEL_RADAR_INIT,1,&c));run(&t);assert(j.phase==AXD_CHANNEL_TRACE_COMPLETE && t.cursor==n-12);
    c=context();c.mhf_clock_up.known=0;t=trace(&j,e,n-12);assert(!axd_phy_channel_init_begin(&j,AXD_CHANNEL_RADAR_INIT,1,&c));run(&t);
    assert(j.phase==AXD_CHANNEL_DEPENDENCY && j.last_field==AXD_CHANNEL_MHF_CLOCK && j.context.mhf0.value==0xa55a);
    c=context();c.mhf_extended.known=0;t=trace(&j,e,n-6);assert(!axd_phy_channel_init_begin(&j,AXD_CHANNEL_RADAR_INIT,1,&c));run(&t);
    assert(j.phase==AXD_CHANNEL_DEPENDENCY && j.last_field==AXD_CHANNEL_MHF_EXTENDED && j.shm_completed==2);
    c=context();c.object.owned=0;t=trace(&j,e,16);assert(!axd_phy_channel_init_begin(&j,AXD_CHANNEL_RADAR_INIT,1,&c));run(&t);
    assert(j.phase==AXD_CHANNEL_DEPENDENCY && j.last_field==AXD_CHANNEL_OBJECT_OWNER && j.context.pi47c==0xcccc);
    c=context();c.thresholds[0].second.known=0;t=trace(&j,NULL,0);assert(!axd_phy_channel_init_begin(&j,AXD_CHANNEL_RADAR_INIT,1,&c));run(&t);
    assert(j.phase==AXD_CHANNEL_DEPENDENCY && j.context.state170==0x3100 && j.context.state172==0xbbbb);
    c=context();c.hwobss_shm_offset.known=0;t=trace(&j,hw_on,N(hw_on)-10);assert(!axd_phy_channel_init_begin(&j,AXD_CHANNEL_HWOBSS,1,&c));run(&t);
    assert(j.phase==AXD_CHANNEL_DEPENDENCY && j.last_field==AXD_CHANNEL_HWOBSS_SHM && !j.read_valid && t.cursor==N(hw_on)-10);
    assert(!axd_phy_channel_init_begin(&j,AXD_CHANNEL_HWOBSS,1,&j.context));assert(j.context.hwobss_shm_offset.known==0);axd_phy_channel_init_cancel(&j);
    assert(axd_phy_channel_init_begin(NULL,AXD_CHANNEL_HWOBSS,1,&c)<0 && axd_phy_channel_init_begin(&j,AXD_CHANNEL_HWOBSS,256,&c)<0);
    assert(axd_phy_channel_init_step(NULL,NULL,NULL)==AXD_CHANNEL_INVALID && axd_phy_channel_init_cancel(NULL)==AXD_CHANNEL_INVALID);
}
int main(void){radar();hwobss();reached_bindings();puts("PHY51 radar/HWOBSS: literal traces, runtime bindings and every callback exit passed");return 0;}
