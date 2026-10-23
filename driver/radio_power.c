/* SPDX-License-Identifier: MIT */
#include "radio_power.h"
enum stage { ENTRY, PREFIX, MODE_SPAN, MODE_WRITE, MODE_NEXT, CORE_FIRST,
    SEQUENCE, PLL_READ, PLL_TEST, PLL_DELAY, PLL_CORES, PLL_FRESH, PLL_COPY,
    RCAL_DELAY, RCAL_READ, RCAL_TEST, RCAL_FRESH, RCAL_VALUE,
    AC_BIND, RADIO_BIND, DATA_BIND, STORE_MULTIPLIER, STORE_SAVED8,
    STORE_SAVED_A, RCCAL_SPAN, RCCAL_ENABLE, RCCAL_SETUP,
    RCCAL_DELAY, RCCAL_READ, RCCAL_TEST, RCCAL_CLEAR, RCCAL_FIRST,
    RCCAL_SECOND, RCCAL_DIFFERENCE, RCCAL_CALCULATE,
    STORE_SAVED6, STORE_RESULT8, STORE_RESULT_MULTIPLIER,
    RCCAL_FINISH, RCCAL_NEXT, CORE_FINAL, FINAL_MULTIPLIER,
    CHANMGR_BIND, CHANMGR_DATA_BIND, CHANMGR_TEST, STORE_ON, CHIP,
    SWITCH_RETURN, REGTBL_BEGIN, RUN_REGTBL, CHANMGR_CHANNEL, FINISH, RUN_REGISTER, RUN_PACKED };
enum sequence { PHY_POWER, PLL_START, PLL_FINISH, RCAL_FINISH, RADIO_FINISH };
enum action { MODIFY=1, PACKED, DELAY };
struct item {unsigned action;uint16_t selector,mask,value;};
#define M(s,m,v) {MODIFY,s,m,v}
#define P(s,v) {PACKED,s,0,v}
#define D(v) {DELAY,0,0,v}
/* Original operation descriptions of recovered control flow. Private table
 * values are supplied separately and are never encoded in these sequences. */
