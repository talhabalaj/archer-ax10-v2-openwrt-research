/* SPDX-License-Identifier: MIT */
#include "core_reset_low.h"
#include <stddef.h>
enum stage { ENTRY, RESET, SAVE_INDEX, SELECT_AUX, SELECT_STEP, UP_BEGIN, UP_STEP, RESET_AUX, RESTORE_BEGIN, RESTORE_STEP, BUS_TAIL, MCTRL_STEP, TAIL_ST1, TAIL_W1, TAIL_ST2, TAIL_W2, FINISH };
struct bridge { struct axd_core_reset_low *j; axd_core_reset_low_io_fn io; void *opaque; };
static int call(struct bridge *b, struct axd_core_reset_low_operation *op,uint32_t *value)
{
    int rc;
    if(!b->io) return -1;
    b->j->attempted_ops++;rc=b->io(b->opaque,op,value);if(!rc)b->j->completed_ops++;return rc;
}
static int ai_io(void *opaque,const struct axd_low_ai_operation *a,uint32_t *v) {struct bridge *b=opaque;struct axd_core_reset_low_operation o={0};o.kind=AXD_LOW_AI;o.wrapper_mapping=b->j->context.state.primary_wrapper;o.ai=*a;return call(b,&o,v);}
static int d11_ai_io(void *opaque,const struct axd_ai_reset_operation *a,uint32_t *v)
{
    struct axd_low_ai_operation translated={(enum axd_low_ai_kind)a->kind,a->wrapper,a->offset,a->value};
    return ai_io(opaque,&translated,v);
}
static int si_io(void *opaque,const struct axd_si_operation *a,uint32_t *v) {struct bridge *b=opaque;struct axd_core_reset_low_operation o={0};o.kind=AXD_LOW_SELECT;o.select=*a;return call(b,&o,v);}
static int core_io(void *opaque,const struct axd_si_core_operation *a,uint32_t *v) {struct bridge *b=opaque;struct axd_core_reset_low_operation o={0};o.kind=AXD_LOW_CORE;o.core=*a;return call(b,&o,v);}
static int d11_io(void *opaque,const struct axd_d11_operation *a,uint32_t *v)
{
    struct bridge *b=opaque;
    struct axd_core_reset_low_operation o={0};
    /* mctrl commits its software cache before its first barrier. Publish that
     * snapshot before an outer callback can observe or fail this operation. */
    if(b->j->stage==MCTRL_STEP)
        b->j->context.mctrl=b->j->mctrl.context;
    o.kind=AXD_LOW_D11;o.d11=*a;
    return call(b,&o,v);
}
static void fail(struct axd_core_reset_low *j) {j->phase=AXD_LOW_FAILED;j->error=AXD_LOW_CHILD_ERROR;}
static int selected(struct axd_core_reset_low *j, struct bridge *b)
{
    enum axd_si_phase p=axd_si_select_step(&j->selector,si_io,b);
    j->context.state=j->selector.state;
    if(p==AXD_SI_FAILED) fail(j);
    else if(p==AXD_SI_DEPENDENCY) j->phase=AXD_LOW_DEPENDENCY;
    else if(p==AXD_SI_CANCELLED && j->phase==AXD_LOW_RUNNING) j->phase=AXD_LOW_CANCELLED;
    return p==AXD_SI_TRACE_COMPLETE && j->phase==AXD_LOW_RUNNING;
}
static int select_begin(struct axd_core_reset_low *j,enum axd_si_command cmd,uint32_t which,unsigned secondary)
{
    if(axd_si_select_begin_wrapper(&j->selector,cmd,which,0,secondary,&j->context.si,&j->context.state)) {fail(j);return 0;}return 1;
}
/* The reset callback supplied to this target wrapper is the actual SI reset.
 * Its AI function selects secondary first when physical metadata says it exists,
 * then restores selector0 and always resets primary. No successful callout stub. */
