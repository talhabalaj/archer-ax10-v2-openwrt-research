/* SPDX-License-Identifier: MIT */
#include "phy_scheduler.h"
#include "job_reset.h"
#include <stddef.h>
enum stage { ENTRY, DISPATCH, CALLBACK, RETURN_CALLBACK, NEXT,
    RUN_PHY, RUN_TABLE, RUN_RADIO, RUN_MAC, RUN_SI, RUN_CHANNEL, RUN_NOISE, RUN_CHANSPEC, BW_RESULT,
    ANT_READ, ANT_SY, ANT_CHOOSE, ANT_SETTER, ANT_ENABLE,
    CACHE_ALLOC_REUSE, CACHE_REUSE_FLAG, CACHE_REUSE_POINTER,
    CACHE_TABLE, CACHE_TABLE_BYTES, CACHE_ALLOC_TABLE, CACHE_USED, CACHE_POINTER,
    TPC_MIN, TPC_MAX, TPC_NEXT, RX_CLEAR, RX_POINTER, RX_CHANNEL,
    HIRSSI_FIRST, HIRSSI_SECOND, HIRSSI_THIRD, HIRSSI_FOURTH,
    NOISE_MODE_STORE, NOISE_MODE_CHILD, NOISE_MODE_RESULT, SLICE, SLICE_STORE, FINISH };
