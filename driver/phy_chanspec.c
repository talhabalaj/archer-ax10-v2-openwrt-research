/* SPDX-License-Identifier: MIT */
#include "phy_chanspec.h"
enum stage { P_DATA, P_AC, P_RADAR, P_RADAR_INFO, RADIO_SET, MODULE0,
    SYNC_READ, SYNC_SY, SYNC_SUSPEND, SYNC_A, SYNC_B, SYNC_173, SYNC_C, SYNC_C80,
    SYNC_40, SYNC_46A, SYNC_GUARD_READ, SYNC_GUARD_SET, SYNC_GUARD_RESTORE,
    SYNC_RADIO, SYNC_16B, SYNC_ENABLE,
    SETUP_READ, SETUP_SET, CORE_MODE, CORE_READ, CORE_SHARED, CORE_BITMAP,
    TEMP, TEMP_STATE, TEMP_AGAIN, BAND, BW_PHY, BW_TYPE, BW_RESET, BWINIT,
    AC_BWINIT, CHIP, SETUP_DELAY, SETUP_TAIL, MODULE1, MODULE2, RADIO20704,
    R_TABLE, R_LOGEN, R_LOGEN_DELAY, R_LOGEN_CLEAR, R_BAND, R_88A, R_9F6, R_FREQ,
    R_CORE, R_TAIL, R_2D, R_PLL,
    CCA_READ, CCA_SY, CCA_SHORT, CCA_FGC_ON, CCA_D1, CCA_ST6, CCA_W6, CCA_D2,
    CCA_TOGGLE_SET, CCA_D3, CCA_TOGGLE_CLEAR, CCA_D10, CCA_ST2, CCA_W2,
    CCA_FGC_OFF, CCA_D4, CCA_ST_RESTORE, CCA_W_RESTORE, CCA_TAIL,
    RUN_PHY, RUN_RADIO, RUN_MAC, RUN_SI, RUN_RAW };
