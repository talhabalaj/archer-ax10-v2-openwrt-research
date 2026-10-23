/* SPDX-License-Identifier: MIT */
#include "phy_noise.h"
enum stage {ENTRY,MODE_HWACI,MODE_PREEMPT,MODE_HWOBSS,MODE_ELNA,MODE_MCLIP,MODE_DONE,
    HWACI,HWACI_ON_GATE,HWACI_SHM_READ,HWACI_SHM_WRITE,HWACI_ON_BODY,
    HWACI_ON_TAIL,HWACI_2G,HWACI_2G_STORES,HWACI_5G,HWACI_5G_STORES,HWACI_DONE,
    PREEMPT,PREEMPT_HALF,PREEMPT_HW,PREEMPT_NEXT,PREEMPT_GUARD,PREEMPT_SET,
    PREEMPT_FILTER,PREEMPT_RESTORE,PREEMPT_READ,PREEMPT_STORE,PREEMPT_DONE,
    FILTER,FILTER_COMMON,FILTER_TARGET,FILTER_TAIL,FILTER_SHM,FILTER_DONE,
    ELNA,ELNA_CORE,ELNA_NEXT,ELNA_DONE,MCLIP_READ,MCLIP_SY,MCLIP_CHOOSE,
    MCLIP_ENTER,MCLIP_GUARD,MCLIP_SET,MCLIP_CORE,MCLIP_COMPARE,MCLIP_WRITE,
    MCLIP_NEXT,MCLIP_RESTORE,MCLIP_EXIT,MCLIP_ENABLE,MCLIP_DONE,FINISH,
    RUN_PHY,RUN_PACKED,RUN_CHANNEL,RUN_OBJECT,RUN_RADIO,RUN_READER,RUN_WRITER,RUN_MAC,RUN_GUARD};