static void reset_step(struct axd_core_reset_low *j,struct bridge *b)
{
    struct axd_low_ai_context c={0};const struct axd_si_core *core;
    uint32_t index=j->context.state.current_index;
    if(j->reset_stage==0) {
        if(j->context.si.interconnect==0) {j->phase=AXD_LOW_DEPENDENCY;return;}
        if(j->context.si.interconnect!=1 && j->context.si.interconnect!=3 && j->context.si.interconnect!=4) {j->reset_stage=6;return;}
        if(!j->context.si.catalog) {j->phase=AXD_LOW_DEPENDENCY;return;}
        if(index>=j->context.si.core_count || index>=AXD_SI_MAX_CORES || index>=j->context.si.catalog_length) {fail(j);return;}
        core=&j->context.si.catalog[index];j->reset_secondary=core->secondary_physical!=0;
        if(j->reset_secondary) {if(select_begin(j,AXD_SI_BY_INDEX,index,1))j->reset_stage=1;}
        else j->reset_stage=2;
    } else if(j->reset_stage==1) {
        if(selected(j,b))j->reset_stage=2;
    } else if(j->reset_stage==2) {
        if(j->context.state.primary_wrapper==0) {j->phase=AXD_LOW_DEPENDENCY;return;}
        if(index>=j->context.si.catalog_length || !j->context.si.catalog) {fail(j);return;}
        core=&j->context.si.catalog[index];c.core_id=(uint16_t)core->core_id;c.revision=j->context.revision;c.interconnect=(uint8_t)j->context.si.interconnect;
        c.d11_special=core->core_id==0x812u && (j->context.revision==61 || (j->context.revision>=128 && j->context.revision<=132));
        c.bits=j->reset_auxiliary?0:j->effective_bits;c.reset_bits=j->reset_auxiliary?0:j->effective_reset_bits;
        j->reset_d11_child=core->core_id==0x812u && j->context.revision==130;
        if(j->reset_d11_child) {
            struct axd_ai_reset_context d={c.core_id,c.revision,c.interconnect,0,c.bits,c.reset_bits};
            if(axd_ai_reset_begin(&j->d11_ai,&d)) {fail(j);return;}
        } else if(axd_low_ai_begin(&j->ai,&c)) {fail(j);return;}
        j->reset_stage=3;
    } else if(j->reset_stage==3) {
        int failed,complete;
        if(j->reset_d11_child) {
            enum axd_ai_reset_phase p=axd_ai_reset_step(&j->d11_ai,d11_ai_io,b);
            failed=p==AXD_AI_FAILED;complete=p==AXD_AI_TRACE_COMPLETE;
        } else {
            enum axd_low_ai_phase p=axd_low_ai_step(&j->ai,ai_io,b);
            failed=p==AXD_LOW_AI_FAILED;complete=p==AXD_LOW_AI_TRACE_COMPLETE;
        }
        if(failed)fail(j);
        else if(complete && j->phase==AXD_LOW_RUNNING) {
            if(j->reset_secondary) {j->reset_secondary=0;if(select_begin(j,AXD_SI_BY_INDEX,index,0))j->reset_stage=4;}
            else j->reset_stage=6;
        }
    } else if(j->reset_stage==4) {
        if(selected(j,b))j->reset_stage=2;
    } else {j->reset_stage=0;j->stage=j->reset_auxiliary?RESTORE_BEGIN:SAVE_INDEX;}
}
int axd_core_reset_low_begin(struct axd_core_reset_low *j,const struct axd_core_reset_low_context *c)
{
    struct axd_core_reset_low_context copy;const struct axd_core_reset_low empty={0};
    if(!j)return -1;if(c){copy=*c;c=&copy;}*j=empty;j->phase=AXD_LOW_FAILED;j->error=AXD_LOW_BAD_ARGUMENT;
    if(!c || c->hw_present>1 || c->reset_callback_present>1 || c->w_present>1)return -1;
    j->context=*c;j->phase=AXD_LOW_RUNNING;j->error=AXD_LOW_NO_ERROR;return 0;
}
enum axd_core_reset_low_phase axd_core_reset_low_step(struct axd_core_reset_low *j,axd_core_reset_low_io_fn io,void *opaque)
{
    struct bridge b={j,io,opaque};struct axd_si_core_context c={0};struct axd_d11_operation d={AXD_D11_STORE_BARRIER,0,0};uint32_t ignored=0;
    if(!j)return AXD_LOW_INVALID;if(j->phase!=AXD_LOW_RUNNING)return j->phase;
    switch(j->stage) {
    case ENTRY:
        if(!j->context.hw_present || !j->context.reset_callback_present){j->phase=AXD_LOW_DONE;break;}
        j->effective_bits=j->context.flags;j->effective_reset_bits=j->context.reset_bits;
        if(j->context.revision>129) {
            j->effective_reset_bits|=0x20;
            if(j->context.revision!=130 || (j->context.w_present && j->context.w138==2))j->effective_bits|=0x20;
            else j->effective_bits&=~0x20u;
        }
        j->stage=RESET;break;
    case RESET:case RESET_AUX:reset_step(j,&b);break;
    case SAVE_INDEX:j->saved_index=j->context.state.current_index;j->stage=SELECT_AUX;break;
    case SELECT_AUX:if(select_begin(j,AXD_SI_BY_ID,0x849,0))j->stage=SELECT_STEP;break;
    case SELECT_STEP:if(selected(j,&b)){if(j->selector.return_value==0)j->phase=AXD_LOW_DONE;else j->stage=UP_BEGIN;}break;
    case UP_BEGIN:
        c.interconnect=j->context.si.interconnect;c.chip_id=j->context.chip_id;c.core_id=0x849;c.primary_wrapper=j->context.state.primary_wrapper;c.wrapper_known=1;
        if(axd_si_core_begin(&j->core,AXD_SICO_IS_UP,0,0,&c))fail(j);else j->stage=UP_STEP;break;
    case UP_STEP:{enum axd_si_core_phase p=axd_si_core_step(&j->core,core_io,&b);if(p==AXD_SICO_FAILED)fail(j);else if(p==AXD_SICO_DEPENDENCY)j->phase=AXD_LOW_DEPENDENCY;else if(p==AXD_SICO_DONE && j->phase==AXD_LOW_RUNNING){j->reset_auxiliary=1;j->stage=j->core.result?RESTORE_BEGIN:RESET_AUX;}}break;
    case RESTORE_BEGIN:if(select_begin(j,AXD_SI_BY_INDEX,j->saved_index,0))j->stage=RESTORE_STEP;break;
    case RESTORE_STEP:if(selected(j,&b))j->stage=BUS_TAIL;break;
    case BUS_TAIL:
        if(j->context.si.bus!=0){j->phase=AXD_LOW_DONE;break;}
        if(axd_mctrl_begin(&j->mctrl,UINT32_MAX,0x400,&j->context.mctrl))fail(j);else j->stage=MCTRL_STEP;break;
    case MCTRL_STEP:{enum axd_mctrl_phase p=axd_mctrl_step(&j->mctrl,d11_io,&b);j->context.mctrl=j->mctrl.context;if(p==AXD_MCTRL_FAILED)fail(j);else if(p==AXD_MCTRL_DONE && j->phase==AXD_LOW_RUNNING)j->stage=TAIL_ST1;}break;
    case TAIL_ST1:case TAIL_W1:case TAIL_ST2:case TAIL_W2:
        if(j->stage==TAIL_W1 || j->stage==TAIL_W2){d.kind=AXD_D11_WRITE16;d.offset=j->stage==TAIL_W1?0xa08:0xa06;}
        if(d11_io(&b,&d,&ignored)) {j->phase=AXD_LOW_FAILED;j->error=AXD_LOW_IO_ERROR;}else j->stage++;break;
    case FINISH:j->phase=AXD_LOW_DONE;break;
    default:fail(j);break;
    }
    return j->phase;
}
enum axd_core_reset_low_phase axd_core_reset_low_cancel(struct axd_core_reset_low *j)
{
    if(!j)return AXD_LOW_INVALID;
    if(j->phase==AXD_LOW_RUNNING){j->phase=AXD_LOW_CANCELLED;axd_ai_reset_cancel(&j->d11_ai);axd_low_ai_cancel(&j->ai);axd_si_select_cancel(&j->selector);axd_si_core_cancel(&j->core);axd_mctrl_cancel(&j->mctrl);}return j->phase;
}