static void fail(struct axd_phy_chanspec *j,enum axd_phy_chanspec_error e){j->phase=AXD_CS_FAILED;j->error=e;}
static void dependency(struct axd_phy_chanspec *j,enum axd_phy_chanspec_dependency d){j->phase=AXD_CS_DEPENDENCY;j->dependency=d;}
static int fact(struct axd_phy_chanspec *j,enum axd_phy_chanspec_field field,struct axd_wake_u32 f,uint32_t limit,unsigned nonnull)
{
    j->last_field=field;j->metadata_accesses++;
    if(!f.known)dependency(j,AXD_CS_METADATA);
    else if(f.known!=1)fail(j,AXD_CS_BAD_ARGUMENT);
    else if(f.value>limit)fail(j,AXD_CS_BOUNDS);
    else if(nonnull && !f.value)fail(j,AXD_CS_BINDING);
    else return 1;return 0;
}
/* A canonical alias must be known, nonnull and equal to its owner. */
static int alias(struct axd_phy_chanspec *j,enum axd_phy_chanspec_field field,struct axd_wake_u32 f,uint32_t owner)
{
    if(!fact(j,field,f,UINT32_MAX,1))return 0;
    if(f.value!=owner){fail(j,AXD_CS_BINDING);return 0;}return 1;
}
struct relay {struct axd_phy_chanspec *j;axd_phy_chanspec_io_fn io;void *opaque;};
static void publish(struct axd_phy_chanspec *j)
{
    struct axd_phy_chanspec_context *c=&j->context;
    if(j->stage==RUN_PHY)c->hardware.phy=j->phy_job.context;
    else if(j->stage==RUN_RADIO){c->radio=j->radio_job.context;c->hardware=j->radio_job.context.hardware;}
    else if(j->stage==RUN_MAC)c->hardware.mac=j->mac_job.context;
    c->radio.hardware=c->hardware;
}
static int send(struct relay *r,const struct axd_phy_chanspec_operation *op,uint32_t *reply)
{
    struct axd_phy_chanspec *j=r->j;publish(j);
    if(!r->io){fail(j,AXD_CS_BAD_ARGUMENT);return -1;}
    j->attempted_ops++;if(r->io(r->opaque,op,reply)){fail(j,AXD_CS_IO_ERROR);return -1;}
    j->completed_ops++;return 0;
}
static int d11(void *opaque,const struct axd_d11_operation *d,uint32_t *reply)
{struct axd_phy_chanspec_operation op={0};op.path=AXD_CS_D11;op.d11=*d;return send(opaque,&op,reply);}
static int si(void *opaque,const struct axd_si_core_operation *s,uint32_t *reply)
{struct axd_phy_chanspec_operation op={0};op.path=AXD_CS_SI;op.si=*s;return send(opaque,&op,reply);}
static void phy(struct axd_phy_chanspec *j,enum axd_phy_command command,uint16_t selector,uint16_t mask,uint16_t value,unsigned next)
{if(axd_phy_register_begin(&j->phy_job,command,selector,mask,value,&j->context.hardware.phy))fail(j,AXD_CS_CHILD_ERROR);else {j->stage=RUN_PHY;j->after_child=next;}}
static void radio(struct axd_phy_chanspec *j,uint16_t selector,uint16_t mask,uint16_t value,unsigned next)
{
    j->context.radio.hardware=j->context.hardware;
    if(axd_radio_register_begin(&j->radio_job,AXD_RADIO_CTL_MODIFY,selector,mask,value,&j->context.radio))fail(j,AXD_CS_CHILD_ERROR);
    else {j->stage=RUN_RADIO;j->after_child=next;}
}
static void mac(struct axd_phy_chanspec *j,enum axd_psm_command command,unsigned next)
{
    struct axd_phy_table_context *h=&j->context.hardware;
    if(!h->mac_context_valid){dependency(j,AXD_CS_MAC_CHILD);return;}
    if(h->mac_context_valid!=1 || h->mac.core!=h->phy.core || h->mac.minor!=h->phy.minor ||
       axd_psm_control_begin_policy(&j->mac_job,command,&h->mac,&h->policy))fail(j,AXD_CS_CHILD_ERROR);
    else {j->stage=RUN_MAC;j->after_child=next;}
}
/* Actual si_core_cflags on the caller-selected D11 mapping. */
static void cflags(struct axd_phy_chanspec *j,uint32_t mask,uint32_t value,unsigned next)
{
    j->last_field=AXD_CS_SI_MAPPING;j->metadata_accesses++;
    if(!j->context.si_known){dependency(j,AXD_CS_METADATA);return;}
    if(j->context.si_known!=1 || axd_si_core_begin(&j->si_job,AXD_SICO_FLAGS,mask,value,&j->context.si))fail(j,AXD_CS_CHILD_ERROR);
    else {j->stage=RUN_SI;j->after_child=next;}
}
static void raw(struct axd_phy_chanspec *j,enum axd_d11_kind kind,uint32_t offset,uint32_t value,unsigned next)
{j->raw=(struct axd_d11_operation){kind,offset,value};j->stage=RUN_RAW;j->after_child=next;}
static void child(struct axd_phy_chanspec *j,struct relay *r)
{
    unsigned done=0,running=0,missing=0,cancelled=0;
    switch(j->stage){
    case RUN_PHY:{enum axd_phy_phase p=axd_phy_register_step(&j->phy_job,d11,r);done=p==AXD_PHY_TRACE_COMPLETE;running=p==AXD_PHY_RUNNING;missing=p==AXD_PHY_DEPENDENCY;cancelled=p==AXD_PHY_CANCELLED;break;}
    case RUN_RADIO:{enum axd_radio_control_phase p=axd_radio_control_step(&j->radio_job,d11,r);done=p==AXD_RADIO_CTL_TRACE_COMPLETE;running=p==AXD_RADIO_CTL_RUNNING;missing=p==AXD_RADIO_CTL_DEPENDENCY;cancelled=p==AXD_RADIO_CTL_CANCELLED;break;}
    case RUN_MAC:{enum axd_psm_phase p=axd_psm_control_step(&j->mac_job,d11,r);done=p==AXD_PSM_DONE;running=p==AXD_PSM_RUNNING;missing=p==AXD_PSM_DEPENDENCY;cancelled=p==AXD_PSM_CANCELLED;j->pending_callouts|=j->mac_job.pending_callouts;break;}
    case RUN_SI:{enum axd_si_core_phase p=axd_si_core_step(&j->si_job,si,r);done=p==AXD_SICO_DONE;running=p==AXD_SICO_RUNNING;missing=p==AXD_SICO_DEPENDENCY;cancelled=p==AXD_SICO_CANCELLED;break;}
    case RUN_RAW:{uint32_t v=0;if(!d11(r,&j->raw,&v)){j->raw.value=v;done=1;}break;}
    default:break;
    }
    publish(j);
    if(j->phase!=AXD_CS_RUNNING)return;
    if(done)j->stage=j->after_child;
    else if(missing)dependency(j,AXD_CS_RESOURCE_CHILD);
    else if(cancelled)j->phase=AXD_CS_CANCELLED;
    else if(!running)fail(j,AXD_CS_CHILD_ERROR);
}
static const uint16_t sync_a[3]={1,0x200,0x40},sync_b[3]={0x100,0x400,0x800};
static const uint16_t sync_173[6]={0x10,0x20,0x80,0x400,0x200,0x800};
static const uint16_t sync_radio[6]={2,0x200,1,0x400,0x100,0x80};
/* Per-core sequence: rows of masks against selector base+core*200, value0.
 * Core count is re-read each iteration, as the actual loops reload PUB+8. */
