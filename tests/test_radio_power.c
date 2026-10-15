/* SPDX-License-Identifier: MIT */
#include "../driver/radio_power.h"
#include "../driver/phy_scheduler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/radio_power_trace.h"
struct fixture {const struct script *s;struct axd_radio_power *j;size_t cursor,fail,cancel;};
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