static const uint8_t zero6[6]={0,0,0,0,0,0},mclip6[6]={0,0,0,0,100,100};
static unsigned differs6(const uint8_t *a,const uint8_t *b)
{unsigned k;for(k=0;k<6;k++)if(a[k]!=b[k])return 1;return 0;}
static void fail(struct axd_phy_noise *j,enum axd_phy_noise_error e){j->phase=AXD_NOISE_FAILED;j->error=e;}
static void dependency(struct axd_phy_noise *j,enum axd_phy_noise_dependency d){j->phase=AXD_NOISE_DEPENDENCY;j->dependency=d;}
static int fact(struct axd_phy_noise *j,enum axd_phy_noise_field field,struct axd_wake_u32 f,uint32_t limit,unsigned nonnull)
{
    j->last_field=field;j->metadata_accesses++;
    if(!f.known)dependency(j,AXD_NOISE_METADATA);
    else if(f.known!=1)fail(j,AXD_NOISE_BAD_ARGUMENT);
    else if(f.value>limit)fail(j,AXD_NOISE_BOUNDS);
    else if(nonnull && !f.value)fail(j,AXD_NOISE_BINDING);
    else return 1;return 0;
}
static int byte(struct axd_phy_noise *j,enum axd_phy_noise_field field,struct axd_wake_u8 f)
{const struct axd_wake_u32 w={f.value,f.known};return fact(j,field,w,255,0);}
struct relay {struct axd_phy_noise *j;axd_d11_io_fn io;void *opaque;};
static void publish(struct axd_phy_noise *j)
{
    switch(j->stage){
    case RUN_PHY:j->context.hardware.phy=j->phy_job.context;break;
    case RUN_PACKED:j->context.hardware=j->packed_job.context;break;
    case RUN_CHANNEL:j->context.channel=j->channel_job.context;j->context.hardware=j->channel_job.context.hardware;break;
    case RUN_RADIO:j->context.radio=j->radio_job.context;j->context.hardware=j->radio_job.context.hardware;break;
    case RUN_READER:j->context.hardware=j->reader_job.context;break;
    case RUN_WRITER:j->context.hardware=j->writer_job.context;break;
    case RUN_MAC:j->context.hardware.mac=j->mac_job.context;break;
    case RUN_GUARD:j->context.hardware.mac=j->guard_job.context.mac;j->context.phy_depth.value=j->guard_job.context.depth;break;
    default:break;
    }
}
static int perform(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct relay *r=opaque;struct axd_phy_noise *j=r->j;publish(j);
    if(!r->io){fail(j,AXD_NOISE_BAD_ARGUMENT);return -1;}
    j->attempted_ops++;if(r->io(r->opaque,op,reply)){fail(j,AXD_NOISE_IO_ERROR);return -1;}j->completed_ops++;return 0;
}
static void phy(struct axd_phy_noise *j,enum axd_phy_command command,uint16_t selector,uint16_t mask,uint16_t value,unsigned next)
{if(axd_phy_register_begin(&j->phy_job,command,selector,mask,value,&j->context.hardware.phy))fail(j,AXD_NOISE_CHILD_ERROR);else {j->stage=RUN_PHY;j->after_child=next;}}
static void packed(struct axd_phy_noise *j,uint16_t selector,uint16_t value,unsigned next)
{if(axd_phy_packed_write_begin(&j->packed_job,selector,value,&j->context.hardware))fail(j,AXD_NOISE_CHILD_ERROR);else {j->stage=RUN_PACKED;j->after_child=next;}}
static void mac(struct axd_phy_noise *j,enum axd_psm_command command,unsigned next)
{
    if(!j->context.hardware.mac_context_valid){dependency(j,AXD_NOISE_MAC_CHILD);return;}
    if(j->context.hardware.mac_context_valid!=1 ||
       j->context.hardware.mac.core!=j->context.hardware.phy.core || j->context.hardware.mac.minor!=j->context.hardware.phy.minor ||
       axd_psm_control_begin_policy(&j->mac_job,command,&j->context.hardware.mac,&j->context.hardware.policy))fail(j,AXD_NOISE_CHILD_ERROR);
    else {j->stage=RUN_MAC;j->after_child=next;}
}
static void guard(struct axd_phy_noise *j,enum axd_phy_guard_command command,unsigned next)
{
    struct axd_phy_guard_context c={0};
    if(!byte(j,AXD_NOISE_DEPTH,j->context.phy_depth))return;
    c.mac=j->context.hardware.mac;c.policy=j->context.hardware.policy;c.depth=j->context.phy_depth.value;
    if(axd_phy_guard_begin(&j->guard_job,command,&c))fail(j,AXD_NOISE_CHILD_ERROR);
    else {j->stage=RUN_GUARD;j->after_child=next;}
}
static void child(struct axd_phy_noise *j,struct relay *r)
{
    unsigned done=0,running=0,missing=0,cancelled=0;
    switch(j->stage){
    case RUN_PHY:{enum axd_phy_phase p=axd_phy_register_step(&j->phy_job,perform,r);done=p==AXD_PHY_TRACE_COMPLETE;running=p==AXD_PHY_RUNNING;missing=p==AXD_PHY_DEPENDENCY;cancelled=p==AXD_PHY_CANCELLED;break;}
    case RUN_PACKED:{enum axd_phy_table_write_phase p=axd_phy_table_write_step(&j->packed_job,perform,r);done=p==AXD_TW_TRACE_COMPLETE;running=p==AXD_TW_RUNNING;missing=p==AXD_TW_DEPENDENCY;cancelled=p==AXD_TW_CANCELLED;break;}
    case RUN_CHANNEL:{enum axd_phy_channel_phase p=axd_phy_channel_init_step(&j->channel_job,perform,r);done=p==AXD_CHANNEL_TRACE_COMPLETE;running=p==AXD_CHANNEL_RUNNING;missing=p==AXD_CHANNEL_DEPENDENCY;cancelled=p==AXD_CHANNEL_CANCELLED;break;}
    case RUN_OBJECT:{enum axd_object_phase p=axd_object_memory_step(&j->object_job,perform,r);done=p==AXD_OBJECT_DONE;running=p==AXD_OBJECT_RUNNING;missing=p==AXD_OBJECT_DEPENDENCY;cancelled=p==AXD_OBJECT_CANCELLED;break;}
    case RUN_RADIO:{enum axd_radio_control_phase p=axd_radio_control_step(&j->radio_job,perform,r);done=p==AXD_RADIO_CTL_TRACE_COMPLETE;running=p==AXD_RADIO_CTL_RUNNING;missing=p==AXD_RADIO_CTL_DEPENDENCY;cancelled=p==AXD_RADIO_CTL_CANCELLED;break;}
    case RUN_READER:{enum axd_phy_table_read_phase p=axd_phy_table_read_step(&j->reader_job,perform,r);done=p==AXD_TR_TRACE_COMPLETE;running=p==AXD_TR_RUNNING;missing=p==AXD_TR_DEPENDENCY;cancelled=p==AXD_TR_CANCELLED;if(done)j->table_reads++;break;}
    case RUN_WRITER:{enum axd_phy_table_write_phase p=axd_phy_table_write_step(&j->writer_job,perform,r);done=p==AXD_TW_TRACE_COMPLETE;running=p==AXD_TW_RUNNING;missing=p==AXD_TW_DEPENDENCY;cancelled=p==AXD_TW_CANCELLED;if(done)j->table_writes++;break;}
    case RUN_MAC:{enum axd_psm_phase p=axd_psm_control_step(&j->mac_job,perform,r);done=p==AXD_PSM_DONE;running=p==AXD_PSM_RUNNING;missing=p==AXD_PSM_DEPENDENCY;cancelled=p==AXD_PSM_CANCELLED;j->pending_callouts|=j->mac_job.pending_callouts;break;}
    case RUN_GUARD:{enum axd_phy_guard_phase p=axd_phy_guard_step(&j->guard_job,perform,r);done=p==AXD_GUARD_TRACE_COMPLETE;running=p==AXD_GUARD_RUNNING;missing=p==AXD_GUARD_DEPENDENCY;cancelled=p==AXD_GUARD_CANCELLED;j->pending_callouts|=j->guard_job.pending_callouts;break;}
    default:break;
    }
    publish(j);
    if(j->phase!=AXD_NOISE_RUNNING)return;
    if(done)j->stage=j->after_child;
    else if(missing)dependency(j,AXD_NOISE_RESOURCE_CHILD);
    else if(cancelled)j->phase=AXD_NOISE_CANCELLED;
    else if(!running)fail(j,AXD_NOISE_CHILD_ERROR);
}
static void helper(struct axd_phy_noise *j,unsigned stage,unsigned enabled,unsigned after)
{j->stage=stage;j->enabled=enabled;j->after_helper=after;j->index=0;j->core_index=0;}
static void helper_done(struct axd_phy_noise *j){j->stage=j->after_helper;}
static void filter(struct axd_phy_noise *j,unsigned after)
{j->stage=FILTER;j->index=0;j->after_filter=after;}
int axd_phy_noise_begin(struct axd_phy_noise *j,enum axd_phy_noise_command command,uint32_t mode,const struct axd_phy_noise_context *context)
{
    const struct axd_phy_noise empty={0};struct axd_phy_noise_context c;
    if(!j)return -1;if(context){c=*context;context=&c;}*j=empty;fail(j,AXD_NOISE_BAD_ARGUMENT);
    if(!context || command<AXD_NOISE_INIT_MODE || command>AXD_NOISE_HWACI_ON ||
       (command!=AXD_NOISE_INIT_MODE && mode>1) || (command==AXD_NOISE_HWACI_OFF && mode!=0) ||
       (command==AXD_NOISE_HWACI_ON && mode!=1))return -1;
    j->context=c;j->command=command;j->mode=mode;j->enabled=mode!=0;
    if(c.hardware.phy.core!=130 || c.hardware.phy.minor>1 || c.hardware.phy.phy_type!=0xb || c.hardware.phy.phy_revision!=51){fail(j,AXD_NOISE_UNSUPPORTED);return -1;}
    if(axd_phy_register_begin(&j->phy_job,AXD_PHY_READ,0,0,0,&c.hardware.phy)){fail(j,AXD_NOISE_CHILD_ERROR);return -1;}
    j->phase=AXD_NOISE_RUNNING;j->error=AXD_NOISE_NO_ERROR;return 0;
}
enum axd_phy_noise_phase axd_phy_noise_step(struct axd_phy_noise *j,axd_d11_io_fn io,void *opaque)
{
    struct relay r={j,io,opaque};struct axd_phy_noise_context *c;
    if(!j)return AXD_NOISE_INVALID;if(j->phase!=AXD_NOISE_RUNNING)return j->phase;c=&j->context;
    switch(j->stage){
    case ENTRY:
        if(!fact(j,AXD_NOISE_PHY,c->phy_pointer,UINT32_MAX,1))break;
        switch(j->command){
        case AXD_NOISE_INIT_MODE:if(!(j->mode&1u))dependency(j,AXD_NOISE_DESENSE_RESET_APPLY);else j->stage=MODE_HWACI;break;
        case AXD_NOISE_HWACI_OFF:helper(j,HWACI,0,FINISH);break;
        case AXD_NOISE_HWACI_ON:helper(j,HWACI,1,FINISH);break;
        case AXD_NOISE_PREEMPT:helper(j,PREEMPT,j->enabled,FINISH);break;
        case AXD_NOISE_POSTFILTER:j->after_helper=FINISH;filter(j,FINISH);break;
        case AXD_NOISE_ELNABYPASS:helper(j,ELNA,j->enabled,FINISH);break;
        case AXD_NOISE_MCLIP:helper(j,MCLIP_READ,j->enabled,FINISH);break;
        default:fail(j,AXD_NOISE_BAD_ARGUMENT);break;
        }break;
    case MODE_HWACI:helper(j,HWACI,(j->mode&2u)!=0,MODE_PREEMPT);break;
    case MODE_PREEMPT:helper(j,PREEMPT,(j->mode&8u)!=0,MODE_HWOBSS);break;
    case MODE_HWOBSS:
        if(fact(j,AXD_NOISE_CHANSPEC,c->chanspec,65535,0)){
            uint32_t bw=c->chanspec.value&0x3800u;c->channel.hardware=c->hardware;
            if(axd_phy_channel_init_begin(&j->channel_job,AXD_CHANNEL_HWOBSS,(((bw-0x1800u)&0xf000u)==0 && (j->mode&0x40u)),&c->channel))fail(j,AXD_NOISE_CHILD_ERROR);
            else {j->stage=RUN_CHANNEL;j->after_child=MODE_ELNA;}
        }break;
    case MODE_ELNA:j->hwobss_completed++;helper(j,ELNA,(j->mode>>7)&1u,MODE_MCLIP);break;
    case MODE_MCLIP:helper(j,MCLIP_READ,(j->mode>>8)&1u,MODE_DONE);break;
    case MODE_DONE:j->stage=FINISH;break;
    case HWACI:{
        static const uint16_t selectors[]={0x5a1,0x5a0,0x5ab},masks[]={0x8000,1,6};
        if(j->enabled){j->stage=HWACI_ON_GATE;break;}
        if(j->index<3)phy(j,AXD_PHY_MODIFY,selectors[j->index],masks[j->index],0,HWACI);
        else j->stage=HWACI_2G;j->index++;break;}
    case HWACI_ON_GATE:
        if(fact(j,AXD_NOISE_CHANSPEC,c->chanspec,65535,0) && fact(j,AXD_NOISE_AC,c->ac_pointer,UINT32_MAX,1) && byte(j,AXD_NOISE_HWACI_ADC_FLAG,c->hwaci_adc_flag)){
            j->index=0;j->stage=c->hwaci_adc_flag.value==1?HWACI_SHM_READ:HWACI_ON_BODY;
        }break;
    case HWACI_SHM_READ:
        if(fact(j,AXD_NOISE_HWACI_SHM,c->hwaci_shm_offset,65535,0)){
            struct axd_d11_operation op={0};uint32_t reply=0;
            if(!c->channel.object.owned){dependency(j,AXD_NOISE_RESOURCE_CHILD);break;}
            if(c->channel.object.owned!=1 || c->channel.object.core!=c->hardware.phy.core || c->channel.object.minor!=c->hardware.phy.minor){fail(j,AXD_NOISE_BINDING);break;}
            if(c->hwaci_shm_offset.value&1u){fail(j,AXD_NOISE_BOUNDS);break;}
            switch(j->index){
            case 0:op.kind=AXD_D11_STORE_BARRIER;break;
            case 1:op.kind=AXD_D11_WRITE32;op.offset=0x160;op.value=0x10000|(c->hwaci_shm_offset.value>>2);break;
            case 2:op.kind=AXD_D11_READ32;op.offset=0x160;break;
            case 3:case 5:op.kind=AXD_D11_FULL_BARRIER;break;
            default:op.kind=AXD_D11_READ16;op.offset=0x164+(c->hwaci_shm_offset.value&2u);break;
            }
            if(!perform(&r,&op,&reply)){
                if(j->index==4)j->hwaci_shm_sample=(uint16_t)reply;
                if(++j->index==6){j->shm_sample_valid=1;j->stage=HWACI_SHM_WRITE;}
            }
        }break;
    case HWACI_SHM_WRITE:{
        struct axd_object_request q={0};q.command=AXD_OBJECT_WRITE16;q.address=c->hwaci_shm_offset.value;q.selector=0x10000;q.value=j->hwaci_shm_sample|8u;
        if(axd_object_memory_begin(&j->object_job,&q,&c->channel.object))fail(j,AXD_NOISE_CHILD_ERROR);
        else {j->index=0;j->stage=RUN_OBJECT;j->after_child=HWACI_ON_BODY;}break;}
    case HWACI_ON_BODY:{
        struct scalar {uint16_t modify,selector,mask,value;};
        static const struct scalar operations[]={
            {1,0x5a0,1,1},{1,0x5a1,0x8000,0x8000},{1,0x5c1,0xc000,0},{1,0x5ab,6,4},
            {1,0x5b4,1,0},{1,0x5ab,0x4000,0x4000},{1,0x5b2,0x40,0},{1,0x5a0,0xf0,0x80},
            {1,0x5a0,0xf00,0x800},{1,0x5a1,0xf,2},{1,0x5a1,0xf0,0x20},{1,0x5a2,0xffff,1},
            {1,0x5a3,0xffff,1},{0,0x5a8,0,0xe848},{0,0x5a9,0,1},{1,0x5b2,0x780,0x300},
            {1,0x5b2,0x7800,0x1800},{1,0x5c1,6,2},{1,0x5ab,1,1},{1,0x5ab,0x8000,0x8000},
            {1,0x5ab,0x1c0,0x140},{1,0x5ab,0x38,0x28},{1,0x5cb,3,1},{1,0x5cb,0x30,0x10},
            {1,0x5cb,0xc,0},{1,0x5cb,0xc0,0x80},{1,0x5cb,0x3800,0x2800},{1,0x5cb,0x700,0x500},
            {1,0x5b3,4,0},{0,0x4718,0,0x451},{0,0x4719,0,0xd34},{0,0x5740,0,0x14},
            {0,0x5741,0,0},{0,0x5742,0,0xa},{0,0x5743,0,0x14},{0,0x5744,0,0},{0,0x5745,0,0xa},
            {0,0x5746,0,2},{0,0x5747,0,0},{0,0x5748,0,0xff9c},{0,0x5749,0,2},{0,0x574a,0,0},
            {0,0x574b,0,0xff9c},{0,0x4714,0,0},{0,0x4715,0,0},{0,0x4716,0,0},{0,0x4717,0,0},
            {0,0x471a,0,0},{0,0x471b,0,0},{0,0x471c,0,0x200},{0,0x471d,0,0x200},
            {0,0x471e,0,0x451},{0,0x471f,0,0xd34}};
        if(j->index==sizeof(operations)/sizeof(operations[0])){j->index=0;j->stage=HWACI_ON_TAIL;}
        else {const struct scalar *s=&operations[j->index];uint16_t v=s->value;
            if(!(c->chanspec.value&0xc000u)){
                if(j->index==37 || j->index==40)v=100;
                if(j->index==49 || j->index==50)v=0x100;
            }
            j->index++;if(s->modify)phy(j,AXD_PHY_MODIFY,s->selector,s->mask,v,HWACI_ON_BODY);else packed(j,s->selector,v,HWACI_ON_BODY);
        }break;}
    case HWACI_ON_TAIL:{static const uint16_t s[]={0x5a1,0x5a0,0x1092,0x1092,0x5ab},m[]={0x8000,1,1,1,0x4000},v[]={0x8000,1,1,0,0x4000};
        if(j->index==5)j->stage=HWACI_2G;else {unsigned n=j->index++;phy(j,AXD_PHY_MODIFY,s[n],m[n],v[n],HWACI_ON_TAIL);}break;}
    case HWACI_2G:
        if(fact(j,AXD_NOISE_AC,c->ac_pointer,UINT32_MAX,1) && fact(j,AXD_NOISE_HWACI_2G,c->hwaci_2g_pointer,UINT32_MAX,1) && byte(j,AXD_NOISE_HWACI_2G_FLAG,c->hwaci_2g_flag)){j->index=0;j->stage=HWACI_2G_STORES;}break;
    case HWACI_2G_STORES:c->hwaci_2g[j->index]=(uint8_t)(j->index==0?5:j->index==1?(c->hwaci_2g_flag.value?3:5):(c->hwaci_2g_flag.value?2:4));j->software_stores++;if(++j->index==3)j->stage=HWACI_5G;break;
    case HWACI_5G:
        if(fact(j,AXD_NOISE_HWACI_5G,c->hwaci_5g_pointer,UINT32_MAX,1) && byte(j,AXD_NOISE_HWACI_5G_FLAG,c->hwaci_5g_flag)){j->index=0;j->stage=HWACI_5G_STORES;}break;
    case HWACI_5G_STORES:c->hwaci_5g[j->index]=(uint8_t)(j->index==0?5:j->index==1?(c->hwaci_5g_flag.value?3:5):(c->hwaci_5g_flag.value?2:4));j->software_stores++;if(++j->index==3)j->stage=HWACI_DONE;break;
    case HWACI_DONE:j->hwaci_completed++;helper_done(j);break;
    case PREEMPT:
        if(fact(j,AXD_NOISE_DATA,c->data_pointer,UINT32_MAX,1)){c->preempt_byte10=(uint8_t)j->enabled;j->software_stores++;j->stage=PREEMPT_HALF;}break;
    case PREEMPT_HALF:c->preempt_halfe=0;j->software_stores++;j->stage=PREEMPT_HW;break;
    case PREEMPT_HW:
        if(!j->enabled){if(j->index<2)phy(j,AXD_PHY_MODIFY,j->index?0x1040:0x141,j->index?1:0x800,0,PREEMPT_NEXT);else j->stage=PREEMPT_FILTER;}
        else if(j->index==0)packed(j,0x1040,0xf041,PREEMPT_NEXT);
        else if(j->index==1){
            if(j->core_index<c->hardware.phy.phy_core_count)phy(j,AXD_PHY_MODIFY,(uint16_t)(0x6f8+j->core_index*0x200),0x3ff,0x1df,PREEMPT_NEXT);
            else {j->index++;j->stage=PREEMPT_HW;}
        }else if(j->index<6){static const uint16_t s[]={0x141,0x1041,0x15e,0x1043},v[]={0xa00,0x2fff,0x3840,0x18};packed(j,s[j->index-2],v[j->index-2],PREEMPT_NEXT);}
        else j->stage=PREEMPT_GUARD;break;
    case PREEMPT_NEXT:if(j->enabled && j->index==1)j->core_index++;else j->index++;j->stage=PREEMPT_HW;break;
    case PREEMPT_GUARD:phy(j,AXD_PHY_READ,0x19e,0,0,PREEMPT_SET);break;
    case PREEMPT_SET:j->preempt_guard=j->phy_job.result;phy(j,AXD_PHY_MODIFY,0x19e,2,2,PREEMPT_FILTER);break;
    case PREEMPT_FILTER:filter(j,j->enabled?PREEMPT_RESTORE:PREEMPT_READ);break;
    case PREEMPT_RESTORE:phy(j,AXD_PHY_MODIFY,0x19e,2,j->preempt_guard&2u,PREEMPT_READ);break;
    case PREEMPT_READ:phy(j,AXD_PHY_READ,0x1040,0,0,PREEMPT_STORE);break;
    case PREEMPT_STORE:c->preempt_halfe=j->phy_job.result;j->software_stores++;j->stage=PREEMPT_DONE;break;
    case PREEMPT_DONE:j->preempt_completed++;helper_done(j);break;
    case FILTER:
        if(fact(j,AXD_NOISE_COMMON,c->common_pointer,UINT32_MAX,1) && fact(j,AXD_NOISE_CHANNEL,c->current_channel,UINT32_MAX,0)){
            if(!c->current_channel.value)dependency(j,AXD_NOISE_CHANNEL_GETSET);
            else {j->index=0;j->stage=j->enabled?FILTER_COMMON:FILTER_TAIL;}
        }break;
    case FILTER_COMMON:{static const uint16_t s[]={0x47d0,0x47d1,0x47d2,0x47d3,0x47d6,0x47d7,0x47d8,0x47d9},v[]={0xae00,0x4d00,0x1e,0x10,0x1a,0x1c,0x1e,10};
        if(j->index==8){j->index=0;j->stage=FILTER_TARGET;}else {unsigned n=j->index++;packed(j,s[n],v[n],FILTER_COMMON);}break;}
    case FILTER_TARGET:{static const uint16_t s[]={0x573a,0x5738,0x5739,0x47d0,0x47d1,0x47d8,0x47d9},v[]={0x2f00,0xffff,0xffff,28000,40000,0x12,0xe};
        if(j->index==7)j->stage=FILTER_TAIL;else {unsigned n=j->index++;packed(j,s[n],v[n],FILTER_TARGET);}break;}
    case FILTER_TAIL:
        if(!j->enabled || fact(j,AXD_NOISE_SHARED_MODE,c->shared_mode,UINT32_MAX,0)){
            j->postfilter_value=(uint16_t)(j->enabled?(c->shared_mode.value&0x20u?0x1f:0x1d):0);
            packed(j,0x47dc,j->postfilter_value,FILTER_SHM);
        }break;
    case FILTER_SHM:
        if(fact(j,AXD_NOISE_POSTFILTER_SHM,c->postfilter_shm,65535,0)){
            struct axd_object_request q={0};q.command=AXD_OBJECT_WRITE16;q.address=c->postfilter_shm.value;q.selector=0x10000;q.value=j->postfilter_value;
            if(c->channel.object.core!=c->hardware.phy.core || c->channel.object.minor!=c->hardware.phy.minor || axd_object_memory_begin(&j->object_job,&q,&c->channel.object))fail(j,AXD_NOISE_CHILD_ERROR);
            else {j->stage=RUN_OBJECT;j->after_child=FILTER_DONE;}
        }break;
    case FILTER_DONE:j->stage=j->after_filter;break;
    case ELNA:
        if(fact(j,AXD_NOISE_CHANSPEC,c->chanspec,65535,0) && fact(j,AXD_NOISE_RADIO_ID,c->radio.radio_id,65535,0))j->stage=c->radio.radio_id.value==0x50e0?ELNA_CORE:ELNA_DONE;break;
    case ELNA_CORE:
        if(j->core_index>=c->hardware.phy.phy_core_count)j->stage=ELNA_DONE;
        else if(j->index<5 || (j->index==5 && j->enabled)){
            static const uint16_t selectors[]={0x75,0x75,0x75,0x70,0x76,0x70},masks[]={0x10,0x20,0x40,0x40,0x400,0x780};
            uint16_t s=j->core_index<2?(uint16_t)(selectors[j->index]+j->core_index*0x200):0xffff;
            unsigned active=j->enabled && (!(c->chanspec.value&0xc000u) || (c->chanspec.value&0x4000u));
            uint16_t v=j->index==5?0x80:(uint16_t)(active?masks[j->index]:0);
            c->radio.hardware=c->hardware;
            if(axd_radio_register_begin(&j->radio_job,AXD_RADIO_CTL_MODIFY,s,masks[j->index],v,&c->radio))fail(j,AXD_NOISE_CHILD_ERROR);
            else {j->stage=RUN_RADIO;j->after_child=ELNA_NEXT;}
        }else {j->index=0;j->core_index++;}break;
    case ELNA_NEXT:j->index++;j->stage=ELNA_CORE;break;
    case ELNA_DONE:j->elnabypass_completed++;helper_done(j);break;
    case MCLIP_READ:case MCLIP_SY:{struct axd_d11_operation op={0};uint32_t reply=0;unsigned s=j->stage;
        op.kind=s==MCLIP_READ?AXD_D11_READ32:AXD_D11_FULL_BARRIER;op.offset=s==MCLIP_READ?0x120:0;
        if(!perform(&r,&op,&reply)){if(s==MCLIP_READ)j->main_control=reply;j->stage=s==MCLIP_READ?MCLIP_SY:MCLIP_CHOOSE;}break;}
    case MCLIP_CHOOSE:if(j->main_control&1u)mac(j,AXD_PSM_SUSPEND_MAIN,MCLIP_ENTER);else j->stage=MCLIP_GUARD;break;
    case MCLIP_ENTER:guard(j,AXD_PHY_ENTER,MCLIP_GUARD);break;
    case MCLIP_GUARD:phy(j,AXD_PHY_READ,0x19e,0,0,MCLIP_SET);break;
    case MCLIP_SET:j->guard_sample=j->phy_job.result;if(j->guard_sample&2u)j->stage=MCLIP_CORE;else phy(j,AXD_PHY_MODIFY,0x19e,2,2,MCLIP_CORE);break;
    case MCLIP_CORE:
        if(j->core_index>=c->hardware.phy.phy_core_count)j->stage=MCLIP_RESTORE;
        else if(fact(j,AXD_NOISE_CHANSPEC,c->chanspec,65535,0)){
            struct axd_phy_table_read_request q={0x144+j->core_index*0x20,6,8,8,j->actual,sizeof(j->actual)};
            unsigned k;const uint8_t *wanted=j->enabled && (c->chanspec.value&0xc000u)?mclip6:zero6;
            for(k=0;k<6;k++)j->desired[k]=wanted[k];
            if(axd_phy_table_read_begin(&j->reader_job,&q,&c->hardware))fail(j,AXD_NOISE_CHILD_ERROR);else {j->stage=RUN_READER;j->after_child=MCLIP_COMPARE;}
        }break;
    case MCLIP_COMPARE:j->stage=differs6(j->actual,j->desired)?MCLIP_WRITE:MCLIP_NEXT;break;
    case MCLIP_WRITE:{const struct axd_phy_table_write_request q={0x144+j->core_index*0x20,6,8,8,j->desired[4]?mclip6:zero6,6};
        if(axd_phy_table_write_begin(&j->writer_job,&q,&c->hardware))fail(j,AXD_NOISE_CHILD_ERROR);else {j->stage=RUN_WRITER;j->after_child=MCLIP_NEXT;}break;}
    case MCLIP_NEXT:j->core_index++;j->stage=MCLIP_CORE;break;
    case MCLIP_RESTORE:phy(j,AXD_PHY_MODIFY,0x19e,2,j->guard_sample&2u,MCLIP_EXIT);break;
    case MCLIP_EXIT:if(j->main_control&1u)guard(j,AXD_PHY_EXIT,MCLIP_ENABLE);else j->stage=MCLIP_DONE;break;
    case MCLIP_ENABLE:mac(j,AXD_PSM_ENABLE_MAIN,MCLIP_DONE);break;
    case MCLIP_DONE:j->mclip_completed++;helper_done(j);break;
    case FINISH:j->phase=AXD_NOISE_TRACE_COMPLETE;break;
    case RUN_PHY:case RUN_PACKED:case RUN_CHANNEL:case RUN_OBJECT:case RUN_RADIO:case RUN_READER:case RUN_WRITER:case RUN_MAC:case RUN_GUARD:child(j,&r);break;
    default:fail(j,AXD_NOISE_BAD_ARGUMENT);break;
    }
    return j->phase;
}
enum axd_phy_noise_phase axd_phy_noise_cancel(struct axd_phy_noise *j)
{
    if(!j)return AXD_NOISE_INVALID;
    if(j->phase==AXD_NOISE_RUNNING){j->phase=AXD_NOISE_CANCELLED;
        axd_phy_register_cancel(&j->phy_job);axd_phy_table_write_cancel(&j->packed_job);axd_phy_table_write_cancel(&j->writer_job);
        axd_phy_table_read_cancel(&j->reader_job);axd_phy_channel_init_cancel(&j->channel_job);axd_object_memory_cancel(&j->object_job);
        axd_radio_control_cancel(&j->radio_job);axd_psm_control_cancel(&j->mac_job);axd_phy_guard_cancel(&j->guard_job);
    }return j->phase;
}
