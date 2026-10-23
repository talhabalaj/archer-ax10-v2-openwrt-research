/* SPDX-License-Identifier: MIT */
#include "radio_regtbl.h"
enum stage {ENTRY,TPC,TPC_DATA,TPC_SHARED,TWO_RANGE,FLAG470,FLAG471,COREUNIT,
    FIRST,FIRST_NEXT,CORE_MOD,CORE_NEXT,BEFORE_GUARD,BEFORE_NEXT,
    GUARD_READ,GUARD_SET,LOAD_STATIC,STATIC_ENTRY,STATIC_NEXT,ZERO_ENTRY,
    ZERO_NEXT,GUARD_RESTORE,TSSI_FIRST,TSSI_SECOND,FLAG_TEST470,FLAG_TEST471,
    PAPD,PAPD_DATA,PAPD_FIRST,PAPD_SECOND,ZERO_TSSI,ZERO_TSSI_NEXT,
    LAST,LAST_NEXT,MOD408,FINAL_WRITE,MOD129F,ZERO63,ZERO63_NEXT,
    TABLE20,TABLE20_NEXT,RUN_WRITER,RUN_PHY,FINISH};
/* Original descriptions of actual immediate control-flow operations. Private
 * static table descriptors/payloads are separate borrowed inputs. */
static const uint16_t first_selectors[]={0x410,0x473e,0x4725,0x4722,0x4723,
    0x4724,0x4725,0x4726,0x4727,0x4750,0x4728,0x4720,0x474d,0x474e,
    0x474f,0x4746,0x4689,0x5700,0x5702,0x5704,0x570a,0x570b};
static const uint16_t last_selectors[]={0x416,0x408,0x410,0x4749,0x473e,0x4725,
    0x4720,0x4721,0x4722,0x4723,0x4724,0x5704,0x474d,0x4726,0x4727,
    0x5702,0x5700,0x4750,0x417,0x416};
