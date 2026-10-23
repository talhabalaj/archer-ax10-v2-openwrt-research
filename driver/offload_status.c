/* SPDX-License-Identifier: MIT */
#include "offload_status.h"
enum stage { CLEAR_READ, CLEAR_WRITE, SCAN_READ, SCAN_TEST, ADVANCE, COMPARE, SWITCH_GUARD, SWITCH_BEGIN, BARRIER, WRITE_CONSUMER, RESTORE_GUARD, RESTORE_BEGIN, FINISH, RUN_SI };
struct relay { struct axd_offload_status *job;axd_offload_status_io_fn io;void *opaque; };
static void fail(struct axd_offload_status *j,enum axd_offload_status_error e)
{j->phase=AXD_OS_FAILED;j->error=e;}
static void dep(struct axd_offload_status *j,enum axd_offload_status_dependency d)
{j->phase=AXD_OS_DEPENDENCY;j->dependency=d;}
static int perform(struct relay *r,const struct axd_offload_status_operation *op,struct axd_offload_status_reply *reply)
{
    if(r->io==NULL){fail(r->job,AXD_OS_BAD_ARGUMENT);return -1;}
    r->job->attempted_ops++;
    if(r->io(r->opaque,op,reply)!=0){fail(r->job,AXD_OS_IO_ERROR);return -1;}
    r->job->completed_ops++;return 0;
}
static int si_io(void *opaque,const struct axd_si_switch_operation *op,struct axd_si_switch_reply *reply)
{
    struct relay *r=opaque;struct axd_offload_status_operation converted={0};
    struct axd_offload_status_reply out={0};
    r->job->context.si=r->job->si_job.context;r->job->si_state_valid=r->job->si_job.state_valid;
    converted.kind=AXD_OS_SI;converted.si=*op;
    if(perform(r,&converted,&out)!=0)return -1;
    *reply=out.si;return 0;
}
static int bitmap(struct axd_offload_status *j,struct axd_offload_status_operation *op,unsigned write,uint32_t cursor)
{
    if(!j->context.bitmap_owned){dep(j,AXD_OS_BITMAP_OWNER);return 0;}
    if(j->context.bitmap==0){fail(j,AXD_OS_BINDING);return 0;}
    if((cursor>>3)>=j->context.bitmap_bytes){fail(j,AXD_OS_BOUNDS);return 0;}
    op->kind=write?AXD_OS_BITMAP_WRITE8:AXD_OS_BITMAP_READ8;
    op->identity=j->context.bitmap;op->offset=cursor>>3;
    if(write)op->value=(uint8_t)(j->bitmap_sample & ~(1u<<(cursor&7u)));
    return 1;
}
static void si_begin(struct axd_offload_status *j,enum axd_si_switch_command command,unsigned next)
{
    if(axd_si_switch_begin(&j->si_job,command,command==AXD_SIS_SWITCH?0x844:j->saved_index,
                             j->saved_index,j->irq_token,&j->context.si)!=0){fail(j,AXD_OS_CHILD_ERROR);return;}
    j->after_si=next;j->stage=RUN_SI;
}
static int si_known(struct axd_offload_status *j)
{
    if(j->context.si_known==0){dep(j,AXD_OS_SI_BINDING);return 0;}
    if(j->context.si_known!=1){fail(j,AXD_OS_BAD_ARGUMENT);return 0;}
    return 1;
}
int axd_offload_status_begin(struct axd_offload_status *j,int32_t index,const struct axd_offload_status_context *context)
{
    static const struct axd_offload_status empty={0};struct axd_offload_status_context c;
    if(j==NULL)return -1;
    if(context!=NULL){c=*context;context=&c;}
    *j=empty;fail(j,AXD_OS_BAD_ARGUMENT);
    if(context==NULL || c.bitmap_owned>1 || c.registers_owned>1 || c.os_owned>1)return -1;
    j->context=c;
    if(c.count<2 || c.count>65536 || (c.count&(c.count-1))!=0 || index<0 ||
       (uint32_t)index>=c.count || c.consumer>=c.count || c.producer>=c.count){fail(j,AXD_OS_BOUNDS);return -1;}
    j->index=(uint32_t)index;j->old_consumer=c.consumer;j->si_state_valid=c.si.state_known==1;
    j->phase=AXD_OS_RUNNING;j->error=AXD_OS_NO_ERROR;return 0;
}
enum axd_offload_status_phase axd_offload_status_step(struct axd_offload_status *j,axd_offload_status_io_fn io,void *opaque)
{
    struct relay r={j,io,opaque};struct axd_offload_status_operation op={0};
    struct axd_offload_status_reply reply={0};unsigned emit=0,before;
    if(j==NULL)return AXD_OS_INVALID;
    if(j->phase!=AXD_OS_RUNNING)return j->phase;
    switch(j->stage){
    case CLEAR_READ:emit=bitmap(j,&op,0,j->index);break;
    case CLEAR_WRITE:emit=bitmap(j,&op,1,j->index);break;
    case SCAN_READ:emit=bitmap(j,&op,0,j->context.consumer);break;
    case SCAN_TEST:
        if((j->bitmap_sample&(1u<<(j->context.consumer&7u)))!=0 || j->context.consumer==j->context.producer)j->stage=COMPARE;
        else j->stage=ADVANCE;
        break;
    case ADVANCE:
        if(j->scanned==j->context.count){fail(j,AXD_OS_BOUNDS);break;}
        j->scanned++;j->context.consumer=(j->context.consumer+1u)&(j->context.count-1u);
        j->consumer_stores++;j->stage=SCAN_READ;break;
    case COMPARE:j->stage=(uint16_t)j->old_consumer==j->context.consumer?FINISH:SWITCH_GUARD;break;
    case SWITCH_GUARD:
        if(si_known(j))j->stage=j->context.si.si.bus==1?SWITCH_BEGIN:BARRIER;
        break;
    case SWITCH_BEGIN:si_begin(j,AXD_SIS_SWITCH,BARRIER);break;
    case BARRIER:
        if(!j->context.os_owned)dep(j,AXD_OS_OS_OWNER);
        else{op.kind=AXD_OS_STORE_BARRIER;emit=1;}
        break;
    case WRITE_CONSUMER:
        if(!j->context.registers_owned)dep(j,AXD_OS_REGISTER_OWNER);
        else if(j->context.registers==0)fail(j,AXD_OS_BINDING);
        else{op.kind=AXD_OS_WRITE32;op.identity=j->context.registers;op.offset=0x20;op.value=j->context.consumer;emit=1;}
        break;
    case RESTORE_GUARD:
        if(si_known(j))j->stage=j->context.si.si.bus==1?RESTORE_BEGIN:FINISH;
        break;
    case RESTORE_BEGIN:si_begin(j,AXD_SIS_RESTORE,FINISH);break;
    case FINISH:j->phase=AXD_OS_TRACE_COMPLETE;break;
    case RUN_SI:{
        enum axd_si_switch_phase p=axd_si_switch_step(&j->si_job,si_io,&r);
        j->context.si=j->si_job.context;j->si_state_valid=j->si_job.state_valid;
        j->saved_index=j->si_job.saved_index;j->irq_token=j->si_job.irq_token;
        if(j->phase!=AXD_OS_RUNNING)break;
        if(p==AXD_SIS_TRACE_COMPLETE)j->stage=j->after_si;
        else if(p==AXD_SIS_DEPENDENCY)dep(j,AXD_OS_SI_CHILD);
        else if(p==AXD_SIS_CANCELLED)j->phase=AXD_OS_CANCELLED;
        else if(p!=AXD_SIS_RUNNING)fail(j,AXD_OS_CHILD_ERROR);
        break;
    }
    default:fail(j,AXD_OS_BAD_ARGUMENT);break;
    }
    if(!emit)return j->phase;
    before=j->stage;
    if(perform(&r,&op,&reply)!=0)return j->phase;
    if(before==CLEAR_READ || before==SCAN_READ)j->bitmap_sample=(uint8_t)reply.value;
    if(before==WRITE_CONSUMER)j->register_writes++;
    j->stage=before+1;return j->phase;
}
enum axd_offload_status_phase axd_offload_status_cancel(struct axd_offload_status *j)
{
    if(j==NULL)return AXD_OS_INVALID;
    if(j->phase==AXD_OS_RUNNING){j->phase=AXD_OS_CANCELLED;if(j->stage==RUN_SI)axd_si_switch_cancel(&j->si_job);}
    return j->phase;
}
