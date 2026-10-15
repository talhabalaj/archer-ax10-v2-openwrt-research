/* SPDX-License-Identifier: MIT */
#include "../driver/phy_init.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independent typed synthetic oracle from the target51/init and HND assembly.
 * Table payloads are original repeated fixtures. Every read has an explicit
 * supplied reply; none comes from writes or an unspecified zero fallback.
 */
struct event {unsigned path,kind;uint32_t offset,value,reply;};
struct script {struct event events[512];size_t count;};
#define N(a) (sizeof(a)/sizeof((a)[0]))
#define D(k,o,v,r) {AXD_PI_D11,AXD_D11_##k,o,v,r}
#define ST D(STORE_BARRIER,0,0,0)
#define SY D(FULL_BARRIER,0,0,0)
#define H(o,v) D(WRITE16,o,v,0)
#define W(o,v) D(WRITE32,o,v,0)
#define R(o,v) D(READ32,o,0,v)
#define Q(o,v) D(READ16,o,0,v)
#define MOD(s,r,v) ST,H(0x3fc,s),ST,Q(0x3fe,r),SY,H(0x3fe,v)
#define READ(s,r) ST,H(0x3fc,s),Q(0x3fe,r),SY
#define P(s,v) ST,W(0x3fc,((uint32_t)(v)<<16)|(s))
#define C(k,o,v,r) {AXD_PI_CONTROL,AXD_SPG_##k,o,v,r}
#define CMOD(o,r,v,back) C(READ32,o,0,r),C(FULL_BARRIER,o,0,0),C(STORE_BARRIER,o,0,0),C(WRITE32,o,v,0),C(READ32,o,0,back),C(FULL_BARRIER,o,0,0)
static const struct event prefix[]={MOD(0x19e,0xa5a7,0xa5a5),MOD(0x1b0,0x1234,0x9234),READ(0,0xfeed12ab)};
static const struct event control[]={CMOD(0x658,0xdeadbeef,0,0x12345678),CMOD(0x65c,0xabcabcde,0xaa8abcde,0x55667788),CMOD(0x6c,0xa5a5f0f3,0xa5a50003,0xdecafbad)};
static const struct event middle[]={
    MOD(0x16e,0x4000,0x4002),MOD(0x16e,0x5002,0x5003),MOD(0x16e,0x6003,0x6013),
    P(0x16f,2000),P(0x170,2000),READ(0x19e,0xface5532),
    MOD(0x19e,0xa55a,0xa55a),MOD(0x10f7,0xffff,0xffef),MOD(0x1120,0x123f,0x123b)
};
static const struct event tail[]={MOD(0x19e,0x1230,0x1232),MOD(0x400,0xabcf,0xabcd)};
static uint8_t table0[32],table1[256],table2[4];
static void append(struct script *s,const struct event *events,size_t count)
{assert(s->count+count<=N(s->events));memcpy(s->events+s->count,events,count*sizeof(*events));s->count+=count;}
static struct script full_script(void)
{
    struct script s={0};unsigned i;
    const struct event h0[]={R(0x120,0),SY,READ(0x19e,2),P(0xd,16),P(0xe,0x1000)};
    const struct event h1[]={R(0x120,0),SY,READ(0x19e,0xaf02),P(0xd,16),P(0xe,0x1040)};
    const struct event h2[]={R(0x120,0),SY,READ(0x19e,0x7702),P(0xd,16),P(0xe,0x1020)};
    const struct event d0[]={P(0x10,0x1122),P(0xf,0x3344)};
    const struct event d1[]={P(0x10,0xa1b2),P(0xf,0xc3d4)};
    const struct event d2[]={P(0x10,0x5566),P(0xf,0x7788)};
    append(&s,prefix,N(prefix));append(&s,control,N(control));append(&s,middle,N(middle));
    append(&s,h0,N(h0));for(i=0;i<8;i++)append(&s,d0,N(d0));
    append(&s,h1,N(h1));for(i=0;i<64;i++)append(&s,d1,N(d1));
    append(&s,h2,N(h2));append(&s,d2,N(d2));append(&s,tail,N(tail));
    assert(s.count==412);return s;
}
static const struct axd_si_core catalog[]={{0x800,0x18000000,0x18100000,0},{0x812,0x18200000,0x18300000,0}};
static struct axd_phy_init_context context(void)
{
    struct axd_phy_init_context c={0};
    c.chip_id=63178;c.hardware.phy.core=130;c.hardware.phy.minor=1;
    c.hardware.phy.phy_type=0xb;c.hardware.phy.phy_revision=51;c.hardware.phy.phy_core_count=1;
    c.hardware.phy.bookkeeping=0x1234;c.hardware.write_threshold=32;
    c.hardware.mac.core=130;c.hardware.mac.minor=1;c.hardware.mac_context_valid=1;
    c.hardware.mac.main_control=3;c.hardware.mac.chip_id=63178;
    c.control.si.interconnect=1;c.control.si.core_count=N(catalog);
    c.control.si.catalog=catalog;c.control.si.catalog_length=N(catalog);
    c.control.state.current_index=1;c.control.state.current_map=0x22000000;
    c.control.state.primary_wrapper=0x22100000;
    c.control.state.cache[0].core=0x20000000;c.control.state.cache[1].core=0x22000000;
    c.control.gpio_reservation_known=1;c.control.gpio_reservation=3;
    c.control_state_known=1;
    c.control.chipcommon_revision=34;
    c.otp_word=(struct axd_wake_u32){0x0b00,1};
    c.tables[0]=(struct axd_phy_init_span){table0,sizeof(table0),1};
    c.tables[1]=(struct axd_phy_init_span){table1,sizeof(table1),1};
    c.tables[2]=(struct axd_phy_init_span){table2,sizeof(table2),1};
    return c;
}
struct fixture {struct axd_phy_init *job;const struct script *script;size_t cursor,fail_at,cancel_at;};
static struct fixture fixture(struct axd_phy_init *j,const struct script *s)
{struct fixture f={0};f.job=j;f.script=s;f.fail_at=f.cancel_at=(size_t)-1;return f;}
static int io(void *opaque,const struct axd_phy_init_operation *op,struct axd_phy_init_reply *reply)
{
    struct fixture *f=opaque;size_t n=f->cursor++;const struct event *e;unsigned kind;uint32_t offset,value;
    assert(n<f->script->count);e=&f->script->events[n];
    if(op->path==AXD_PI_D11){kind=op->d11.kind;offset=op->d11.offset;value=op->d11.value;}
    else{kind=op->control.kind;offset=op->control.offset;value=op->control.value;
        assert(op->control.mapping==0x20000000);
        assert(f->job->context.control.state.current_index==1);
        assert(f->job->context.control.state.current_map==0x22000000);
    }
    if(op->path!=e->path||kind!=e->kind||offset!=e->offset||value!=e->value)
        fprintf(stderr,"event%zu: actual%u/%u/%x/%x expected%u/%u/%x/%x\n",n,
                (unsigned)op->path,kind,offset,value,e->path,e->kind,e->offset,e->value);
    assert(op->path==e->path&&kind==e->kind&&offset==e->offset&&value==e->value);
    if(n==f->cancel_at)assert(axd_phy_init_cancel(f->job)==AXD_PI_CANCELLED);
    if(n==f->fail_at)return -1;
    reply->value=e->reply;reply->control.value=e->reply;return 0;
}
static void run(struct axd_phy_init *j,struct fixture *f)
{
    unsigned steps=0;
    while(j->phase==AXD_PI_RUNNING){size_t cursor=f->cursor;axd_phy_init_step(j,io,f);
        assert(f->cursor-cursor<=1);assert(++steps<6000);}
}
static void terminal(struct axd_phy_init *j,struct fixture *f)
{
    struct axd_phy_init before=*j;size_t cursor=f->cursor;
    assert(j->phase!=AXD_PI_RUNNING);
    assert(axd_phy_init_step(j,io,f)==before.phase);assert(axd_phy_init_step(j,NULL,NULL)==before.phase);
    assert(axd_phy_init_cancel(j)==before.phase);assert(f->cursor==cursor&&memcmp(&before,j,sizeof(before))==0);
}
static void full_and_faults(void)
{
    struct script s=full_script();struct axd_phy_init j;struct axd_phy_init_context c=context();
    struct fixture f=fixture(&j,&s);size_t cut;unsigned mode;
    assert(axd_phy_init_begin(&j,&c)==0);run(&j,&f);
    assert(j.phase==AXD_PI_TRACE_COMPLETE&&j.error==AXD_PI_NO_ERROR&&f.cursor==412);
    assert(j.phy0_valid&&j.phy0_sample==0x12ab&&j.guard_valid&&j.guard_sample==0x5532);
    assert(j.voltage_value==0x00800000&&j.tables_completed==3&&j.pending_callouts==0);
    assert(j.completed_ops==412&&j.attempted_ops==412&&j.context.control.state.current_index==1);
    terminal(&j,&f);
    c=j.context;assert(axd_phy_init_begin(&j,&j.context)==0);
    assert(memcmp(&c,&j.context,sizeof(c))==0&&j.tables_completed==0);
    assert(axd_phy_init_cancel(&j)==AXD_PI_CANCELLED);terminal(&j,&f);
    for(cut=0;cut<s.count;cut++)for(mode=0;mode<3;mode++){
        c=context();assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);
        if(mode!=1)f.fail_at=cut;if(mode!=0)f.cancel_at=cut;run(&j,&f);
        assert(j.phase==(mode==1?AXD_PI_CANCELLED:AXD_PI_FAILED));
        assert(j.error==(mode==1?AXD_PI_NO_ERROR:AXD_PI_IO_ERROR));
        assert(j.attempted_ops==cut+1&&j.completed_ops==cut+(mode==1)&&f.cursor==cut+1);
        assert(j.phy0_valid==(cut>15||(cut==15&&mode==1)));
        assert(j.guard_valid==(cut>59||(cut==59&&mode==1)));
        assert(j.context.hardware.phy.bookkeeping==(cut<5||(cut==5&&mode!=1)?0x1234:0));
        assert(j.tables_completed==(cut<120?0u:cut<386?1u:cut<400?2u:3u));
        assert(j.context.control.state.current_index==1);terminal(&j,&f);
    }
}
static void voltage_and_dependencies(void)
{
    static const uint8_t voltage_units[2][32]={
        {5,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31},
        {0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28}
    };
    unsigned phy_nonzero,otp;
    struct script s=full_script();struct axd_phy_init j;struct axd_phy_init_context c;
    struct fixture f;
    for(phy_nonzero=0;phy_nonzero<2;phy_nonzero++)for(otp=0;otp<32;otp++){
        uint32_t expected=(uint32_t)voltage_units[phy_nonzero][otp]<<20;
        c=context();c.otp_word.value=(otp<<8)|0xe000u;
        s=full_script();s.events[14].reply=phy_nonzero?0xfeed0001:0xfeed0000;
        s.events[25].value=0xaa0abcde|expected;
        assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
        assert(j.phase==AXD_PI_TRACE_COMPLETE&&j.voltage_value==expected&&f.cursor==s.count);
    }
    s=full_script();c=context();c.otp_word.known=0;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_DEPENDENCY&&j.last_field==AXD_PI_OTP&&f.cursor==16);
    assert(j.phy0_valid&&j.phy0_sample==0x12ab);terminal(&j,&f);
    c=context();c.otp_word.value=0x10000;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_FAILED&&j.error==AXD_PI_BAD_ARGUMENT&&f.cursor==16);
    c=context();c.control.gpio_reservation_known=0;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_DEPENDENCY&&f.cursor==28);terminal(&j,&f);
    c=context();c.control_state_known=0;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_DEPENDENCY&&j.last_field==AXD_PI_SI_STATE&&f.cursor==16);
    assert(axd_phy_init_begin(&j,&j.context)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_DEPENDENCY&&j.last_field==AXD_PI_SI_STATE);terminal(&j,&f);
    for(unsigned slot=0;slot<3;slot++){
        const size_t reached[3]={78,120,386};
        c=context();c.tables[slot].known=0;
        assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
        assert(j.phase==AXD_PI_DEPENDENCY&&j.last_field==(enum axd_phy_init_field)(AXD_PI_TABLE0+slot));
        assert(f.cursor==reached[slot]&&j.tables_completed==slot);terminal(&j,&f);
    }
    c=context();c.tables[1].length=255;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_FAILED&&j.error==AXD_PI_TABLE_ERROR&&f.cursor==120);
    c=context();c.tables[0].data=NULL;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_FAILED&&j.error==AXD_PI_BINDING&&f.cursor==78);
    c=context();assert(axd_phy_init_begin(NULL,&c)==-1);assert(axd_phy_init_begin(&j,NULL)==-1);
    c.chip_id=1;assert(axd_phy_init_begin(&j,&c)==-1&&j.error==AXD_PI_UNSUPPORTED);
    c=context();c.hardware.phy.phy_revision=50;
    assert(axd_phy_init_begin(&j,&c)==-1&&j.error==AXD_PI_PHY_ERROR);
    assert(axd_phy_init_step(NULL,NULL,NULL)==AXD_PI_INVALID&&axd_phy_init_cancel(NULL)==AXD_PI_INVALID);
}
int main(void)
{
    for(unsigned i=0;i<sizeof(table0);i+=4){table0[i]=0x44;table0[i+1]=0x33;table0[i+2]=0x22;table0[i+3]=0x11;}
    for(unsigned i=0;i<sizeof(table1);i+=4){table1[i]=0xd4;table1[i+1]=0xc3;table1[i+2]=0xb2;table1[i+3]=0xa1;}
    table2[0]=0x88;table2[1]=0x77;table2[2]=0x66;table2[3]=0x55;
    full_and_faults();voltage_and_dependencies();
    puts("PHY51 table init:412-event literal chain,64 voltage cases and all callback exits passed");return 0;
}