static void cores(struct axd_phy_chanspec *j,uint16_t base,const uint16_t *mask,unsigned parts,unsigned next,unsigned self)
{
    if(j->core_index>=j->context.hardware.phy.phy_core_count){j->core_index=0;j->part=0;j->stage=next;return;}
    phy(j,AXD_PHY_MODIFY,(uint16_t)(base+j->core_index*0x200),mask[j->part],0,self);
    if(++j->part==parts){j->part=0;j->core_index++;}
}
static void sync_step(struct axd_phy_chanspec *j)
{
    struct axd_phy_chanspec_context *c=&j->context;
    switch(j->stage){
    case SYNC_READ:raw(j,AXD_D11_READ32,0x120,0,SYNC_SY);break;
    case SYNC_SY:j->main_control=j->raw.value;raw(j,AXD_D11_FULL_BARRIER,0,0,SYNC_SUSPEND);break;
    case SYNC_SUSPEND:
        j->mac_enabled=j->main_control&1u;j->core_index=0;j->part=0;
        if(j->mac_enabled)mac(j,AXD_PSM_SUSPEND_MAIN,SYNC_A);else j->stage=SYNC_A;break;
    case SYNC_A:cores(j,0x725,sync_a,3,SYNC_B,SYNC_A);break;
    case SYNC_B:cores(j,0x601,sync_b,3,SYNC_173,SYNC_B);break;
    case SYNC_173:
        phy(j,AXD_PHY_MODIFY,0x173,sync_173[j->part],0,SYNC_173);
        if(++j->part==6){j->part=0;j->after_child=SYNC_C;}break;
    case SYNC_C:
        if(j->core_index>=c->hardware.phy.phy_core_count){j->core_index=0;j->stage=SYNC_40;break;}
        phy(j,AXD_PHY_MODIFY,(uint16_t)(0x601+j->core_index*0x200),0x1000,0x1000,SYNC_C80);break;
    case SYNC_C80:phy(j,AXD_PHY_MODIFY,(uint16_t)(0x601+j->core_index*0x200),0x80,0x80,SYNC_C);j->core_index++;break;
    case SYNC_40:phy(j,AXD_PHY_MODIFY,0x173,0x40,0,SYNC_46A);break;
    case SYNC_46A:phy(j,AXD_PHY_MODIFY,0x46a,0x1e,0x1e,SYNC_GUARD_READ);break;
    case SYNC_GUARD_READ:phy(j,AXD_PHY_READ,0x19e,0,0,SYNC_GUARD_SET);break;
    case SYNC_GUARD_SET:j->sync_guard=j->phy_job.result;phy(j,AXD_PHY_MODIFY,0x19e,2,2,CCA_READ);j->after_cca=SYNC_GUARD_RESTORE;break;
    case SYNC_GUARD_RESTORE:phy(j,AXD_PHY_MODIFY,0x19e,2,(uint16_t)(j->sync_guard&2u),SYNC_RADIO);j->core_index=0;j->part=0;break;
    case SYNC_RADIO:
        if(j->core_index>=c->hardware.phy.phy_core_count){j->core_index=0;j->part=0;j->stage=SYNC_16B;break;}
        radio(j,j->core_index==0?0x88:j->core_index==1?0x288:0xffff,sync_radio[j->part],0,SYNC_RADIO);
        if(++j->part==6){j->part=0;j->core_index++;}break;
    case SYNC_16B:phy(j,AXD_PHY_MODIFY,0x16b,0x1e0,0,SYNC_ENABLE);break;
    case SYNC_ENABLE:
        j->sync_completed=1;
        if(j->mac_enabled)mac(j,AXD_PSM_ENABLE_MAIN,SETUP_READ);else j->stage=SETUP_READ;break;
    default:fail(j,AXD_CS_BAD_ARGUMENT);break;
    }
}
static uint32_t bw_reset_value(uint32_t bw)
{return bw==0x1000?0x40:bw==0x1800?0x80:bw==0x2000||bw==0x3000?0xc0:bw==0x2800?0x800:0;}
static void setup_step(struct axd_phy_chanspec *j)
{
    struct axd_phy_chanspec_context *c=&j->context;uint32_t bw=c->chanspec.value&0x3800u;
    switch(j->stage){
    case SETUP_READ:phy(j,AXD_PHY_READ,0x19e,0,0,SETUP_SET);break;
    case SETUP_SET:
        j->setup_guard=j->phy_job.result;c->chanmgr_byte29=(uint8_t)(j->setup_guard&1u);j->software_stores++;
        phy(j,AXD_PHY_MODIFY,0x19e,1,1,CORE_MODE);break;
    case CORE_MODE:if(fact(j,AXD_CS_PHYMODE,c->phymode,0xffff,0))phy(j,AXD_PHY_READ,0xb,0,0,CORE_SHARED);break;
    case CORE_SHARED:{
        unsigned shift=(c->phymode.value&0xfff7u)!=0;
        c->hardware.phy.phy_core_count=(uint8_t)((j->phy_job.result&7u)>>shift);c->radio.hardware=c->hardware;j->software_stores++;
        if(c->phymode.value!=7){j->stage=CORE_BITMAP;break;}
        /* phy_get_current_core only compares this word with PI (no dereference);
         * phy_get_other_pi is the identity, so the other PI's count is this one. */
        if(fact(j,AXD_CS_SHARED_PI,c->shared_current_pi,UINT32_MAX,0)){
            if(c->shared_current_pi.value!=c->radio.phy_pointer.value)j->software_stores++;
            j->stage=CORE_BITMAP;
        }break;}
    case CORE_BITMAP:
        c->core_bitmap18=(uint8_t)~(0xffu<<c->hardware.phy.phy_core_count);j->software_stores++;j->stage=TEMP;break;
    case TEMP:
        if(fact(j,AXD_CS_TEMP,c->temp_pointer,UINT32_MAX,0)){
            if(c->temp_pointer.value)j->stage=TEMP_STATE;else {j->corestate_completed=1;j->stage=BAND;}
        }break;
    case TEMP_STATE:
        if(fact(j,AXD_CS_TEMP_STATE,c->temp_state,UINT32_MAX,1)){
            c->tempsense_byte5=(uint8_t)(c->core_bitmap18|(c->core_bitmap18<<4));j->software_stores++;
            if(c->phymode.value==7)j->stage=TEMP_AGAIN;else {j->corestate_completed=1;j->stage=BAND;}
        }break;
    case TEMP_AGAIN:
        c->tempsense_byte5=(uint8_t)(c->core_bitmap18|(c->core_bitmap18<<4));j->software_stores++;
        j->corestate_completed=1;j->stage=BAND;break;
    case BAND:
        c->band_byte9=(c->chanspec.value&0xc000u)==0;c->ac_flags_be|=4;c->ac_bw_b8=bw;j->software_stores+=3;
        j->stage=BW_PHY;break;
    case BW_PHY:
        if(alias(j,AXD_CS_BAND_PHY,c->band_phy_pointer,c->radio.phy_pointer.value)){
            c->bwstate396=(uint16_t)bw;j->bwstate_stored=1;j->software_stores++;j->stage=BW_TYPE;
        }break;
    case BW_TYPE:
        if(fact(j,AXD_CS_BAND_TYPE,c->band_phy_type,0xffff,0)){
            if(c->band_phy_type.value==11)j->stage=BW_RESET;else dependency(j,AXD_CS_BMAC_PHY_RESET);
        }break;
    case BW_RESET:
        if(fact(j,AXD_CS_BMAC_CHANSPEC,c->bmac_chanspec,0xffff,0)){
            cflags(j,0x8c0,bw_reset_value(c->bmac_chanspec.value&0x3800u),BWINIT);
        }break;
    case BWINIT:
        if(fact(j,AXD_CS_BWINIT,c->bwinit_slot,UINT32_MAX,0)){
            if(!c->bwinit_slot.value)dependency(j,AXD_CS_PHY_INIT_FALLBACK);else j->stage=AC_BWINIT;
        }break;
    case AC_BWINIT:
        /* The registered AC bwinit leaf returns -EINVAL; the caller ignores it.
         * Any other registered slot is an unidentified callback. */
        if(fact(j,AXD_CS_AC_BWINIT,c->ac_bwinit_label,UINT32_MAX,1)){
            if(c->bwinit_slot.value!=c->ac_bwinit_label.value)dependency(j,AXD_CS_RESOURCE_CHILD);
            else {j->bandwidth_completed=1;j->stage=CHIP;}
        }break;
    case CHIP:
        if(fact(j,AXD_CS_CHIP,c->radio.chip_id,UINT32_MAX,0)){
            if(c->radio.chip_id.value==0x4335 && bw==0x2000)dependency(j,AXD_CS_CHIP4335_BRANCH);else j->stage=SETUP_DELAY;
        }break;
    case SETUP_DELAY:raw(j,AXD_D11_DELAY_US,0,2,SETUP_TAIL);break;
    case SETUP_TAIL:
        c->ac_flags_be|=2;j->software_stores++;
        phy(j,AXD_PHY_MODIFY,3,0x100,(c->chanspec.value&0xc000u)?0x100:0,MODULE1);break;
    /* chanspec_setup_tpc returns immediately for PHY revisions above46. */
    case MODULE1:j->modules_completed=1;if(c->hardware.phy.phy_revision>46){j->modules_completed=2;j->stage=MODULE2;}else fail(j,AXD_CS_UNSUPPORTED);break;
    case MODULE2:
        if(fact(j,AXD_CS_RADIO_ID,c->radio.radio_id,0xffff,0)){
            if(c->radio.radio_id.value==0x2069)dependency(j,AXD_CS_RADIO2069_SETUP);
            else if(c->radio.radio_id.value==0x50e0){j->after_cca=RADIO20704;j->stage=CCA_READ;}
            else {j->modules_completed=3;dependency(j,AXD_CS_MODULE_SETUP_PHY);}
        }break;
    case RADIO20704:
        /* Third argument: AC flags bits1/2 (set by module0) as a boolean. */
        j->radio20704_argument=(c->ac_flags_be&6u)!=0;j->stage=R_TABLE;break;
    default:fail(j,AXD_CS_BAD_ARGUMENT);break;
    }
}
/* wlc_bmac_phyclk_fgc: only band phy types4 and11 reach si_core_cflags(2,on?2:0). */
static void fgc(struct axd_phy_chanspec *j,unsigned on,unsigned next)
{
    if(!fact(j,AXD_CS_BAND_TYPE,j->context.band_phy_type,0xffff,0))return;
    if(j->context.band_phy_type.value==4 || j->context.band_phy_type.value==11)cflags(j,2,on?2:0,next);
    else j->stage=next;
}
static void cca_step(struct axd_phy_chanspec *j)
{
    switch(j->stage){
    case CCA_READ:raw(j,AXD_D11_READ16,0x7c0,0,CCA_SY);break;
    case CCA_SY:j->cca_sample=(uint16_t)j->raw.value;j->cca_valid=1;raw(j,AXD_D11_FULL_BARRIER,0,0,CCA_SHORT);break;
    case CCA_SHORT:if(j->cca_sample&2u)j->stage=CCA_FGC_ON;else raw(j,AXD_D11_DELAY_US,0,1,CCA_FGC_ON);break;
    case CCA_FGC_ON:fgc(j,1,CCA_D1);break;
    case CCA_D1:raw(j,AXD_D11_DELAY_US,0,1,CCA_ST6);break;
    case CCA_ST6:raw(j,AXD_D11_STORE_BARRIER,0,0,CCA_W6);break;
    case CCA_W6:raw(j,AXD_D11_WRITE16,0x7c0,6,CCA_D2);break;
    case CCA_D2:raw(j,AXD_D11_DELAY_US,0,1,CCA_TOGGLE_SET);break;
    case CCA_TOGGLE_SET:phy(j,AXD_PHY_MODIFY,1,0x4000,0x4000,CCA_D3);break;
    case CCA_D3:raw(j,AXD_D11_DELAY_US,0,1,CCA_TOGGLE_CLEAR);break;
    case CCA_TOGGLE_CLEAR:phy(j,AXD_PHY_MODIFY,1,0x4000,0,CCA_D10);break;
    case CCA_D10:raw(j,AXD_D11_DELAY_US,0,10,CCA_ST2);break;
    case CCA_ST2:raw(j,AXD_D11_STORE_BARRIER,0,0,CCA_W2);break;
    case CCA_W2:raw(j,AXD_D11_WRITE16,0x7c0,2,CCA_FGC_OFF);break;
    case CCA_FGC_OFF:fgc(j,0,CCA_D4);break;
    case CCA_D4:raw(j,AXD_D11_DELAY_US,0,2,CCA_ST_RESTORE);break;
    case CCA_ST_RESTORE:raw(j,AXD_D11_STORE_BARRIER,0,0,CCA_W_RESTORE);break;
    case CCA_W_RESTORE:raw(j,AXD_D11_WRITE16,0x7c0,j->cca_sample,CCA_TAIL);break;
    case CCA_TAIL:j->cca_completed++;phy(j,AXD_PHY_MODIFY,0x400,2,0,j->after_cca);break;
    default:fail(j,AXD_CS_BAD_ARGUMENT);break;
    }
}
/* wlc_get_20704_chan_tune_table: radio revision <3 uses one table for every
 * band, revision3 one per band; the SI package word picks the variant. */
