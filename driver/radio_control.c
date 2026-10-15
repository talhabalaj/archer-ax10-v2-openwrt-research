/* SPDX-License-Identifier: MIT */
#include "radio_control.h"
enum stage { ENTRY, SWITCH_CONTEXT, SWITCH_PI, COMMON_INFO, COMMON_DISPATCH, COMMON_SLOT,
    REG_INFO, REG_DISPATCH, REG_CONTEXT, REG_PI, REG_AC, REG_CHANMGR,
    REG_DATA, REG_STORE, PHY_BIND, CLEAR47E, OFF_CLEAR47E, SHARED_BIND,
    OFF_WRITES, OFF_MODIFY, OFF_PUBLIC, OFF_ID, OFF_REV, OFF_CORE,
    OFF_TAIL_WRITES, OFF_CHIP, ON_PUBLIC, ON_ID, ON_WRITES, ON_DELAY,
    ON_LAST_WRITE, ON_REV, ON_SPAN, ON_RECORD, ON_RECORD_DONE, ON_MODIFY, ON_END,
    FINISH, RUN_PACKED, RUN_PHY, RUN_RADIO };
enum radio_stage { R_ENTRY, R_PUBLIC, R_OFFSET, R_ST, R_ADDRESS, R_READ,
    R_SY, R_SHARED, R_COUNTER, R_FLUSH_READ, R_FLUSH_SY, R_CHECK, R_WRITE_ST,
    R_WRITE_ADDRESS, R_DATA_ST, R_WRITE_DATA, R_BUS_REV, R_POST_READ,
    R_POST_SY, R_RETURN };
