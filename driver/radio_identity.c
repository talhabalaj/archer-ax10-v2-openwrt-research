/* SPDX-License-Identifier: MIT */
#include "radio_identity.h"
#include <stddef.h>
enum stage { ENTRY, CALLBACK_CONTEXT, CALLBACK_PI, PHY, ADDRESS0_ST, ADDRESS0,
    HIGH_READ, HIGH_SY, ADDRESS1_ST, ADDRESS1, LOW_READ, LOW_SY, COMBINE,
    PUBLIC, STORE_ID, STORE_REVISION, STORE_VERSION, FINISH };
static void fail(struct axd_radio_identity *j,enum axd_radio_identity_error error)
{j->phase=AXD_RID_FAILED;j->error=error;}
static int pointer(struct axd_radio_identity *j,enum axd_radio_identity_field field,struct axd_wake_u32 value)
{
    j->last_field=field;j->metadata_accesses++;
    if(value.known==0)j->phase=AXD_RID_DEPENDENCY;
    else if(value.known!=1)fail(j,AXD_RID_BAD_ARGUMENT);
    else if(value.value==0)fail(j,AXD_RID_BINDING);
    else return 1;
    return 0;
}
int axd_radio_identity_begin(struct axd_radio_identity *j,enum axd_radio_identity_command command,
                              uint32_t input_code,const struct axd_radio_identity_context *context)
{
    const struct axd_radio_identity empty={0};struct axd_radio_identity_context copy;
    if(!j)return -1;
    if(context){copy=*context;context=&copy;}
    *j=empty;fail(j,AXD_RID_BAD_ARGUMENT);
    if(!context || command<AXD_RID_QUERY_DIRECT || command>AXD_RID_PARSE_COMMON)return -1;
    j->context=*context;j->command=command;j->input_code=input_code;
    if(context->core!=130 || context->minor>1){fail(j,AXD_RID_UNSUPPORTED);return -1;}
    j->phase=AXD_RID_RUNNING;j->error=AXD_RID_NO_ERROR;return 0;
}
enum axd_radio_identity_phase axd_radio_identity_step(struct axd_radio_identity *j,axd_d11_io_fn io,void *opaque)
{
    struct axd_d11_operation op={0};uint32_t reply=0;unsigned direct=0;
    if(!j)return AXD_RID_INVALID;
    if(j->phase!=AXD_RID_RUNNING)return j->phase;
    switch(j->stage) {
    case ENTRY:j->stage=j->command==AXD_RID_QUERY_REGISTERED?CALLBACK_CONTEXT:PHY;break;
    case CALLBACK_CONTEXT:if(pointer(j,AXD_RID_CALLBACK_CONTEXT,j->context.callback_context))j->stage=CALLBACK_PI;break;
    case CALLBACK_PI:if(pointer(j,AXD_RID_CALLBACK_PI,j->context.callback_pi))j->stage=PHY;break;
    case PHY:
        if(pointer(j,AXD_RID_PHY,j->context.phy_pointer)) {
            if(j->command==AXD_RID_QUERY_REGISTERED && j->context.callback_pi.value!=j->context.phy_pointer.value)fail(j,AXD_RID_BINDING);
            else if(j->command==AXD_RID_PARSE_AC || j->command==AXD_RID_PARSE_COMMON){j->result_code=j->input_code;j->result_valid=1;j->stage=PUBLIC;}
            else j->stage=ADDRESS0_ST;
        }
        break;
    case ADDRESS0_ST:case ADDRESS1_ST:op.kind=AXD_D11_STORE_BARRIER;direct=1;break;
    case ADDRESS0:case ADDRESS1:op.kind=AXD_D11_WRITE16;op.offset=0x3d8;op.value=j->stage==ADDRESS1;direct=1;break;
    case HIGH_READ:case LOW_READ:op.kind=AXD_D11_READ16;op.offset=0x3da;direct=1;break;
    case HIGH_SY:case LOW_SY:op.kind=AXD_D11_FULL_BARRIER;direct=1;break;
    case COMBINE:
        j->result_code=((uint32_t)j->high_sample<<16)|j->low_sample;j->result_valid=1;
        j->stage=j->command==AXD_RID_QUERY_PARSE_AC?PUBLIC:FINISH;break;
    case PUBLIC:if(pointer(j,AXD_RID_PUBLIC,j->context.public_pointer))j->stage=STORE_ID;break;
    case STORE_ID:
        j->context.radio_id=j->command==AXD_RID_PARSE_COMMON?(uint16_t)(j->result_code>>12):(uint16_t)j->result_code;
        j->decoded_stores|=1;j->stage=STORE_REVISION;break;
    case STORE_REVISION:
        j->context.radio_revision=(uint8_t)(j->result_code>>(j->command==AXD_RID_PARSE_COMMON?28:16));
        j->decoded_stores|=2;j->stage=STORE_VERSION;break;
    case STORE_VERSION:
        j->context.radio_version=j->command==AXD_RID_PARSE_COMMON?(uint8_t)(j->result_code&15u):0;
        j->decoded_stores|=4;j->stage=FINISH;break;
    case FINISH:j->phase=AXD_RID_TRACE_COMPLETE;break;
    default:fail(j,AXD_RID_BAD_ARGUMENT);break;
    }
    if(direct) {
        unsigned stage=j->stage;
        if(!io){fail(j,AXD_RID_BAD_ARGUMENT);return j->phase;}
        j->attempted_ops++;
        if(io(opaque,&op,&reply)){fail(j,AXD_RID_IO_ERROR);return j->phase;}
        j->completed_ops++;
        if(stage==HIGH_READ){j->high_sample=(uint16_t)reply;j->samples_observed|=1;}
        else if(stage==LOW_READ){j->low_sample=(uint16_t)reply;j->samples_observed|=2;}
        else if(stage==HIGH_SY)j->barriers_completed|=1;
        else if(stage==LOW_SY)j->barriers_completed|=2;
        j->stage++;
    }
    return j->phase;
}
enum axd_radio_identity_phase axd_radio_identity_cancel(struct axd_radio_identity *j)
{if(!j)return AXD_RID_INVALID;if(j->phase==AXD_RID_RUNNING)j->phase=AXD_RID_CANCELLED;return j->phase;}
