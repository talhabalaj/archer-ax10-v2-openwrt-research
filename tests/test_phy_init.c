/* SPDX-License-Identifier: MIT */
#include "../driver/phy_init.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independent typed synthetic oracle from the target51/init and HND assembly.
 * Table payloads are original repeated fixtures. Every read has an explicit
 * supplied reply; none comes from writes or an unspecified zero fallback.
 */
#include "fixtures/phy_init_trace.h"
struct fixture {struct axd_phy_init *job;const struct axd_test_table_script *script;size_t cursor,fail_at,cancel_at;};
static struct fixture fixture(struct axd_phy_init *j,const struct axd_test_table_script *s)
{struct fixture f={0};f.job=j;f.script=s;f.fail_at=f.cancel_at=(size_t)-1;return f;}
static int io(void *opaque,const struct axd_phy_init_operation *op,struct axd_phy_init_reply *reply)
{
    struct fixture *f=opaque;size_t n=f->cursor++;const struct axd_test_table_event *e;unsigned kind;uint32_t offset,value;
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
    struct axd_test_table_script s=axd_test_table_script_build();struct axd_phy_init j;struct axd_phy_init_context c=axd_test_table_context();
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
        c=axd_test_table_context();assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);
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
    struct axd_test_table_script s=axd_test_table_script_build();struct axd_phy_init j;struct axd_phy_init_context c;
    struct fixture f;
    for(phy_nonzero=0;phy_nonzero<2;phy_nonzero++)for(otp=0;otp<32;otp++){
        uint32_t expected=(uint32_t)voltage_units[phy_nonzero][otp]<<20;
        c=axd_test_table_context();c.otp_word.value=(otp<<8)|0xe000u;
        s=axd_test_table_script_build();s.events[14].reply=phy_nonzero?0xfeed0001:0xfeed0000;
        s.events[25].value=0xaa0abcde|expected;
        assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
        assert(j.phase==AXD_PI_TRACE_COMPLETE&&j.voltage_value==expected&&f.cursor==s.count);
    }
    s=axd_test_table_script_build();c=axd_test_table_context();c.otp_word.known=0;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_DEPENDENCY&&j.last_field==AXD_PI_OTP&&f.cursor==16);
    assert(j.phy0_valid&&j.phy0_sample==0x12ab);terminal(&j,&f);
    c=axd_test_table_context();c.otp_word.value=0x10000;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_FAILED&&j.error==AXD_PI_BAD_ARGUMENT&&f.cursor==16);
    c=axd_test_table_context();c.control.gpio_reservation_known=0;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_DEPENDENCY&&f.cursor==28);terminal(&j,&f);
    c=axd_test_table_context();c.control_state_known=0;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_DEPENDENCY&&j.last_field==AXD_PI_SI_STATE&&f.cursor==16);
    assert(axd_phy_init_begin(&j,&j.context)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_DEPENDENCY&&j.last_field==AXD_PI_SI_STATE);terminal(&j,&f);
    for(unsigned slot=0;slot<3;slot++){
        const size_t reached[3]={78,120,386};
        c=axd_test_table_context();c.tables[slot].known=0;
        assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
        assert(j.phase==AXD_PI_DEPENDENCY&&j.last_field==(enum axd_phy_init_field)(AXD_PI_TABLE0+slot));
        assert(f.cursor==reached[slot]&&j.tables_completed==slot);terminal(&j,&f);
    }
    c=axd_test_table_context();c.tables[1].length=255;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_FAILED&&j.error==AXD_PI_TABLE_ERROR&&f.cursor==120);
    c=axd_test_table_context();c.tables[0].data=NULL;
    assert(axd_phy_init_begin(&j,&c)==0);f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PI_FAILED&&j.error==AXD_PI_BINDING&&f.cursor==78);
    c=axd_test_table_context();assert(axd_phy_init_begin(NULL,&c)==-1);assert(axd_phy_init_begin(&j,NULL)==-1);
    c.chip_id=1;assert(axd_phy_init_begin(&j,&c)==-1&&j.error==AXD_PI_UNSUPPORTED);
    c=axd_test_table_context();c.hardware.phy.phy_revision=50;
    assert(axd_phy_init_begin(&j,&c)==-1&&j.error==AXD_PI_PHY_ERROR);
    assert(axd_phy_init_step(NULL,NULL,NULL)==AXD_PI_INVALID&&axd_phy_init_cancel(NULL)==AXD_PI_INVALID);
}
int main(void)
{
    full_and_faults();voltage_and_dependencies();
    puts("PHY51 table init:412-event literal chain,64 voltage cases and all callback exits passed");return 0;
}
