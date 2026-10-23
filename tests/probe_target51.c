/* SPDX-License-Identifier: MIT */
#include "../driver/phy_scheduler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/radio_power_trace.h"
#include "fixtures/radio_regtbl_trace.h"
#include "fixtures/phy_init_trace.h"
#include "fixtures/phy_noise_trace.h"
#include "fixtures/phy_chanspec_trace.h"

/* This is an executable functional observation, not a regression assertion
 * that an incomplete child must stay incomplete. Every required registration
 * remains present. All values and private-span replacements are synthetic. */
struct prefix_event {unsigned path,kind;uint32_t offset,value,reply;};
static const struct prefix_event initialization_prefix[]={
    {AXD_PS_ALLOCATE_ZERO,0,32,0x7777,0x40000},
    {AXD_PS_ALLOCATE_ZERO,0,12,0x7777,0x41000},
    {AXD_PS_D11,AXD_D11_STORE_BARRIER,0,0,0},
    {AXD_PS_D11,AXD_D11_WRITE16,0x3e6,0,0},
    {AXD_PS_SI,AXD_SICO_READ32,0x408,0,0x40},
    {AXD_PS_SI,AXD_SICO_FULL_BARRIER,0,0,0}
};
static const struct axd_si_core catalog[]={
    {0x800,0x18000000,0x18100000,0},
    {0x812,0x18200000,0x18300000,0}
};
struct observation {struct axd_phy_scheduler *job;const struct prefix_event *events;
    size_t count,cursor;unsigned trace_failed;};