static uint32_t tune_select(uint32_t revision,uint32_t package,uint32_t band,uint32_t *count)
{
    unsigned variant=package==0x84000000u?0:package==0x85000000u?1:package==0x86000000u?2:3;
    *count=0;if(variant==3)return AXD_CS_TUNE_NONE;
    if(revision<3){*count=0x56;return AXD_CS_TUNE_REV0_63178+variant;}
    if(revision!=3)return AXD_CS_TUNE_NONE;
    if(band==0x4000){*count=0x7f;return AXD_CS_TUNE_REV3_63178_6G+variant*3;}
    if(band==0xc000){*count=0x48;return AXD_CS_TUNE_REV3_63178_5G+variant*3;}
    if(band==0){*count=0xe;return AXD_CS_TUNE_REV3_63178_2G+variant*3;}
    return AXD_CS_TUNE_NONE;
}
/* 5/6GHz band writes: selector, mask, row byte, left shift (mask applied). */
static const struct {uint16_t selector,mask;uint8_t byte,shift;} band56[11]={
    {0x9f5,0xf,4,0},{0x1f4,0xf,5,0},{0x3f4,0xf,6,0},{0x6e,0xf000,7,12},{0x26e,0xf000,8,12},
    {0x73,6,9,1},{0x273,6,10,1},{0x2d,0xf,11,0},{0x22d,0xf,12,0},{0x1c6,0x78,13,3},{0x3c6,0x78,14,3}};
