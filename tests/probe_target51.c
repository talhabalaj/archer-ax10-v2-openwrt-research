/* SPDX-License-Identifier: MIT */
#include "../driver/phy_scheduler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/radio_power_trace.h"

/* This is an executable functional observation, not a regression assertion
 * that an incomplete child must stay incomplete. Every required registration
 * remains present. All values and private-span replacements are synthetic. */
struct prefix_event {unsigned path,kind;uint32_t offset,value,reply;};
static const struct prefix_event initialization_prefix[]={
    {AXD_PS_ALLOCATE_ZERO,0,32,0x7777,0x8000},
    {AXD_PS_ALLOCATE_ZERO,0,12,0x7777,0x9000},
    {AXD_PS_D11,AXD_D11_STORE_BARRIER,0,0,0},
    {AXD_PS_D11,AXD_D11_WRITE16,0x3e6,0,0},
    {AXD_PS_SI,AXD_SICO_READ32,0x408,0,0x40},
    {AXD_PS_SI,AXD_SICO_FULL_BARRIER,0,0,0}
};
static const struct axd_si_core catalog[]={
    {0x800,0x18000000,0x18100000,0},
    {0x812,0x18200000,0x18300000,0}
};
struct observation {struct axd_phy_scheduler *job;const struct script *radio;
    size_t cursor;unsigned trace_failed;};