struct relay { struct axd_radio_control *j; axd_d11_io_fn io; void *opaque; };
static void fail(struct axd_radio_control *j,enum axd_radio_control_error error)
{j->phase=AXD_RADIO_CTL_FAILED;j->error=error;}
static void dependency(struct axd_radio_control *j,enum axd_radio_control_dependency d)
{j->phase=AXD_RADIO_CTL_DEPENDENCY;j->dependency=d;}
static int fact(struct axd_radio_control *j,enum axd_radio_control_field field,struct axd_wake_u32 v,unsigned pointer)
{
    j->last_field=field;j->metadata_accesses++;
    if(v.known==0)dependency(j,AXD_RADIO_CTL_METADATA);
    else if(v.known!=1)fail(j,AXD_RADIO_CTL_BAD_ARGUMENT);
    else if(pointer && !v.value)fail(j,AXD_RADIO_CTL_BINDING);
    else return 1;
    return 0;
}
static int perform(struct relay *r,struct axd_d11_operation op,uint32_t *reply)
{
    struct axd_radio_control *j=r->j;
    if(j->stage==RUN_PACKED)j->context.hardware=j->packed_job.context;
    else if(j->stage==RUN_PHY)j->context.hardware.phy=j->phy_job.context;
    if(!r->io){fail(j,AXD_RADIO_CTL_BAD_ARGUMENT);return -1;}
    j->attempted_ops++;
    if(r->io(r->opaque,&op,reply)){fail(j,AXD_RADIO_CTL_IO_ERROR);return -1;}
    j->completed_ops++;return 0;
}
static int child_io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{return perform(opaque,*op,reply);}
static void packed(struct axd_radio_control *j,uint16_t selector,uint16_t value,unsigned resume)
{
    if(axd_phy_packed_write_begin(&j->packed_job,selector,value,&j->context.hardware))fail(j,AXD_RADIO_CTL_CHILD_ERROR);
    else {j->stage=RUN_PACKED;j->after_child=resume;}
}
static void radio(struct axd_radio_control *j,unsigned command,uint16_t selector,uint16_t mask,uint16_t value,unsigned resume)
{j->radio_command=command;j->selector=selector;j->mask=mask;j->value=value;j->radio_stage=R_ENTRY;j->stage=RUN_RADIO;j->after_radio=resume;}
static void child(struct axd_radio_control *j,struct relay *r)
{
    unsigned done,running,cancelled,dep;
    if(j->stage==RUN_PACKED) {
        enum axd_phy_table_write_phase p=axd_phy_table_write_step(&j->packed_job,child_io,r);
        j->context.hardware=j->packed_job.context;
        done=p==AXD_TW_TRACE_COMPLETE;running=p==AXD_TW_RUNNING;cancelled=p==AXD_TW_CANCELLED;dep=p==AXD_TW_DEPENDENCY;
    } else {
        enum axd_phy_phase p=axd_phy_register_step(&j->phy_job,child_io,r);
        j->context.hardware.phy=j->phy_job.context;
        done=p==AXD_PHY_TRACE_COMPLETE;running=p==AXD_PHY_RUNNING;cancelled=p==AXD_PHY_CANCELLED;dep=p==AXD_PHY_DEPENDENCY;
    }
    if(j->phase!=AXD_RADIO_CTL_RUNNING)return;
    if(done)j->stage=j->after_child;
    else if(dep)dependency(j,AXD_RADIO_CTL_METADATA);
    else if(cancelled)j->phase=AXD_RADIO_CTL_CANCELLED;
    else if(!running)fail(j,AXD_RADIO_CTL_CHILD_ERROR);
}
static void radio_step(struct axd_radio_control *j,struct relay *r)
{
    struct axd_d11_operation op={0};uint32_t reply=0;unsigned direct=0,s=j->radio_stage;
    switch(s) {
    case R_ENTRY:
        if(j->radio_command==AXD_RADIO_CTL_WRITE){j->write_value=j->value;j->radio_stage=R_SHARED;}
        else if(j->selector==0xffff){j->result=0xffff;j->result_valid=1;j->write_value=(uint16_t)((0xffffu&~j->mask)|(j->value&j->mask));j->radio_stage=j->radio_command==AXD_RADIO_CTL_READ?R_RETURN:R_SHARED;}
        else j->radio_stage=R_PUBLIC;
        break;
    case R_PUBLIC:if(fact(j,AXD_RADIO_CTL_PUBLIC,j->context.public_pointer,1))j->radio_stage=R_OFFSET;break;
    case R_OFFSET:
        if(fact(j,AXD_RADIO_CTL_READ_OFFSET,j->context.radio_read_offset,0)) {
            if(j->context.radio_read_offset.value>0xffff)fail(j,AXD_RADIO_CTL_BAD_ARGUMENT);
            else j->radio_stage=R_ST;
        }break;
    case R_ST:case R_WRITE_ST:case R_DATA_ST:op.kind=AXD_D11_STORE_BARRIER;direct=1;break;
    case R_ADDRESS:case R_WRITE_ADDRESS:op.kind=AXD_D11_WRITE16;op.offset=0x3d8;op.value=j->selector|(s==R_ADDRESS?j->context.radio_read_offset.value:0);direct=1;break;
    case R_READ:op.kind=AXD_D11_READ16;op.offset=0x3da;direct=1;break;
    case R_SY:case R_FLUSH_SY:case R_POST_SY:op.kind=AXD_D11_FULL_BARRIER;direct=1;break;
    case R_SHARED:if(fact(j,AXD_RADIO_CTL_SHARED,j->context.shared_pointer,1))j->radio_stage=R_COUNTER;break;
    case R_COUNTER:
        if(j->context.hardware.shared_bus_word==1) {
            j->context.hardware.phy.bookkeeping=(uint16_t)(j->context.hardware.phy.bookkeeping+1u);
            j->software_stores++;
            j->radio_stage=j->context.hardware.phy.bookkeeping>=j->context.hardware.write_threshold?R_FLUSH_READ:R_CHECK;
        }else j->radio_stage=R_CHECK;
        break;
    case R_FLUSH_READ:op.kind=AXD_D11_READ32;op.offset=0x120;direct=1;break;
    case R_CHECK:j->radio_stage=j->selector==0xffff?R_RETURN:R_WRITE_ST;break;
    case R_WRITE_DATA:op.kind=AXD_D11_WRITE16;op.offset=0x3da;op.value=j->write_value;direct=1;break;
    case R_BUS_REV:
        if(j->context.hardware.shared_bus_word!=2)j->radio_stage=R_RETURN;
        else if(fact(j,AXD_RADIO_CTL_BUS_REVISION,j->context.shared_bus_revision,0))j->radio_stage=j->context.shared_bus_revision.value<=3?R_POST_READ:R_RETURN;
        break;
    case R_POST_READ:op.kind=AXD_D11_READ16;op.offset=0x3e0;direct=1;break;
    case R_RETURN:j->stage=j->after_radio;break;
    default:fail(j,AXD_RADIO_CTL_BAD_ARGUMENT);break;
    }
    if(direct && !perform(r,op,&reply)) {
        if(s==R_READ)j->sample=(uint16_t)reply;
        if(s==R_FLUSH_READ || s==R_POST_READ)j->flush_reads++;
        if(s==R_SY) {
            j->context.hardware.phy.bookkeeping=0;j->software_stores++;
            j->result=j->sample;j->result_valid=1;
            j->write_value=(uint16_t)((j->sample&~j->mask)|(j->value&j->mask));
            j->radio_stage=j->radio_command==AXD_RADIO_CTL_READ?R_RETURN:R_SHARED;
        }else if(s==R_FLUSH_SY){j->context.hardware.phy.bookkeeping=0;j->software_stores++;j->radio_stage=R_CHECK;}
        else if(s==R_POST_SY)j->radio_stage=R_RETURN;
        else j->radio_stage++;
    }
}
int axd_radio_control_begin(struct axd_radio_control *j,enum axd_radio_control_command command,const struct axd_radio_control_context *context)
{
    struct axd_radio_control_context copy;const struct axd_radio_control empty={0};
    if(!j)return -1;if(context){copy=*context;context=&copy;}*j=empty;fail(j,AXD_RADIO_CTL_BAD_ARGUMENT);
    if(!context || command<AXD_RADIO_CTL_AC_SWITCH_OFF || command>AXD_RADIO_CTL_COMMON_INIT)return -1;
    j->context=*context;j->command=command;
    if(context->hardware.phy.core!=130 || context->hardware.phy.minor>1 || context->hardware.phy.phy_type!=0xb || context->hardware.phy.phy_revision!=51 || context->hardware.phy.phy_core_count>4){fail(j,AXD_RADIO_CTL_UNSUPPORTED);return -1;}
    j->phase=AXD_RADIO_CTL_RUNNING;j->error=AXD_RADIO_CTL_NO_ERROR;return 0;
}
int axd_radio_register_begin(struct axd_radio_control *j,enum axd_radio_control_command command,uint16_t selector,uint16_t mask,uint16_t value,const struct axd_radio_control_context *context)
{
    if(axd_radio_control_begin(j,AXD_RADIO_CTL_AC_SWITCH_OFF,context))return -1;
    if(command<AXD_RADIO_CTL_READ || command>AXD_RADIO_CTL_MODIFY || (command==AXD_RADIO_CTL_READ && (mask||value)) || (command==AXD_RADIO_CTL_WRITE && mask)){fail(j,AXD_RADIO_CTL_BAD_ARGUMENT);return -1;}
    j->command=command;j->selector=selector;j->mask=mask;j->value=value;return 0;
}
enum axd_radio_control_phase axd_radio_control_step(struct axd_radio_control *j,axd_d11_io_fn transport,void *opaque)
{
    struct relay r={j,transport,opaque};
    if(!j)return AXD_RADIO_CTL_INVALID;if(j->phase!=AXD_RADIO_CTL_RUNNING)return j->phase;
    switch(j->stage) {
    case ENTRY:
        if(j->command==AXD_RADIO_CTL_READ && j->selector==0xffff)radio(j,j->command,j->selector,0,0,FINISH);
        else if(j->command==AXD_RADIO_CTL_AC_SWITCH_OFF || j->command==AXD_RADIO_CTL_AC_SWITCH_ON)j->stage=SWITCH_CONTEXT;
        else j->stage=j->command==AXD_RADIO_CTL_AC_ON_REGISTERED?REG_INFO:PHY_BIND;
        break;
    case SWITCH_CONTEXT:if(fact(j,AXD_RADIO_CTL_CALLBACK_CONTEXT,j->context.callback_context,1))j->stage=SWITCH_PI;break;
    case SWITCH_PI:
        if(fact(j,AXD_RADIO_CTL_CALLBACK_PI,j->context.callback_pi,1)){j->registered_entry=1;j->stage=PHY_BIND;}
        break;
    case COMMON_INFO:if(fact(j,AXD_RADIO_CTL_RADIO_INFO,j->context.radio_info,1))j->stage=COMMON_DISPATCH;break;
    case COMMON_DISPATCH:if(fact(j,AXD_RADIO_CTL_DISPATCH,j->context.dispatch,1))j->stage=COMMON_SLOT;break;
    case COMMON_SLOT:
        if(fact(j,AXD_RADIO_CTL_INIT_CALLBACK,j->context.init_callback,0)) {
            if(j->context.init_callback.value)dependency(j,AXD_RADIO_CTL_COMMON_CALLBACK);
            else j->stage=FINISH;
        }break;
    case REG_INFO:if(fact(j,AXD_RADIO_CTL_RADIO_INFO,j->context.radio_info,1))j->stage=REG_DISPATCH;break;
    case REG_DISPATCH:if(fact(j,AXD_RADIO_CTL_DISPATCH,j->context.dispatch,1))j->stage=REG_CONTEXT;break;
    case REG_CONTEXT:if(fact(j,AXD_RADIO_CTL_CALLBACK_CONTEXT,j->context.callback_context,1))j->stage=REG_AC;break;
    case REG_PI:if(fact(j,AXD_RADIO_CTL_CALLBACK_PI,j->context.callback_pi,1))j->stage=PHY_BIND;break;
    case REG_AC:if(fact(j,AXD_RADIO_CTL_AC,j->context.ac_pointer,1))j->stage=REG_CHANMGR;break;
    case REG_CHANMGR:if(fact(j,AXD_RADIO_CTL_CHANMGR,j->context.chanmgr_pointer,1))j->stage=REG_DATA;break;
    case REG_DATA:if(fact(j,AXD_RADIO_CTL_CHANMGR_DATA,j->context.chanmgr_data_pointer,1))j->stage=REG_STORE;break;
    case REG_STORE:j->context.chanmgr_data10=0;j->software_stores++;j->registered_entry=1;j->stage=REG_PI;break;
    case PHY_BIND:
        if(fact(j,AXD_RADIO_CTL_PHY,j->context.phy_pointer,1)) {
            if(j->registered_entry && j->context.callback_pi.value!=j->context.phy_pointer.value)fail(j,AXD_RADIO_CTL_BINDING);
            else if(j->command==AXD_RADIO_CTL_COMMON_INIT)j->stage=COMMON_INFO;
            else if(j->command>=AXD_RADIO_CTL_READ)radio(j,j->command,j->selector,j->mask,j->value,FINISH);
            else j->stage=CLEAR47E;
        }break;
    case CLEAR47E:j->context.pi47e=0;j->software_stores++;j->stage=j->command==AXD_RADIO_CTL_AC_SWITCH_OFF?OFF_CLEAR47E:ON_PUBLIC;break;
    case OFF_CLEAR47E:j->context.pi47e=0;j->software_stores++;j->stage=SHARED_BIND;break;
    case SHARED_BIND:if(fact(j,AXD_RADIO_CTL_SHARED,j->context.shared_pointer,1))j->stage=OFF_WRITES;break;
    case OFF_WRITES: {
        const uint16_t selectors[]={0x473e,0x4739,0x473a,0x4725,0x4729,0x4721,0x4728,0x4720};
        const uint16_t values[]={0,0,0,0x1fff,0,0xffff,0,0x3ff};
        unsigned i=j->index++;packed(j,selectors[i],values[i],j->index==8?OFF_MODIFY:OFF_WRITES);break;
    }
    case OFF_MODIFY:
        if(axd_phy_register_begin(&j->phy_job,AXD_PHY_MODIFY,0x408,2,0,&j->context.hardware.phy))fail(j,AXD_RADIO_CTL_CHILD_ERROR);
        else {j->stage=RUN_PHY;j->after_child=OFF_PUBLIC;}break;
    case OFF_PUBLIC:if(fact(j,AXD_RADIO_CTL_PUBLIC,j->context.public_pointer,1))j->stage=OFF_ID;break;
    case OFF_ID:
        if(fact(j,AXD_RADIO_CTL_ID,j->context.radio_id,0)) {
            if(j->context.radio_id.value>0xffff)fail(j,AXD_RADIO_CTL_BAD_ARGUMENT);
            else j->stage=j->context.radio_id.value==0x2069?OFF_REV:OFF_TAIL_WRITES;
        }
        break;
    case OFF_REV:
        if(fact(j,AXD_RADIO_CTL_REVISION,j->context.radio_revision,0)) {
            if(j->context.radio_revision.value>0xff)fail(j,AXD_RADIO_CTL_BAD_ARGUMENT);
            else j->stage=(j->context.radio_revision.value>>4)==2 && j->context.hardware.phy.phy_core_count?OFF_CORE:OFF_TAIL_WRITES;
        }
        break;
    case OFF_CORE: {
        unsigned i=j->core_index++;radio(j,AXD_RADIO_CTL_MODIFY,(uint16_t)((i<<9)|0xc),0x80,0,j->core_index==j->context.hardware.phy.phy_core_count?OFF_TAIL_WRITES:OFF_CORE);break;
    }
    case OFF_TAIL_WRITES:if(j->index++==8)packed(j,0x417,0,OFF_TAIL_WRITES);else packed(j,0x416,1,OFF_CHIP);break;
    case OFF_CHIP:
        if(fact(j,AXD_RADIO_CTL_CHIP,j->context.chip_id,0)) {
            if(j->context.chip_id.value==63178)j->stage=FINISH;
            else dependency(j,AXD_RADIO_CTL_OTHER_OFF_TAIL);
        }break;
    case ON_PUBLIC:if(fact(j,AXD_RADIO_CTL_PUBLIC,j->context.public_pointer,1))j->stage=ON_ID;break;
    case ON_ID:
        if(fact(j,AXD_RADIO_CTL_ID,j->context.radio_id,0)) {
            if(j->context.radio_id.value==0x50e0)j->stage=ON_WRITES;
            else dependency(j,AXD_RADIO_CTL_OTHER_ON_DISPATCH);
        }break;
    case ON_WRITES: {
        const uint16_t selectors[]={0x4728,0x4720,0x415,0x40e,0x40c,0x408,0x417,0x416,0x408};
        const uint16_t values[]={0,0x3ff,0,0,0x2000,0,0,0xd,7};
        if(!j->index && !fact(j,AXD_RADIO_CTL_SHARED,j->context.shared_pointer,1))break;
        {unsigned i=j->index++;packed(j,selectors[i],values[i],j->index==9?ON_DELAY:ON_WRITES);}break;
    }
    case ON_DELAY: {
        struct axd_d11_operation op={AXD_D11_DELAY_US,0,1000};uint32_t ignored=0;
        if(!perform(&r,op,&ignored))j->stage=ON_LAST_WRITE;break;
    }
    case ON_LAST_WRITE:packed(j,0x408,6,ON_REV);break;
    case ON_REV:
        if(fact(j,AXD_RADIO_CTL_REVISION,j->context.radio_revision,0)) {
            if(j->context.radio_revision.value>3)fail(j,AXD_RADIO_CTL_BINDING);
            else j->stage=ON_SPAN;
        }break;
    case ON_SPAN:
        j->last_field=AXD_RADIO_CTL_PREFREGS;j->metadata_accesses++;
        if(!j->context.prefregs)dependency(j,AXD_RADIO_CTL_METADATA);
        else j->stage=ON_RECORD;break;
    case ON_RECORD: {
        const uint8_t *p;uint16_t selector,value;
        if(j->prefreg_cursor>j->context.prefregs_length || j->context.prefregs_length-j->prefreg_cursor<2){fail(j,AXD_RADIO_CTL_BOUNDS);break;}
        p=j->context.prefregs+j->prefreg_cursor;selector=(uint16_t)(p[0]|((unsigned)p[1]<<8));
        if(selector==0xffff){j->stage=ON_MODIFY;break;}
        if(j->context.prefregs_length-j->prefreg_cursor<4){fail(j,AXD_RADIO_CTL_BOUNDS);break;}
        value=(uint16_t)(p[2]|((unsigned)p[3]<<8));
        radio(j,AXD_RADIO_CTL_WRITE,selector,0,value,ON_RECORD_DONE);break;
    }
    case ON_RECORD_DONE:j->prefreg_cursor+=4;j->prefreg_writes++;j->stage=ON_RECORD;break;
    case ON_MODIFY:radio(j,AXD_RADIO_CTL_MODIFY,0x88b,0x3f,1,ON_END);break;
    case ON_END:dependency(j,AXD_RADIO_CTL_LOGEN_PLL_CONTINUATION);break;
    case FINISH:j->phase=AXD_RADIO_CTL_TRACE_COMPLETE;break;
    case RUN_RADIO:radio_step(j,&r);break;
    default:child(j,&r);break;
    }
    return j->phase;
}
enum axd_radio_control_phase axd_radio_control_cancel(struct axd_radio_control *j)
{
    if(!j)return AXD_RADIO_CTL_INVALID;
    if(j->phase==AXD_RADIO_CTL_RUNNING){j->phase=AXD_RADIO_CTL_CANCELLED;
        if(j->stage==RUN_PACKED)axd_phy_table_write_cancel(&j->packed_job);
        else if(j->stage==RUN_PHY)axd_phy_register_cancel(&j->phy_job);
    }return j->phase;
}
