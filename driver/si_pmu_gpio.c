/* SPDX-License-Identifier: MIT */
#include "si_pmu_gpio.h"
#include <stddef.h>
enum stage { ENTRY, FIND_PMU, START_REGISTER, RUN_REGISTER, NEXT_VREG, FINISH };
enum register_stage { REG_ENTRY, REG_BUS0, REG_MAP, REG_PCI, REG_DISABLE_GUARD, REG_DISABLE,
    REG_SAVE, REG_SELECT, REG_READ, REG_READ_SY, REG_COMPUTE, REG_ST, REG_WRITE,
    REG_FINAL_READ, REG_FINAL_SY, REG_RESTORE, REG_RESTORE_GUARD, REG_RESTORE_IRQ,
    REG_DONE, REG_SELECT_STEP };
struct relay {struct axd_si_pmu_gpio *job;axd_si_pmu_gpio_io_fn io;void *opaque;};
static void fail(struct axd_si_pmu_gpio *j,enum axd_si_pmu_gpio_error e)
{j->phase=AXD_SPG_FAILED;j->error=e;}
static void dependency(struct axd_si_pmu_gpio *j,enum axd_si_pmu_gpio_dependency d)
{j->phase=AXD_SPG_DEPENDENCY;j->dependency=d;}
static int catalog(struct axd_si_pmu_gpio *j,uint32_t index)
{
    j->metadata_accesses++;
    if(j->context.si.catalog==NULL){dependency(j,AXD_SPG_CATALOG);return 0;}
    if(j->context.si.core_count>AXD_SI_MAX_CORES || index>=j->context.si.core_count ||
       index>=AXD_SI_MAX_CORES || index>=j->context.si.catalog_length){fail(j,AXD_SPG_BOUNDS);return 0;}
    return 1;
}
static int perform(struct relay *r,const struct axd_si_pmu_gpio_operation *op,struct axd_si_pmu_gpio_reply *reply)
{
    if(!r->io){fail(r->job,AXD_SPG_BAD_ARGUMENT);return -1;}
    r->job->attempted_ops++;
    if(r->io(r->opaque,op,reply)){fail(r->job,AXD_SPG_IO_ERROR);return -1;}
    r->job->completed_ops++;return 0;
}
static int selector_io(void *opaque,const struct axd_si_operation *op,uint32_t *value)
{
    struct relay *r=opaque;struct axd_si_pmu_gpio_operation converted={0};struct axd_si_pmu_gpio_reply reply={0};
    r->job->context.state=r->job->selector.state;
    converted.kind=AXD_SPG_SELECT;converted.select=*op;
    if(perform(r,&converted,&reply))return -1;
    *value=reply.value;return 0;
}
static int guard(struct axd_si_pmu_gpio *j)
{
    uint32_t index=j->context.state.current_index;
    if(!catalog(j,index))return 0;
    return j->context.si.catalog[index].core_id==j->context.irq_core_id;
}
static int select_begin(struct axd_si_pmu_gpio *j,uint32_t index,unsigned resume)
{
    if(axd_si_select_begin(&j->selector,AXD_SI_BY_INDEX,index,0,&j->context.si,&j->context.state)) {
        fail(j,AXD_SPG_CHILD_ERROR);return 0;
    }
    j->after_select=resume;j->register_stage=REG_SELECT_STEP;return 1;
}
static void irq(struct axd_si_pmu_gpio *j,struct relay *r,unsigned restore)
{
    struct axd_si_pmu_gpio_operation op={0};struct axd_si_pmu_gpio_reply reply={0};
    op.kind=restore?AXD_SPG_IRQ_RESTORE:AXD_SPG_IRQ_DISABLE;
    op.callback=restore?j->context.restore_callback:j->context.disable_callback;
    op.callback_context=j->context.irq_context;op.value=restore?j->irq_token:0;
    op.state=&j->context.state;
    if(r->io)j->state_valid=0;
    if(perform(r,&op,&reply))return;
    if(restore)j->restore_called++;else {j->disable_called++;j->irq_token=reply.value;}
    j->state_valid=reply.irq_state_known==1;
    if(j->state_valid)j->context.state=reply.irq_state;
    if(j->phase!=AXD_SPG_RUNNING)return;
    if(reply.irq_state_known==0){dependency(j,AXD_SPG_IRQ_STATE);return;}
    if(reply.irq_state_known!=1){fail(j,AXD_SPG_BAD_ARGUMENT);return;}
    j->register_stage=restore?REG_DONE:REG_SAVE;
}
static int mmio(struct axd_si_pmu_gpio *j,struct relay *r,enum axd_si_pmu_gpio_kind kind,uint32_t value)
{
    struct axd_si_pmu_gpio_operation op={0};struct axd_si_pmu_gpio_reply reply={0};
    if((kind==AXD_SPG_READ32 || kind==AXD_SPG_WRITE32) && j->mapping==0){fail(j,AXD_SPG_BINDING);return 0;}
    op.kind=kind;op.mapping=j->mapping;op.offset=j->offset;op.value=value;
    if(perform(r,&op,&reply))return 0;
    if(kind==AXD_SPG_READ32)j->last_read=reply.value;
    j->register_stage++;return 1;
}
static void register_step(struct axd_si_pmu_gpio *j,struct relay *r)
{
    const struct axd_si_context *c=&j->context.si;
    switch(j->register_stage) {
    case REG_ENTRY:
        if(c->interconnect==0){dependency(j,AXD_SPG_SB);break;}
        if(c->interconnect!=1 && c->interconnect!=3 && c->interconnect!=4){j->result=0;j->register_stage=REG_DONE;break;}
        if(j->requested_index>31){j->result=0;j->register_stage=REG_DONE;break;}
        if(c->core_count>AXD_SI_MAX_CORES || j->requested_index>=c->core_count){fail(j,AXD_SPG_BOUNDS);break;}
        j->register_stage=c->bus==0?REG_BUS0:(c->bus==1?REG_PCI:REG_DISABLE_GUARD);break;
    case REG_BUS0:
        j->direct_mapping=1;j->mapping=j->context.state.cache[j->requested_index].core;
        j->register_stage=j->mapping?REG_READ:REG_MAP;break;
    case REG_MAP: {
        struct axd_si_pmu_gpio_operation op={0};struct axd_si_pmu_gpio_reply reply={0};
        if(!catalog(j,j->requested_index))break;
        op.kind=AXD_SPG_SELECT;op.select.kind=AXD_SI_MAP;op.select.resource=AXD_SI_CORE;
        op.select.address=c->catalog[j->requested_index].core_physical;op.select.size=0x1000;
        if(perform(r,&op,&reply))break;
        j->mapping=reply.value;j->context.state.cache[j->requested_index].core=reply.value;
        j->register_stage=REG_READ;break;
    }
    case REG_PCI: {
        uint32_t id,base=j->context.state.current_map;unsigned fast;
        if(!catalog(j,j->requested_index))break;
        id=c->catalog[j->requested_index].core_id;
        fast=c->bus_core_id==0x83c || c->bus_core_id==0x820 ||
             (c->bus_core_id==0x804 && j->context.bus_core_revision>12);
        if(id==0x800 && fast){j->mapping=base+0x3000u;j->direct_mapping=1;j->register_stage=REG_READ;}
        else if(j->requested_index==j->context.bus_core_index){
            j->mapping=base+(fast?0x2000u:(j->offset<0xf00?0x1800u:0x1000u));
            j->direct_mapping=1;j->register_stage=REG_READ;
        } else j->register_stage=REG_DISABLE_GUARD;
        break;
    }
    case REG_DISABLE_GUARD:
        if(j->context.disable_callback==0)j->register_stage=REG_SAVE;
        else if(guard(j))j->register_stage=REG_DISABLE;
        else if(j->phase==AXD_SPG_RUNNING)j->register_stage=REG_SAVE;
        break;
    case REG_DISABLE:irq(j,r,0);break;
    case REG_SAVE:j->saved_index=j->context.state.current_index;j->register_stage=REG_SELECT;break;
    case REG_SELECT:(void)select_begin(j,j->requested_index,REG_READ);break;
    case REG_READ:
        if((j->effective_mask|j->effective_value)==0)j->register_stage=REG_FINAL_READ;
        else (void)mmio(j,r,AXD_SPG_READ32,0);
        break;
    case REG_READ_SY:(void)mmio(j,r,AXD_SPG_FULL_BARRIER,0);break;
    case REG_COMPUTE:j->pending_value=(j->last_read&~j->effective_mask)|j->effective_value;j->register_stage=REG_ST;break;
    case REG_ST:(void)mmio(j,r,AXD_SPG_STORE_BARRIER,0);break;
    case REG_WRITE:(void)mmio(j,r,AXD_SPG_WRITE32,j->pending_value);break;
    case REG_FINAL_READ:(void)mmio(j,r,AXD_SPG_READ32,0);break;
    case REG_FINAL_SY:
        if(mmio(j,r,AXD_SPG_FULL_BARRIER,0))j->result=j->last_read;
        break;
    case REG_RESTORE:
        if(j->direct_mapping)j->register_stage=REG_DONE;
        else if(j->saved_index==j->requested_index)j->register_stage=REG_RESTORE_GUARD;
        else (void)select_begin(j,j->saved_index,REG_RESTORE_GUARD);
        break;
    case REG_RESTORE_GUARD:
        if(j->context.restore_callback==0)j->register_stage=REG_DONE;
        else if(guard(j))j->register_stage=REG_RESTORE_IRQ;
        else if(j->phase==AXD_SPG_RUNNING)j->register_stage=REG_DONE;
        break;
    case REG_RESTORE_IRQ:irq(j,r,1);break;
    case REG_SELECT_STEP: {
        enum axd_si_phase p=axd_si_select_step(&j->selector,selector_io,r);
        j->context.state=j->selector.state;
        if(j->phase!=AXD_SPG_RUNNING)break;
        if(p==AXD_SI_TRACE_COMPLETE){if(j->after_select==REG_READ)j->mapping=j->selector.return_value;j->register_stage=j->after_select;}
        else if(p==AXD_SI_DEPENDENCY)dependency(j,AXD_SPG_CATALOG);
        else if(p==AXD_SI_CANCELLED)j->phase=AXD_SPG_CANCELLED;
        else if(p!=AXD_SI_RUNNING)fail(j,AXD_SPG_CHILD_ERROR);
        break;
    }
    case REG_DONE:j->register_completed++;j->stage=j->after_register;break;
    default:fail(j,AXD_SPG_BAD_ARGUMENT);break;
    }
}
static void start_register(struct axd_si_pmu_gpio *j)
{
    j->offset=j->command==AXD_SPG_GPIO?0x6cu:(j->vreg_part?0x65cu:0x658u);
    if(j->command==AXD_SPG_VREG){j->effective_mask=j->vreg_part?j->mask:UINT32_MAX;j->effective_value=j->vreg_part?j->value:j->register_index;}
    j->direct_mapping=0;j->irq_token=0;j->register_stage=REG_ENTRY;
    j->after_register=j->command==AXD_SPG_GPIO?FINISH:NEXT_VREG;j->stage=RUN_REGISTER;
}
int axd_si_pmu_gpio_begin(struct axd_si_pmu_gpio *j,enum axd_si_pmu_gpio_command command,
                           uint32_t register_index,uint32_t mask,uint32_t value,
                           const struct axd_si_pmu_gpio_context *context)
{
    static const struct axd_si_pmu_gpio empty={0};struct axd_si_pmu_gpio_context copy;
    if(!j)return -1;
    if(context){copy=*context;context=&copy;}*j=empty;fail(j,AXD_SPG_BAD_ARGUMENT);
    if(!context || command<AXD_SPG_VREG || command>AXD_SPG_GPIO)return -1;
    j->context=*context;j->command=command;j->register_index=register_index;j->mask=mask;j->value=value;
    j->state_valid=1;j->phase=AXD_SPG_RUNNING;j->error=AXD_SPG_NO_ERROR;return 0;
}
enum axd_si_pmu_gpio_phase axd_si_pmu_gpio_step(struct axd_si_pmu_gpio *j,axd_si_pmu_gpio_io_fn transport,void *opaque)
{
    struct relay r={j,transport,opaque};
    if(!j)return AXD_SPG_INVALID;
    if(j->phase!=AXD_SPG_RUNNING)return j->phase;
    switch(j->stage) {
    case ENTRY:
        if(j->command==AXD_SPG_VREG){j->search_index=0;j->stage=FIND_PMU;}
        else {
            j->effective_mask=j->mask;j->effective_value=j->value;
            if(j->register_index!=2 && j->context.si.bus==0 && (j->mask|j->value)!=0) {
                j->metadata_accesses++;
                if(j->context.gpio_reservation_known==0){dependency(j,AXD_SPG_GPIO_RESERVATION);break;}
                if(j->context.gpio_reservation_known!=1){fail(j,AXD_SPG_BAD_ARGUMENT);break;}
                j->effective_mask=j->register_index?j->mask&j->context.gpio_reservation:j->mask&~j->context.gpio_reservation;
                j->effective_value=j->value&j->effective_mask;
            }
            j->requested_index=0;j->stage=START_REGISTER;
        }
        break;
    case FIND_PMU:
        if(j->context.chipcommon_revision<=34 || (j->context.capabilities_ext&0x40u)==0){j->requested_index=0;j->stage=START_REGISTER;break;}
        if(j->search_index==j->context.si.core_count){j->requested_index=33;j->stage=START_REGISTER;break;}
        if(!catalog(j,j->search_index))break;
        if(j->context.si.catalog[j->search_index].core_id==0x827){j->requested_index=j->search_index;j->stage=START_REGISTER;}
        else j->search_index++;
        break;
    case START_REGISTER:start_register(j);break;
    case RUN_REGISTER:register_step(j,&r);break;
    case NEXT_VREG:if(j->vreg_part==0){j->vreg_part=1;j->search_index=0;j->stage=FIND_PMU;}else j->stage=FINISH;break;
    case FINISH:j->phase=AXD_SPG_TRACE_COMPLETE;break;
    default:fail(j,AXD_SPG_BAD_ARGUMENT);break;
    }
    return j->phase;
}
enum axd_si_pmu_gpio_phase axd_si_pmu_gpio_cancel(struct axd_si_pmu_gpio *j)
{
    if(!j)return AXD_SPG_INVALID;
    if(j->phase==AXD_SPG_RUNNING){j->phase=AXD_SPG_CANCELLED;if(j->register_stage==REG_SELECT_STEP)axd_si_select_cancel(&j->selector);}
    return j->phase;
}