static int operation(void *opaque,const struct axd_phy_scheduler_operation *op,
                        struct axd_phy_scheduler_reply *reply)
{
    struct observation *f=opaque;const struct prefix_event *e;
    if(f->cursor>=f->count){f->trace_failed=1;return -1;}e=&f->events[f->cursor];
    if((unsigned)op->path!=e->path){f->trace_failed=1;return -1;}
    if(e->path==AXD_PS_ALLOCATE_ZERO){
        if(op->bytes!=e->offset || op->allocator!=e->value){f->trace_failed=1;return -1;}
    }else if(e->path==AXD_PS_SI){
        if((unsigned)op->si.kind!=e->kind || op->si.offset!=e->offset ||
           op->si.value!=e->value || op->si.wrapper!=0x22100000){f->trace_failed=1;return -1;}
    }else if(e->path==AXD_PS_CONTROL){
        if((unsigned)op->control.kind!=e->kind || op->control.offset!=e->offset ||
           op->control.value!=e->value || op->control.mapping!=0x20000000){f->trace_failed=1;return -1;}
        if(memcmp(&f->job->context.table,&f->job->table_job.context,sizeof(f->job->context.table))){f->trace_failed=1;return -1;}
    }else if(e->path==AXD_PS_D11){
        if((unsigned)op->d11.kind!=e->kind || op->d11.offset!=e->offset ||
           op->d11.value!=e->value){
            fprintf(stderr,"target event%zu callback%u actual%u/%x/%x expected%u/%x/%x\n",f->cursor,(unsigned)f->job->current,
                (unsigned)op->d11.kind,op->d11.offset,op->d11.value,e->kind,e->offset,e->value);
            f->trace_failed=1;return -1;
        }
        if(f->job->current==AXD_INIT_RADIO &&
            (memcmp(&f->job->context.radio_power,&f->job->radio_job.context,sizeof(f->job->context.radio_power)) ||
             memcmp(&f->job->context.table.hardware,&f->job->radio_job.context.radio.hardware,sizeof(f->job->context.table.hardware)))){
            f->trace_failed=1;return -1;
        }
        if(f->job->current==AXD_INIT_RADAR && memcmp(&f->job->context.channel,&f->job->channel_job.context,sizeof(f->job->context.channel))){f->trace_failed=1;return -1;}
    }else {f->trace_failed=1;return -1;}
    reply->value=e->reply;reply->control.value=e->reply;f->cursor++;return 0;
}
static struct axd_phy_scheduler_context target_context(void)
{
    struct axd_phy_scheduler_context c={0};struct axd_phy_table_context hardware;
    static uint8_t synthetic_prefregs[220];unsigned i;
    c.table=axd_test_table_context();hardware=c.table.hardware;
    c.radio_power=context();c.radio=c.radio_power.radio;
    /* Preserve the pinned55-record static loop shape without publishing any
     * proprietary selector/value pair. These54 records are original fixtures. */
    for(i=0;i<54;i++){
        uint16_t selector=(uint16_t)(0x100+i),value=(uint16_t)(0x4200+i);
        synthetic_prefregs[4*i]=(uint8_t)selector;synthetic_prefregs[4*i+1]=(uint8_t)(selector>>8);
        synthetic_prefregs[4*i+2]=(uint8_t)value;synthetic_prefregs[4*i+3]=(uint8_t)(value>>8);
    }
    synthetic_prefregs[216]=255;synthetic_prefregs[217]=255;
    c.radio.prefregs=synthetic_prefregs;c.radio.prefregs_length=sizeof(synthetic_prefregs);
    hardware.phy=c.radio.hardware.phy;c.radio.hardware=hardware;
    c.radio_power.regtbl=rrt_context(1);
    c.noise=axd_noise_fixture_context(2);c.noise.ac_pointer=c.radio.ac_pointer;
    c.noise.common_pointer=(struct axd_wake_u32){0x1a000,1};c.noise.data_pointer=(struct axd_wake_u32){0x1b000,1};
    c.noise.current_channel=(struct axd_wake_u32){0x1c000,1};
    c.noise.hwaci_2g_pointer=(struct axd_wake_u32){0x1d000,1};c.noise.hwaci_5g_pointer=(struct axd_wake_u32){0x1e000,1};
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
    c.radar_enabled=(struct axd_wake_u8){1,1};
    c.channel.radar_state_pointer=(struct axd_wake_u32){0x15000,1};
    for(i=0;i<8;i++){
        c.channel.thresholds[i].first=(struct axd_wake_u32){0x3100+i,1};
        c.channel.thresholds[i].second=(struct axd_wake_u32){0x4200+i,1};
    }
    c.channel.state176=(struct axd_wake_u32){0x5678,1};c.channel.state1b0=(struct axd_wake_u32){0x789a,1};
    c.channel.radar_shm_offset=(struct axd_wake_u32){0x262,1};
    c.channel.object=(struct axd_object_context){130,1,1};
    c.channel.current_band=(struct axd_wake_u32){0x16000,1};c.channel.mhf_cache_pointer=(struct axd_wake_u32){0x17000,1};
    c.channel.mhf0=(struct axd_wake_u32){0x855a,1};c.channel.mhf0_shm_offset=(struct axd_wake_u32){0x1a4,1};
    c.channel.mhf_clock_up=(struct axd_wake_u8){1,1};c.channel.mhf_extended=(struct axd_wake_u8){1,1};
    c.channel.hwobss_shm_offset=c.noise.channel.hwobss_shm_offset;
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
    case AXD_RPOWER_REGTBL_CHANNEL_TAIL:return "wlc_phy_set_regtbl_on_pwron_acphy";
    case AXD_RPOWER_CHANSPEC_TAIL:return "wlc_phy_chanspec_set_acphy";
    case AXD_RPOWER_DIVISION_BY_ZERO:return "ARM_unsigned_divide_by_zero";
    case AXD_RPOWER_PREFIX_CHILD:return "radio_control_prefix";
    case AXD_RPOWER_REGISTER_CHILD:return "radio_register_child";
    case AXD_RPOWER_METADATA:return "required_radio_runtime_metadata";
    default:return "radio_power";}
}
static const char *chanspec_symbol(const struct axd_phy_chanspec *j)
{
    switch(j->dependency){
    case AXD_CS_RADIO20704_SETUP:return "wlc_phy_chanspec_radio20704_setup";
    case AXD_CS_PLL_TUNE:return "wlc_phy_radio20704_pll_tune";
    case AXD_CS_RADIO20704_2G:return "radio20704_setup_2g_branch";
    case AXD_CS_RADIO20704_6G:return "radio20704_setup_6g_branch";
    case AXD_CS_RADIO2069_SETUP:return "chanspec_setup_radio_2069";
    case AXD_CS_MODULE_SETUP_PHY:return "chanspec_setup_phy";
    case AXD_CS_MAC_CHILD:return "wlc_bmac_suspend_mac_and_wait";
    case AXD_CS_BMAC_PHY_RESET:return "wlc_bmac_phy_reset";
    case AXD_CS_PHY_INIT_FALLBACK:return "phy_init";
    case AXD_CS_CHIP4335_BRANCH:return "chanspec_setup_chip4335";
    case AXD_CS_METADATA:return "required_chanspec_runtime_metadata";
    default:return "phy_ac_chanmgr_init_chanspec";}
}
static const char *scheduler_symbol(enum axd_phy_scheduler_dependency d)
{
    switch(d){case AXD_PS_NOISE_MODE:return "phy_ac_noise_set_mode";
    case AXD_PS_CHANSPEC_CHILD:return "phy_ac_chanmgr_init_chanspec";
    case AXD_PS_RADAR_CHILD:return "phy_ac_radar_init";
    case AXD_PS_TABLE_CHILD:return "wlc_phy_init_acphy";
    case AXD_PS_METADATA:return "required_runtime_metadata";
    default:return "required_registered_callback";}
}
int main(void)
{
    struct script s={0};struct axd_phy_scheduler job;
    struct axd_phy_scheduler_context c=target_context();struct axd_phy_registry registry;
    struct axd_wake_u32 labels[AXD_PHY_TARGET_INIT_COUNT];
    struct observation f={0};unsigned i,steps=0,bw;size_t total;
    struct axd_test_table_script table_expected=axd_test_table_script_build();
    struct axd_noise_fixture_script noise_expected=axd_noise_fixture_full(2,1,0);
    struct prefix_event *expected;size_t capacity=110000;
    if(c.chanspec.known!=1 || c.chanspec.value>65535)return 1;
    /* Explicit synthetic callback13 inputs; canonical pointers are rebound. */
    c.chanspec_init=cs_context();c.chanspec_init.band_phy_pointer=c.radio.phy_pointer;
    c.chanspec_init.shared_current_pi=c.radio.phy_pointer;c.chanspec_init.bmac_chanspec.known=0;
    switch(c.chanspec.value&0x3800u){case 0x1000:bw=20;break;case 0x1800:bw=40;break;
    case 0x2000:bw=80;break;case 0x2800:bw=160;break;default:bw=0;break;}
    s.capacity=100000;s.events=malloc(s.capacity*sizeof(*s.events));
    expected=malloc(capacity*sizeof(*expected));if(!s.events || !expected){free(s.events);free(expected);return 1;}
    total=sizeof(initialization_prefix)/sizeof(initialization_prefix[0]);
    memcpy(expected,initialization_prefix,sizeof(initialization_prefix));
    full(&s,2,0);
    /* Literal prefix has nine packed writes, delay, then packed408=6 before
     * the static record loop. Insert its54 independent four-event writes. */
    {struct event records[216];struct script pairs={records,0,216,0,0,0};
        for(i=0;i<54;i++){
            st(&pairs);add(&pairs,AXD_D11_WRITE16,0x3d8,0x100+i,0);
            st(&pairs);add(&pairs,AXD_D11_WRITE16,0x3da,0x4200+i,0);
        }
        assert(s.count+216<s.capacity && s.count>=21);
        memmove(s.events+21+216,s.events+21,(s.count-21)*sizeof(*s.events));
        memcpy(s.events+21,records,sizeof(records));s.count+=216;
    }
    rrt_full(&s,2,0,0,&c.radio_power.regtbl);
    for(i=0;i<s.count;i++)expected[total++]=(struct prefix_event){AXD_PS_D11,s.events[i].kind,s.events[i].offset,s.events[i].value,s.events[i].reply};
    for(i=0;i<table_expected.count;i++){
        const struct axd_test_table_event *e=&table_expected.events[i];
        expected[total++]=(struct prefix_event){e->path,e->kind,e->offset,e->value,e->reply};
    }
    /* Enabled real5GHz radar path, including both actual MHF writers. */
    s.count=0;
    packed(&s,0x255,0x3100);packed(&s,0x256,0x4200);packed(&s,0x266,0x3100);packed(&s,0x267,0x4200);
    packed(&s,0x25d,0);packed(&s,0x259,0x5678);packed(&s,0x25f,0x5f88);packed(&s,0x25a,1);
    for(i=0;i<3;i++){
        uint32_t selector=i==2?0x110000:0x10000,address=i?0x1a4:0x262,value=i?0xa55a:0x4200;
        st(&s);add(&s,AXD_D11_WRITE32,0x160,selector|(address>>2),0);
        add(&s,AXD_D11_READ32,0x160,0,0xdeadbeef);sy(&s);st(&s);add(&s,AXD_D11_WRITE16,0x164+(address&2),value,0);
        if(i==0){
            packed(&s,0x25c,1);packed(&s,0x260,0x3206);packed(&s,0x264,0x141);
            packed(&s,0x25e,0x34a3);packed(&s,0x260,0x3237);packed(&s,0x261,0x14);
            packed(&s,0x25b,8);packed(&s,0x262,200);packed(&s,0x263,25);packed(&s,0x262,600);packed(&s,0x250,0xa419);
        }
    }
    /* Actual inactive-MAC antdiv branch supplies a fresh maincontrol sample. */
    add(&s,AXD_D11_READ32,0x120,0,0xabc00002);sy(&s);
    assert(total+s.count<capacity);
    for(i=0;i<s.count;i++)expected[total++]=(struct prefix_event){AXD_PS_D11,s.events[i].kind,s.events[i].offset,s.events[i].value,s.events[i].reply};
    for(i=0;i<noise_expected.count;i++){
        const struct axd_noise_fixture_event *e=&noise_expected.events[i];
        expected[total++]=(struct prefix_event){AXD_PS_D11,e->kind,e->offset,e->value,e->reply};
    }
    /* Callback13 module0 for the same2-core5GHz20MHz scenario, MAC inactive. */
    {static struct cs_event events[1024];struct cs_script cs={events,0,1024};
        const struct cs_options o={2,0,0x11,0x1232,1,2,0,0x12,0,0};cs_module0(&cs,&o);cs_module2(&cs,&o);
        assert(total+cs.count<capacity);
        for(i=0;i<cs.count;i++)expected[total++]=(struct prefix_event){cs.events[i].path==AXD_CS_SI?AXD_PS_SI:AXD_PS_D11,
            cs.events[i].kind,cs.events[i].offset,cs.events[i].value,cs.events[i].reply};
    }
    f=(struct observation){&job,expected,total,0,0};
    /* Synthetic labels/allocations sit apart from the regtbl fixture's synthetic
     * pointer bindings (0x10000..0x19000); distinct objects never alias. */
    for(i=0;i<AXD_PHY_TARGET_INIT_COUNT;i++)labels[i]=(struct axd_wake_u32){0x30000+i*0x1000,1};
    labels[5]=c.radio.radio_info;
    if(axd_phy_registry_target51(&registry,labels) || axd_phy_scheduler_begin(&job,&registry,&c)){free(s.events);free(expected);return 1;}
    while(job.phase==AXD_PS_RUNNING){
        size_t old=f.cursor;
        axd_phy_scheduler_step(&job,operation,&f);
        if(f.cursor-old>1 || ++steps>2000000){f.trace_failed=1;break;}
    }
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
    printf(",\"cold_static_loaded\":%s,\"radio_returned\":%s,\"prefregs_span_bytes\":%zu,\"error\":%u,\"dependency\":%u",
        c.radio_power.regtbl.load_static.value?"true":"false",
        job.radio_job.phase==AXD_RPOWER_TRACE_COMPLETE && job.radio_job.regtbl_completed?"true":"false",job.radio_job.context.radio.prefregs_length,job.error,job.dependency);
    if(f.trace_failed)printf(",\"failure\":{\"layer\":\"independent_trace\",\"dependency\":%u,\"field\":0,\"symbol\":\"unexpected_operation_or_unconsumed_queue\"}",job.dependency);
    else if(job.phase!=AXD_PS_TRACE_COMPLETE){
        if(job.current==AXD_INIT_RADIO)printf(",\"failure\":{\"layer\":\"radio_power\",\"dependency\":%u,\"field\":%u,\"symbol\":\"%s\",\"parent_dependency\":%u,\"error\":%u,\"switch_completed\":%s}",
            job.radio_job.dependency,job.radio_job.last_field,radio_symbol(&job.radio_job),job.dependency,job.radio_job.error,job.radio_job.switch_completed?"true":"false");
        else if(job.current==AXD_INIT_CHANSPEC && job.dependency==AXD_PS_CHANSPEC_CHILD)
            printf(",\"failure\":{\"layer\":\"phy_chanspec\",\"dependency\":%u,\"field\":%u,\"symbol\":\"%s\",\"parent_dependency\":%u,\"error\":%u,\"modules_completed\":%u}",
                job.chanspec_job.dependency,job.chanspec_job.last_field,chanspec_symbol(&job.chanspec_job),job.dependency,job.chanspec_job.error,job.chanspec_job.modules_completed);
        else printf(",\"failure\":{\"layer\":\"phy_scheduler\",\"dependency\":%u,\"field\":0,\"symbol\":\"%s\",\"error\":%u}",job.dependency,scheduler_symbol(job.dependency),job.error);
    }
    puts("}");free(s.events);free(expected);
    return f.trace_failed || job.phase==AXD_PS_FAILED || job.phase==AXD_PS_CANCELLED?1:0;
}
