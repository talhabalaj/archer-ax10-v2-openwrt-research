/* SPDX-License-Identifier: MIT */
#include "phy_table_read.h"
enum stage {ENTRY,SY,SUSPEND,READ_GUARD,GUARD,ID,OFFSET,DATA,STORE,NEXT,
    CLEAR_GUARD,ENABLE,FINISH,RUN_PHY,RUN_MAC,RUN_PACKED};
static void fail(struct axd_phy_table_read *j,enum axd_phy_table_read_error e)
{j->phase=AXD_TR_FAILED;j->error=e;}
struct relay {struct axd_phy_table_read *j;axd_d11_io_fn io;void *opaque;};
static int perform(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct relay *r=opaque;struct axd_phy_table_read *j=r->j;
    if(j->stage==RUN_PHY)j->context.phy=j->phy_job.context;
    else if(j->stage==RUN_MAC)j->context.mac=j->mac_job.context;
    else if(j->stage==RUN_PACKED)j->context=j->packed_job.context;
    if(!r->io){fail(j,AXD_TR_BAD_ARGUMENT);return -1;}
    j->attempted_ops++;
    if(r->io(r->opaque,op,reply)){fail(j,AXD_TR_IO_ERROR);return -1;}
    j->completed_ops++;
    if(j->stage==RUN_PHY && j->after_child==ID && op->kind==AXD_D11_WRITE16 && op->offset==0x3fe)j->changed_guard=1;
    return 0;
}
static void phy(struct axd_phy_table_read *j,enum axd_phy_command cmd,uint16_t selector,uint16_t mask,uint16_t value,unsigned next)
{
    if(axd_phy_register_begin(&j->phy_job,cmd,selector,mask,value,&j->context.phy))fail(j,AXD_TR_CHILD_ERROR);
    else {j->stage=RUN_PHY;j->after_child=next;}
}
static void packed(struct axd_phy_table_read *j,uint16_t selector,uint16_t value,unsigned next)
{
    if(axd_phy_packed_write_begin(&j->packed_job,selector,value,&j->context))fail(j,AXD_TR_CHILD_ERROR);
    else {j->stage=RUN_PACKED;j->after_child=next;}
}
static void mac(struct axd_phy_table_read *j,enum axd_psm_command cmd,unsigned next)
{
    if(!j->context.mac_context_valid){j->phase=AXD_TR_DEPENDENCY;j->dependency=AXD_TR_MAC_CONTEXT;return;}
    if(j->context.mac.core!=j->context.phy.core || j->context.mac.minor!=j->context.phy.minor ||
       axd_psm_control_begin_policy(&j->mac_job,cmd,&j->context.mac,&j->context.policy))fail(j,AXD_TR_CHILD_ERROR);
    else {j->stage=RUN_MAC;j->after_child=next;}
}
static void child(struct axd_phy_table_read *j,struct relay *r)
{
    unsigned done,running,missing,cancelled;
    if(j->stage==RUN_PHY){
        enum axd_phy_phase p=axd_phy_register_step(&j->phy_job,perform,r);
        j->context.phy=j->phy_job.context;
        if(j->after_child==STORE && j->phy_job.result_valid){j->sample=j->phy_job.result;j->sample_valid=1;}
        done=p==AXD_PHY_TRACE_COMPLETE;running=p==AXD_PHY_RUNNING;missing=p==AXD_PHY_DEPENDENCY;cancelled=p==AXD_PHY_CANCELLED;
    }else if(j->stage==RUN_MAC){
        enum axd_psm_phase p=axd_psm_control_step(&j->mac_job,perform,r);
        j->context.mac=j->mac_job.context;j->pending_callouts|=j->mac_job.pending_callouts;
        done=p==AXD_PSM_DONE;running=p==AXD_PSM_RUNNING;missing=p==AXD_PSM_DEPENDENCY;cancelled=p==AXD_PSM_CANCELLED;
    }else{
        enum axd_phy_table_write_phase p=axd_phy_table_write_step(&j->packed_job,perform,r);
        j->context=j->packed_job.context;
        done=p==AXD_TW_TRACE_COMPLETE;running=p==AXD_TW_RUNNING;missing=p==AXD_TW_DEPENDENCY;cancelled=p==AXD_TW_CANCELLED;
    }
    if(j->phase!=AXD_TR_RUNNING)return;
    if(done)j->stage=j->after_child;
    else if(missing){j->phase=AXD_TR_DEPENDENCY;j->dependency=AXD_TR_MAC_CALLOUT;}
    else if(cancelled)j->phase=AXD_TR_CANCELLED;
    else if(!running)fail(j,AXD_TR_CHILD_ERROR);
}
int axd_phy_table_read_begin(struct axd_phy_table_read *j,const struct axd_phy_table_read_request *request,const struct axd_phy_table_context *context)
{
    const struct axd_phy_table_read empty={0};struct axd_phy_table_read_request q;struct axd_phy_table_context c;
    uintptr_t start=(uintptr_t)j,end;
    if(!j)return -1;
    if(request){q=*request;request=&q;}if(context){c=*context;context=&c;}
    *j=empty;fail(j,AXD_TR_BAD_ARGUMENT);
    if(!request || !context || c.mac_context_valid>1)return -1;
    j->context=c;j->request=q;
    if(c.phy.core!=130 || c.phy.minor>1 || c.phy.phy_type!=0xb || c.phy.phy_revision!=51 || q.width!=8){fail(j,AXD_TR_UNSUPPORTED);return -1;}
    end=(uintptr_t)q.data;
    if(q.count && (!q.data || q.count>q.data_length || end>UINTPTR_MAX-q.count ||
       (end<start+sizeof(*j) && end+q.count>start))){fail(j,AXD_TR_BAD_DATA);return -1;}
    if(axd_phy_register_begin(&j->phy_job,AXD_PHY_READ,0,0,0,&c.phy)){fail(j,AXD_TR_CHILD_ERROR);return -1;}
    j->phase=AXD_TR_RUNNING;j->error=AXD_TR_NO_ERROR;return 0;
}
enum axd_phy_table_read_phase axd_phy_table_read_step(struct axd_phy_table_read *j,axd_d11_io_fn io,void *opaque)
{
    struct relay r={j,io,opaque};struct axd_d11_operation op={0};uint32_t reply=0;unsigned s;
    if(!j)return AXD_TR_INVALID;if(j->phase!=AXD_TR_RUNNING)return j->phase;
    switch(j->stage){
    case ENTRY:case SY:
        s=j->stage;op.kind=s==ENTRY?AXD_D11_READ32:AXD_D11_FULL_BARRIER;op.offset=s==ENTRY?0x120:0;
        if(!perform(&r,&op,&reply)){if(s==ENTRY)j->initial_control=reply;j->stage=s==ENTRY?SY:SUSPEND;}break;
    case SUSPEND:if(j->initial_control&1u)mac(j,AXD_PSM_SUSPEND_MAIN,READ_GUARD);else j->stage=READ_GUARD;break;
    case READ_GUARD:phy(j,AXD_PHY_READ,0x19e,0,0,GUARD);break;
    case GUARD:if(j->phy_job.result&2u)j->stage=ID;else phy(j,AXD_PHY_MODIFY,0x19e,2,2,ID);break;
    case ID:packed(j,0xd,(uint16_t)j->request.table_id,OFFSET);break;
    case OFFSET:packed(j,0xe,(uint16_t)j->request.offset,DATA);break;
    case DATA:if(j->index==j->request.count)j->stage=CLEAR_GUARD;else {j->sample_valid=0;phy(j,AXD_PHY_READ,0xf,0,0,STORE);}break;
    case STORE:j->request.data[j->index]=(uint8_t)j->sample;j->bytes_completed++;j->stage=NEXT;break;
    case NEXT:j->index++;j->stage=DATA;break;
    case CLEAR_GUARD:if(j->changed_guard)phy(j,AXD_PHY_MODIFY,0x19e,2,0,ENABLE);else j->stage=ENABLE;break;
    case ENABLE:if(j->initial_control&1u)mac(j,AXD_PSM_ENABLE_MAIN,FINISH);else j->stage=FINISH;break;
    case FINISH:j->phase=AXD_TR_TRACE_COMPLETE;break;
    case RUN_PHY:case RUN_MAC:case RUN_PACKED:child(j,&r);break;
    default:fail(j,AXD_TR_BAD_ARGUMENT);break;
    }
    return j->phase;
}
enum axd_phy_table_read_phase axd_phy_table_read_cancel(struct axd_phy_table_read *j)
{
    if(!j)return AXD_TR_INVALID;
    if(j->phase==AXD_TR_RUNNING){j->phase=AXD_TR_CANCELLED;axd_phy_register_cancel(&j->phy_job);axd_psm_control_cancel(&j->mac_job);axd_phy_table_write_cancel(&j->packed_job);}
    return j->phase;
}