/* 5GHz per-core rows for core0; core1 adds 200; other cores use ffff. The
 * final row is the loop tail (1f3/3f3/ffff, 1c00, c00). */
static const uint16_t core5g[25][3]={
    {0x1f1,1,0},{0x1f1,0x200,0},{0x1f6,0x400,0x400},{0x1f1,0x100,0},{0x1f6,0x800,0x800},
    {0x1f2,0x100,0x100},{0x1f2,0x400,0x400},{0x1f1,0x400,0},{0x74,0x100,0},{0x74,0x4000,0x4000},
    {0x73,1,1},{0x2e,3,0},{0x2e,4,0},{0x29,1,1},{0x1df,0x1000,0},{0x18,8,8},{0x1c6,4,4},
    {0x29,4,4},{0x1c6,2,0},{0x2d,0x20,0},{0x1c6,1,0},{0x1c9,0x38,0x28},{0x1c9,0x7000,0x5000},
    {0x1c9,7,5},{0x1f3,0x1c00,0xc00}};
static const uint16_t tail5g[4][3]={{0x88a,0x38,0x18},{0x88a,0xe000,0},{0x9f6,0x4000,0x4000},{0x9f5,0xe000,0xa000}};
static uint16_t per_core(uint16_t selector,unsigned core){return core<2?(uint16_t)(selector+core*0x200):0xffff;}
/* wlc_phy_chanspec_radio20704_setup, 5GHz frequency branch (3001..6289MHz). */
static void radio_step(struct axd_phy_chanspec *j)
{
    struct axd_phy_chanspec_context *c=&j->context;uint32_t band=c->chanspec.value&0xc000u,count,i;
    const uint8_t *row=j->tune_row;
    switch(j->stage){
    case R_TABLE:
        if(!fact(j,AXD_CS_RADIO_REVISION,c->radio.radio_revision,255,0))break;
        if(c->radio.radio_revision.value>3){j->modules_completed=3;dependency(j,AXD_CS_MODULE_SETUP_PHY);break;}
        if(!fact(j,AXD_CS_SIH_PACKAGE,c->sih_package,UINT32_MAX,0))break;
        j->tune_selected=tune_select(c->radio.radio_revision.value,c->sih_package.value,band,&count);
        /* No selected table: chan2freq returns -1 and the setup returns. */
        if(j->tune_selected==AXD_CS_TUNE_NONE){j->modules_completed=3;dependency(j,AXD_CS_MODULE_SETUP_PHY);break;}
        j->last_field=AXD_CS_TUNE_TABLE;j->metadata_accesses++;
        if(!c->tune_known){dependency(j,AXD_CS_METADATA);break;}
        if(c->tune_known!=1){fail(j,AXD_CS_BAD_ARGUMENT);break;}
        if(!c->tune_rows || c->tune_table!=j->tune_selected || c->tune_count!=count){fail(j,AXD_CS_BINDING);break;}
        for(i=0;i<count;i++){
            const uint8_t *r=c->tune_rows+16u*i;
            if((uint32_t)(r[0]|(r[1]<<8))==(c->chanspec.value&0xffu)){j->tune_row=r;break;}
        }
        if(!j->tune_row){j->modules_completed=3;dependency(j,AXD_CS_MODULE_SETUP_PHY);break;}
        j->tune_frequency=(uint16_t)(j->tune_row[2]|(j->tune_row[3]<<8));j->part=0;
        j->stage=j->radio20704_argument==1?R_LOGEN:R_BAND;break;
    case R_LOGEN:phy(j,AXD_PHY_MODIFY,0x728,0x100,0x100,R_LOGEN_DELAY);break;
    case R_LOGEN_DELAY:raw(j,AXD_D11_DELAY_US,0,1,R_LOGEN_CLEAR);break;
    case R_LOGEN_CLEAR:phy(j,AXD_PHY_MODIFY,0x728,0x100,0,R_BAND);break;
    case R_BAND:
        if(band==0){dependency(j,AXD_CS_RADIO20704_2G);break;}
        if((band!=0x4000 && band!=0xc000) || j->part==11){j->part=0;j->stage=R_88A;break;}
        {uint32_t v=(uint32_t)row[band56[j->part].byte]<<band56[j->part].shift;
            if(band56[j->part].shift==12)v&=band56[j->part].mask;
            radio(j,band56[j->part].selector,band56[j->part].mask,(uint16_t)v,R_BAND);j->part++;}
        break;
    case R_88A:
        if(j->part==0){radio(j,0x88a,0x80,0x80,R_88A);j->part=1;}
        else {j->part=0;radio(j,0x9f6,1,0,R_FREQ);}
        break;
    case R_FREQ:
        if(j->tune_frequency<=0xbb8)dependency(j,AXD_CS_RADIO20704_2G);
        else if(j->tune_frequency>0x1891)dependency(j,AXD_CS_RADIO20704_6G);
        else j->stage=R_9F6;
        break;
    case R_9F6:
        if(fact(j,AXD_CS_SIH_PACKAGE,c->sih_package,UINT32_MAX,0)){
            uint32_t w=c->sih_package.value,f=j->tune_frequency,k=f-0x15a4u;unsigned set;
            if(w==0x84000000u)set=f==0x143c || f==0x1351;
            else if(((w+0x7b000000u)&~0x1000000u)!=0)set=0;
            else set=f==0x143c || f==0x1351 || (k<=0x1e && ((0x40100401u>>k)&1u)) || f==0x15cc || f==0x15f4 || f==0x15fe;
            j->core_index=0;j->part=0;radio(j,0x9f6,0x2000,set?0x2000:0,R_CORE);
        }break;
    case R_CORE:
        if(j->core_index>=c->hardware.phy.phy_core_count){j->core_index=0;j->part=0;j->stage=R_TAIL;break;}
        radio(j,per_core(core5g[j->part][0],j->core_index),core5g[j->part][1],core5g[j->part][2],R_CORE);
        if(++j->part==25){j->part=0;j->core_index++;}
        break;
    case R_TAIL:
        if(j->part==4){j->part=0;j->core_index=0;j->stage=R_2D;break;}
        radio(j,tail5g[j->part][0],tail5g[j->part][1],tail5g[j->part][2],R_TAIL);j->part++;break;
    case R_2D:
        if(j->core_index>=c->hardware.phy.phy_core_count){j->stage=R_PLL;break;}
        radio(j,per_core(0x2d,j->core_index),0x10,j->tune_frequency<=0x176f?0x10:0,R_2D);j->core_index++;break;
    case R_PLL:dependency(j,AXD_CS_PLL_TUNE);break;
    default:fail(j,AXD_CS_BAD_ARGUMENT);break;
    }
}
int axd_phy_chanspec_begin(struct axd_phy_chanspec *j,const struct axd_phy_chanspec_context *context)
{
    const struct axd_phy_chanspec empty={0};struct axd_phy_chanspec_context c;
    if(!j)return -1;if(context){c=*context;context=&c;}*j=empty;fail(j,AXD_CS_BAD_ARGUMENT);
    if(!context)return -1;
    j->context=c;
    if(c.hardware.phy.core!=130 || c.hardware.phy.minor>1 || c.hardware.phy.phy_type!=0xb || c.hardware.phy.phy_revision!=51){fail(j,AXD_CS_UNSUPPORTED);return -1;}
    j->context.radio.hardware=j->context.hardware;
    j->phase=AXD_CS_RUNNING;j->error=AXD_CS_NO_ERROR;j->stage=P_DATA;return 0;
}
enum axd_phy_chanspec_phase axd_phy_chanspec_step(struct axd_phy_chanspec *j,axd_phy_chanspec_io_fn io,void *opaque)
{
    struct relay r;struct axd_phy_chanspec_context *c;
    if(!j)return AXD_CS_INVALID;if(j->phase!=AXD_CS_RUNNING)return j->phase;
    r.j=j;r.io=io;r.opaque=opaque;c=&j->context;
    if(j->stage>=RUN_PHY){child(j,&r);return j->phase;}
    if(j->stage>=SYNC_READ && j->stage<=SYNC_ENABLE){sync_step(j);return j->phase;}
    if(j->stage>=SETUP_READ && j->stage<=RADIO20704){setup_step(j);return j->phase;}
    if(j->stage>=R_TABLE && j->stage<=R_PLL){radio_step(j);return j->phase;}
    if(j->stage>=CCA_READ && j->stage<=CCA_TAIL){cca_step(j);return j->phase;}
    switch(j->stage){
    case P_DATA:
        if(fact(j,AXD_CS_AC,c->radio.ac_pointer,UINT32_MAX,1) && fact(j,AXD_CS_CHANMGR_DATA,c->radio.chanmgr_data_pointer,UINT32_MAX,1)){
            c->chanmgr_data11=0;j->software_stores++;j->stage=P_AC;
        }break;
    case P_AC:c->ac_flags_be|=1;j->software_stores++;j->stage=P_RADAR;break;
    case P_RADAR:
        if(fact(j,AXD_CS_PHY,c->radio.phy_pointer,UINT32_MAX,1) && fact(j,AXD_CS_RADAR,c->radar_pointer,UINT32_MAX,0))
            j->stage=c->radar_pointer.value?P_RADAR_INFO:RADIO_SET;
        break;
    case P_RADAR_INFO:
        if(fact(j,AXD_CS_RADAR_INFO,c->radar_info,UINT32_MAX,0)){
            if(c->radar_info.value){c->radar_indicator=1;j->software_stores++;}
            j->stage=RADIO_SET;
        }break;
    case RADIO_SET:
        if(fact(j,AXD_CS_COMMON,c->common_pointer,UINT32_MAX,1) && fact(j,AXD_CS_CHANSPEC,c->chanspec,0xffff,0)){
            /* Previous chanspec halfword, then PI38c=argument (the same value here). */
            c->common_previous_chanspec=(uint16_t)c->chanspec.value;j->software_stores+=2;j->stage=MODULE0;
        }break;
    case MODULE0:if(fact(j,AXD_CS_CHANMGR,c->radio.chanmgr_pointer,UINT32_MAX,1))j->stage=SYNC_READ;break;
    default:fail(j,AXD_CS_BAD_ARGUMENT);break;
    }
    return j->phase;
}
enum axd_phy_chanspec_phase axd_phy_chanspec_cancel(struct axd_phy_chanspec *j)
{
    if(!j)return AXD_CS_INVALID;
    if(j->phase==AXD_CS_RUNNING){
        j->phase=AXD_CS_CANCELLED;
        if(j->stage==RUN_PHY)axd_phy_register_cancel(&j->phy_job);
        else if(j->stage==RUN_RADIO)axd_radio_control_cancel(&j->radio_job);
        else if(j->stage==RUN_MAC)axd_psm_control_cancel(&j->mac_job);
        else if(j->stage==RUN_SI)axd_si_core_cancel(&j->si_job);
    }
    return j->phase;
}