static int callback_valid(enum axd_phy_init_callback c)
{
    return c==AXD_INIT_UNKNOWN || c==AXD_INIT_CACHE || (c>=AXD_INIT_ANALOG && c<=AXD_INIT_CHANSPEC) ||
        c==AXD_INIT_PAPD || c==AXD_INIT_RXGCRS || c==AXD_INIT_BTCX || c==AXD_INIT_HIRSSI;
}
static int registry_valid(const struct axd_phy_registry *r)
{
    unsigned i;
    if(!r || r->init_count>r->total_count || r->total_count>AXD_PHY_REGISTRY_CAPACITY)return 0;
    for(i=0;i<r->init_count;i++)if(!callback_valid(r->entries[i].callback) ||
        (i && r->entries[i-1].priority>r->entries[i].priority))return 0;
    return 1;
}
void axd_phy_registry_clear(struct axd_phy_registry *r)
{if(r){static const struct axd_phy_registry empty={0};*r=empty;}}
int axd_phy_registry_add(struct axd_phy_registry *r,enum axd_phy_init_callback callback,
                           const struct axd_wake_u32 *context,uint32_t priority)
{
    struct axd_wake_u32 copy;unsigned at,i;
    if(!r || !context)return -1;
    copy=*context;
    if(!registry_valid(r) || !callback_valid(callback) || priority>255u)return -1;
    if(r->total_count==AXD_PHY_REGISTRY_CAPACITY)return -22;
    for(at=0;at<r->init_count && r->entries[at].priority<priority;at++){}
    for(i=r->total_count;i>at;i--)r->entries[i]=r->entries[i-1];
    r->entries[at].callback=callback;r->entries[at].context=copy;r->entries[at].priority=(uint8_t)priority;
    r->init_count++;r->total_count++;return 0;
}
int axd_phy_registry_target51(struct axd_phy_registry *r,const struct axd_wake_u32 contexts[AXD_PHY_TARGET_INIT_COUNT])
{
    static const enum axd_phy_init_callback order[]={AXD_INIT_CACHE,AXD_INIT_ANALOG,AXD_INIT_RADIO,
        AXD_INIT_TABLE,AXD_INIT_TPC,AXD_INIT_RADAR,AXD_INIT_ANTDIV,AXD_INIT_NOISE_RESET,
        AXD_INIT_NOISE,AXD_INIT_BTCX,AXD_INIT_PAPD,AXD_INIT_BANDWIDTH,AXD_INIT_CHANSPEC,
        AXD_INIT_RXGCRS,AXD_INIT_HIRSSI,AXD_INIT_TYPE};
    static const unsigned sorted_index[]={0,1,5,6,7,8,9,4,10,14,12,2,11,13,15,3};
    struct axd_wake_u32 copy[AXD_PHY_TARGET_INIT_COUNT];struct axd_phy_registry result;unsigned i;
    if(!r || !contexts)return -1;
    for(i=0;i<AXD_PHY_TARGET_INIT_COUNT;i++)copy[i]=contexts[i];
    axd_phy_registry_clear(&result);
    for(i=0;i<AXD_PHY_TARGET_INIT_COUNT;i++)
        if(axd_phy_registry_add(&result,order[i],&copy[sorted_index[i]],(uint32_t)order[i]))return -1;
    *r=result;return 0;
}
static void fail(struct axd_phy_scheduler *j,enum axd_phy_scheduler_error e)
{j->phase=AXD_PS_FAILED;j->error=e;}
static void dependency(struct axd_phy_scheduler *j,enum axd_phy_scheduler_dependency d)
{j->phase=AXD_PS_DEPENDENCY;j->dependency=d;}
static int known(struct axd_phy_scheduler *j,struct axd_wake_u32 value,uint32_t limit,unsigned nonnull)
{
    j->metadata_accesses++;
    if(!value.known)dependency(j,AXD_PS_METADATA);
    else if(value.known!=1)fail(j,AXD_PS_BAD_ARGUMENT);
    else if(value.value>limit)fail(j,AXD_PS_BOUNDS);
    else if(nonnull && !value.value)fail(j,AXD_PS_BINDING);
    else return 1;
    return 0;
}
static int byte(struct axd_phy_scheduler *j,struct axd_wake_u8 value)
{const struct axd_wake_u32 wide={value.value,value.known};return known(j,wide,255,0);}
static void complete(struct axd_phy_scheduler *j)
{j->callback_result=0;j->stage=RETURN_CALLBACK;}
struct relay {struct axd_phy_scheduler *j;axd_phy_scheduler_io_fn io;void *opaque;};
static int perform(struct relay *r,const struct axd_phy_scheduler_operation *o,struct axd_phy_scheduler_reply *reply)
{
    struct axd_phy_scheduler *j=r->j;
    if(!r->io){fail(j,AXD_PS_BAD_ARGUMENT);return -1;}
    j->attempted_ops++;
    if(r->io(r->opaque,o,reply)){fail(j,AXD_PS_IO_ERROR);return -1;}
    j->completed_ops++;return 0;
}
static int d11_io(void *opaque,const struct axd_d11_operation *d,uint32_t *value)
{
    struct relay *r=opaque;struct axd_phy_scheduler *j=r->j;
    struct axd_phy_scheduler_operation o={0};struct axd_phy_scheduler_reply reply={0};int result;
    if(j->stage==RUN_PHY)j->context.table.hardware.phy=j->phy_job.context;
    else if(j->stage==RUN_MAC)j->context.table.hardware.mac=j->mac_job.context;
    else if(j->stage==RUN_RADIO){j->context.radio_power=j->radio_job.context;j->context.radio=j->radio_job.context.radio;j->context.table.hardware=j->radio_job.context.radio.hardware;}
    else if(j->stage==RUN_CHANNEL){j->context.channel=j->channel_job.context;j->context.table.hardware=j->channel_job.context.hardware;}
    else if(j->stage==RUN_NOISE){j->context.noise=j->noise_job.context;j->context.channel=j->noise_job.context.channel;j->context.radio=j->noise_job.context.radio;j->context.table.hardware=j->noise_job.context.hardware;}
    o.path=AXD_PS_D11;o.d11=*d;result=perform(r,&o,&reply);*value=reply.value;return result;
}
static int table_io(void *opaque,const struct axd_phy_init_operation *a,struct axd_phy_init_reply *value)
{
    struct relay *r=opaque;struct axd_phy_scheduler_operation o={0};struct axd_phy_scheduler_reply reply={0};int result;
    r->j->context.table=r->j->table_job.context;
    o.path=a->path==AXD_PI_D11?AXD_PS_D11:AXD_PS_CONTROL;o.d11=a->d11;o.control=a->control;
    result=perform(r,&o,&reply);value->value=reply.value;value->control=reply.control;return result;
}
static int si_io(void *opaque,const struct axd_si_core_operation *s,uint32_t *value)
{
    struct relay *r=opaque;struct axd_phy_scheduler_operation o={0};struct axd_phy_scheduler_reply reply={0};int result;
    o.path=AXD_PS_SI;o.si=*s;result=perform(r,&o,&reply);*value=reply.value;return result;
}
static void chanspec_publish(struct axd_phy_scheduler *j)
{
    struct axd_phy_scheduler_context *c=&j->context;
    c->chanspec_init=j->chanspec_job.context;c->table.hardware=j->chanspec_job.context.hardware;
    c->radio=j->chanspec_job.context.radio;c->radio.hardware=c->table.hardware;
    /* PI396 is one word: callback4 read it, callback13's bandwidth set writes it. */
    if(j->chanspec_job.bwstate_stored)c->bandwidth_state=(struct axd_wake_u32){j->chanspec_job.context.bwstate396,1};
}
static int chanspec_io(void *opaque,const struct axd_phy_chanspec_operation *a,uint32_t *value)
{
    struct relay *r=opaque;struct axd_phy_scheduler_operation o={0};struct axd_phy_scheduler_reply reply={0};int result;
    chanspec_publish(r->j);
    o.path=a->path==AXD_CS_D11?AXD_PS_D11:AXD_PS_SI;o.d11=a->d11;o.si=a->si;
    result=perform(r,&o,&reply);*value=reply.value;return result;
}
static void finish_child(struct axd_phy_scheduler *j,unsigned done,unsigned missing,unsigned cancelled)
{
    if(j->phase!=AXD_PS_RUNNING)return;
    if(done)j->stage=j->after_child;
    else if(missing)dependency(j,j->stage==RUN_CHANSPEC?AXD_PS_CHANSPEC_CHILD:j->stage==RUN_NOISE?AXD_PS_NOISE_MODE:j->stage==RUN_CHANNEL?AXD_PS_RADAR_CHILD:j->stage==RUN_RADIO?AXD_PS_RADIO_CHILD:
        j->stage==RUN_TABLE?AXD_PS_TABLE_CHILD:j->stage==RUN_MAC?AXD_PS_MAC_CHILD:AXD_PS_SI_CHILD);
    else if(cancelled)j->phase=AXD_PS_CANCELLED;
    else fail(j,AXD_PS_CHILD_ERROR);
}
static void run_child(struct axd_phy_scheduler *j,struct relay *r)
{
    if(j->stage==RUN_PHY){
        enum axd_phy_phase p=axd_phy_register_step(&j->phy_job,d11_io,r);
        j->context.table.hardware.phy=j->phy_job.context;
        if(p!=AXD_PHY_RUNNING)finish_child(j,p==AXD_PHY_TRACE_COMPLETE,p==AXD_PHY_DEPENDENCY,p==AXD_PHY_CANCELLED);
    }else if(j->stage==RUN_TABLE){
        enum axd_phy_init_phase p=axd_phy_init_step(&j->table_job,table_io,r);
        j->context.table=j->table_job.context;
        if(p!=AXD_PI_RUNNING)finish_child(j,p==AXD_PI_TRACE_COMPLETE,p==AXD_PI_DEPENDENCY,p==AXD_PI_CANCELLED);
    }else if(j->stage==RUN_RADIO){
        enum axd_radio_power_phase p=axd_radio_power_step(&j->radio_job,d11_io,r);
        j->context.radio_power=j->radio_job.context;j->context.radio=j->radio_job.context.radio;j->context.table.hardware=j->radio_job.context.radio.hardware;
        if(p!=AXD_RPOWER_RUNNING)finish_child(j,p==AXD_RPOWER_TRACE_COMPLETE,p==AXD_RPOWER_DEPENDENCY,p==AXD_RPOWER_CANCELLED);
    }else if(j->stage==RUN_CHANNEL){
        enum axd_phy_channel_phase p=axd_phy_channel_init_step(&j->channel_job,d11_io,r);
        j->context.channel=j->channel_job.context;j->context.table.hardware=j->channel_job.context.hardware;
        if(p!=AXD_CHANNEL_RUNNING)finish_child(j,p==AXD_CHANNEL_TRACE_COMPLETE,p==AXD_CHANNEL_DEPENDENCY,p==AXD_CHANNEL_CANCELLED);
    }else if(j->stage==RUN_NOISE){
        enum axd_phy_noise_phase p=axd_phy_noise_step(&j->noise_job,d11_io,r);
        j->context.noise=j->noise_job.context;j->context.channel=j->noise_job.context.channel;j->context.radio=j->noise_job.context.radio;j->context.table.hardware=j->noise_job.context.hardware;
        if(p!=AXD_NOISE_RUNNING)finish_child(j,p==AXD_NOISE_TRACE_COMPLETE,p==AXD_NOISE_DEPENDENCY,p==AXD_NOISE_CANCELLED);
    }else if(j->stage==RUN_CHANSPEC){
        enum axd_phy_chanspec_phase p=axd_phy_chanspec_step(&j->chanspec_job,chanspec_io,r);
        chanspec_publish(j);
        if(p!=AXD_CS_RUNNING)finish_child(j,p==AXD_CS_TRACE_COMPLETE,p==AXD_CS_DEPENDENCY,p==AXD_CS_CANCELLED);
    }else if(j->stage==RUN_MAC){
        enum axd_psm_phase p=axd_psm_control_step(&j->mac_job,d11_io,r);
        j->context.table.hardware.mac=j->mac_job.context;
        if(p!=AXD_PSM_RUNNING)finish_child(j,p==AXD_PSM_DONE,p==AXD_PSM_DEPENDENCY,p==AXD_PSM_CANCELLED);
    }else{
        enum axd_si_core_phase p=axd_si_core_step(&j->si_job,si_io,r);
        if(p!=AXD_SICO_RUNNING){j->bw_flags=j->si_job.result;finish_child(j,p==AXD_SICO_DONE,p==AXD_SICO_DEPENDENCY,p==AXD_SICO_CANCELLED);}
    }
}
static void mac(struct axd_phy_scheduler *j,enum axd_psm_command command,unsigned next)
{
    struct axd_phy_table_context *h=&j->context.table.hardware;
    if(!h->mac_context_valid){dependency(j,AXD_PS_MAC_CHILD);return;}
    if(h->mac_context_valid!=1){fail(j,AXD_PS_BAD_ARGUMENT);return;}
    if(axd_psm_control_begin_policy(&j->mac_job,command,&h->mac,&h->policy)){fail(j,AXD_PS_CHILD_ERROR);return;}
    j->after_child=next;j->stage=RUN_MAC;
}
/* Current selected SI core mapping, as used by the actual core-flags helpers. */
static int si_mapping(struct axd_phy_scheduler *j,struct axd_si_core_context *c)
{
    struct axd_si_pmu_gpio_context *s=&j->context.table.control;static const struct axd_si_core_context empty={0};
    *c=empty;
    if(j->context.table.control_state_known==0){dependency(j,AXD_PS_SI_CHILD);return 0;}
    if(j->context.table.control_state_known!=1){fail(j,AXD_PS_BAD_ARGUMENT);return 0;}
    c->interconnect=s->si.interconnect;c->chip_id=j->context.table.chip_id;
    c->primary_wrapper=s->state.primary_wrapper;c->wrapper_known=1;
    if(c->interconnect==1 || c->interconnect==3 || c->interconnect==4){
        if(!s->si.catalog || s->state.current_index>=s->si.core_count || s->state.current_index>=s->si.catalog_length){dependency(j,AXD_PS_SI_CHILD);return 0;}
        c->core_id=s->si.catalog[s->state.current_index].core_id;
    }
    return 1;
}
static void bandwidth_check(struct axd_phy_scheduler *j)
{
    struct axd_si_core_context c;
    if(!si_mapping(j,&c))return;
    if(axd_si_core_begin(&j->si_job,AXD_SICO_FLAGS,0,0,&c)){fail(j,AXD_PS_CHILD_ERROR);return;}
    j->after_child=BW_RESULT;j->stage=RUN_SI;
}
static void callback(struct axd_phy_scheduler *j)
{
    struct axd_phy_scheduler_context *c=&j->context;
    switch(j->current){
    case AXD_INIT_CACHE:
        if(known(j,c->cache.reuse_state,255,0)){
            if(c->cache.reuse_state.value==255){
                if(known(j,c->cache.reuse_bytes,65535,0))j->stage=c->cache.reuse_bytes.value?CACHE_ALLOC_REUSE:CACHE_TABLE;
            }else j->stage=CACHE_TABLE;
        }break;
    case AXD_INIT_ANALOG:
        if(axd_phy_register_begin(&j->phy_job,AXD_PHY_ANALOG_RESET,0,0,0,&c->table.hardware.phy))fail(j,AXD_PS_CHILD_ERROR);
        else {j->stage=RUN_PHY;j->after_child=RETURN_CALLBACK;}break;
    case AXD_INIT_BANDWIDTH:
        if(known(j,c->bandwidth_state,65535,0) && known(j,c->chanspec,65535,0)){
            if(c->bandwidth_state.value!=(c->chanspec.value&0x3800u))dependency(j,AXD_PS_BANDWIDTH_SET);
            else bandwidth_check(j);
        }break;
    case AXD_INIT_TYPE:case AXD_INIT_PAPD:complete(j);break;
    case AXD_INIT_NOISE_RESET:
        if(known(j,c->flags3b0,UINT32_MAX,0)){
            if(c->flags3b0.value&0x802u)complete(j);
            else if(known(j,c->chanspec,65535,0)){
                struct axd_wake_u32 mode=c->chanspec.value&0xc000u?c->interference5g:c->interference2g;
                if(known(j,mode,UINT32_MAX,0)){j->selected_mode=mode.value;j->stage=NOISE_MODE_STORE;}
            }
        }break;
    case AXD_INIT_RADIO:
        c->radio.hardware=c->table.hardware;
        c->radio_power.radio=c->radio;
        if(axd_radio_power_begin(&j->radio_job,AXD_RPOWER_ON_REGISTERED,0,&c->radio_power))fail(j,AXD_PS_CHILD_ERROR);
        else {j->stage=RUN_RADIO;j->after_child=RETURN_CALLBACK;}break;
    case AXD_INIT_TABLE:
        if(axd_phy_init_begin(&j->table_job,&c->table))fail(j,AXD_PS_CHILD_ERROR);
        else {j->stage=RUN_TABLE;j->after_child=RETURN_CALLBACK;}break;
    case AXD_INIT_TPC:
        if(c->phy_core_count_known==0)dependency(j,AXD_PS_METADATA);
        else if(c->phy_core_count_known!=1)fail(j,AXD_PS_BAD_ARGUMENT);
        else if(c->table.hardware.phy.phy_core_count>4)fail(j,AXD_PS_BOUNDS);
        else if(!c->table.hardware.phy.phy_core_count)complete(j);
        else {j->core_index=0;j->stage=TPC_MIN;}break;
    case AXD_INIT_RADAR:
        if(known(j,c->chanspec,65535,0)){
            if(!(c->chanspec.value&0x4000u))complete(j);
            else if(!c->radar_enabled.known)dependency(j,AXD_PS_RADAR_CHILD);
            else if(byte(j,c->radar_enabled)){
                c->channel.hardware=c->table.hardware;c->channel.chanspec=c->chanspec;
                if(axd_phy_channel_init_begin(&j->channel_job,AXD_CHANNEL_RADAR_INIT,c->radar_enabled.value,&c->channel))fail(j,AXD_PS_CHILD_ERROR);
                else {j->stage=RUN_CHANNEL;j->after_child=RETURN_CALLBACK;}
            }
        }break;
    case AXD_INIT_ANTDIV:
        if(byte(j,c->clocks)){
            if(!c->clocks.value){j->antdiv_result=-11;complete(j);}else j->stage=ANT_READ;
        }break;
    case AXD_INIT_NOISE:
        if(known(j,c->flags3b0,UINT32_MAX,0)){
            if(c->flags3b0.value&0x802u)complete(j);
            else if(byte(j,c->forced_interference) && known(j,c->chanspec,65535,0)){
                struct axd_wake_u32 mode;
                if(!c->forced_interference.value)mode=c->chanspec.value&0xc000u?c->interference5g:c->interference2g;
                else mode=c->chanspec.value&0xc000u?c->interference_override5g:c->interference_override2g;
                if(known(j,mode,UINT32_MAX,0)){
                    j->selected_mode=mode.value;
                    if(c->forced_interference.value && (c->chanspec.value&0xc000u) && j->selected_mode>1)j->selected_mode=0;
                    j->stage=NOISE_MODE_STORE;
                }
            }
        }break;
    case AXD_INIT_CHANSPEC:{
        struct axd_phy_chanspec_context *k=&c->chanspec_init;
        if(!si_mapping(j,&k->si))break;
        k->si_known=1;k->hardware=c->table.hardware;k->radio=c->radio;k->radio.hardware=c->table.hardware;k->chanspec=c->chanspec;k->bmac_chanspec=c->hardware_chanspec;
        if(axd_phy_chanspec_begin(&j->chanspec_job,k))fail(j,AXD_PS_CHILD_ERROR);
        else {j->stage=RUN_CHANSPEC;j->after_child=RETURN_CALLBACK;}
        break;}
    case AXD_INIT_RXGCRS:
        if(byte(j,c->rxgcrs_enabled)){if(c->rxgcrs_enabled.value)j->stage=RX_CLEAR;else complete(j);}break;
    case AXD_INIT_BTCX:c->btcx_byte18=255;j->stage=RETURN_CALLBACK;break;
    case AXD_INIT_HIRSSI:j->stage=HIRSSI_FIRST;break;
    default:dependency(j,AXD_PS_UNKNOWN_CALLBACK);break;
    }
}
int axd_phy_scheduler_begin(struct axd_phy_scheduler *j,const struct axd_phy_registry *registry,const struct axd_phy_scheduler_context *context)
{
    const struct axd_phy_scheduler_context *c;
    if(!j)return -1;
    if(axd_job_overlaps(j,sizeof(*j),registry,sizeof(*registry),&j->registry))registry=NULL;
    else if(registry && registry!=&j->registry)j->registry=*registry;
    if(axd_job_overlaps(j,sizeof(*j),context,sizeof(*context),&j->context))context=NULL;
    else if(context && context!=&j->context)j->context=*context;
    axd_job_zero_except(j,sizeof(*j),offsetof(struct axd_phy_scheduler,registry),registry?sizeof(j->registry):0,
        offsetof(struct axd_phy_scheduler,context),context?sizeof(j->context):0);
    fail(j,AXD_PS_BAD_ARGUMENT);
    if(!registry || !context || !registry_valid(&j->registry))return -1;
    c=&j->context;
    if(c->table.hardware.phy.core!=130 || c->table.hardware.phy.minor>1 ||
        c->table.hardware.phy.phy_type!=0xb || c->table.hardware.phy.phy_revision!=51 || c->table.chip_id!=0xf6ca){fail(j,AXD_PS_UNSUPPORTED);return -1;}
    j->phase=AXD_PS_RUNNING;j->error=AXD_PS_NO_ERROR;return 0;
}
static void allocate(struct axd_phy_scheduler *j,struct relay *r,uint32_t bytes,unsigned next)
{
    struct axd_phy_scheduler_operation o={0};struct axd_phy_scheduler_reply reply={0};
    if(!j->context.allocator.known){dependency(j,AXD_PS_ALLOCATOR);return;}
    if(!known(j,j->context.allocator,UINT32_MAX,1))return;
    o.path=AXD_PS_ALLOCATE_ZERO;o.allocator=j->context.allocator.value;o.bytes=bytes;
    if(!perform(r,&o,&reply)){j->allocation_result=reply.value;j->allocation_observed++;j->stage=next;}
}
enum axd_phy_scheduler_phase axd_phy_scheduler_step(struct axd_phy_scheduler *j,axd_phy_scheduler_io_fn io,void *opaque)
{
    struct relay r={j,io,opaque};struct axd_phy_scheduler_context *c;
    if(!j)return AXD_PS_INVALID;
    if(j->phase!=AXD_PS_RUNNING)return j->phase;
    c=&j->context;
    switch(j->stage){
    case ENTRY:j->stage=j->registry.init_count?DISPATCH:SLICE;break;
    case DISPATCH:
        j->current=j->registry.entries[j->index].callback;j->callback_result=0;j->substage=0;
        if(j->current==AXD_INIT_UNKNOWN)dependency(j,AXD_PS_UNKNOWN_CALLBACK);
        else if(j->current==AXD_INIT_PAPD || known(j,j->registry.entries[j->index].context,UINT32_MAX,1))j->stage=CALLBACK;
        break;
    case CALLBACK:callback(j);break;
    case RETURN_CALLBACK:
        if(j->callback_result){j->return_value=j->callback_result;j->return_valid=1;j->phase=AXD_PS_TRACE_COMPLETE;}
        else {j->callbacks_completed++;j->stage=NEXT;}break;
    case NEXT:j->index++;j->stage=j->index<j->registry.init_count?DISPATCH:SLICE;break;
    case RUN_PHY:case RUN_TABLE:case RUN_RADIO:case RUN_MAC:case RUN_SI:case RUN_CHANNEL:case RUN_NOISE:case RUN_CHANSPEC:run_child(j,&r);break;
    case BW_RESULT:
        if(known(j,c->hardware_chanspec,65535,0)){
            uint32_t flags=0;
            switch(c->hardware_chanspec.value&0x3800u){case 0x1000:flags=0x40;break;case 0x1800:flags=0x80;break;
            case 0x2000:case 0x3000:flags=0xc0;break;case 0x2800:flags=0x800;break;default:break;}
            j->bandwidth_check_result=(j->bw_flags&0x8c0u)==flags?0:-20;complete(j);
        }break;
    case CACHE_ALLOC_REUSE:allocate(j,&r,c->cache.reuse_bytes.value,CACHE_REUSE_FLAG);break;
    case CACHE_REUSE_FLAG:
        if(!j->allocation_result)dependency(j,AXD_PS_FATAL_ALLOCATION);
        else {c->cache.reuse_state=(struct axd_wake_u32){0,1};j->stage=CACHE_REUSE_POINTER;}break;
    case CACHE_REUSE_POINTER:c->cache.reuse_buffer=(struct axd_wake_u32){j->allocation_result,1};j->stage=CACHE_TABLE;break;
    case CACHE_TABLE:
        if(known(j,c->cache.table_pointer,UINT32_MAX,0)){
            if(c->cache.table_pointer.value)complete(j);
            else if(known(j,c->cache.entries,65535,0))j->stage=CACHE_TABLE_BYTES;
        }break;
    case CACHE_TABLE_BYTES:c->cache.table_bytes=c->cache.entries.value<<2;j->stage=CACHE_ALLOC_TABLE;break;
    case CACHE_ALLOC_TABLE:allocate(j,&r,c->cache.table_bytes,CACHE_USED);break;
    case CACHE_USED:
        if(!j->allocation_result)dependency(j,AXD_PS_FATAL_ALLOCATION);
        else {c->cache.table_used=0;j->stage=CACHE_POINTER;}break;
    case CACHE_POINTER:c->cache.table_pointer=(struct axd_wake_u32){j->allocation_result,1};complete(j);break;
    case NOISE_MODE_STORE:
        c->interference_current=(struct axd_wake_u32){j->selected_mode,1};
        if(j->current==AXD_INIT_NOISE_RESET)complete(j);else j->stage=NOISE_MODE_CHILD;break;
    case NOISE_MODE_CHILD:
        c->noise.hardware=c->table.hardware;c->noise.channel=c->channel;c->noise.radio=c->radio;
        c->noise.chanspec=c->chanspec;c->noise.shared_mode=c->interference_current;
        if(axd_phy_noise_begin(&j->noise_job,AXD_NOISE_INIT_MODE,j->selected_mode,&c->noise))fail(j,AXD_PS_CHILD_ERROR);
        else {j->stage=RUN_NOISE;j->after_child=NOISE_MODE_RESULT;}break;
    case NOISE_MODE_RESULT:c->noise_mode378=j->selected_mode;complete(j);break;
    case TPC_MIN:c->tpc_min[j->core_index]=0x32;j->stage=TPC_MAX;break;
    case TPC_MAX:c->tpc_max[j->core_index]=c->tpc_min[j->core_index];j->stage=TPC_NEXT;break;
    case TPC_NEXT:
        if(++j->core_index<c->table.hardware.phy.phy_core_count)j->stage=TPC_MIN;else complete(j);break;
    case ANT_READ:case ANT_SY:{
        struct axd_phy_scheduler_operation o={0};struct axd_phy_scheduler_reply reply={0};unsigned stage=j->stage;
        o.path=AXD_PS_D11;o.d11.kind=stage==ANT_READ?AXD_D11_READ32:AXD_D11_FULL_BARRIER;
        if(stage==ANT_READ)o.d11.offset=0x120;
        if(!perform(&r,&o,&reply)){if(stage==ANT_READ)j->mac_sample=reply.value;j->stage=stage==ANT_READ?ANT_SY:ANT_CHOOSE;}break;}
    case ANT_CHOOSE:if(j->mac_sample&1u)mac(j,AXD_PSM_SUSPEND_MAIN,ANT_SETTER);else j->stage=ANT_SETTER;break;
    case ANT_SETTER:if(j->mac_sample&1u)j->stage=ANT_ENABLE;else {j->antdiv_result=0;complete(j);}break;
    case ANT_ENABLE:j->antdiv_result=0;mac(j,AXD_PSM_ENABLE_MAIN,RETURN_CALLBACK);break;
    case RX_CLEAR:c->rxgcrs_bytea8=0;j->stage=RX_POINTER;break;
    case RX_POINTER:if(known(j,c->channel_state_pointer,UINT32_MAX,1))j->stage=RX_CHANNEL;break;
    case RX_CHANNEL:if(known(j,c->chanspec,65535,0)){c->channel_state_chanspec=(uint16_t)c->chanspec.value;complete(j);}break;
    case HIRSSI_FIRST:c->hirssi_half18=65535;j->stage=HIRSSI_SECOND;break;
    case HIRSSI_SECOND:c->hirssi_half1a=65535;j->stage=HIRSSI_THIRD;break;
    case HIRSSI_THIRD:c->hirssi_byte16=0;j->stage=HIRSSI_FOURTH;break;
    case HIRSSI_FOURTH:c->hirssi_byte17=0;complete(j);break;
    case SLICE:
        if(known(j,c->slice_unit,UINT32_MAX,0) && known(j,c->slice_up,UINT32_MAX,0))j->stage=SLICE_STORE;
        break;
    case SLICE_STORE:{unsigned shift=(uint8_t)c->slice_unit.value;c->slice_up.value|=shift<32?UINT32_C(1)<<shift:0;j->slice_up_stored=1;j->stage=FINISH;break;}
    case FINISH:j->return_value=0;j->return_valid=1;j->phase=AXD_PS_TRACE_COMPLETE;break;
    default:fail(j,AXD_PS_BAD_ARGUMENT);break;
    }
    return j->phase;
}
enum axd_phy_scheduler_phase axd_phy_scheduler_cancel(struct axd_phy_scheduler *j)
{
    if(!j)return AXD_PS_INVALID;
    if(j->phase==AXD_PS_RUNNING){
        j->phase=AXD_PS_CANCELLED;axd_phy_init_cancel(&j->table_job);axd_phy_register_cancel(&j->phy_job);
        axd_psm_control_cancel(&j->mac_job);axd_radio_power_cancel(&j->radio_job);axd_si_core_cancel(&j->si_job);
        axd_phy_channel_init_cancel(&j->channel_job);
        axd_phy_noise_cancel(&j->noise_job);
        axd_phy_chanspec_cancel(&j->chanspec_job);
    }
    return j->phase;
}
