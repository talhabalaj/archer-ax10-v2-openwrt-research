/* SPDX-License-Identifier: MIT */
#include "si_switch.h"
enum stage { ENTRY, DISABLE_GUARD, DISABLE, SAVE_INDEX, SELECT_BEGIN, SELECT_RUN, RESTORE_GUARD, RESTORE, FINISH };
struct relay { struct axd_si_switch *job; axd_si_switch_io_fn io; void *opaque; };
static void fail(struct axd_si_switch *j,enum axd_si_switch_error e)
{ j->phase=AXD_SIS_FAILED;j->error=e; }
static void dep(struct axd_si_switch *j,enum axd_si_switch_dependency d)
{ j->phase=AXD_SIS_DEPENDENCY;j->dependency=d; }
static int perform(struct relay *r,const struct axd_si_switch_operation *op,struct axd_si_switch_reply *reply)
{
    if(r->io==NULL){fail(r->job,AXD_SIS_BAD_ARGUMENT);return -1;}
    r->job->attempted_ops++;
    if(r->io(r->opaque,op,reply)!=0){fail(r->job,AXD_SIS_IO_ERROR);return -1;}
    r->job->completed_ops++;return 0;
}
static int select_io(void *opaque,const struct axd_si_operation *op,uint32_t *value)
{
    struct relay *r=opaque;struct axd_si_switch_operation converted={0};
    struct axd_si_switch_reply reply={0};converted.kind=AXD_SIS_SELECT;converted.select=*op;
    r->job->context.state=r->job->selector.state;
    if(perform(r,&converted,&reply)!=0)return -1;
    *value=reply.value;return 0;
}
static int fast(const struct axd_si_switch_context *c)
{
    return c->si.bus==1 && (c->si.bus_core_id==0x83c || c->si.bus_core_id==0x820 ||
        (c->si.bus_core_id==0x804 && c->bus_core_revision>12));
}
static int guard(struct axd_si_switch *j)
{
    uint32_t i=j->context.state.current_index;const struct axd_si_context *c=&j->context.si;
    if(c->catalog==NULL){dep(j,AXD_SIS_CATALOG);return 0;}
    if(i>=c->core_count || i>=AXD_SI_MAX_CORES || (size_t)i>=c->catalog_length){fail(j,AXD_SIS_BINDING);return 0;}
    return c->catalog[i].core_id==j->context.irq_core_id;
}
static int state_known(struct axd_si_switch *j)
{
    if(j->context.state_known==0){dep(j,AXD_SIS_IRQ_STATE);return 0;}
    if(j->context.state_known!=1){fail(j,AXD_SIS_BAD_ARGUMENT);return 0;}
    return 1;
}
static void irq(struct axd_si_switch *j,struct relay *r,unsigned restore)
{
    struct axd_si_switch_operation op={0};struct axd_si_switch_reply reply={0};
    op.kind=restore?AXD_SIS_IRQ_RESTORE:AXD_SIS_IRQ_DISABLE;
    op.callback=restore?j->context.restore_callback:j->context.disable_callback;
    op.callback_context=j->context.irq_context;op.value=restore?j->irq_token:0;
    op.state=&j->context.state;
    if(r->io!=NULL){j->state_valid=0;j->context.state_known=0;}
    if(perform(r,&op,&reply)!=0)return;
    if(restore)j->restore_called=1;
    else{j->disable_called=1;j->irq_token=reply.value;j->token_stored=1;}
    j->state_valid=reply.irq_state_known==1;
    j->context.state_known=(uint8_t)j->state_valid;
    if(j->state_valid)j->context.state=reply.irq_state;
    if(j->phase!=AXD_SIS_RUNNING)return;
    if(reply.irq_state_known==0){dep(j,AXD_SIS_IRQ_STATE);return;}
    if(reply.irq_state_known!=1){fail(j,AXD_SIS_BAD_ARGUMENT);return;}
    j->stage=restore?FINISH:SAVE_INDEX;
}
int axd_si_switch_begin(struct axd_si_switch *j,enum axd_si_switch_command command,
                          uint32_t requested,uint32_t saved_index,uint32_t irq_token,
                          const struct axd_si_switch_context *context)
{
    static const struct axd_si_switch empty={0};struct axd_si_switch_context snapshot;
    if(j==NULL)return -1;
    if(context!=NULL){snapshot=*context;context=&snapshot;}
    *j=empty;fail(j,AXD_SIS_BAD_ARGUMENT);
    if(context==NULL || (command!=AXD_SIS_SWITCH && command!=AXD_SIS_RESTORE))return -1;
    j->context=*context;j->command=command;j->requested=requested;
    j->saved_index=saved_index;j->irq_token=irq_token;j->state_valid=j->context.state_known==1;
    j->phase=AXD_SIS_RUNNING;j->error=AXD_SIS_NO_ERROR;return 0;
}
enum axd_si_switch_phase axd_si_switch_step(struct axd_si_switch *j,axd_si_switch_io_fn io,void *opaque)
{
    struct relay r={j,io,opaque};
    if(j==NULL)return AXD_SIS_INVALID;
    if(j->phase!=AXD_SIS_RUNNING)return j->phase;
    switch(j->stage){
    case ENTRY:
        if(j->command==AXD_SIS_RESTORE && fast(&j->context) &&
           (j->requested==0x800 || j->requested==j->context.si.bus_core_id)){j->stage=FINISH;break;}
        if(!state_known(j))break;
        if(fast(&j->context)){
            if(j->command==AXD_SIS_RESTORE){
                if(j->requested==0x800 || j->requested==j->context.si.bus_core_id)j->stage=FINISH;
                else j->stage=SELECT_BEGIN;
            }else{
                j->saved_index=j->requested;j->saved_stored=1;
                if(j->requested==0x800){
                    j->return_value=j->context.state.current_map;
                    if(j->return_value!=0)j->return_value+=0x3000u;
                    j->stage=FINISH;
                }else if(j->requested==j->context.si.bus_core_id){
                    j->return_value=j->context.state.current_map+0x2000u;j->stage=FINISH;
                }else j->stage=DISABLE_GUARD;
            }
        }else j->stage=j->command==AXD_SIS_SWITCH?DISABLE_GUARD:SELECT_BEGIN;
        break;
    case DISABLE_GUARD:
        if(j->context.disable_callback==0)j->stage=SAVE_INDEX;
        else if(guard(j))j->stage=DISABLE;
        else if(j->phase==AXD_SIS_RUNNING)j->stage=SAVE_INDEX;
        break;
    case DISABLE:irq(j,&r,0);break;
    case SAVE_INDEX:j->saved_index=j->context.state.current_index;j->saved_stored=1;j->stage=SELECT_BEGIN;break;
    case SELECT_BEGIN:
        if(axd_si_select_begin(&j->selector,j->command==AXD_SIS_SWITCH?AXD_SI_BY_ID:AXD_SI_BY_INDEX,
                                 j->requested,0,&j->context.si,&j->context.state)!=0)fail(j,AXD_SIS_CHILD_ERROR);
        else j->stage=SELECT_RUN;
        break;
    case SELECT_RUN:{
        enum axd_si_phase p=axd_si_select_step(&j->selector,select_io,&r);
        j->context.state=j->selector.state;
        if(j->phase!=AXD_SIS_RUNNING)break;
        if(p==AXD_SI_TRACE_COMPLETE){
            j->selection_completed=1;j->return_value=j->selector.return_value;
            j->stage=j->command==AXD_SIS_SWITCH?FINISH:RESTORE_GUARD;
        }else if(p==AXD_SI_DEPENDENCY)dep(j,AXD_SIS_SELECTION);
        else if(p==AXD_SI_CANCELLED)j->phase=AXD_SIS_CANCELLED;
        else if(p!=AXD_SI_RUNNING)fail(j,AXD_SIS_CHILD_ERROR);
        break;
    }
    case RESTORE_GUARD:
        if(j->context.restore_callback==0)j->stage=FINISH;
        else if(guard(j))j->stage=RESTORE;
        else if(j->phase==AXD_SIS_RUNNING)j->stage=FINISH;
        break;
    case RESTORE:irq(j,&r,1);break;
    case FINISH:j->phase=AXD_SIS_TRACE_COMPLETE;break;
    default:fail(j,AXD_SIS_BAD_ARGUMENT);break;
    }
    return j->phase;
}
enum axd_si_switch_phase axd_si_switch_cancel(struct axd_si_switch *j)
{
    if(j==NULL)return AXD_SIS_INVALID;
    if(j->phase==AXD_SIS_RUNNING){j->phase=AXD_SIS_CANCELLED;if(j->stage==SELECT_RUN)axd_si_select_cancel(&j->selector);}
    return j->phase;
}
