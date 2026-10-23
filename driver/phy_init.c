/* SPDX-License-Identifier: MIT */
#include "phy_init.h"

enum stage {
    ENTRY, SET1B0, READ0, OTP, VOLTAGE, GPIO, SET16E2, SET16E1, SET16E10,
    WRITE16F, WRITE170, READ19E, SET19E, CLEAR10F7, CLEAR1120,
    TABLE0, TABLE1, TABLE2, RESTORE19E, CLEAR400, FINISH,
    RUN_PHY, RUN_TABLE, RUN_CONTROL
};
struct relay {struct axd_phy_init *job;axd_phy_init_io_fn io;void *context;};
static void fail(struct axd_phy_init *j,enum axd_phy_init_error e)
{j->phase=AXD_PI_FAILED;j->error=e;}
static int known(struct axd_phy_init *j,enum axd_phy_init_field f,unsigned k)
{
    j->last_field=f;j->metadata_accesses++;
    if(k==1)return 1;
    if(k==0)j->phase=AXD_PI_DEPENDENCY;else fail(j,AXD_PI_BAD_ARGUMENT);
    return 0;
}
static int perform(struct relay *r,const struct axd_phy_init_operation *op,struct axd_phy_init_reply *reply)
{
    if(r->io==NULL){fail(r->job,AXD_PI_BAD_ARGUMENT);return -1;}
    r->job->attempted_ops++;
    if(r->io(r->context,op,reply)!=0){fail(r->job,AXD_PI_IO_ERROR);return -1;}
    r->job->completed_ops++;return 0;
}
static int d11_io(void *opaque,const struct axd_d11_operation *op,uint32_t *value)
{
    struct relay *r=opaque;struct axd_phy_init_operation converted={0};struct axd_phy_init_reply reply={0};
    if(r->job->stage==RUN_PHY)r->job->context.hardware.phy=r->job->phy_job.context;
    else r->job->context.hardware=r->job->table_job.context;
    converted.path=AXD_PI_D11;converted.d11=*op;
    if(perform(r,&converted,&reply)!=0)return -1;
    *value=reply.value;return 0;
}
static int control_io(void *opaque,const struct axd_si_pmu_gpio_operation *op,struct axd_si_pmu_gpio_reply *reply)
{
    struct relay *r=opaque;struct axd_phy_init_operation converted={0};struct axd_phy_init_reply result={0};
    r->job->context.control=r->job->control_job.context;
    r->job->context.control_state_known=(uint8_t)r->job->control_job.state_valid;
    converted.path=AXD_PI_CONTROL;converted.control=*op;
    if(perform(r,&converted,&result)!=0)return -1;
    *reply=result.control;return 0;
}
static void phy(struct axd_phy_init *j,enum axd_phy_command command,uint16_t selector,
                 uint16_t mask,uint16_t value,unsigned resume)
{
    if(axd_phy_register_begin(&j->phy_job,command,selector,mask,value,&j->context.hardware.phy)!=0)
        fail(j,AXD_PI_PHY_ERROR);
    else{j->stage=RUN_PHY;j->after_child=resume;}
}
static void packed(struct axd_phy_init *j,uint16_t selector,uint16_t value,unsigned resume)
{
    if(axd_phy_packed_write_begin(&j->table_job,selector,value,&j->context.hardware)!=0)
        fail(j,AXD_PI_TABLE_ERROR);
    else{j->stage=RUN_TABLE;j->after_child=resume;}
}
static void table(struct axd_phy_init *j,unsigned slot,unsigned resume)
{
    const uint32_t counts[3]={8,64,1},offsets[3]={0x1000,0x1040,0x1020};
    struct axd_phy_init_span *span=&j->context.tables[slot];
    struct axd_phy_table_write_request request={0x10,counts[slot],offsets[slot],32,span->data,span->length};
    if(!known(j,(enum axd_phy_init_field)(AXD_PI_TABLE0+slot),span->known))return;
    if(span->data==NULL){fail(j,AXD_PI_BINDING);return;}
    if(axd_phy_table_write_begin(&j->table_job,&request,&j->context.hardware)!=0)
        fail(j,AXD_PI_TABLE_ERROR);
    else{j->stage=RUN_TABLE;j->after_child=resume;}
}
static void control(struct axd_phy_init *j,enum axd_si_pmu_gpio_command command,
                     uint32_t reg,uint32_t mask,uint32_t value,unsigned resume)
{
    if(!known(j,AXD_PI_SI_STATE,j->context.control_state_known))return;
    if(axd_si_pmu_gpio_begin(&j->control_job,command,reg,mask,value,&j->context.control)!=0)
        fail(j,AXD_PI_CONTROL_ERROR);
    else{j->stage=RUN_CONTROL;j->after_child=resume;}
}
static void child_step(struct axd_phy_init *j,struct relay *r)
{
    unsigned done=0,running=0,cancelled=0,dependency=0;
    if(j->stage==RUN_PHY){
        enum axd_phy_phase p=axd_phy_register_step(&j->phy_job,d11_io,r);
        j->context.hardware.phy=j->phy_job.context;
        if(j->phy_job.command==AXD_PHY_READ&&j->phy_job.result_valid){
            if(j->after_child==OTP){j->phy0_sample=j->phy_job.result;j->phy0_valid=1;}
            else if(j->after_child==SET19E){j->guard_sample=j->phy_job.result;j->guard_valid=1;}
        }
        done=p==AXD_PHY_TRACE_COMPLETE;running=p==AXD_PHY_RUNNING;cancelled=p==AXD_PHY_CANCELLED;
        dependency=p==AXD_PHY_DEPENDENCY;
        if(!done&&!running&&!cancelled&&!dependency&&j->phase==AXD_PI_RUNNING)fail(j,AXD_PI_PHY_ERROR);
    }else if(j->stage==RUN_TABLE){
        enum axd_phy_table_write_phase p=axd_phy_table_write_step(&j->table_job,d11_io,r);
        j->context.hardware=j->table_job.context;j->pending_callouts|=j->table_job.pending_callouts;
        done=p==AXD_TW_TRACE_COMPLETE;running=p==AXD_TW_RUNNING;cancelled=p==AXD_TW_CANCELLED;
        dependency=p==AXD_TW_DEPENDENCY;
        if(done&&j->phase==AXD_PI_RUNNING&&!j->table_job.direct)j->tables_completed++;
        if(!done&&!running&&!cancelled&&!dependency&&j->phase==AXD_PI_RUNNING)fail(j,AXD_PI_TABLE_ERROR);
    }else{
        enum axd_si_pmu_gpio_phase p=axd_si_pmu_gpio_step(&j->control_job,control_io,r);
        j->context.control=j->control_job.context;
        j->context.control_state_known=(uint8_t)j->control_job.state_valid;
        done=p==AXD_SPG_TRACE_COMPLETE;running=p==AXD_SPG_RUNNING;cancelled=p==AXD_SPG_CANCELLED;
        dependency=p==AXD_SPG_DEPENDENCY;
        if(!done&&!running&&!cancelled&&!dependency&&j->phase==AXD_PI_RUNNING)fail(j,AXD_PI_CONTROL_ERROR);
    }
    if(j->phase!=AXD_PI_RUNNING)return;
    if(done)j->stage=j->after_child;
    else if(cancelled)j->phase=AXD_PI_CANCELLED;
    else if(dependency)j->phase=AXD_PI_DEPENDENCY;
}
int axd_phy_init_begin(struct axd_phy_init *j,const struct axd_phy_init_context *context)
{
    static const struct axd_phy_init empty={0};struct axd_phy_init_context snapshot;
    if(j==NULL)return -1;
    if(context!=NULL){snapshot=*context;context=&snapshot;}
    *j=empty;fail(j,AXD_PI_BAD_ARGUMENT);
    if(context==NULL)return -1;
    j->context=*context;
    if(context->chip_id!=63178){fail(j,AXD_PI_UNSUPPORTED);return -1;}
    if(axd_phy_register_begin(&j->phy_job,AXD_PHY_READ,0,0,0,&context->hardware.phy)!=0){
        fail(j,AXD_PI_PHY_ERROR);return -1;
    }
    j->phase=AXD_PI_RUNNING;j->error=AXD_PI_NO_ERROR;j->stage=ENTRY;return 0;
}
enum axd_phy_init_phase axd_phy_init_step(struct axd_phy_init *j,axd_phy_init_io_fn io,void *context)
{
    struct relay r={j,io,context};
    if(j==NULL)return AXD_PI_INVALID;
    if(j->phase!=AXD_PI_RUNNING)return j->phase;
    switch(j->stage){
    case ENTRY:phy(j,AXD_PHY_MODIFY,0x19e,2,0,SET1B0);break;
    case SET1B0:phy(j,AXD_PHY_MODIFY,0x1b0,0x8000,0x8000,READ0);break;
    case READ0:phy(j,AXD_PHY_READ,0,0,0,OTP);break;
    case OTP:
        if(known(j,AXD_PI_OTP,j->context.otp_word.known)){
            uint32_t otp=(j->context.otp_word.value>>8)&0x1fu;
            if(j->context.otp_word.value>UINT16_MAX){fail(j,AXD_PI_BAD_ARGUMENT);break;}
            j->voltage_value=((j->phy0_sample&0xffu)!=0?(otp>3?otp-3:0):(otp!=0?otp:5))<<20;
            j->stage=VOLTAGE;
        }
        break;
    case VOLTAGE:control(j,AXD_SPG_VREG,0,0x01f00000,j->voltage_value,GPIO);break;
    case GPIO:control(j,AXD_SPG_GPIO,0,0xffff,0,SET16E2);break;
    case SET16E2:phy(j,AXD_PHY_MODIFY,0x16e,2,2,SET16E1);break;
    case SET16E1:phy(j,AXD_PHY_MODIFY,0x16e,1,1,SET16E10);break;
    case SET16E10:phy(j,AXD_PHY_MODIFY,0x16e,0x10,0x10,WRITE16F);break;
    case WRITE16F:packed(j,0x16f,2000,WRITE170);break;
    case WRITE170:packed(j,0x170,2000,READ19E);break;
    case READ19E:phy(j,AXD_PHY_READ,0x19e,0,0,SET19E);break;
    case SET19E:phy(j,AXD_PHY_MODIFY,0x19e,2,2,CLEAR10F7);break;
    case CLEAR10F7:phy(j,AXD_PHY_MODIFY,0x10f7,0x10,0,CLEAR1120);break;
    case CLEAR1120:phy(j,AXD_PHY_MODIFY,0x1120,4,0,TABLE0);break;
    case TABLE0:table(j,0,TABLE1);break;
    case TABLE1:table(j,1,TABLE2);break;
    case TABLE2:table(j,2,RESTORE19E);break;
    case RESTORE19E:phy(j,AXD_PHY_MODIFY,0x19e,2,j->guard_sample&2u,CLEAR400);break;
    case CLEAR400:phy(j,AXD_PHY_MODIFY,0x400,2,0,FINISH);break;
    case FINISH:j->phase=AXD_PI_TRACE_COMPLETE;break;
    default:child_step(j,&r);break;
    }
    return j->phase;
}
enum axd_phy_init_phase axd_phy_init_cancel(struct axd_phy_init *j)
{
    if(j==NULL)return AXD_PI_INVALID;
    if(j->phase==AXD_PI_RUNNING){
        j->phase=AXD_PI_CANCELLED;
        if(j->stage==RUN_PHY)axd_phy_register_cancel(&j->phy_job);
        else if(j->stage==RUN_TABLE)axd_phy_table_write_cancel(&j->table_job);
        else if(j->stage==RUN_CONTROL)axd_si_pmu_gpio_cancel(&j->control_job);
    }
    return j->phase;
}