static int operation(void *opaque,const struct axd_phy_scheduler_operation *op,
                        struct axd_phy_scheduler_reply *reply)
{
    struct observation *f=opaque;unsigned path,kind;uint32_t offset,value,sample;
    if(f->cursor<sizeof(initialization_prefix)/sizeof(initialization_prefix[0])){
        const struct prefix_event *e=&initialization_prefix[f->cursor];
        path=e->path;kind=e->kind;offset=e->offset;value=e->value;sample=e->reply;
    }else{
        size_t n=f->cursor-sizeof(initialization_prefix)/sizeof(initialization_prefix[0]);
        const struct event *e;
        if(n>=f->radio->count){f->trace_failed=1;return -1;}
        e=&f->radio->events[n];path=AXD_PS_D11;kind=e->kind;
        offset=e->offset;value=e->value;sample=e->reply;
    }
    if((unsigned)op->path!=path){f->trace_failed=1;return -1;}
    if(path==AXD_PS_ALLOCATE_ZERO){
        if(op->bytes!=offset || op->allocator!=value){f->trace_failed=1;return -1;}
    }else if(path==AXD_PS_SI){
        if((unsigned)op->si.kind!=kind || op->si.offset!=offset ||
           op->si.value!=value || op->si.wrapper!=0x22100000){f->trace_failed=1;return -1;}
    }else if(path==AXD_PS_D11){
        if((unsigned)op->d11.kind!=kind || op->d11.offset!=offset ||
           op->d11.value!=value){f->trace_failed=1;return -1;}
        if(f->job->current==AXD_INIT_RADIO &&
            (memcmp(&f->job->context.radio_power,&f->job->radio_job.context,sizeof(f->job->context.radio_power)) ||
             memcmp(&f->job->context.table.hardware,&f->job->radio_job.context.radio.hardware,sizeof(f->job->context.table.hardware)))){
            f->trace_failed=1;return -1;
        }
    }else {f->trace_failed=1;return -1;}
    reply->value=sample;f->cursor++;return 0;
}
static struct axd_phy_scheduler_context target_context(void)
{
    struct axd_phy_scheduler_context c={0};
    c.radio_power=context();c.radio=c.radio_power.radio;
    c.table.hardware=c.radio.hardware;c.table.chip_id=63178;
    c.table.control.si.interconnect=1;c.table.control.si.core_count=2;
    c.table.control.si.catalog=catalog;c.table.control.si.catalog_length=2;
    c.table.control.state.current_index=1;c.table.control.state.current_map=0x22000000;
    c.table.control.state.primary_wrapper=0x22100000;
    c.table.control.state.cache[0].core=0x20000000;
    c.table.control.state.cache[1].core=0x22000000;c.table.control_state_known=1;
    c.allocator=(struct axd_wake_u32){0x7777,1};
    c.cache.reuse_state=(struct axd_wake_u32){255,1};
    c.cache.reuse_bytes=(struct axd_wake_u32){32,1};
    c.cache.reuse_buffer=(struct axd_wake_u32){0xcafe,1};
    c.cache.entries=(struct axd_wake_u32){3,1};
    c.cache.table_pointer=(struct axd_wake_u32){0,1};
    c.flags3b0=(struct axd_wake_u32){0,1};
    /* Explicit synthetic 5GHz/20MHz/channel36; this guard must reach radar. */
    c.chanspec=(struct axd_wake_u32){0xd024,1};
    c.bandwidth_state=(struct axd_wake_u32){0x1000,1};
    c.hardware_chanspec=c.chanspec;
    c.interference2g=(struct axd_wake_u32){0x12345678,1};
    c.interference5g=(struct axd_wake_u32){0x89abcdef,1};
    c.interference_current=(struct axd_wake_u32){0xfeedface,1};
    c.forced_interference=(struct axd_wake_u8){1,0};
    c.clocks=(struct axd_wake_u8){1,1};c.phy_core_count_known=1;
    c.rxgcrs_enabled=(struct axd_wake_u8){1,1};
    c.channel_state_pointer=(struct axd_wake_u32){0xabcd,1};
    c.slice_unit=(struct axd_wake_u32){1,1};c.slice_up=(struct axd_wake_u32){0x80,1};
    return c;
}
static const char *phase(enum axd_phy_scheduler_phase p)
{
    switch(p){case AXD_PS_RUNNING:return "running";case AXD_PS_TRACE_COMPLETE:return "trace_complete";
    case AXD_PS_DEPENDENCY:return "dependency";case AXD_PS_CANCELLED:return "cancelled";
    default:return "failed";}
}
static const char *radio_symbol(const struct axd_radio_power *j)
{
    switch(j->dependency){
    case AXD_RPOWER_REGTBL_ON_PWRON:return "wlc_phy_set_regtbl_on_pwron_acphy";
    case AXD_RPOWER_REGTBL_CHANNEL_TAIL:return "wlc_phy_set_regtbl_on_pwron_acphy_and_chanspec";
    case AXD_RPOWER_DIVISION_BY_ZERO:return "ARM_unsigned_divide_by_zero";
    case AXD_RPOWER_PREFIX_CHILD:return "radio_control_prefix";
    case AXD_RPOWER_REGISTER_CHILD:return "radio_register_child";
    case AXD_RPOWER_METADATA:return "required_radio_runtime_metadata";
    default:return "radio_power";}
}
int main(void)
{
    struct script s={0};struct axd_phy_scheduler job;
    struct axd_phy_scheduler_context c=target_context();struct axd_phy_registry registry;
    struct axd_wake_u32 labels[AXD_PHY_TARGET_INIT_COUNT];
    struct observation f={&job,&s,0,0};unsigned i,steps=0,bw;size_t total;
    if(c.chanspec.known!=1 || c.chanspec.value>65535)return 1;
    switch(c.chanspec.value&0x3800u){case 0x1000:bw=20;break;case 0x1800:bw=40;break;
    case 0x2000:bw=80;break;case 0x2800:bw=160;break;default:bw=0;break;}
    s.capacity=4096;s.events=malloc(s.capacity*sizeof(*s.events));
    if(!s.events)return 1;full(&s,2,0);
    for(i=0;i<AXD_PHY_TARGET_INIT_COUNT;i++)labels[i]=(struct axd_wake_u32){0x10000+i*16,1};
    labels[5]=c.radio.radio_info;
    if(axd_phy_registry_target51(&registry,labels) || axd_phy_scheduler_begin(&job,&registry,&c)){free(s.events);return 1;}
    while(job.phase==AXD_PS_RUNNING){
        size_t old=f.cursor;
        axd_phy_scheduler_step(&job,operation,&f);
        if(f.cursor-old>1 || ++steps>100000){f.trace_failed=1;break;}
    }
    total=s.count+sizeof(initialization_prefix)/sizeof(initialization_prefix[0]);
    if(f.cursor!=total)f.trace_failed=1;
    printf("{\"schema_version\":1,\"scenario\":\"synthetic_target51_full16_explicit50e0\",\"chanspec\":{\"raw\":%u,\"band\":\"%s\",\"bandwidth_mhz\":%u,\"channel\":%u},\"registry_priorities\":[",
        c.chanspec.value,(c.chanspec.value&0xc000u)==0xc000u?"5GHz":(c.chanspec.value&0xc000u)==0?"2GHz":"unknown",bw,c.chanspec.value&255u);
    for(i=0;i<registry.init_count;i++)printf("%s%u",i?",":"",registry.entries[i].priority);
    printf("],\"phase\":\"%s\",\"callbacks_completed\":%u,\"current_callback\":%u,"
        "\"return_valid\":%s,\"return_value\":%d,\"slice_up_stored\":%s,"
        "\"trace_consumed\":%s,\"trace_events\":%zu",
        f.trace_failed?"failed":phase(job.phase),job.callbacks_completed,(unsigned)job.current,
        job.return_valid?"true":"false",job.return_value,job.slice_up_stored?"true":"false",
        f.cursor==total?"true":"false",f.cursor);
    if(f.trace_failed)printf(",\"failure\":{\"layer\":\"independent_trace\",\"dependency\":%u,\"field\":0,\"symbol\":\"unexpected_operation_or_unconsumed_queue\"}",job.dependency);
    else if(job.phase!=AXD_PS_TRACE_COMPLETE){
        if(job.current==AXD_INIT_RADIO)printf(",\"failure\":{\"layer\":\"radio_power\",\"dependency\":%u,\"field\":%u,\"symbol\":\"%s\",\"parent_dependency\":%u,\"error\":%u,\"switch_completed\":%s}",
            job.radio_job.dependency,job.radio_job.last_field,radio_symbol(&job.radio_job),job.dependency,job.radio_job.error,job.radio_job.switch_completed?"true":"false");
        else printf(",\"failure\":{\"layer\":\"phy_scheduler\",\"dependency\":%u,\"field\":0,\"symbol\":\"required_registered_callback\",\"error\":%u}",job.dependency,job.error);
    }
    puts("}");free(s.events);
    return f.trace_failed || job.phase==AXD_PS_FAILED || job.phase==AXD_PS_CANCELLED?1:0;
}
