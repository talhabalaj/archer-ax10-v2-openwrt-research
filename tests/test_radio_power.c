/* SPDX-License-Identifier: MIT */
#include "../driver/radio_power.h"
#include "../driver/phy_scheduler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct event {enum axd_d11_kind kind;uint32_t offset,value,reply;};
struct script {struct event *events;size_t count,capacity,calibration,cal0,cal1;};
struct fixture {const struct script *s;struct axd_radio_power *j;size_t cursor,fail,cancel;};
static const uint8_t mode_data[]={1,2,3,0x55,4,5,6,7,8,9,0xaa,0xbb,11,12,13,14,15,16};
static const uint8_t rccal_data[]={0x12,0x34,0xaa,0xbb,0x56,7,0x89,10};
static const uint8_t prefregs[]={0xff,0xff};
static void add(struct script *s,enum axd_d11_kind k,uint32_t o,uint32_t v,uint32_t r)
{assert(s->count<s->capacity);s->events[s->count++]=(struct event){k,o,v,r};}
static void st(struct script *s){add(s,AXD_D11_STORE_BARRIER,0,0,0);}
static void sy(struct script *s){add(s,AXD_D11_FULL_BARRIER,0,0,0);}
static void delay(struct script *s,uint32_t us){add(s,AXD_D11_DELAY_US,0,us,0);}
static void packed(struct script *s,uint16_t sel,uint16_t val)
{st(s);add(s,AXD_D11_WRITE32,0x3fc,sel|((uint32_t)val<<16),0);}
static void read_radio(struct script *s,uint16_t sel,uint32_t sample)
{st(s);add(s,AXD_D11_WRITE16,0x3d8,sel|0x8000u,0);add(s,AXD_D11_READ16,0x3da,0,sample);sy(s);}
static void modify(struct script *s,uint16_t sel,uint16_t mask,uint16_t value)
{
    /* An explicit independent fresh reply for every actual read. The expected
     * data sample never derives from a preceding model write. */
    if(sel==0xffff)return;
    read_radio(s,sel,0x1a5a5);st(s);add(s,AXD_D11_WRITE16,0x3d8,sel,0);st(s);
    add(s,AXD_D11_WRITE16,0x3da,(0xa5a5u&~mask)|(value&mask),0);
}
static void logen(struct script *s,unsigned mode)
{
    unsigned a=1+mode,b=4+mode,c=7+mode,d=11+mode,e=14+mode;
    modify(s,0x88c,4,(uint16_t)(a<<2));modify(s,0x9f6,0x4000,(uint16_t)(a<<14));
    modify(s,0x88a,1,(uint16_t)a);modify(s,0x88a,0x100,(uint16_t)(a<<8));modify(s,0x88a,2,(uint16_t)(a<<1));modify(s,0x88a,4,0);
    modify(s,0x1f1,0x4000,(uint16_t)(b<<14));modify(s,0x1f1,0x8000,(uint16_t)(b<<15));modify(s,0x1f1,2,(uint16_t)(b<<1));modify(s,0x1f1,0x2000,(uint16_t)(b<<13));
    modify(s,0x1f2,1,(uint16_t)d);modify(s,0x1f2,0x40,(uint16_t)(b<<6));modify(s,0x1f2,0x800,(uint16_t)(b<<11));modify(s,0x1f2,0x1000,(uint16_t)(b<<12));
    modify(s,0x3f1,0x4000,(uint16_t)(c<<14));modify(s,0x3f1,0x8000,(uint16_t)(c<<15));modify(s,0x3f1,2,(uint16_t)(c<<1));modify(s,0x3f1,0x2000,(uint16_t)(c<<13));
    modify(s,0x3f2,1,(uint16_t)e);modify(s,0x3f2,0x40,(uint16_t)(c<<6));modify(s,0x3f2,0x800,(uint16_t)(c<<11));modify(s,0x3f2,0x1000,(uint16_t)(c<<12));
}
static void prefix(struct script *s)
{
    packed(s,0x4728,0);packed(s,0x4720,0x3ff);packed(s,0x415,0);packed(s,0x40e,0);packed(s,0x40c,0x2000);
    packed(s,0x408,0);packed(s,0x417,0);packed(s,0x416,0xd);packed(s,0x408,7);delay(s,1000);packed(s,0x408,6);modify(s,0x88b,0x3f,1);
}
static void full(struct script *s,unsigned cores,unsigned timeout)
{
    unsigned i;uint16_t rcal=timeout?50:32,cal=timeout?0x10a4:55928;
    prefix(s);logen(s,0);
    for(i=0;i<cores;i++){uint16_t q=i<2?(uint16_t)(i?0x3f2:0x1f2):0xffff;modify(s,q,0x20,0x20);modify(s,q,0x200,0x200);modify(s,i<2?(uint16_t)(i?0x2a3:0xa3):0xffff,0x8000,0x8000);}
    packed(s,0x417,0xd);packed(s,0x408,2);packed(s,0x4728,0x180);delay(s,1000);packed(s,0x417,4);packed(s,0x4728,0x80);packed(s,0x4720,0x180);
    modify(s,0x884,8,8);modify(s,0x9fa,0x100,0x100);modify(s,0x9fa,0x1e00,0x1000);delay(s,1);
    modify(s,0x80e,0xfc0,0);modify(s,0x80e,3,1);modify(s,0x80d,0x780,0);modify(s,0x80d,0x40,0);modify(s,0x80d,0x30,0);modify(s,0x80d,0xc,0);modify(s,0x80d,2,0);modify(s,0x80d,1,0);
    modify(s,0x80f,3,0);modify(s,0x80f,0x300,0x100);modify(s,0x9f9,0x2000,0x2000);modify(s,0x9f9,0x4000,0x4000);
    modify(s,0x83c,0x3f,50);modify(s,0x815,8,0);modify(s,0x83d,0x7f,50);modify(s,0x815,0x10,0x10);
    modify(s,0x9f9,0x4000,0);modify(s,0x9f9,0x2000,0);modify(s,0x88b,0x3f,1);modify(s,0x9f9,0x2000,0x2000);
    modify(s,0x87b,0x40,0);modify(s,0x87b,8,0);modify(s,0x87b,2,2);modify(s,0x87b,0x20,0x20);modify(s,0x815,0x40,0x40);
    delay(s,30);modify(s,0x812,0x4000,0x4000);modify(s,0x812,0x4000,0);delay(s,30);modify(s,0x87b,1,1);delay(s,25);
    for(i=0;i<(timeout?101u:1u);i++){read_radio(s,0x882,timeout?0:0x40);if(timeout)delay(s,100);}
    for(i=0;i<cores;i++){modify(s,i<2?(uint16_t)(i?0x256:0x56):0xffff,1,1);modify(s,i<2?(uint16_t)(i?0x26a:0x6a):0xffff,0x80,0x80);}
    if(!timeout){read_radio(s,0x882,0x2a);modify(s,0x87a,0x3f,0x2a);}
    modify(s,0x87b,0x40,0x40);modify(s,0x87b,8,8);modify(s,0x87b,2,0);modify(s,0x87b,1,0);modify(s,0x9f9,0x2000,0);modify(s,0x9f9,0x2000,0x2000);modify(s,0x9f9,0x4000,0x4000);
    modify(s,0x231,0x100,0x100);modify(s,0x3e9,1,1);modify(s,0x813,0x80,0x80);modify(s,0x87c,1,1);
    for(i=0;i<(timeout?101u:1u);i++){delay(s,1000);read_radio(s,0x87c,timeout?0:2);}
    if(!timeout)read_radio(s,0x87c,0x80);
    modify(s,0x83d,0x7f,rcal);modify(s,0x815,8,0);modify(s,0x83c,0x3f,rcal);modify(s,0x815,0x10,0x10);
    modify(s,0x231,0x100,0);modify(s,0x3e9,1,0);modify(s,0x813,0x80,0);modify(s,0x87c,1,0);modify(s,0x9f9,0x4000,0);modify(s,0x9f9,0x2000,0);
    s->calibration=s->count;modify(s,0x9f9,0x2000,0x2000);
    for(i=0;i<2;i++){
        unsigned k;
        modify(s,0x2ee,0x30,0x10);modify(s,0x2f0,0x40,i?0:0x40);modify(s,0x2f0,0x30,i?0x10:0);
        modify(s,0x2ee,0xff00,(uint16_t)(i?0x3400:0x1200));modify(s,0x2ef,0x1fff,(uint16_t)(i?0xa89:0x756));
        modify(s,0x2f0,1,0);delay(s,1);modify(s,0x2f0,1,1);delay(s,35);modify(s,0x2ee,1,0);modify(s,0x2ee,1,1);
        for(k=0;k<(timeout?10000u:1u);k++){delay(s,100);read_radio(s,0x2f3,timeout?0:1);}
        modify(s,0x2ee,1,0);read_radio(s,0x2f4,i?10:0x1ffe);read_radio(s,0x2f2,i?30:8);
        if(i)s->cal1=s->count;else s->cal0=s->count;
        modify(s,0x2f0,1,0);
    }
    for(i=0;i<cores;i++){modify(s,i<2?(uint16_t)(i?0x285:0x85):0xffff,0xffff,cal);modify(s,i<2?(uint16_t)(i?0x249:0x49):0xffff,0xffff,cal);modify(s,i<2?(uint16_t)(i?0x24a:0x4a):0xffff,0xffff,cal);}
    modify(s,0x9f9,0x2000,0);
    modify(s,0x83e,0x20,0x20);modify(s,0x83e,0x10,0x10);modify(s,0x837,0x20,0x20);modify(s,0x837,0x10,0x10);
    modify(s,0x83e,0x400,0x400);modify(s,0x83e,0x100,0x100);modify(s,0x83e,0x200,0x200);modify(s,0x83e,0x80,0x80);
    modify(s,0x818,0x2000,0x2000);modify(s,0x829,8,8);modify(s,0x829,2,2);modify(s,0x829,0x10,0x10);modify(s,0x827,0x10,0x10);modify(s,0x829,0x1e00,0x1e00);modify(s,0x831,8,8);modify(s,0x825,1,1);modify(s,0x833,4,4);modify(s,0x9f6,0x8000,0x8000);
}
static struct axd_radio_power_context context(void)
{
    struct axd_radio_power_context c={0};struct axd_radio_control_context *a=&c.radio;
    a->hardware.phy=(struct axd_phy_context){.core=130,.minor=1,.phy_type=0xb,.phy_revision=51,.phy_core_count=2,.bookkeeping=42};
    a->phy_pointer=(struct axd_wake_u32){0x1000,1};a->public_pointer=(struct axd_wake_u32){0x2000,1};a->shared_pointer=(struct axd_wake_u32){0x3000,1};a->radio_info=(struct axd_wake_u32){0x4000,1};a->dispatch=(struct axd_wake_u32){0x5000,1};a->callback_context=(struct axd_wake_u32){0x6000,1};a->callback_pi=a->phy_pointer;
    a->ac_pointer=(struct axd_wake_u32){0x7000,1};a->chanmgr_pointer=(struct axd_wake_u32){0x8000,1};a->chanmgr_data_pointer=(struct axd_wake_u32){0x9000,1};a->radio_id=(struct axd_wake_u32){0x50e0,1};a->radio_revision=(struct axd_wake_u32){0,1};a->radio_read_offset=(struct axd_wake_u32){0x8000,1};a->chip_id=(struct axd_wake_u32){63178,1};a->pi47e=7;
    a->prefregs=prefregs;a->prefregs_length=sizeof(prefregs);c.mode_data=mode_data;c.mode_length=sizeof(mode_data);c.rccal_data=rccal_data;c.rccal_length=sizeof(rccal_data);
    c.ac_radio_pointer=(struct axd_wake_u32){0xa000,1};c.ac_radio_data_pointer=(struct axd_wake_u32){0xb000,1};c.clock_word=(struct axd_wake_u32){16384,1};
    c.rccal_multiplier=0xdeadbeef;c.rccal_saved6=0xaaaa;c.rccal_saved8=0xbbbb;c.rccal_saved_a=0xcccc;return c;
}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;assert(f->cursor<f->s->count);e=&f->s->events[f->cursor++];
    if(op->kind!=e->kind || op->offset!=e->offset || op->value!=e->value)fprintf(stderr,"radio power mismatch event%zu kind%d/%d offset%x/%x value%x/%x stage%u\n",f->cursor,op->kind,e->kind,op->offset,e->offset,op->value,e->value,f->j->stage);
    assert(op->kind==e->kind && op->offset==e->offset && op->value==e->value);*reply=e->reply;
    if(f->cursor==1 && f->j->command!=AXD_RPOWER_LOGEN && f->j->context.radio.hardware.shared_bus_word==1)
        assert(f->j->context.radio.hardware.phy.bookkeeping==43);
    assert(f->j->context.radio.pi47e==0 || f->j->command==AXD_RPOWER_LOGEN);
    if(f->cursor==f->cancel)axd_radio_power_cancel(f->j);return f->cursor==f->fail?-1:0;
}
static void run(struct axd_radio_power *j,const struct script *s,size_t fail,size_t cancel)
{
    struct fixture f={s,j,0,fail,cancel};unsigned steps=0;
    while(j->phase==AXD_RPOWER_RUNNING){size_t old=f.cursor;axd_radio_power_step(j,io,&f);assert(f.cursor-old<=1);assert(++steps<3000000);}
    assert(j->attempted_ops==f.cursor && j->completed_ops==f.cursor-(fail && f.cursor==fail));
    if(fail || cancel)assert(f.cursor==(fail?fail:cancel));
    {struct axd_radio_power before=*j;axd_radio_power_step(j,io,&f);axd_radio_power_cancel(j);assert(!memcmp(&before,j,sizeof(*j)));}
}
struct scheduler_fixture {struct fixture f;struct axd_phy_scheduler *j;};
static int scheduler_io(void *opaque,const struct axd_phy_scheduler_operation *op,struct axd_phy_scheduler_reply *reply)
{
    struct scheduler_fixture *f=opaque;int error;
    assert(op->path==AXD_PS_D11);
    assert(!memcmp(&f->j->context.radio_power,&f->j->radio_job.context,sizeof(f->j->context.radio_power)));
    assert(!memcmp(&f->j->context.radio,&f->j->radio_job.context.radio,sizeof(f->j->context.radio)));
    assert(!memcmp(&f->j->context.table.hardware,&f->j->radio_job.context.radio.hardware,sizeof(f->j->context.table.hardware)));
    error=io(&f->f,&op->d11,&reply->value);
    if(f->f.cursor==f->f.cancel)axd_phy_scheduler_cancel(f->j);
    return error;
}
static void scheduler(const struct script *s,const struct axd_radio_power_context *c)
{
    struct axd_phy_scheduler j;struct axd_phy_scheduler_context p={0};struct axd_phy_registry r;
    const struct axd_wake_u32 label={0x1234,1};size_t cuts[]={0,s->calibration+1,s->cal0+1,s->cal1+1};
    unsigned mode,cut;
    axd_phy_registry_clear(&r);assert(!axd_phy_registry_add(&r,AXD_INIT_RADIO,&label,7));
    p.table.hardware=c->radio.hardware;p.table.chip_id=63178;p.radio=c->radio;p.radio_power=*c;
    for(cut=0;cut<4;cut++)for(mode=0;mode<(cut?3u:1u);mode++){
        struct scheduler_fixture f={0};unsigned steps=0;
        assert(!axd_phy_scheduler_begin(&j,&r,&p));f.j=&j;f.f=(struct fixture){s,&j.radio_job,0,0,0};
        if(cut){if(mode!=1)f.f.fail=cuts[cut];if(mode!=0)f.f.cancel=cuts[cut];}
        while(j.phase==AXD_PS_RUNNING){size_t old=f.f.cursor;axd_phy_scheduler_step(&j,scheduler_io,&f);assert(f.f.cursor-old<=1 && ++steps<100000);}
        assert(!j.callbacks_completed && !j.slice_up_stored && !j.return_valid);
        assert(!memcmp(&j.context.radio_power,&j.radio_job.context,sizeof(j.context.radio_power)));
        if(!cut)assert(j.phase==AXD_PS_DEPENDENCY && j.dependency==AXD_PS_RADIO_CHILD && j.radio_job.switch_completed && j.radio_job.dependency==AXD_RPOWER_REGTBL_ON_PWRON);
        else assert(j.phase==(mode==1?AXD_PS_CANCELLED:AXD_PS_FAILED) && f.f.cursor==cuts[cut]);
    }
}
static void calibration_edges(struct script *s)
{
    struct axd_radio_power j;struct axd_radio_power_context c=context();
    const unsigned values[]={1,2,60,61};unsigned k;size_t i,fresh;
    for(k=0;k<4;k++){
        unsigned value=values[k],expected=value>=2 && value<=60?value:50;
        s->count=0;full(s,2,0);fresh=0;
        for(i=0;i<s->count;i++)if(s->events[i].kind==AXD_D11_READ16 && s->events[i].reply==0x80){s->events[i].reply=value<<2;fresh=i;break;}
        assert(fresh);
        for(i=fresh;i<s->count;i++)if(s->events[i].kind==AXD_D11_WRITE16 && s->events[i].offset==0x3d8 && (s->events[i].value==0x83d || s->events[i].value==0x83c)){
            unsigned mask=s->events[i].value==0x83d?0x7f:0x3f;
            assert(s->events[i+2].kind==AXD_D11_WRITE16 && s->events[i+2].offset==0x3da);
            s->events[i+2].value=(0xa5a5u&~mask)|(expected&mask);
            if(mask==0x3f)break;
        }
        assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,s,0,0);assert(j.phase==AXD_RPOWER_TRACE_COMPLETE && j.rcal_value==expected);
    }
    s->count=0;full(s,2,0);
    for(i=0;i<s->count;i++){
        if(s->events[i].kind==AXD_D11_READ16 && s->events[i].reply==8)s->events[i].reply=0x1ff0;
        if(s->events[i].kind==AXD_D11_WRITE16 && s->events[i].offset==0x3da && s->events[i].value==55928)s->events[i].value=344;
    }
    assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,s,0,0);
    assert(j.phase==AXD_RPOWER_TRACE_COMPLETE && j.context.rccal_saved6==344 && j.context.rccal_saved8==344);
    s->count=0;full(s,2,0);
    for(i=0;i<s->count;i++)if(s->events[i].kind==AXD_D11_READ16 && s->events[i].reply==30)s->events[i].reply=10;
    assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,s,0,0);
    assert(j.phase==AXD_RPOWER_DEPENDENCY && j.dependency==AXD_RPOWER_DIVISION_BY_ZERO && j.quotient==0 && j.completed_ops==s->cal1 && j.context.rccal_multiplier==0x1000);
}
int main(void)
{
    struct script s={0};struct axd_radio_power j;struct axd_radio_power_context c;unsigned mode;size_t i;
    s.capacity=131072;s.events=malloc(s.capacity*sizeof(*s.events));assert(s.events);
    for(mode=0;mode<3;mode++){
        c=context();s.count=0;logen(&s,mode);assert(!axd_radio_power_begin(&j,AXD_RPOWER_LOGEN,mode,&c));run(&j,&s,0,0);
        assert(j.phase==AXD_RPOWER_TRACE_COMPLETE && j.logen_completed && j.completed_ops==176 && j.context.radio.hardware.phy.bookkeeping==0);
    }
    c=context();s.count=0;full(&s,2,0);assert(s.count==1157);assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,&s,0,0);
    assert(j.phase==AXD_RPOWER_TRACE_COMPLETE && j.switch_completed && j.prefix_completed && j.context.radio.pi47e==1);
    assert(j.context.rccal_saved6==55928 && j.context.rccal_saved8==55928 && j.context.rccal_multiplier==43 && j.context.rccal_saved_a==43);
    assert(j.pll_reads==1 && j.rcal_reads==1 && j.rccal_reads[0]==1 && j.rccal_reads[1]==1);
    scheduler(&s,&c);
    assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&j.context));assert(j.context.rccal_saved6==55928);
    for(mode=0;mode<3;mode++)for(i=1;i<=s.count;i++){
        c=context();assert(!axd_radio_power_begin(&j,AXD_RPOWER_ON_REGISTERED,0,&c));run(&j,&s,mode==1?0:i,mode==0?0:i);
        assert(j.phase==(mode==1?AXD_RPOWER_CANCELLED:AXD_RPOWER_FAILED) && !j.switch_completed && j.context.radio.pi47e==0);
        if(mode!=1)assert(j.error==AXD_RPOWER_IO_ERROR);
    }
    c=context();assert(!axd_radio_power_begin(&j,AXD_RPOWER_ON_REGISTERED,0,&c));run(&j,&s,0,0);assert(j.phase==AXD_RPOWER_DEPENDENCY && j.dependency==AXD_RPOWER_REGTBL_ON_PWRON && j.switch_completed && j.context.radio.pi47e==1);
    c=context();c.radio.chanmgr_data10=1;assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,&s,0,0);assert(j.dependency==AXD_RPOWER_REGTBL_CHANNEL_TAIL && !j.switch_completed && j.context.radio.pi47e==0);
    c=context();c.clock_word.known=0;assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,&s,0,0);assert(j.dependency==AXD_RPOWER_METADATA && j.last_field==AXD_RPOWER_CLOCK && j.completed_ops==s.cal0 && j.context.rccal_saved8==0x10a4);
    c=context();c.clock_word.value=0;assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,&s,0,0);assert(j.dependency==AXD_RPOWER_DIVISION_BY_ZERO && j.completed_ops==s.cal0);
    c=context();c.rccal_data=NULL;assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,&s,0,0);assert(j.last_field==AXD_RPOWER_RCCAL_DATA && j.completed_ops==s.calibration && j.context.rccal_saved_a==0x1000 && j.context.rccal_saved6==0xaaaa);
    for(mode=3;mode<=4;mode++){c=context();c.radio.hardware.phy.phy_core_count=(uint8_t)mode;s.count=0;full(&s,mode,0);assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,&s,0,0);assert(j.phase==AXD_RPOWER_TRACE_COMPLETE && j.switch_completed);}
    c=context();c.clock_word.known=0;s.count=0;full(&s,2,1);assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,&s,0,0);
    assert(j.phase==AXD_RPOWER_TRACE_COMPLETE && j.pll_reads==101 && j.rcal_reads==101 && j.rccal_reads[0]==10000 && j.rccal_reads[1]==10000);
    assert(j.context.rccal_saved6==0xaaaa && j.context.rccal_saved8==0x10a4 && j.context.rccal_multiplier==0x1000 && j.context.rccal_saved_a==0x1000);
    calibration_edges(&s);
    c=context();c.mode_length=17;assert(!axd_radio_power_begin(&j,AXD_RPOWER_LOGEN,0,&c));run(&j,&s,0,0);assert(j.phase==AXD_RPOWER_FAILED && j.error==AXD_RPOWER_BOUNDS && !j.attempted_ops);
    c=context();c.radio.hardware.shared_bus_word=1;c.radio.hardware.write_threshold=65535;s.count=0;st(&s);
    assert(!axd_radio_power_begin(&j,AXD_RPOWER_SWITCH_ON,0,&c));run(&j,&s,1,0);
    assert(j.phase==AXD_RPOWER_FAILED && j.context.radio.hardware.phy.bookkeeping==43 && j.context.radio.pi47e==0);
    assert(axd_radio_power_begin(&j,AXD_RPOWER_LOGEN,3,&c));assert(axd_radio_power_begin(&j,AXD_RPOWER_ON_REGISTERED,1,&c));
    free(s.events);puts("radio-power literal protocols, polls, calibration stores and callback exits passed");return 0;
}
