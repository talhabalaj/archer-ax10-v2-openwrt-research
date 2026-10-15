/* SPDX-License-Identifier: MIT */
#include "phy_channel_init.h"
#include <stddef.h>
enum stage {ENTRY,RADAR_CHANSPEC,RADAR_FIRST,RADAR_SECOND,RADAR_ENABLE,
    RADAR_INITIAL,RADAR_INITIAL_NEXT,RADAR_SHM,RADAR_PI47C,RADAR_POST,
    RADAR_POST_NEXT,RADAR_TAIL,RADAR_TAIL_NEXT,RADAR_BAND,RADAR_BAND_NEXT,
    MHF_BAND,MHF_CACHE,MHF_READ,MHF_STORE,MHF_CLOCK,MHF_OFFSET,MHF_PRIMARY,
    MHF_EXTENDED,MHF_MIRROR,MHF_DONE,HWOBSS,HWOBSS_NEXT,HWOBSS_OFFSET,
    HWOBSS_READ,HWOBSS_SHM,RUN_PACKED,RUN_PHY,RUN_OBJECT,FINISH};
static void fail(struct axd_phy_channel_init *j,enum axd_phy_channel_error e)
{j->phase=AXD_CHANNEL_FAILED;j->error=e;}
static int fact(struct axd_phy_channel_init *j,enum axd_phy_channel_field field,
                   struct axd_wake_u32 value,uint32_t limit,unsigned nonnull)
{
    j->last_field=field;j->metadata_accesses++;
    if(!value.known)j->phase=AXD_CHANNEL_DEPENDENCY;
    else if(value.known!=1)fail(j,AXD_CHANNEL_BAD_ARGUMENT);
    else if(value.value>limit)fail(j,AXD_CHANNEL_BOUNDS);
    else if(nonnull && !value.value)fail(j,AXD_CHANNEL_BINDING);
    else return 1;
    return 0;
}
static int byte(struct axd_phy_channel_init *j,enum axd_phy_channel_field field,struct axd_wake_u8 value)
{const struct axd_wake_u32 wide={value.value,value.known};return fact(j,field,wide,255,0);}
struct relay {struct axd_phy_channel_init *j;axd_d11_io_fn io;void *opaque;};
static int child_io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct relay *r=opaque;struct axd_phy_channel_init *j=r->j;
    if(j->stage==RUN_PACKED)j->context.hardware=j->packed_job.context;
    else if(j->stage==RUN_PHY)j->context.hardware.phy=j->phy_job.context;
    if(!r->io){fail(j,AXD_CHANNEL_BAD_ARGUMENT);return -1;}
    j->attempted_ops++;
    if(r->io(r->opaque,op,reply)){fail(j,AXD_CHANNEL_IO_ERROR);return -1;}
    j->completed_ops++;return 0;
}
static void packed(struct axd_phy_channel_init *j,uint16_t selector,uint16_t value,unsigned next)
{
    if(axd_phy_packed_write_begin(&j->packed_job,selector,value,&j->context.hardware))fail(j,AXD_CHANNEL_CHILD_ERROR);
    else {j->stage=RUN_PACKED;j->after_child=next;}
}
static void phy(struct axd_phy_channel_init *j,enum axd_phy_command command,uint16_t selector,
                  uint16_t mask,uint16_t value,unsigned next)
{
    if(axd_phy_register_begin(&j->phy_job,command,selector,mask,value,&j->context.hardware.phy))fail(j,AXD_CHANNEL_CHILD_ERROR);
    else {j->stage=RUN_PHY;j->after_child=next;}
}
static void shm(struct axd_phy_channel_init *j,uint16_t address,uint16_t value,uint32_t selector,unsigned next)
{
    struct axd_object_request request={0};
    j->last_field=AXD_CHANNEL_OBJECT_OWNER;
    if(!j->context.object.owned){j->phase=AXD_CHANNEL_DEPENDENCY;return;}
    if(j->context.object.owned!=1){fail(j,AXD_CHANNEL_BAD_ARGUMENT);return;}
    if(j->context.object.core!=j->context.hardware.phy.core || j->context.object.minor!=j->context.hardware.phy.minor){fail(j,AXD_CHANNEL_BINDING);return;}
    request.command=AXD_OBJECT_WRITE16;request.address=address;request.value=value;request.selector=selector;
    if(axd_object_memory_begin(&j->object_job,&request,&j->context.object))fail(j,AXD_CHANNEL_CHILD_ERROR);
    else {j->stage=RUN_OBJECT;j->after_child=next;}
}
static void child(struct axd_phy_channel_init *j,struct relay *r)
{
    unsigned done,running,missing,cancelled;
    if(j->stage==RUN_PACKED){
        enum axd_phy_table_write_phase p=axd_phy_table_write_step(&j->packed_job,child_io,r);
        j->context.hardware=j->packed_job.context;
        done=p==AXD_TW_TRACE_COMPLETE;running=p==AXD_TW_RUNNING;missing=p==AXD_TW_DEPENDENCY;cancelled=p==AXD_TW_CANCELLED;
        if(done)j->packed_completed++;
    }else if(j->stage==RUN_PHY){
        enum axd_phy_phase p=axd_phy_register_step(&j->phy_job,child_io,r);
        j->context.hardware.phy=j->phy_job.context;
        if(j->phy_job.command==AXD_PHY_READ && j->phy_job.result_valid){j->read_sample=j->phy_job.result;j->read_valid=1;}
        done=p==AXD_PHY_TRACE_COMPLETE;running=p==AXD_PHY_RUNNING;missing=p==AXD_PHY_DEPENDENCY;cancelled=p==AXD_PHY_CANCELLED;
        if(done && j->phy_job.command==AXD_PHY_MODIFY)j->modify_completed++;
    }else{
        enum axd_object_phase p=axd_object_memory_step(&j->object_job,child_io,r);
        j->context.object=j->object_job.context;
        done=p==AXD_OBJECT_DONE;running=p==AXD_OBJECT_RUNNING;missing=p==AXD_OBJECT_DEPENDENCY;cancelled=p==AXD_OBJECT_CANCELLED;
        if(done)j->shm_completed++;
    }
    if(j->phase!=AXD_CHANNEL_RUNNING)return;
    if(done)j->stage=j->after_child;
    else if(missing)j->phase=AXD_CHANNEL_DEPENDENCY;
    else if(cancelled)j->phase=AXD_CHANNEL_CANCELLED;
    else if(!running)fail(j,AXD_CHANNEL_CHILD_ERROR);
}
int axd_phy_channel_init_begin(struct axd_phy_channel_init *j,enum axd_phy_channel_command command,
                                unsigned enabled,const struct axd_phy_channel_context *context)
{
    const struct axd_phy_channel_init empty={0};struct axd_phy_channel_context copy;
    if(!j)return -1;if(context){copy=*context;context=&copy;}*j=empty;fail(j,AXD_CHANNEL_BAD_ARGUMENT);
    if(!context || command<AXD_CHANNEL_RADAR_INIT || command>AXD_CHANNEL_HWOBSS || enabled>255)return -1;
    j->context=*context;j->command=command;j->enabled=enabled!=0;
    if(context->hardware.phy.core!=130 || context->hardware.phy.minor>1 || context->hardware.phy.phy_type!=0xb || context->hardware.phy.phy_revision!=51){fail(j,AXD_CHANNEL_UNSUPPORTED);return -1;}
    j->phase=AXD_CHANNEL_RUNNING;j->error=AXD_CHANNEL_NO_ERROR;return 0;
}
enum axd_phy_channel_phase axd_phy_channel_init_step(struct axd_phy_channel_init *j,axd_d11_io_fn io,void *opaque)
{
    struct relay r={j,io,opaque};struct axd_phy_channel_context *c;uint32_t bw;
    if(!j)return AXD_CHANNEL_INVALID;if(j->phase!=AXD_CHANNEL_RUNNING)return j->phase;c=&j->context;
    bw=c->chanspec.value&0x3800u;
    switch(j->stage){
    case ENTRY:
        if(j->command==AXD_CHANNEL_HWOBSS)j->stage=HWOBSS;
        else if(fact(j,AXD_CHANNEL_RADAR_STATE,c->radar_state_pointer,UINT32_MAX,1))j->stage=RADAR_CHANSPEC;
        break;
    case RADAR_CHANSPEC:
        if(fact(j,AXD_CHANNEL_CHANSPEC,c->chanspec,65535,0)){
            unsigned band=(c->chanspec.value&255u)<71u?0:4;
            j->threshold_index=band+(bw==0x1000?0u:bw==0x1800?1u:bw==0x2000?2u:3u);j->stage=RADAR_FIRST;
        }break;
    case RADAR_FIRST:
        if(fact(j,AXD_CHANNEL_THRESHOLD_FIRST,c->thresholds[j->threshold_index].first,65535,0)){
            c->state170=(uint16_t)c->thresholds[j->threshold_index].first.value;j->software_stores++;j->stage=RADAR_SECOND;
        }break;
    case RADAR_SECOND:
        if(fact(j,AXD_CHANNEL_THRESHOLD_SECOND,c->thresholds[j->threshold_index].second,65535,0)){
            c->state172=(uint16_t)c->thresholds[j->threshold_index].second.value;j->software_stores++;j->stage=RADAR_ENABLE;
        }break;
    case RADAR_ENABLE:
        if(!j->enabled)packed(j,0x25c,0,RADAR_TAIL);
        else j->stage=RADAR_INITIAL;
        break;
    case RADAR_INITIAL:{
        const uint16_t selectors[]={0x255,0x256,0x266,0x267,0x25d,0x259,0x25f,0x25a};
        uint16_t value=0;
        switch(j->index){
        case 0:case 2:value=c->state170;break;case 1:case 3:value=c->state172;break;
        case 5:if(!fact(j,AXD_CHANNEL_STATE176,c->state176,65535,0))return j->phase;value=(uint16_t)c->state176.value;break;
        case 6:value=bw==0x2000 && (c->chanspec.value&255u)>=71?0x5e88:0x5f88;break;
        case 7:value=1;break;default:break;
        }
        packed(j,selectors[j->index],value,RADAR_INITIAL_NEXT);break;}
    case RADAR_INITIAL_NEXT:if(++j->index<8)j->stage=RADAR_INITIAL;else {j->index=0;j->stage=RADAR_SHM;}break;
    case RADAR_SHM:
        if(fact(j,AXD_CHANNEL_RADAR_SHM,c->radar_shm_offset,65535,0))shm(j,(uint16_t)c->radar_shm_offset.value,c->state172,0x10000,RADAR_PI47C);
        break;
    case RADAR_PI47C:
        if(fact(j,AXD_CHANNEL_STATE1B0,c->state1b0,65535,0)){c->pi47c=(uint16_t)c->state1b0.value;j->software_stores++;j->stage=RADAR_POST;}
        break;
    case RADAR_POST:{
        const uint16_t selectors[]={0x25c,0x260,0x264},values[]={1,0x3206,0x141};
        packed(j,selectors[j->index],values[j->index],RADAR_POST_NEXT);break;}
    case RADAR_POST_NEXT:if(++j->index<3)j->stage=RADAR_POST;else {j->index=0;j->stage=RADAR_TAIL;}break;
    case RADAR_TAIL:{
        const uint16_t selectors[]={0x25e,0x260,0x261};uint16_t value;
        if(j->index==0)value=bw==0x1000 && (c->chanspec.value&255u)>=71?0xb4a3:0x34a3;
        else if(j->index==1)value=bw==0x3000?0x32f7:0x3237;else value=0x14;
        packed(j,selectors[j->index],value,RADAR_TAIL_NEXT);break;}
    case RADAR_TAIL_NEXT:if(++j->index<3)j->stage=RADAR_TAIL;else {j->index=0;j->stage=RADAR_BAND;}break;
    case RADAR_BAND:{
        const uint16_t selectors[4][5]={{0x25b,0x262,0x263,0x262,0x250},{0x25b,0x262,0x263,0x262,0x250},
            {0x25b,0x262,0x263,0x250,0x262},{0x25b,0x262,0x263,0x250,0x262}};
        const uint16_t values[4][5]={{8,200,25,600,0xa419},{16,400,50,1200,0xac32},
            {31,800,100,0xac64,2400},{0,1600,200,0xacc8,4800}};
        unsigned band=bw==0x1000?0u:bw==0x1800?1u:bw==0x2000?2u:3u;
        packed(j,selectors[band][j->index],values[band][j->index],RADAR_BAND_NEXT);break;}
    case RADAR_BAND_NEXT:if(++j->index<5)j->stage=RADAR_BAND;else j->stage=MHF_BAND;break;
    case MHF_BAND:
        if(fact(j,AXD_CHANNEL_CURRENT_BAND,c->current_band,UINT32_MAX,0))j->stage=c->current_band.value?MHF_CACHE:MHF_DONE;
        break;
    case MHF_CACHE:if(fact(j,AXD_CHANNEL_MHF_CACHE,c->mhf_cache_pointer,UINT32_MAX,1))j->stage=MHF_READ;break;
    case MHF_READ:if(fact(j,AXD_CHANNEL_MHF0,c->mhf0,65535,0)){j->mhf_before=(uint16_t)c->mhf0.value;j->stage=MHF_STORE;}break;
    case MHF_STORE:c->mhf0.value=(j->mhf_before&~UINT32_C(0x2000))|(j->enabled?0x2000u:0);j->software_stores++;j->stage=MHF_CLOCK;break;
    case MHF_CLOCK:
        if(byte(j,AXD_CHANNEL_MHF_CLOCK,c->mhf_clock_up))j->stage=c->mhf_clock_up.value && c->mhf0.value!=j->mhf_before?MHF_OFFSET:MHF_DONE;
        break;
    case MHF_OFFSET:if(fact(j,AXD_CHANNEL_MHF_OFFSET,c->mhf0_shm_offset,65535,0))j->stage=MHF_PRIMARY;break;
    case MHF_PRIMARY:shm(j,(uint16_t)c->mhf0_shm_offset.value,(uint16_t)c->mhf0.value,0x10000,MHF_EXTENDED);break;
    case MHF_EXTENDED:
        if(byte(j,AXD_CHANNEL_MHF_EXTENDED,c->mhf_extended))j->stage=c->mhf_extended.value?MHF_MIRROR:MHF_DONE;
        break;
    case MHF_MIRROR:shm(j,(uint16_t)c->mhf0_shm_offset.value,(uint16_t)c->mhf0.value,0x110000,MHF_DONE);break;
    case MHF_DONE:j->mhf_completed=1;j->stage=FINISH;break;
    case HWOBSS:
        if(j->enabled){
            const uint16_t selectors[]={0x1100,0x358,0x359,0x35a,0x13a,0x16c,0x1101,0x110f};
            const uint16_t values[]={0x7800,0xb0,0x37f,0x3340,0x48,0,0x200,0x280};
            if(j->index==5)phy(j,AXD_PHY_MODIFY,0x16c,0x8000,0,HWOBSS_NEXT);
            else packed(j,selectors[j->index],values[j->index],HWOBSS_NEXT);
        }else{
            const uint16_t selectors[]={0x1100,0x13a,0x358,0x359},values[]={0x7000,0x3f,0xc07f,100};
            if(j->index==4)phy(j,AXD_PHY_MODIFY,0x35a,0xfff,100,HWOBSS_NEXT);
            else packed(j,selectors[j->index],values[j->index],HWOBSS_NEXT);
        }break;
    case HWOBSS_NEXT:if(++j->index<(j->enabled?8u:5u))j->stage=HWOBSS;else j->stage=HWOBSS_OFFSET;break;
    case HWOBSS_OFFSET:if(fact(j,AXD_CHANNEL_HWOBSS_SHM,c->hwobss_shm_offset,65535,0))j->stage=HWOBSS_READ;break;
    case HWOBSS_READ:phy(j,AXD_PHY_READ,0x13a,0,0,HWOBSS_SHM);break;
    case HWOBSS_SHM:shm(j,(uint16_t)c->hwobss_shm_offset.value,j->read_sample,0x10000,FINISH);break;
    case RUN_PACKED:case RUN_PHY:case RUN_OBJECT:child(j,&r);break;
    case FINISH:j->phase=AXD_CHANNEL_TRACE_COMPLETE;break;
    default:fail(j,AXD_CHANNEL_BAD_ARGUMENT);break;
    }
    return j->phase;
}
enum axd_phy_channel_phase axd_phy_channel_init_cancel(struct axd_phy_channel_init *j)
{
    if(!j)return AXD_CHANNEL_INVALID;
    if(j->phase==AXD_CHANNEL_RUNNING){j->phase=AXD_CHANNEL_CANCELLED;
        axd_phy_table_write_cancel(&j->packed_job);axd_phy_register_cancel(&j->phy_job);axd_object_memory_cancel(&j->object_job);}
    return j->phase;
}