static const struct item phy_power[]={P(0x417,0xd),P(0x408,2),P(0x4728,0x180),D(1000),P(0x417,4),P(0x4728,0x80),P(0x4720,0x180)};
static const struct item pll_start[]={
    M(0x884,8,8),M(0x9fa,0x100,0x100),M(0x9fa,0x1e00,0x1000),D(1),
    M(0x80e,0xfc0,0),M(0x80e,3,1),M(0x80d,0x780,0),M(0x80d,0x40,0),
    M(0x80d,0x30,0),M(0x80d,0xc,0),M(0x80d,2,0),M(0x80d,1,0),
    M(0x80f,3,0),M(0x80f,0x300,0x100),M(0x9f9,0x2000,0x2000),M(0x9f9,0x4000,0x4000),
    M(0x83c,0x3f,0x32),M(0x815,8,0),M(0x83d,0x7f,0x32),M(0x815,0x10,0x10),
    M(0x9f9,0x4000,0),M(0x9f9,0x2000,0),M(0x88b,0x3f,1),M(0x9f9,0x2000,0x2000),
    M(0x87b,0x40,0),M(0x87b,8,0),M(0x87b,2,2),M(0x87b,0x20,0x20),M(0x815,0x40,0x40),
    D(30),M(0x812,0x4000,0x4000),M(0x812,0x4000,0),D(30),M(0x87b,1,1),D(25)
};
static const struct item pll_finish[]={M(0x87b,0x40,0x40),M(0x87b,8,8),M(0x87b,2,0),M(0x87b,1,0),M(0x9f9,0x2000,0),M(0x9f9,0x2000,0x2000),M(0x9f9,0x4000,0x4000),M(0x231,0x100,0x100),M(0x3e9,1,1),M(0x813,0x80,0x80),M(0x87c,1,1)};
static const struct item rcal_finish[]={M(0x815,0x10,0x10),M(0x231,0x100,0),M(0x3e9,1,0),M(0x813,0x80,0),M(0x87c,1,0),M(0x9f9,0x4000,0),M(0x9f9,0x2000,0)};
static const struct item radio_finish[]={M(0x83e,0x20,0x20),M(0x83e,0x10,0x10),M(0x837,0x20,0x20),M(0x837,0x10,0x10),M(0x83e,0x400,0x400),M(0x83e,0x100,0x100),M(0x83e,0x200,0x200),M(0x83e,0x80,0x80),M(0x818,0x2000,0x2000),M(0x829,8,8),M(0x829,2,2),M(0x829,0x10,0x10),M(0x827,0x10,0x10),M(0x829,0x1e00,0x1e00),M(0x831,8,8),M(0x825,1,1),M(0x833,4,4),M(0x9f6,0x8000,0x8000)};
#undef M
#undef P
#undef D
struct relay {struct axd_radio_power *j;axd_d11_io_fn io;void *opaque;};
static void fail(struct axd_radio_power *j,enum axd_radio_power_error e)
{j->phase=AXD_RPOWER_FAILED;j->error=e;}
static void dependency(struct axd_radio_power *j,enum axd_radio_power_dependency d)
{j->phase=AXD_RPOWER_DEPENDENCY;j->dependency=d;}
static int fact(struct axd_radio_power *j,enum axd_radio_power_field f,struct axd_wake_u32 v,unsigned pointer)
{
    j->last_field=f;j->metadata_accesses++;
    if(!v.known)dependency(j,AXD_RPOWER_METADATA);
    else if(v.known!=1)fail(j,AXD_RPOWER_BAD_ARGUMENT);
    else if(pointer && !v.value)fail(j,AXD_RPOWER_BINDING);
    else return 1;
    return 0;
}
static int span(struct axd_radio_power *j,enum axd_radio_power_field field,const uint8_t *p,size_t bytes,size_t need)
{
    j->last_field=field;j->metadata_accesses++;
    if(!p)dependency(j,AXD_RPOWER_METADATA);
    else if(bytes<need)fail(j,AXD_RPOWER_BOUNDS);
    else return 1;
    return 0;
}
static uint16_t half(const uint8_t *p)
{return (uint16_t)(p[0]|((unsigned)p[1]<<8));}
static int perform(struct relay *r,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct axd_radio_power *j=r->j;
    if(j->stage==PREFIX || j->stage==RUN_REGISTER)j->context.radio=j->control_job.context;
    else if(j->stage==RUN_PACKED)j->context.radio.hardware=j->packed_job.context;
    else if(j->stage==RUN_REGTBL){j->context.regtbl=j->regtbl_job.context;j->context.radio.hardware=j->regtbl_job.context.hardware;}
    if(!r->io){fail(j,AXD_RPOWER_BAD_ARGUMENT);return -1;}
    j->attempted_ops++;
    if(r->io(r->opaque,op,reply)){fail(j,AXD_RPOWER_IO_ERROR);return -1;}
    j->completed_ops++;return 0;
}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{return perform(opaque,op,reply);}
static void reg(struct axd_radio_power *j,enum axd_radio_control_command c,uint16_t s,uint16_t m,uint16_t v,unsigned next)
{
    if(axd_radio_register_begin(&j->control_job,c,s,m,v,&j->context.radio))fail(j,AXD_RPOWER_CHILD_ERROR);
    else {j->stage=RUN_REGISTER;j->after_child=next;}
}
static void modify(struct axd_radio_power *j,uint16_t s,uint16_t m,uint16_t v,unsigned next)
{reg(j,AXD_RADIO_CTL_MODIFY,s,m,v,next);}
static void read_radio(struct axd_radio_power *j,uint16_t s,unsigned next)
{reg(j,AXD_RADIO_CTL_READ,s,0,0,next);}
static void packed(struct axd_radio_power *j,uint16_t s,uint16_t v,unsigned next)
{
    if(axd_phy_packed_write_begin(&j->packed_job,s,v,&j->context.radio.hardware))fail(j,AXD_RPOWER_CHILD_ERROR);
    else {j->stage=RUN_PACKED;j->after_child=next;}
}
static void delay(struct axd_radio_power *j,struct relay *r,uint32_t us,unsigned next)
{const struct axd_d11_operation op={AXD_D11_DELAY_US,0,us};uint32_t ignored=0;if(!perform(r,&op,&ignored))j->stage=next;}
static void start_sequence(struct axd_radio_power *j,unsigned sequence)
{j->sequence=sequence;j->index=0;j->stage=SEQUENCE;}
static void sequence(struct axd_radio_power *j,struct relay *r)
{
    const struct item *p;size_t count;
    switch(j->sequence){
    case PHY_POWER:p=phy_power;count=sizeof(phy_power)/sizeof(*p);break;
    case PLL_START:p=pll_start;count=sizeof(pll_start)/sizeof(*p);break;
    case PLL_FINISH:p=pll_finish;count=sizeof(pll_finish)/sizeof(*p);break;
    case RCAL_FINISH:p=rcal_finish;count=sizeof(rcal_finish)/sizeof(*p);break;
    default:p=radio_finish;count=sizeof(radio_finish)/sizeof(*p);break;
    }
    if(j->index==count){
        switch(j->sequence){
        case PHY_POWER:start_sequence(j,PLL_START);break;
        case PLL_START:j->poll_count=0;j->stage=PLL_READ;break;
        case PLL_FINISH:j->poll_count=0;j->stage=RCAL_DELAY;break;
        case RCAL_FINISH:j->stage=AC_BIND;break;
        default:j->stage=CHANMGR_BIND;break;
        }return;
    }
    p+=j->index;
    if(p->action==MODIFY){j->index++;modify(j,p->selector,p->mask,p->value,SEQUENCE);}
    else if(p->action==PACKED){j->index++;packed(j,p->selector,p->value,SEQUENCE);}
    else {delay(j,r,p->value,SEQUENCE);if(j->stage==SEQUENCE && j->phase==AXD_RPOWER_RUNNING)j->index++;}
}
static void child(struct axd_radio_power *j,struct relay *r)
{
    unsigned done,running,cancelled,dep;
    if(j->stage==RUN_PACKED){
        enum axd_phy_table_write_phase p=axd_phy_table_write_step(&j->packed_job,io,r);
        j->context.radio.hardware=j->packed_job.context;
        done=p==AXD_TW_TRACE_COMPLETE;running=p==AXD_TW_RUNNING;cancelled=p==AXD_TW_CANCELLED;dep=p==AXD_TW_DEPENDENCY;
    }else{
        enum axd_radio_control_phase p=axd_radio_control_step(&j->control_job,io,r);
        j->context.radio=j->control_job.context;
        if(j->stage==PREFIX && p==AXD_RADIO_CTL_DEPENDENCY && j->control_job.dependency==AXD_RADIO_CTL_LOGEN_PLL_CONTINUATION){
            if(j->phase==AXD_RPOWER_RUNNING){j->prefix_completed=1;j->stage=MODE_SPAN;}return;
        }
        done=p==AXD_RADIO_CTL_TRACE_COMPLETE;running=p==AXD_RADIO_CTL_RUNNING;cancelled=p==AXD_RADIO_CTL_CANCELLED;dep=p==AXD_RADIO_CTL_DEPENDENCY;
        if(done)j->sample=j->control_job.result;
    }
    if(j->phase!=AXD_RPOWER_RUNNING)return;
    if(done)j->stage=j->after_child;
    else if(dep)dependency(j,j->stage==PREFIX?AXD_RPOWER_PREFIX_CHILD:AXD_RPOWER_REGISTER_CHILD);
    else if(cancelled)j->phase=AXD_RPOWER_CANCELLED;
    else if(!running)fail(j,AXD_RPOWER_CHILD_ERROR);
}
int axd_radio_power_begin(struct axd_radio_power *j,enum axd_radio_power_command command,unsigned mode,const struct axd_radio_power_context *context)
{
    struct axd_radio_power_context copy;static const struct axd_radio_power empty={0};
    if(!j)return -1;
    if(context){copy=*context;context=&copy;}*j=empty;fail(j,AXD_RPOWER_BAD_ARGUMENT);
    if(!context || command<AXD_RPOWER_LOGEN || command>AXD_RPOWER_ON_REGISTERED || mode>2 || (command!=AXD_RPOWER_LOGEN && mode))return -1;
    j->context=*context;j->command=command;j->mode=mode;
    if(context->radio.hardware.phy.core!=130 || context->radio.hardware.phy.minor>1 || context->radio.hardware.phy.phy_type!=0xb || context->radio.hardware.phy.phy_revision!=51 || context->radio.hardware.phy.phy_core_count>4){fail(j,AXD_RPOWER_BOUNDS);return -1;}
    j->phase=AXD_RPOWER_RUNNING;j->error=AXD_RPOWER_NO_ERROR;return 0;
}
enum axd_radio_power_phase axd_radio_power_step(struct axd_radio_power *j,axd_d11_io_fn transport,void *opaque)
{
    struct relay r={j,transport,opaque};struct axd_radio_power_context *c;
    if(!j)return AXD_RPOWER_INVALID;
    if(j->phase!=AXD_RPOWER_RUNNING)return j->phase;
    c=&j->context;
    switch(j->stage){
    case ENTRY:
        if(j->command==AXD_RPOWER_LOGEN)j->stage=MODE_SPAN;
        else if(axd_radio_control_begin(&j->control_job,j->command==AXD_RPOWER_SWITCH_ON?AXD_RADIO_CTL_AC_SWITCH_ON:AXD_RADIO_CTL_AC_ON_REGISTERED,&c->radio))fail(j,AXD_RPOWER_CHILD_ERROR);
        else j->stage=PREFIX;
        break;
    case PREFIX:case RUN_REGISTER:case RUN_PACKED:child(j,&r);break;
    case MODE_SPAN:if(span(j,AXD_RPOWER_MODE_DATA,c->mode_data,c->mode_length,18))j->stage=MODE_WRITE;break;
    case MODE_WRITE:{
        static const uint16_t selectors[]={0x88c,0x9f6,0x88a,0x88a,0x88a,0x88a,0x1f1,0x1f1,0x1f1,0x1f1,0x1f2,0x1f2,0x1f2,0x1f2,0x3f1,0x3f1,0x3f1,0x3f1,0x3f2,0x3f2,0x3f2,0x3f2};
        static const uint16_t masks[]={4,0x4000,1,0x100,2,4,0x4000,0x8000,2,0x2000,1,0x40,0x800,0x1000,0x4000,0x8000,2,0x2000,1,0x40,0x800,0x1000};
        static const uint8_t shifts[]={2,14,0,8,1,2,14,15,1,13,0,6,11,12,14,15,1,13,0,6,11,12};
        unsigned i=j->index,offset=i<5?j->mode:i==5?18:((i==10 || i==18)?12:4)+(i>=14?3:0)+j->mode;
        uint16_t value=offset==18?0:(uint16_t)((uint32_t)c->mode_data[offset]<<shifts[i]);
        modify(j,selectors[i],masks[i],value,MODE_NEXT);break;
    }
    case MODE_NEXT:
        if(++j->index<22)j->stage=MODE_WRITE;
        else {j->logen_completed=1;j->index=0;j->core_index=0;j->part=0;j->stage=j->command==AXD_RPOWER_LOGEN?FINISH:CORE_FIRST;}
        break;
    case CORE_FIRST:{
        unsigned core=j->core_index;uint16_t selector,value;
        if(core==c->radio.hardware.phy.phy_core_count){start_sequence(j,PHY_POWER);break;}
        selector=core<2?(uint16_t)(j->part==2?(core?0x2a3:0xa3):(core?0x3f2:0x1f2)):0xffff;
        value=j->part==0?0x20:j->part==1?0x200:0x8000;
        if(++j->part==3){j->part=0;j->core_index++;}modify(j,selector,value,value,CORE_FIRST);break;
    }
    case SEQUENCE:sequence(j,&r);break;
    case PLL_READ:read_radio(j,0x882,PLL_TEST);break;
    case PLL_TEST:
        j->pll_reads++;j->pll_locked=(j->sample&0x40u)!=0;
        if(j->pll_locked){j->core_index=0;j->part=0;j->stage=PLL_CORES;}
        else j->stage=PLL_DELAY;
        break;
    case PLL_DELAY:
        delay(j,&r,100,PLL_READ);
        if(j->stage==PLL_READ && j->phase==AXD_RPOWER_RUNNING && ++j->poll_count==101){j->core_index=0;j->part=0;j->stage=PLL_CORES;}
        break;
    case PLL_CORES:{
        unsigned core=j->core_index;uint16_t selector,mask=j->part?0x80:1;
        if(core==c->radio.hardware.phy.phy_core_count){j->stage=j->pll_locked?PLL_FRESH:SEQUENCE;if(!j->pll_locked)start_sequence(j,PLL_FINISH);break;}
        selector=core<2?(uint16_t)(j->part?(core?0x26a:0x6a):(core?0x256:0x56)):0xffff;
        if(++j->part==2){j->part=0;j->core_index++;}modify(j,selector,mask,mask,PLL_CORES);break;
    }
    case PLL_FRESH:read_radio(j,0x882,PLL_COPY);break;
    case PLL_COPY:modify(j,0x87a,0x3f,(uint16_t)(j->sample&0x3f),SEQUENCE);j->sequence=PLL_FINISH;j->index=0;break;
    case RCAL_DELAY:delay(j,&r,1000,RCAL_READ);break;
    case RCAL_READ:read_radio(j,0x87c,RCAL_TEST);break;
    case RCAL_TEST:
        j->rcal_reads++;j->rcal_done=(j->sample&2u)!=0;
        if(j->rcal_done)j->stage=RCAL_FRESH;
        else if(++j->poll_count==101){j->rcal_value=0x32;j->index=0;j->stage=RCAL_VALUE;}
        else j->stage=RCAL_DELAY;
        break;
    case RCAL_FRESH:read_radio(j,0x87c,RCAL_VALUE);j->index=0;j->rcal_value=0xffff;break;
    case RCAL_VALUE:
        if(!j->index && j->rcal_value==0xffff){uint16_t v=(uint16_t)((j->sample>>2)&0x3f);j->rcal_value=v>=2 && v<=60?v:0x32;}
        if(j->index==0)modify(j,0x83d,0x7f,j->rcal_value,RCAL_VALUE);
        else if(j->index==1)modify(j,0x815,8,0,RCAL_VALUE);
        else if(j->index==2)modify(j,0x83c,0x3f,j->rcal_value,RCAL_VALUE);
        else {start_sequence(j,RCAL_FINISH);break;}
        j->index++;break;
    case AC_BIND:if(fact(j,AXD_RPOWER_AC,c->radio.ac_pointer,1))j->stage=RADIO_BIND;break;
    case RADIO_BIND:if(fact(j,AXD_RPOWER_AC_RADIO,c->ac_radio_pointer,1))j->stage=DATA_BIND;break;
    case DATA_BIND:if(fact(j,AXD_RPOWER_AC_DATA,c->ac_radio_data_pointer,1))j->stage=STORE_MULTIPLIER;break;
    case STORE_MULTIPLIER:c->rccal_multiplier=0x1000;j->software_stores++;j->stage=STORE_SAVED8;break;
    case STORE_SAVED8:c->rccal_saved8=0x10a4;j->software_stores++;j->stage=STORE_SAVED_A;break;
    case STORE_SAVED_A:c->rccal_saved_a=(uint16_t)c->rccal_multiplier;j->software_stores++;j->stage=RCCAL_SPAN;break;
    case RCCAL_SPAN:if(span(j,AXD_RPOWER_RCCAL_DATA,c->rccal_data,c->rccal_length,8))j->stage=RCCAL_ENABLE;break;
    case RCCAL_ENABLE:j->rccal_iteration=0;j->part=0;modify(j,0x9f9,0x2000,0x2000,RCCAL_SETUP);break;
    case RCCAL_SETUP:{
        unsigned i=j->part++,n=j->rccal_iteration;const uint8_t *p=c->rccal_data;
        switch(i){
        case 0:modify(j,0x2ee,0x30,0x10,RCCAL_SETUP);break;
        case 1:modify(j,0x2f0,0x40,n?0:0x40,RCCAL_SETUP);break;
        case 2:modify(j,0x2f0,0x30,n?0x10:0,RCCAL_SETUP);break;
        case 3:modify(j,0x2ee,0xff00,(uint16_t)((unsigned)p[n]<<8),RCCAL_SETUP);break;
        case 4:modify(j,0x2ef,0x1fff,half(p+4+n*2),RCCAL_SETUP);break;
        case 5:modify(j,0x2f0,1,0,RCCAL_SETUP);break;
        case 6:delay(j,&r,1,RCCAL_SETUP);break;
        case 7:modify(j,0x2f0,1,1,RCCAL_SETUP);break;
        case 8:delay(j,&r,35,RCCAL_SETUP);break;
        case 9:modify(j,0x2ee,1,0,RCCAL_SETUP);break;
        case 10:modify(j,0x2ee,1,1,RCCAL_SETUP);break;
        default:j->poll_count=0;j->stage=RCCAL_DELAY;break;
        }break;
    }
    case RCCAL_DELAY:delay(j,&r,100,RCCAL_READ);break;
    case RCCAL_READ:read_radio(j,0x2f3,RCCAL_TEST);break;
    case RCCAL_TEST:
        j->rccal_reads[j->rccal_iteration]++;j->rccal_done=j->sample&1u;
        j->stage=j->rccal_done || ++j->poll_count==10000?RCCAL_CLEAR:RCCAL_DELAY;break;
    /* Poll exit leaves r3=0 on both done and limit paths in the actual ARM. */
    case RCCAL_CLEAR:modify(j,0x2ee,1,0,RCCAL_FIRST);break;
    case RCCAL_FIRST:read_radio(j,0x2f4,RCCAL_SECOND);break;
    case RCCAL_SECOND:j->rccal_first=j->sample;read_radio(j,0x2f2,RCCAL_DIFFERENCE);break;
    case RCCAL_DIFFERENCE:{
        int32_t a=(int32_t)(j->rccal_first&0x1fff),b=(int32_t)(j->sample&0x1fff);
        if(a&0x1000)a-=0x2000;
        if(b&0x1000)b-=0x2000;
        j->difference=(uint32_t)(b-a);j->stage=RCCAL_CALCULATE;break;
    }
    case RCCAL_CALCULATE:
        if(!j->rccal_done){j->division_result=j->rccal_iteration?0x1000:0x10a4;j->stage=j->rccal_iteration?STORE_RESULT_MULTIPLIER:STORE_RESULT8;}
        else if(fact(j,AXD_RPOWER_CLOCK,c->clock_word,0)){
            uint32_t divisor=c->clock_word.value>>12;
            if(!divisor){dependency(j,AXD_RPOWER_DIVISION_BY_ZERO);break;}
            j->quotient=(j->difference*(j->rccal_iteration?UINT32_C(0x12f89):UINT32_C(0x12430)))/divisor;
            if(j->rccal_iteration){
                if(!j->quotient){dependency(j,AXD_RPOWER_DIVISION_BY_ZERO);break;}
                j->division_result=UINT32_C(0x1000000)/j->quotient;j->stage=STORE_RESULT_MULTIPLIER;
            }else {j->division_result=(uint16_t)j->quotient;j->stage=STORE_SAVED6;}
        }break;
    case STORE_SAVED6:c->rccal_saved6=(uint16_t)j->division_result;j->software_stores++;j->stage=STORE_RESULT8;break;
    case STORE_RESULT8:c->rccal_saved8=(uint16_t)j->division_result;j->software_stores++;j->stage=RCCAL_FINISH;break;
    case STORE_RESULT_MULTIPLIER:c->rccal_multiplier=j->division_result;j->software_stores++;j->stage=RCCAL_FINISH;break;
    case RCCAL_FINISH:modify(j,0x2f0,1,0,RCCAL_NEXT);break;
    case RCCAL_NEXT:
        if(++j->rccal_iteration<2){j->part=0;j->stage=RCCAL_SETUP;}
        else {j->core_index=0;j->part=0;j->stage=CORE_FINAL;}
        break;
    case CORE_FINAL:{
        unsigned core=j->core_index;uint16_t selector;
        if(core==c->radio.hardware.phy.phy_core_count){modify(j,0x9f9,0x2000,0,FINAL_MULTIPLIER);break;}
        selector=core<2?(uint16_t)(j->part==0?(core?0x285:0x85):j->part==1?(core?0x249:0x49):(core?0x24a:0x4a)):0xffff;
        if(++j->part==3){j->part=0;j->core_index++;}modify(j,selector,0xffff,c->rccal_saved8,CORE_FINAL);break;
    }
    case FINAL_MULTIPLIER:c->rccal_saved_a=(uint16_t)c->rccal_multiplier;j->software_stores++;start_sequence(j,RADIO_FINISH);break;
    case CHANMGR_BIND:if(fact(j,AXD_RPOWER_CHANMGR,c->radio.chanmgr_pointer,1))j->stage=CHANMGR_DATA_BIND;break;
    case CHANMGR_DATA_BIND:if(fact(j,AXD_RPOWER_CHANMGR_DATA,c->radio.chanmgr_data_pointer,1))j->stage=CHANMGR_TEST;break;
    case CHANMGR_TEST:
        if(c->radio.chanmgr_data10){j->after_child=CHANMGR_CHANNEL;j->stage=REGTBL_BEGIN;}else j->stage=STORE_ON;break;
    case STORE_ON:c->radio.pi47e=1;j->software_stores++;j->stage=CHIP;break;
    case CHIP:
        if(fact(j,AXD_RPOWER_CHIP,c->radio.chip_id,0)){
            if(c->radio.chip_id.value==63178)j->stage=SWITCH_RETURN;
            else dependency(j,AXD_RPOWER_OTHER_CHIP_TAIL);
        }break;
    case SWITCH_RETURN:j->switch_completed=1;j->after_child=FINISH;j->stage=j->command==AXD_RPOWER_ON_REGISTERED?REGTBL_BEGIN:FINISH;break;
    case REGTBL_BEGIN:
        j->context.regtbl.hardware=j->context.radio.hardware;
        if(axd_radio_regtbl_begin(&j->regtbl_job,&j->context.regtbl))fail(j,AXD_RPOWER_CHILD_ERROR);
        else j->stage=RUN_REGTBL;
        break;
    case RUN_REGTBL:{
        enum axd_radio_regtbl_phase p=axd_radio_regtbl_step(&j->regtbl_job,io,&r);
        j->context.regtbl=j->regtbl_job.context;j->context.radio.hardware=j->regtbl_job.context.hardware;
        if(j->phase!=AXD_RPOWER_RUNNING)break;
        if(p==AXD_RRT_TRACE_COMPLETE){j->regtbl_completed=1;j->stage=j->after_child;}
        else if(p==AXD_RRT_DEPENDENCY)dependency(j,j->after_child==CHANMGR_CHANNEL?AXD_RPOWER_REGTBL_CHANNEL_TAIL:AXD_RPOWER_REGTBL_ON_PWRON);
        else if(p==AXD_RRT_CANCELLED)j->phase=AXD_RPOWER_CANCELLED;
        else if(p!=AXD_RRT_RUNNING)fail(j,AXD_RPOWER_CHILD_ERROR);
        break;}
    case CHANMGR_CHANNEL:dependency(j,AXD_RPOWER_CHANSPEC_TAIL);break;
    case FINISH:j->phase=AXD_RPOWER_TRACE_COMPLETE;break;
    default:fail(j,AXD_RPOWER_BAD_ARGUMENT);break;
    }
    return j->phase;
}
enum axd_radio_power_phase axd_radio_power_cancel(struct axd_radio_power *j)
{
    if(!j)return AXD_RPOWER_INVALID;
    if(j->phase==AXD_RPOWER_RUNNING){j->phase=AXD_RPOWER_CANCELLED;
        if(j->stage==PREFIX || j->stage==RUN_REGISTER)axd_radio_control_cancel(&j->control_job);
        else if(j->stage==RUN_PACKED)axd_phy_table_write_cancel(&j->packed_job);
        else if(j->stage==RUN_REGTBL)axd_radio_regtbl_cancel(&j->regtbl_job);
    }return j->phase;
}
