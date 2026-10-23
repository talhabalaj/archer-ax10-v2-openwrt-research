/* SPDX-License-Identifier: MIT */
#include "fixtures/phy_noise_trace.h"
#include "../driver/phy_scheduler.h"
#include <stdio.h>
struct trace {struct axd_phy_noise *j;const struct axd_noise_fixture_script *s;size_t cursor,fault;unsigned mode,book;};
static struct trace trace(struct axd_phy_noise *j,const struct axd_noise_fixture_script *s){struct trace t={j,s,0,(size_t)-1,0,0x1234};return t;}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct trace *t=opaque;size_t n=t->cursor++;const struct axd_noise_fixture_event *e;
    if(n>=t->s->count)fprintf(stderr,"noise extra axd_noise_fixture_event%zu kind%u/%x/%x stage%u\n",n,op->kind,op->offset,op->value,t->j->stage);
    assert(n<t->s->count);e=&t->s->events[n];
    if(op->kind!=e->kind || op->offset!=e->offset || op->value!=e->value)fprintf(stderr,"noise axd_noise_fixture_event%zu actual%u/%x/%x expected%u/%x/%x stage%u\n",n,op->kind,op->offset,op->value,e->kind,e->offset,e->value,t->j->stage);
    assert(op->kind==e->kind && op->offset==e->offset && op->value==e->value);
    if(op->kind==AXD_D11_STORE_BARRIER && n+1<t->s->count){
        const struct axd_noise_fixture_event *next=&t->s->events[n+1];
        if((next->kind==AXD_D11_WRITE32 && next->offset==0x3fc) ||
           (next->kind==AXD_D11_WRITE16 && next->offset==0x3d8 && !(next->value&0x8000u)))t->book=(t->book+1u)&65535u;
    }
    assert(t->j->context.hardware.phy.bookkeeping==t->book);
    assert(t->j->attempted_ops==n+1 && t->j->completed_ops==n);
    if(n==t->fault && (t->mode&2))assert(axd_phy_noise_cancel(t->j)==AXD_NOISE_CANCELLED);
    if(n==t->fault && (t->mode&1))return -1;
    if(op->kind==AXD_D11_WRITE16 && (op->offset==0x3fe ||
       (op->offset==0x3fc && n+1<t->s->count && t->s->events[n+1].kind==AXD_D11_READ16)))t->book=0;
    if(op->kind==AXD_D11_FULL_BARRIER && n && t->s->events[n-1].kind==AXD_D11_READ16 && t->s->events[n-1].offset==0x3da)t->book=0;
    *reply=e->reply;return 0;
}
static void terminal(struct trace *t)
{struct axd_phy_noise copy=*t->j;size_t n=t->cursor;assert(axd_phy_noise_step(t->j,io,t)==copy.phase && axd_phy_noise_step(t->j,NULL,NULL)==copy.phase);assert(axd_phy_noise_cancel(t->j)==copy.phase && n==t->cursor && !memcmp(&copy,t->j,sizeof(copy)));}
static void run(struct trace *t){unsigned steps=0;while(t->j->phase==AXD_NOISE_RUNNING){size_t n=t->cursor;assert(++steps<10000);axd_phy_noise_step(t->j,io,t);assert(t->cursor-n<=1);}terminal(t);}
static void mode_paths(void)
{
    struct axd_phy_noise j;unsigned active,cores,optional;
    for(active=0;active<2;active++)for(cores=1;cores<=2;cores++)for(optional=0;optional<=active;optional++){
        struct axd_phy_noise_context c=axd_noise_fixture_context(cores);struct axd_noise_fixture_script s=axd_noise_fixture_full(cores,active,optional);struct trace t=trace(&j,&s);size_t k;unsigned mode;
        c.hwaci_adc_flag.value=(uint8_t)optional;
        assert(!axd_phy_noise_begin(&j,AXD_NOISE_INIT_MODE,active?0x89abcdef:1,&c));run(&t);
        assert(j.phase==AXD_NOISE_TRACE_COMPLETE && t.cursor==s.count && j.hwaci_completed==1 && j.preempt_completed==1 && j.hwobss_completed==1 && j.elnabypass_completed==1 && j.mclip_completed==1);
        assert(j.context.hwaci_2g[0]==5 && j.context.hwaci_2g[1]==5 && j.context.hwaci_2g[2]==4 && j.context.hwaci_5g[0]==5 && j.context.hwaci_5g[1]==3 && j.context.hwaci_5g[2]==2);
        assert(j.context.preempt_byte10==active && j.context.preempt_halfe==0x5678 && j.table_reads==cores && j.table_writes==(active?cores:0));
        if(cores==1 && !optional)for(k=0;k<s.count;k++)for(mode=1;mode<=3;mode++){
            t=trace(&j,&s);t.fault=k;t.mode=mode;assert(!axd_phy_noise_begin(&j,AXD_NOISE_INIT_MODE,active?0x89abcdef:1,&c));run(&t);
            assert(j.phase==(mode&1?AXD_NOISE_FAILED:AXD_NOISE_CANCELLED) && j.error==(mode&1?AXD_NOISE_IO_ERROR:AXD_NOISE_NO_ERROR));assert(t.cursor==k+1 && j.completed_ops==k+(mode==2));
            {size_t hwa=active?AXD_NF_N(axd_noise_fixture_hwaci_on):AXD_NF_N(axd_noise_fixture_hwaci_off),pre=active?74u:AXD_NF_N(axd_noise_fixture_preempt_off);
                assert(j.context.hwaci_2g[0]==(k<hwa?0xaau:5u) && j.context.hwaci_5g[0]==(k<hwa?0xbbu:5u));
                assert(j.context.preempt_byte10==(k<hwa?0xccu:active));
                assert(j.context.preempt_halfe==(k<hwa?0xddddu:k<hwa+pre?0u:0x5678u));
            }
        }
    }
}
static void reached_dependencies(void)
{
    struct axd_phy_noise j;struct axd_phy_noise_context c=axd_noise_fixture_context(1);struct axd_noise_fixture_script s={0};struct trace t=trace(&j,&s);
    assert(!axd_phy_noise_begin(&j,AXD_NOISE_INIT_MODE,0,&c));run(&t);assert(j.phase==AXD_NOISE_DEPENDENCY && j.dependency==AXD_NOISE_DESENSE_RESET_APPLY && !t.cursor && j.context.preempt_byte10==0xcc);
    {const struct axd_noise_fixture_event e[]={AXD_NF_M(0x141,0x800,0),AXD_NF_M(0x1040,1,0)};axd_noise_fixture_append(&s,e,AXD_NF_N(e));}
    c.current_channel=axd_noise_fixture_known(0);assert(!axd_phy_noise_begin(&j,AXD_NOISE_PREEMPT,0,&c));t=trace(&j,&s);run(&t);
    assert(j.phase==AXD_NOISE_DEPENDENCY && j.dependency==AXD_NOISE_CHANNEL_GETSET && t.cursor==12 && j.context.preempt_byte10==0 && j.context.preempt_halfe==0);
    c=axd_noise_fixture_context(1);c.hwaci_5g_flag.known=0;s=(struct axd_noise_fixture_script){0};axd_noise_fixture_append(&s,axd_noise_fixture_hwaci_off,AXD_NF_N(axd_noise_fixture_hwaci_off));t=trace(&j,&s);
    assert(!axd_phy_noise_begin(&j,AXD_NOISE_HWACI_OFF,0,&c));run(&t);assert(j.phase==AXD_NOISE_DEPENDENCY && j.context.hwaci_2g[0]==5 && j.context.hwaci_5g[0]==0xbb);
    assert(!axd_phy_noise_begin(&j,AXD_NOISE_HWACI_OFF,0,&j.context));axd_phy_noise_cancel(&j);
    assert(axd_phy_noise_begin(NULL,AXD_NOISE_INIT_MODE,1,&c)<0 && axd_phy_noise_begin(&j,AXD_NOISE_MCLIP,2,&c)<0);
    assert(axd_phy_noise_step(NULL,NULL,NULL)==AXD_NOISE_INVALID && axd_phy_noise_cancel(NULL)==AXD_NOISE_INVALID);
}
static void active_mclip(void)
{
    const struct axd_noise_fixture_event prefix[]={AXD_NF_R(0x120,1),AXD_NF_SY,
        AXD_NF_R(0x120,3),AXD_NF_SY,AXD_NF_R(0x120,3),AXD_NF_SY,AXD_NF_R(0x128,0),AXD_NF_SY,
        AXD_NF_ST,AXD_NF_W(0x120,0x04000002),AXD_NF_R(0x128,1),AXD_NF_SY,AXD_NF_R(0x128,1),AXD_NF_SY,AXD_NF_R(0x120,2),AXD_NF_SY};
    const struct axd_noise_fixture_event suffix[]={AXD_NF_R(0x120,2),AXD_NF_SY,AXD_NF_ST,AXD_NF_W(0x120,0x04000003),AXD_NF_ST,AXD_NF_W(0x128,1),
        AXD_NF_R(0x120,3),AXD_NF_SY,AXD_NF_R(0x128,0),AXD_NF_SY,
        AXD_NF_ST,AXD_NF_W(0x160,0x10010),AXD_NF_R(0x160,0xdeadbeef),AXD_NF_SY,AXD_NF_Q(0x164,2),AXD_NF_SY};
    struct axd_phy_noise j;struct axd_phy_noise_context c=axd_noise_fixture_context(1);struct axd_noise_fixture_script s={0},body={0};struct trace t;size_t k;unsigned mode;
    c.hardware.mac.core=130;c.hardware.mac.minor=1;c.hardware.mac.main_control=3;c.hardware.mac.wake_reasons=8;
    c.hardware.mac.chip_id=63178;c.hardware.mac.band_present=1;c.hardware.mac_context_valid=1;
    axd_noise_fixture_append(&s,prefix,AXD_NF_N(prefix));axd_noise_fixture_mclip(&body,1,0,0);
    axd_noise_fixture_append(&s,body.events+2,body.count-2);axd_noise_fixture_append(&s,suffix,AXD_NF_N(suffix));
    t=trace(&j,&s);assert(!axd_phy_noise_begin(&j,AXD_NOISE_MCLIP,0,&c));run(&t);
    assert(j.phase==AXD_NOISE_TRACE_COMPLETE && t.cursor==s.count && j.mclip_completed==1 && j.context.phy_depth.value==1);
    assert(j.context.hardware.mac.main_depth==0 && j.context.hardware.mac.main_control==3 && j.context.hardware.mac.wake_reasons==8);
    for(k=0;k<s.count;k++)for(mode=1;mode<=3;mode++){
        t=trace(&j,&s);t.fault=k;t.mode=mode;assert(!axd_phy_noise_begin(&j,AXD_NOISE_MCLIP,0,&c));run(&t);
        assert(j.phase==(mode&1?AXD_NOISE_FAILED:AXD_NOISE_CANCELLED) && t.cursor==k+1 && !j.mclip_completed);
    }
}
static void optional_and_band_paths(void)
{
    struct axd_phy_noise j;struct axd_phy_noise_context c=axd_noise_fixture_context(1);
    struct axd_noise_fixture_script s=axd_noise_fixture_full(1,1,1);struct trace t;size_t k;unsigned mode;
    c.hwaci_adc_flag.value=1;
    for(k=0;k<AXD_NF_N(axd_noise_fixture_hwaci_shm);k++)for(mode=1;mode<=3;mode++){
        t=trace(&j,&s);t.fault=k;t.mode=mode;assert(!axd_phy_noise_begin(&j,AXD_NOISE_INIT_MODE,0x89abcdef,&c));run(&t);
        assert(j.phase==(mode&1?AXD_NOISE_FAILED:AXD_NOISE_CANCELLED) && t.cursor==k+1 && !j.hwaci_completed && j.context.preempt_byte10==0xcc);
        assert(j.shm_sample_valid==(k>5 || (k==5 && mode==2)));
    }
    s=(struct axd_noise_fixture_script){0};axd_noise_fixture_append(&s,axd_noise_fixture_hwaci_on,AXD_NF_N(axd_noise_fixture_hwaci_on));
    for(k=0;k<s.count;k++)if(s.events[k].kind==AXD_D11_WRITE32){
        unsigned sel=s.events[k].value&65535u;
        if(sel==0x5746 || sel==0x5749)s.events[k].value=(100u<<16)|sel;
        if(sel==0x471c || sel==0x471d)s.events[k].value=(0x100u<<16)|sel;
    }
    c=axd_noise_fixture_context(1);c.chanspec=axd_noise_fixture_known(0x1024);c.hwaci_adc_flag.value=2;c.hwaci_shm_offset.known=0;
    t=trace(&j,&s);assert(!axd_phy_noise_begin(&j,AXD_NOISE_HWACI_ON,1,&c));run(&t);assert(j.phase==AXD_NOISE_TRACE_COMPLETE && t.cursor==s.count && !j.shm_sample_valid);
    s=(struct axd_noise_fixture_script){0};axd_noise_fixture_mclip(&s,1,0,0);t=trace(&j,&s);
    assert(!axd_phy_noise_begin(&j,AXD_NOISE_MCLIP,1,&c));run(&t);assert(j.phase==AXD_NOISE_TRACE_COMPLETE && j.table_reads==1 && !j.table_writes);
    {const struct axd_noise_fixture_event on[]={AXD_NF_P(0x1100,0x7800),AXD_NF_P(0x358,0xb0),AXD_NF_P(0x359,0x37f),AXD_NF_P(0x35a,0x3340),AXD_NF_P(0x13a,0x48),
        AXD_NF_M(0x16c,0x8000,0),AXD_NF_P(0x1101,0x200),AXD_NF_P(0x110f,0x280),AXD_NF_READ(0x13a,0xface4321),AXD_NF_SH(0x3b2,0x4321)};
        s=(struct axd_noise_fixture_script){0};axd_noise_fixture_append(&s,axd_noise_fixture_hwaci_on,AXD_NF_N(axd_noise_fixture_hwaci_on));axd_noise_fixture_preempt_on(&s,1);
        axd_noise_fixture_append(&s,on,AXD_NF_N(on));axd_noise_fixture_elna(&s,1,1);axd_noise_fixture_mclip(&s,1,1,1);
        c=axd_noise_fixture_context(1);c.chanspec=axd_noise_fixture_known(0xd824);t=trace(&j,&s);
        assert(!axd_phy_noise_begin(&j,AXD_NOISE_INIT_MODE,0x89abcdef,&c));run(&t);assert(j.phase==AXD_NOISE_TRACE_COMPLETE && j.hwobss_completed && t.cursor==s.count);
    }
}
struct outer_trace {struct axd_phy_scheduler *j;struct trace child;};
static int outer_io(void *opaque,const struct axd_phy_scheduler_operation *op,struct axd_phy_scheduler_reply *reply)
{
    struct outer_trace *t=opaque;
    assert(op->path==AXD_PS_D11 && !t->j->callbacks_completed && !t->j->slice_up_stored && t->j->context.noise_mode378==0x76543210);
    assert(!memcmp(&t->j->context.noise,&t->j->noise_job.context,sizeof(t->j->context.noise)));
    assert(!memcmp(&t->j->context.table.hardware,&t->j->noise_job.context.hardware,sizeof(t->j->context.table.hardware)));
    if(t->child.cursor==t->child.fault && (t->child.mode&2))axd_phy_scheduler_cancel(t->j);
    return io(&t->child,&op->d11,&reply->value);
}
static void outer_run(struct outer_trace *t)
{unsigned steps=0;while(t->j->phase==AXD_PS_RUNNING){size_t n=t->child.cursor;assert(++steps<10000);axd_phy_scheduler_step(t->j,outer_io,t);assert(t->child.cursor-n<=1);}}
static void scheduler_child(void)
{
    struct axd_phy_scheduler j;struct axd_phy_scheduler_context c={0};struct axd_phy_registry r;
    struct axd_phy_noise_context n=axd_noise_fixture_context(1);struct axd_wake_u32 label={0x5555,1};
    struct axd_noise_fixture_script s=axd_noise_fixture_full(1,1,0);struct outer_trace t;size_t k;unsigned mode;
    axd_phy_registry_clear(&r);assert(!axd_phy_registry_add(&r,AXD_INIT_NOISE,&label,12));assert(!axd_phy_registry_add(&r,AXD_INIT_BTCX,&label,20));
    c.table.chip_id=63178;c.table.hardware=n.hardware;c.noise=n;c.radio=n.radio;c.channel=n.channel;
    c.flags3b0=axd_noise_fixture_known(0);c.chanspec=n.chanspec;c.forced_interference=(struct axd_wake_u8){1,0};
    c.interference5g=axd_noise_fixture_known(0x89abcdef);c.slice_unit=axd_noise_fixture_known(0);c.slice_up=axd_noise_fixture_known(0x80);
    c.noise_mode378=0x76543210;c.btcx_byte18=0xaa;
    assert(!axd_phy_scheduler_begin(&j,&r,&c));t.j=&j;t.child=trace(&j.noise_job,&s);outer_run(&t);
    assert(j.phase==AXD_PS_TRACE_COMPLETE && j.callbacks_completed==2 && j.slice_up_stored && t.child.cursor==s.count);
    assert(j.context.noise_mode378==0x89abcdef && j.context.btcx_byte18==255 && j.context.slice_up.value==0x81);
    for(k=0;k<s.count;k++)for(mode=1;mode<=3;mode++){
        assert(!axd_phy_scheduler_begin(&j,&r,&c));t.child=trace(&j.noise_job,&s);t.child.fault=k;t.child.mode=mode;outer_run(&t);
        assert(j.phase==(mode&1?AXD_PS_FAILED:AXD_PS_CANCELLED) && !j.callbacks_completed && !j.slice_up_stored);
        assert(j.context.noise_mode378==0x76543210 && j.context.btcx_byte18==0xaa && t.child.cursor==k+1);
    }
    assert(!axd_phy_scheduler_begin(&j,&r,&c));t.child=trace(&j.noise_job,&s);
    while(j.noise_job.phase!=AXD_NOISE_TRACE_COMPLETE){assert(j.phase==AXD_PS_RUNNING);axd_phy_scheduler_step(&j,outer_io,&t);}
    assert(j.context.noise_mode378==0x76543210 && !j.callbacks_completed);
    axd_phy_scheduler_cancel(&j);assert(axd_phy_scheduler_step(&j,outer_io,&t)==AXD_PS_CANCELLED && j.context.noise_mode378==0x76543210);
}
int main(void){mode_paths();reached_dependencies();active_mclip();optional_and_band_paths();scheduler_child();puts("PHY51 noise: actual ordered mode children, optional SHM, per-core radio/table effects and every callback prefix passed");return 0;}