static void fail(struct axd_radio_regtbl *j,enum axd_radio_regtbl_error e)
{j->phase=AXD_RRT_FAILED;j->error=e;}
static int fact(struct axd_radio_regtbl *j,enum axd_radio_regtbl_field f,
                   struct axd_wake_u32 v,uint32_t limit,unsigned pointer)
{
    j->last_field=f;j->metadata_accesses++;
    if(!v.known)j->phase=AXD_RRT_DEPENDENCY;
    else if(v.known!=1)fail(j,AXD_RRT_BAD_ARGUMENT);
    else if(v.value>limit)fail(j,AXD_RRT_BOUNDS);
    else if(pointer && !v.value)fail(j,AXD_RRT_BINDING);
    else return 1;return 0;
}
static int byte(struct axd_radio_regtbl *j,enum axd_radio_regtbl_field f,struct axd_wake_u8 v)
{const struct axd_wake_u32 w={v.value,v.known};return fact(j,f,w,255,0);}
struct relay {struct axd_radio_regtbl *j;axd_d11_io_fn io;void *opaque;};
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct relay *r=opaque;struct axd_radio_regtbl *j=r->j;
    if(j->stage==RUN_WRITER)j->context.hardware=j->writer.context;
    else j->context.hardware.phy=j->phy.context;
    if(!r->io){fail(j,AXD_RRT_BAD_ARGUMENT);return -1;}
    j->attempted_ops++;
    if(r->io(r->opaque,op,reply)){fail(j,AXD_RRT_IO_ERROR);return -1;}
    j->completed_ops++;return 0;
}
static void packed(struct axd_radio_regtbl *j,uint16_t s,uint16_t v,unsigned next)
{
    if(axd_phy_packed_write_begin(&j->writer,s,v,&j->context.hardware))fail(j,AXD_RRT_CHILD_ERROR);
    else{j->stage=RUN_WRITER;j->after_child=next;}
}
static void phy(struct axd_radio_regtbl *j,enum axd_phy_command command,uint16_t s,
                  uint16_t mask,uint16_t value,unsigned next)
{
    if(axd_phy_register_begin(&j->phy,command,s,mask,value,&j->context.hardware.phy))fail(j,AXD_RRT_CHILD_ERROR);
    else{j->stage=RUN_PHY;j->after_child=next;}
}
static void table(struct axd_radio_regtbl *j,const struct axd_phy_table_write_request *q,
                    unsigned ext,unsigned next)
{
    int error=ext?axd_phy_table_write_ext_begin(&j->writer,q,&j->context.hardware):
        axd_phy_table_write_begin(&j->writer,q,&j->context.hardware);
    if(error)fail(j,AXD_RRT_CHILD_ERROR);else{j->stage=RUN_WRITER;j->after_child=next;}
}
static void zero(struct axd_radio_regtbl *j,uint32_t id,uint32_t offset,uint32_t width,unsigned next)
{
    const struct axd_phy_table_write_request q={id,1,offset,width,j->data,sizeof(j->data)};
    unsigned i;for(i=0;i<sizeof(j->data);i++)j->data[i]=0;table(j,&q,0,next);
}
static int descriptors(struct axd_radio_regtbl *j,enum axd_radio_regtbl_field field,
                          struct axd_radio_regtbl_span s,size_t count)
{
    j->last_field=field;j->metadata_accesses++;
    if(!s.known)j->phase=AXD_RRT_DEPENDENCY;
    else if(s.known!=1)fail(j,AXD_RRT_BAD_ARGUMENT);
    else if(!s.entries)fail(j,AXD_RRT_BINDING);
    else if(s.count!=count)fail(j,AXD_RRT_BOUNDS);
    else return 1;return 0;
}
static void child(struct axd_radio_regtbl *j,struct relay *r)
{
    unsigned done,running,missing,cancelled;
    if(j->stage==RUN_WRITER){
        enum axd_phy_table_write_phase p=axd_phy_table_write_step(&j->writer,io,r);
        j->context.hardware=j->writer.context;done=p==AXD_TW_TRACE_COMPLETE;
        running=p==AXD_TW_RUNNING;missing=p==AXD_TW_DEPENDENCY;cancelled=p==AXD_TW_CANCELLED;
        if(done){if(j->writer.direct)j->packed_completed++;else j->table_completed++;}
    }else{
        enum axd_phy_phase p=axd_phy_register_step(&j->phy,io,r);
        j->context.hardware.phy=j->phy.context;done=p==AXD_PHY_TRACE_COMPLETE;
        running=p==AXD_PHY_RUNNING;missing=p==AXD_PHY_DEPENDENCY;cancelled=p==AXD_PHY_CANCELLED;
        if(j->phy.command==AXD_PHY_READ && j->phy.result_valid)j->guard_sample=j->phy.result;
        if(done && j->phy.command==AXD_PHY_MODIFY)j->modify_completed++;
    }
    if(j->phase!=AXD_RRT_RUNNING)return;
    if(done)j->stage=j->after_child;else if(missing)j->phase=AXD_RRT_DEPENDENCY;
    else if(cancelled)j->phase=AXD_RRT_CANCELLED;else if(!running)fail(j,AXD_RRT_CHILD_ERROR);
}
int axd_radio_regtbl_begin(struct axd_radio_regtbl *j,const struct axd_radio_regtbl_context *context)
{
    const struct axd_radio_regtbl empty={0};struct axd_radio_regtbl_context copy;
    if(!j)return -1;if(context){copy=*context;context=&copy;}*j=empty;fail(j,AXD_RRT_BAD_ARGUMENT);
    if(!context)return -1;j->context=*context;
    if(context->hardware.phy.core!=130 || context->hardware.phy.minor>1 ||
       context->hardware.phy.phy_type!=11 || context->hardware.phy.phy_revision!=51 ||
       context->hardware.phy.phy_core_count<1 || context->hardware.phy.phy_core_count>4){fail(j,AXD_RRT_UNSUPPORTED);return -1;}
    j->phase=AXD_RRT_RUNNING;j->error=AXD_RRT_NO_ERROR;return 0;
}
enum axd_radio_regtbl_phase axd_radio_regtbl_step(struct axd_radio_regtbl *j,axd_d11_io_fn fn,void *opaque)
{
    struct relay r={j,fn,opaque};struct axd_radio_regtbl_context *c;uint32_t band;
    if(!j)return AXD_RRT_INVALID;if(j->phase!=AXD_RRT_RUNNING)return j->phase;
    c=&j->context;band=c->chanspec.value&0xc000u;
    switch(j->stage){
    case ENTRY:
        if(fact(j,AXD_RRT_CHANSPEC,c->chanspec,65535,0))j->stage=(!band || (band&0x4000u))?TPC:COREUNIT;
        break;
    case TPC:if(fact(j,AXD_RRT_TPC,c->tpc_pointer,UINT32_MAX,1))j->stage=TPC_DATA;break;
    case TPC_DATA:if(fact(j,AXD_RRT_TPC_DATA,c->tpc_data_pointer,UINT32_MAX,1))j->stage=TPC_SHARED;break;
    case TPC_SHARED:if(fact(j,AXD_RRT_TPC_SHARED,c->tpc_shared_pointer,UINT32_MAX,1))j->stage=TWO_RANGE;break;
    case TWO_RANGE:
        if(byte(j,band?AXD_RRT_TWO_RANGE5G:AXD_RRT_TWO_RANGE2G,band?c->tpc_two_range5g:c->tpc_two_range2g))
            j->stage=(band?c->tpc_two_range5g.value:c->tpc_two_range2g.value)?FLAG470:COREUNIT;
        break;
    case FLAG470:if(byte(j,AXD_RRT_FLAG470,c->flag470)){
        if(c->flag470.value && !band){j->tssi=1;j->stage=COREUNIT;}else j->stage=FLAG471;}break;
    case FLAG471:if(byte(j,AXD_RRT_FLAG471,c->flag471)){j->tssi=c->flag471.value && (band&0x4000u);j->stage=COREUNIT;}break;
    case COREUNIT:
        if(fact(j,AXD_RRT_BMAC,c->bmac_pointer,UINT32_MAX,1) &&
           fact(j,AXD_RRT_BMAC_DEVICE,c->bmac_device_pointer,UINT32_MAX,1) &&
           fact(j,AXD_RRT_COREUNIT,c->bmac_word150,UINT32_MAX,0)){
            /* Actual local wlc_bmac_coreunit: load input+18 pointer,
             * test its full word150; zero returns1, otherwise2. Ignored by
             * this native caller, with no physical I/O or software store. */
            j->coreunit=c->bmac_word150.value?2u:1u;j->stage=FIRST;
        }break;
    case FIRST:{uint16_t value=j->index==0?0x77:j->index==10?0x80:j->index==11?0x180:0;
        packed(j,first_selectors[j->index],value,FIRST_NEXT);break;}
    case FIRST_NEXT:if(++j->index<sizeof(first_selectors)/sizeof(first_selectors[0]))j->stage=FIRST;
        else{j->index=0;phy(j,AXD_PHY_MODIFY,0x408,0x800,0,CORE_MOD);}break;
    case CORE_MOD:
        if(j->core_index<c->hardware.phy.phy_core_count)phy(j,AXD_PHY_MODIFY,(uint16_t)(0x747+j->core_index*0x200),0xf,0xf,CORE_NEXT);
        else j->stage=BEFORE_GUARD;break;
    case CORE_NEXT:j->core_index++;j->stage=CORE_MOD;break;
    case BEFORE_GUARD:packed(j,j->index?0x4721:0x4729,j->index?0x5000:0,BEFORE_NEXT);break;
    case BEFORE_NEXT:if(++j->index<2)j->stage=BEFORE_GUARD;else {j->index=0;j->stage=GUARD_READ;}break;
    case GUARD_READ:phy(j,AXD_PHY_READ,0x19e,0,0,GUARD_SET);break;
    case GUARD_SET:phy(j,AXD_PHY_MODIFY,0x19e,2,2,LOAD_STATIC);break;
    case LOAD_STATIC:
        if(byte(j,AXD_RRT_LOAD_STATIC,c->load_static))j->stage=c->load_static.value?STATIC_ENTRY:GUARD_RESTORE;break;
    case STATIC_ENTRY:
        if(descriptors(j,AXD_RRT_TABLES,c->tables,9))table(j,&c->tables.entries[j->index],1,STATIC_NEXT);break;
    case STATIC_NEXT:if(++j->index<9)j->stage=STATIC_ENTRY;else{j->index=0;j->zero_index=0;j->stage=ZERO_ENTRY;}break;
    case ZERO_ENTRY:
        if(descriptors(j,AXD_RRT_ZEROS,c->zeros,8)){
            const struct axd_phy_table_write_request *q=&c->zeros.entries[j->index];
            if(j->zero_index<q->count)zero(j,q->table_id,j->zero_index,q->width,ZERO_NEXT);
            else {j->zero_index=0;if(++j->index==8){j->index=0;j->stage=GUARD_RESTORE;}}
        }break;
    case ZERO_NEXT:j->zero_index++;j->stage=ZERO_ENTRY;break;
    case GUARD_RESTORE:phy(j,AXD_PHY_MODIFY,0x19e,2,j->guard_sample&2,TSSI_FIRST);break;
    case TSSI_FIRST:packed(j,0x4645,0x25c,TSSI_SECOND);break;
    case TSSI_SECOND:if(j->tssi)packed(j,0x464c,0x25c,FLAG_TEST470);else j->stage=FLAG_TEST470;break;
    case FLAG_TEST470:
        if(byte(j,AXD_RRT_FLAG470,c->flag470)){
            if(c->flag470.value && !band){j->index=0;j->part=0;j->stage=ZERO_TSSI;}else j->stage=FLAG_TEST471;}break;
    case FLAG_TEST471:
        if(byte(j,AXD_RRT_FLAG471,c->flag471)){
            if(c->flag471.value && (band&0x4000u)){j->index=0;j->part=0;j->stage=ZERO_TSSI;}else j->stage=PAPD;}break;
    case PAPD:if(fact(j,AXD_RRT_PAPD,c->papd_pointer,UINT32_MAX,1))j->stage=PAPD_DATA;break;
    case PAPD_DATA:if(fact(j,AXD_RRT_PAPD_DATA,c->papd_data_pointer,UINT32_MAX,1))j->stage=PAPD_FIRST;break;
    case PAPD_FIRST:
        if(byte(j,AXD_RRT_PAPD_BYTE0,c->papd_byte0)){
            if(c->papd_byte0.value){j->index=0;j->part=0;j->stage=ZERO_TSSI;}else j->stage=PAPD_SECOND;}break;
    case PAPD_SECOND:if(byte(j,AXD_RRT_PAPD_BYTE4,c->papd_byte4)){
        j->index=0;j->part=0;j->stage=c->papd_byte4.value?ZERO_TSSI:LAST;}break;
    case ZERO_TSSI:zero(j,j->part?0x7d:0x5d,j->index,8,ZERO_TSSI_NEXT);break;
    case ZERO_TSSI_NEXT:
        if(!j->part){j->part=1;j->stage=ZERO_TSSI;}else{j->part=0;if(++j->index<128)j->stage=ZERO_TSSI;else{j->index=0;j->stage=LAST;}}break;
    case LAST:{uint16_t value=j->index==1?0x306:j->index==2?1:j->index==18?4:j->index==19?0xd:0;
        packed(j,last_selectors[j->index],value,LAST_NEXT);break;}
    case LAST_NEXT:if(++j->index<sizeof(last_selectors)/sizeof(last_selectors[0]))j->stage=LAST;else{j->index=0;j->stage=MOD408;}break;
    case MOD408:phy(j,AXD_PHY_MODIFY,0x408,2,2,FINAL_WRITE);break;
    case FINAL_WRITE:packed(j,0x474c,0,MOD129F);break;
    case MOD129F:phy(j,AXD_PHY_MODIFY,0x129f,1,1,ZERO63);break;
    case ZERO63:zero(j,0x3f,j->index,32,ZERO63_NEXT);break;
    case ZERO63_NEXT:if(++j->index<228)j->stage=ZERO63;else{j->index=0;j->stage=TABLE20;}break;
    case TABLE20:{
        uint16_t first=0,second=0;unsigned i;
        switch(j->index){case 0:first=0x703c;second=0x1c;break;case 1:first=0x7014;second=0x1c;break;
        case 2:first=0x700e;second=0x2c;break;case 3:first=0x702c;second=0x2c;break;
        case 4:first=0x6020;second=0x20;break;case 5:first=0x400;break;default:break;}
        for(i=0;i<sizeof(j->data);i++)j->data[i]=0;
        j->data[0]=(uint8_t)first;j->data[1]=(uint8_t)(first>>8);j->data[2]=(uint8_t)second;j->data[3]=(uint8_t)(second>>8);
        {const struct axd_phy_table_write_request q={0x14,1,j->index,48,j->data,6};table(j,&q,0,TABLE20_NEXT);}break;}
    case TABLE20_NEXT:if(++j->index<7)j->stage=TABLE20;else j->stage=FINISH;break;
    case RUN_WRITER:case RUN_PHY:child(j,&r);break;
    case FINISH:j->phase=AXD_RRT_TRACE_COMPLETE;break;
    default:fail(j,AXD_RRT_BAD_ARGUMENT);break;
    }return j->phase;
}
enum axd_radio_regtbl_phase axd_radio_regtbl_cancel(struct axd_radio_regtbl *j)
{
    if(!j)return AXD_RRT_INVALID;
    if(j->phase==AXD_RRT_RUNNING){j->phase=AXD_RRT_CANCELLED;axd_phy_table_write_cancel(&j->writer);axd_phy_register_cancel(&j->phy);}
    return j->phase;
}
