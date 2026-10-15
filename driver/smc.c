/* SPDX-License-Identifier: MIT */
#include "smc.h"
#include <stddef.h>
enum stage {
    ENTRY, RESET_BIND, RESET_LEAF, RESET_NEXT, RESET_RETURN,
    PULSE_READ, PULSE_SY, PULSE_ST_SET, PULSE_SET, PULSE_ST_CLEAR, PULSE_CLEAR,
    DOWNLOAD_BIND, IMAGE, TABLE_BEGIN, DOWNLOAD_RETURN, LOAD_COMMIT, FINISH,
    RUN_PHY, RUN_TABLE
};
struct relay {struct axd_smc *job;axd_d11_io_fn io;void *opaque;};
static void fail(struct axd_smc *j,enum axd_smc_error error)
{j->phase=AXD_SMC_FAILED;j->error=error;}
static int pointer(struct axd_smc *j,enum axd_smc_field field,struct axd_wake_u32 p)
{
    j->last_field=field;j->metadata_accesses++;
    if(p.known==0)j->phase=AXD_SMC_DEPENDENCY;
    else if(p.known!=1)fail(j,AXD_SMC_BAD_ARGUMENT);
    else if(p.value==0)fail(j,AXD_SMC_BINDING);
    else return 1;
    return 0;
}
static int binding(struct axd_smc *j,unsigned download)
{
    struct axd_wake_u32 p={0};enum axd_smc_field field=AXD_SMC_NO_FIELD;
    switch(j->binding_index) {
    case 0:p=j->context.band;field=AXD_SMC_BAND;break;
    case 1:p=j->context.phy_pointer;field=AXD_SMC_PHY_POINTER;break;
    case 2:p=j->context.smc_info;field=AXD_SMC_INFO;break;
    case 3:p=j->context.dispatch;field=AXD_SMC_DISPATCH;break;
    case 4:p=j->context.callback_context;field=AXD_SMC_CALLBACK_CONTEXT;break;
    case 5:p=download?j->context.download_callback:j->context.reset_callback;field=download?AXD_SMC_DOWNLOAD_CALLBACK:AXD_SMC_RESET_CALLBACK;break;
    case 6:p=j->context.callback_pi;field=AXD_SMC_CALLBACK_PI;break;
    default:
        if(j->context.callback_pi.value!=j->context.phy_pointer.value)fail(j,AXD_SMC_BINDING);
        else return 1;
        return 0;
    }
    if(pointer(j,field,p))j->binding_index++;
    return 0;
}
static int perform(struct relay *r,const struct axd_d11_operation *op,uint32_t *value)
{
    struct axd_smc *j=r->job;
    if(j->stage==RUN_PHY)j->context.hardware.phy=j->phy_job.context;
    else if(j->stage==RUN_TABLE)j->context.hardware=j->table_job.context;
    if(!r->io){fail(j,AXD_SMC_BAD_ARGUMENT);return -1;}
    j->attempted_ops++;
    if(r->io(r->opaque,op,value)){fail(j,AXD_SMC_IO_ERROR);return -1;}
    j->completed_ops++;return 0;
}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *value)
{return perform(opaque,op,value);}
static void reset(struct axd_smc *j,unsigned value,unsigned resume)
{
    j->reset_value=value;j->reset_index=0;j->binding_index=0;
    j->after_reset=resume;j->stage=RESET_BIND;
}
static void child(struct axd_smc *j,struct relay *r)
{
    unsigned done=0,running=0,cancelled=0,dependency=0;
    if(j->stage==RUN_PHY) {
        enum axd_phy_phase p=axd_phy_register_step(&j->phy_job,io,r);
        j->context.hardware.phy=j->phy_job.context;
        done=p==AXD_PHY_TRACE_COMPLETE;running=p==AXD_PHY_RUNNING;
        cancelled=p==AXD_PHY_CANCELLED;dependency=p==AXD_PHY_DEPENDENCY;
    } else {
        enum axd_phy_table_write_phase p=axd_phy_table_write_step(&j->table_job,io,r);
        j->context.hardware=j->table_job.context;
        j->pending_callouts|=j->table_job.pending_callouts;
        done=p==AXD_TW_TRACE_COMPLETE;running=p==AXD_TW_RUNNING;
        cancelled=p==AXD_TW_CANCELLED;dependency=p==AXD_TW_DEPENDENCY;
    }
    if(j->phase!=AXD_SMC_RUNNING)return;
    if(done)j->stage=j->after_child;
    else if(dependency)j->phase=AXD_SMC_DEPENDENCY;
    else if(cancelled)j->phase=AXD_SMC_CANCELLED;
    else if(!running)fail(j,AXD_SMC_CHILD_ERROR);
}
unsigned axd_smc_hw_supported(uint32_t core_revision)
{return core_revision>127u;}
int axd_smc_begin(struct axd_smc *j,enum axd_smc_command command,
                   const struct axd_smc_image *image,const struct axd_smc_context *context)
{
    struct axd_smc_context copy;struct axd_smc_image image_copy;
    const struct axd_smc empty={0};
    if(!j)return -1;
    if(context){copy=*context;context=&copy;}
    if(image){image_copy=*image;image=&image_copy;}
    *j=empty;fail(j,AXD_SMC_BAD_ARGUMENT);
    if(!context || command<AXD_SMC_LOAD || command>AXD_SMC_RESET1)return -1;
    j->context=*context;if(image)j->image=*image;j->command=command;
    if(context->hardware.phy.core!=130 || context->hardware.phy.minor>1 ||
       context->hardware.phy.phy_type!=0xb || context->hardware.phy.phy_revision!=51) {
        fail(j,AXD_SMC_UNSUPPORTED);return -1;
    }
    j->phase=AXD_SMC_RUNNING;j->error=AXD_SMC_NO_ERROR;return 0;
}
enum axd_smc_phase axd_smc_step(struct axd_smc *j,axd_d11_io_fn transport,void *opaque)
{
    struct relay r={j,transport,opaque};struct axd_d11_operation op={0};
    uint32_t value=0;unsigned direct=0;
    if(!j)return AXD_SMC_INVALID;
    if(j->phase!=AXD_SMC_RUNNING)return j->phase;
    switch(j->stage) {
    case ENTRY:
        if(j->command==AXD_SMC_LOAD)reset(j,j->context.loaded!=0,j->context.loaded?LOAD_COMMIT:PULSE_READ);
        else reset(j,j->command==AXD_SMC_RESET1,FINISH);
        break;
    case RESET_BIND:if(binding(j,0))j->stage=RESET_LEAF;break;
    case RESET_LEAF:
        if(j->reset_value==0) {
            if(axd_phy_packed_write_begin(&j->table_job,0x140b,0,&j->context.hardware))fail(j,AXD_SMC_CHILD_ERROR);
            else {j->stage=RUN_TABLE;j->after_child=RESET_RETURN;}
        } else {
            const uint16_t selectors[4]={0x1409,0x1410,0x140b,0x140b};
            const uint16_t masks[4]={0x8000,2,1,2};
            if(axd_phy_register_begin(&j->phy_job,AXD_PHY_MODIFY,selectors[j->reset_index],masks[j->reset_index],masks[j->reset_index],&j->context.hardware.phy))fail(j,AXD_SMC_CHILD_ERROR);
            else {j->stage=RUN_PHY;j->after_child=RESET_NEXT;}
        }
        break;
    case RESET_NEXT:j->reset_index++;j->stage=j->reset_index==4?RESET_RETURN:RESET_LEAF;break;
    case RESET_RETURN:
        if(j->reset_value)j->reset1_completed++;else j->reset0_completed++;
        j->stage=j->after_reset;break;
    case PULSE_READ:op.kind=AXD_D11_READ16;op.offset=0x7c0;direct=1;break;
    case PULSE_SY:op.kind=AXD_D11_FULL_BARRIER;direct=1;break;
    case PULSE_ST_SET:case PULSE_ST_CLEAR:op.kind=AXD_D11_STORE_BARRIER;direct=1;break;
    case PULSE_SET:case PULSE_CLEAR:
        op.kind=AXD_D11_WRITE16;op.offset=0x7c0;
        op.value=j->stage==PULSE_SET?j->last_read|0x80u:j->last_read&~0x80u;
        direct=1;break;
    case DOWNLOAD_BIND:if(binding(j,1))j->stage=IMAGE;break;
    case IMAGE:
        j->last_field=AXD_SMC_IMAGE;j->metadata_accesses++;
        j->image_rows=(uint16_t)(j->image.source_bytes/6u);
        if(j->image.data==NULL){j->phase=AXD_SMC_DEPENDENCY;break;}
        if((size_t)j->image_rows>j->image.length/6u){fail(j,AXD_SMC_BOUNDS);break;}
        j->stage=TABLE_BEGIN;break;
    case TABLE_BEGIN: {
        struct axd_phy_table_write_request request={47,j->image_rows,0,48,j->image.data,j->image.length};
        if(axd_phy_table_write_begin(&j->table_job,&request,&j->context.hardware))fail(j,AXD_SMC_CHILD_ERROR);
        else {j->stage=RUN_TABLE;j->after_child=DOWNLOAD_RETURN;}
        break;
    }
    case DOWNLOAD_RETURN:j->context.ac_downloaded=1;j->download_completed=1;reset(j,1,LOAD_COMMIT);break;
    case LOAD_COMMIT:j->context.loaded=1;j->loaded_committed=1;j->stage=FINISH;break;
    case FINISH:j->phase=AXD_SMC_TRACE_COMPLETE;break;
    default:child(j,&r);break;
    }
    if(direct && perform(&r,&op,&value)==0) {
        if(op.kind==AXD_D11_READ16)j->last_read=(uint16_t)value;
        if(j->stage==PULSE_CLEAR){j->binding_index=0;j->stage=DOWNLOAD_BIND;}
        else j->stage++;
    }
    return j->phase;
}
enum axd_smc_phase axd_smc_cancel(struct axd_smc *j)
{
    if(!j)return AXD_SMC_INVALID;
    if(j->phase==AXD_SMC_RUNNING) {j->phase=AXD_SMC_CANCELLED;
        if(j->stage==RUN_PHY)axd_phy_register_cancel(&j->phy_job);
        else if(j->stage==RUN_TABLE)axd_phy_table_write_cancel(&j->table_job);
    }
    return j->phase;
}
