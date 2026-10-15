/* SPDX-License-Identifier: MIT */
#include "si_core.h"
#include <stddef.h>
int axd_si_core_begin(struct axd_si_core_job *j, enum axd_si_core_command command, uint32_t mask, uint32_t value, const struct axd_si_core_context *c)
{
    struct axd_si_core_context copy;
    const struct axd_si_core_job empty={0};
    if(!j) return -1;
    if(c) {copy=*c;c=&copy;}
    *j=empty;j->phase=AXD_SICO_FAILED;j->error=AXD_SICO_BAD_ARGUMENT;
    if(!c || c->wrapper_known>1 || command<AXD_SICO_IS_UP || command>AXD_SICO_FLAGS) return -1;
    j->context=*c;j->command=command;j->mask=mask;j->value=value;j->phase=AXD_SICO_RUNNING;j->error=AXD_SICO_NO_ERROR;
    return 0;
}
enum axd_si_core_phase axd_si_core_step(struct axd_si_core_job *j, axd_si_core_io_fn io, void *opaque)
{
    struct axd_si_core_operation op={AXD_SICO_READ32,0,0,0}; uint32_t read=0;
    if(!j) return AXD_SICO_INVALID;
    if(j->phase!=AXD_SICO_RUNNING) return j->phase;
    if(j->stage==0) {
        if(j->context.interconnect==0) {j->phase=AXD_SICO_DEPENDENCY;return j->phase;}
        if(j->context.interconnect!=1 && j->context.interconnect!=3 && j->context.interconnect!=4) {j->phase=AXD_SICO_DONE;return j->phase;}
        if(j->command==AXD_SICO_FLAGS && (j->context.core_id==0x827u || ((j->context.chip_id==0xcf26u || (j->context.chip_id&~8u)==0xcf12u) && j->context.core_id==0x50bu))) {j->phase=AXD_SICO_DONE;return j->phase;}
        if(!j->context.wrapper_known || j->context.primary_wrapper==0) {j->phase=AXD_SICO_DEPENDENCY;return j->phase;}
        j->stage=1;return j->phase;
    }
    op.wrapper=j->context.primary_wrapper;
    if(j->command==AXD_SICO_IS_UP) {
        switch(j->stage) {
        case 1: op.offset=0x408;break;
        case 2: op.kind=AXD_SICO_FULL_BARRIER;break;
        case 3: if((j->last_read&3u)!=1u) {j->phase=AXD_SICO_DONE;return j->phase;} j->stage=4;return j->phase;
        case 4: op.offset=0x800;break;
        case 5: op.kind=AXD_SICO_FULL_BARRIER;break;
        default:j->result=(j->last_read&1u)^1u;j->phase=AXD_SICO_DONE;return j->phase;
        }
    } else {
        switch(j->stage) {
        case 1: op.offset=0x408;break;
        case 2:op.kind=AXD_SICO_FULL_BARRIER;break;
        case 3:if((j->mask|j->value)==0) {j->result=j->last_read;j->phase=AXD_SICO_DONE;return j->phase;} j->value=(j->last_read&~j->mask)|j->value; j->stage=4;return j->phase;
        case 4:op.kind=AXD_SICO_STORE_BARRIER;break;
        case 5:op.kind=AXD_SICO_WRITE32;op.offset=0x408;op.value=j->value;break;
        case 6:op.offset=0x408;break;
        case 7:op.kind=AXD_SICO_FULL_BARRIER;break;
        default:j->result=j->last_read;j->phase=AXD_SICO_DONE;return j->phase;
        }
    }
    if(!io) {j->phase=AXD_SICO_FAILED;j->error=AXD_SICO_BAD_ARGUMENT;return j->phase;}
    j->attempted_ops++;
    if(io(opaque,&op,&read)!=0) {j->phase=AXD_SICO_FAILED;j->error=AXD_SICO_IO_ERROR;return j->phase;}
    j->completed_ops++;if(op.kind==AXD_SICO_READ32) j->last_read=read;j->stage++;
    return j->phase;
}
enum axd_si_core_phase axd_si_core_cancel(struct axd_si_core_job *j) {if(!j)return AXD_SICO_INVALID;if(j->phase==AXD_SICO_RUNNING)j->phase=AXD_SICO_CANCELLED;return j->phase;}
