/* SPDX-License-Identifier: MIT */
#include "dma_rx_fill.h"
enum stage {
    ENTRY,PLAN,ALLOCATE,AUX_READ,FLAGS_READ,FLAGS_WRITE,AUX_WRITE,ACCOUNT,
    EXTRA_GATE,EXTRA_PULL,ALIGN_GATE,ALIGN_DATA_READ,ALIGN_PLAN,ALIGN_PULL,
    ALIGN_NONLINEAR_READ,ALIGN_LENGTH_READ,TRIM_PLAN,TRIM_OS,LINEAR_DATA_READ,
    LINEAR_LENGTH_WRITE,LINEAR_TAIL_WRITE,ZERO_DATA_READ,ZERO_DATA_WRITE,
    MAP_DATA_READ,TRANSLATE,TRACK_GATE,SLOT_WRITE,PACKET_NEXT_CLEAR,LINK_GATE,
    OLD_TAIL_LINK,STORE_HEAD,STORE_TAIL,DESCRIPTOR_PREPARE,WRITE_WORDS,
    READ_WORDS,PARITY_CHECK,PARITY_WRITE,ADVANCE_INDEX,BUFFER_DONE,
    ALLOCATION_FAILURE,IDLE_STATUS_READ,IDLE_STATUS_SY,IDLE_PRODUCER_READ,
    IDLE_PRODUCER_SY,IDLE_TEST,NO_BUFFER_STORE,PUBLISH_OWNER,PUBLISH_ST,
    PUBLISH_WRITE,STORE_OUT,RETURN
};
static void fail(struct axd_dma_rx_fill *j,enum axd_rx_fill_error error)
{j->phase=AXD_RXF_FAILED;j->error=error;}
static int owner(struct axd_dma_rx_fill *j,unsigned owned,enum axd_rx_fill_dependency reason)
{if(owned)return 1;j->phase=AXD_RXF_DEPENDENCY;j->dependency=reason;return 0;}
static void done(struct axd_dma_rx_fill *j,uint32_t value)
{j->return_value=value;j->return_valid=1;j->phase=AXD_RXF_TRACE_COMPLETE;}
static int packet(struct axd_dma_rx_fill *j,struct axd_rx_fill_operation *op,
    uint32_t label,uint32_t offset,unsigned write,uint32_t value)
{
    if(!owner(j,j->context.rx.packet_owned,AXD_RXF_PACKET_OWNER))return 0;
    if(!label){fail(j,AXD_RXF_BINDING);return 0;}
    op->kind=write?AXD_RXF_WRITE32:AXD_RXF_READ32;op->resource=AXD_RXF_PACKET;
    op->identity=label;op->offset=offset;op->value=value;return 1;
}
static int memory(struct axd_dma_rx_fill *j,struct axd_rx_fill_operation *op,
    enum axd_rx_fill_resource resource,uint32_t identity,unsigned write)
{
    if(!owner(j,resource==AXD_RXF_AUX?j->context.aux_owned:j->context.data_owned,
              resource==AXD_RXF_AUX?AXD_RXF_AUX_OWNER:AXD_RXF_DATA_OWNER))return 0;
    if(!identity){fail(j,AXD_RXF_BINDING);return 0;}
    op->kind=write?AXD_RXF_WRITE32:AXD_RXF_READ32;op->resource=resource;op->identity=identity;return 1;
}
static int os(struct axd_dma_rx_fill *j,struct axd_rx_fill_operation *op,
    enum axd_rx_fill_kind kind,uint32_t identity,uint32_t value)
{
    if(!owner(j,j->context.os_owned,AXD_RXF_OS_OWNER))return 0;
    op->kind=kind;op->resource=kind==AXD_RXF_ACCOUNT?AXD_RXF_OS:AXD_RXF_PACKET;
    op->identity=identity;op->value=value;return 1;
}
static unsigned odd(uint32_t value)
{value^=value>>16;value^=value>>8;value^=value>>4;value^=value>>2;value^=value>>1;return value&1u;}
int axd_dma_rx_fill_begin(struct axd_dma_rx_fill *j,const struct axd_rx_fill_context *context)
{
    struct axd_rx_fill_context c;const struct axd_dma_rx_fill empty={0};
    if(j==NULL)return -1;
    if(context!=NULL){c=*context;context=&c;}
    *j=empty;fail(j,AXD_RXF_BAD_ARGUMENT);
    if(context==NULL || c.os_owned>1 || c.data_owned>1 || c.aux_owned>1 || c.slots_owned>1 ||
       c.rx.packet_owned>1 || c.rx.descriptor_owned>1 || c.rx.status_owned>1)return -1;
    j->context=c;j->phase=AXD_RXF_RUNNING;j->error=AXD_RXF_NO_ERROR;return 0;
}
enum axd_rx_fill_phase axd_dma_rx_fill_step(struct axd_dma_rx_fill *j,axd_rx_fill_io_fn io,void *opaque)
{
    static const uint32_t offsets[4]={8,12,0,4};
    struct axd_rx_fill_operation op={0};uint32_t reply=0;unsigned emit=0,before;
    if(j==NULL)return AXD_RXF_INVALID;
    if(j->phase!=AXD_RXF_RUNNING)return j->phase;
    switch(j->stage){
    case ENTRY:
        if(j->context.disabled)done(j,0);
        else if(j->context.split_receive){j->phase=AXD_RXF_DEPENDENCY;j->dependency=AXD_RXF_SPLIT_PATH;}
        else if(j->context.segment_mode){j->phase=AXD_RXF_DEPENDENCY;j->dependency=AXD_RXF_SEGMENT_PATH;}
        else if(j->context.rx.core!=130 || j->context.rx.minor>1)fail(j,AXD_RXF_UNSUPPORTED);
        else if(j->context.rx.ring.count==0 ? (j->context.rx.ring.in || j->context.rx.ring.out):
            axd_dma64_ring_check(&j->context.rx.ring)!=AXD_DMA64_OK)fail(j,AXD_RXF_BOUNDS);
        else j->stage=PLAN;
        break;
    case PLAN:{
        uint32_t used=j->context.rx.ring.count?
            (j->context.rx.ring.out-j->context.rx.ring.in)&(j->context.rx.ring.count-1u):0;
        j->target=j->context.receive_posts<j->context.rx.ring.count?j->context.receive_posts:j->context.rx.ring.count;
        j->index=j->context.rx.ring.out;
        if(used>j->target){done(j,1);break;}
        j->needed=j->target-used;j->return_value=1;
        if(!j->needed){j->stage=PUBLISH_OWNER;break;}
        j->alignment=j->context.flags&0x1000u?8u:j->context.flags&0x10u?16u:1u;
        j->extra=j->context.buffer_size>204?j->context.extra_headroom:0;
        if(j->extra>UINT32_MAX-j->context.buffer_size-(j->alignment-1u)){fail(j,AXD_RXF_BOUNDS);break;}
        j->allocation_size=j->context.buffer_size+j->extra+j->alignment-1u;
        j->tail=j->context.flags&0x10000u?j->context.rx.tail:0;j->stage=ALLOCATE;break;
    }
    case ALLOCATE:
        if(!owner(j,j->context.os_owned,AXD_RXF_OS_OWNER))break;
        if(!j->context.allocator.known){j->phase=AXD_RXF_DEPENDENCY;j->dependency=AXD_RXF_ALLOCATOR_BINDING;break;}
        if(j->context.allocator.known!=1){fail(j,AXD_RXF_BAD_ARGUMENT);break;}
        if(!j->context.allocator.value){fail(j,AXD_RXF_BINDING);break;}
        op.kind=AXD_RXF_ALLOCATE;op.resource=AXD_RXF_ALLOCATOR;op.identity=j->context.allocator.value;op.value=j->allocation_size;emit=1;break;
    case AUX_READ:emit=packet(j,&op,j->packet,0x160,0,0);break;
    case FLAGS_READ:emit=packet(j,&op,j->packet,0x1c,0,0);break;
    case FLAGS_WRITE:emit=packet(j,&op,j->packet,0x1c,1,j->packet_flags&~0x20u);break;
    case AUX_WRITE:emit=memory(j,&op,AXD_RXF_AUX,j->aux,1);break;
    case ACCOUNT:emit=os(j,&op,AXD_RXF_ACCOUNT,j->context.os_context,1);op.argument=1;break;
    case EXTRA_GATE:j->stage=j->extra?EXTRA_PULL:ALIGN_GATE;break;
    case EXTRA_PULL:emit=os(j,&op,AXD_RXF_PULL,j->packet,j->extra);break;
    case ALIGN_GATE:j->stage=j->alignment==1?ZERO_DATA_READ:ALIGN_DATA_READ;break;
    case ALIGN_DATA_READ:emit=packet(j,&op,j->packet,0x28,0,0);break;
    case ALIGN_PLAN:j->length=(j->alignment-j->data)&(j->alignment-1u);j->stage=j->length?ALIGN_PULL:ALIGN_NONLINEAR_READ;break;
    case ALIGN_PULL:emit=os(j,&op,AXD_RXF_PULL,j->packet,j->length);break;
    case ALIGN_NONLINEAR_READ:emit=packet(j,&op,j->packet,0xe8,0,0);break;
    case ALIGN_LENGTH_READ:emit=packet(j,&op,j->packet,0x2c,0,0);break;
    case TRIM_PLAN:j->aligned_length=j->length&~(j->alignment-1u);j->stage=j->nonlinear?TRIM_OS:LINEAR_DATA_READ;break;
    case TRIM_OS:emit=os(j,&op,AXD_RXF_TRIM,j->packet,j->aligned_length);break;
    case LINEAR_DATA_READ:emit=packet(j,&op,j->packet,0x28,0,0);break;
    case LINEAR_LENGTH_WRITE:emit=packet(j,&op,j->packet,0x2c,1,j->aligned_length);break;
    case LINEAR_TAIL_WRITE:
        if(j->data>UINT32_MAX-j->aligned_length){fail(j,AXD_RXF_BOUNDS);break;}
        emit=packet(j,&op,j->packet,0x15c,1,j->data+j->aligned_length);break;
    case ZERO_DATA_READ:emit=packet(j,&op,j->packet,0x28,0,0);break;
    case ZERO_DATA_WRITE:emit=memory(j,&op,AXD_RXF_DATA,j->data,1);break;
    case MAP_DATA_READ:emit=packet(j,&op,j->packet,0x28,0,0);break;
    case TRANSLATE:
        if(!owner(j,j->context.os_owned,AXD_RXF_OS_OWNER))break;
        if(!owner(j,j->context.data_owned,AXD_RXF_DATA_OWNER))break;
        if(!j->data){fail(j,AXD_RXF_BINDING);break;}
        op.kind=AXD_RXF_TRANSLATE;op.resource=AXD_RXF_DATA;op.identity=j->data;emit=1;break;
    case TRACK_GATE:j->stage=j->context.flags&0x10000u?PACKET_NEXT_CLEAR:SLOT_WRITE;break;
    case SLOT_WRITE:
        if(!owner(j,j->context.slots_owned,AXD_RXF_SLOT_OWNER))break;
        if(j->index>=j->context.slot_count){fail(j,AXD_RXF_BOUNDS);break;}
        op.kind=AXD_RXF_WRITE32;op.resource=AXD_RXF_SLOT;op.identity=j->context.slots_identity;op.offset=j->index*4u;op.value=j->packet;emit=1;break;
    case PACKET_NEXT_CLEAR:emit=packet(j,&op,j->packet,4,1,0);break;
    case LINK_GATE:j->stage=j->tail?OLD_TAIL_LINK:STORE_HEAD;break;
    case OLD_TAIL_LINK:emit=packet(j,&op,j->tail,4,1,j->packet);break;
    case STORE_HEAD:j->context.rx.head=j->packet;j->head_stores++;j->stage=STORE_TAIL;break;
    case STORE_TAIL:j->context.rx.tail=j->tail=j->packet;j->tail_stores++;j->stage=DESCRIPTOR_PREPARE;break;
    case DESCRIPTOR_PREPARE:{
        const struct axd_dma64_format format={j->physical,j->context.data_offset_low,j->context.data_offset_high,
            AXD_DMA64_SOF|(j->index==j->context.rx.ring.count-1u?AXD_DMA64_EOT:0),j->context.buffer_size,0};
        if(!owner(j,j->context.rx.descriptor_owned,AXD_RXF_DESCRIPTOR_OWNER))break;
        if(j->context.rx.descriptor_bytes<16 || j->index>(j->context.rx.descriptor_bytes-16u)/16u){fail(j,AXD_RXF_BOUNDS);break;}
        if(axd_dma64_format(&j->descriptor,&format)!=AXD_DMA64_OK){fail(j,AXD_RXF_BOUNDS);break;}
        j->cursor=0;j->parity_sample=0;j->stage=WRITE_WORDS;break;
    }
    case WRITE_WORDS:case READ_WORDS:case PARITY_WRITE:
        op.kind=j->stage==READ_WORDS?AXD_RXF_READ32:AXD_RXF_WRITE32;op.resource=AXD_RXF_DESCRIPTOR;
        op.identity=j->context.rx.descriptor_identity;op.offset=j->index*16u+(j->stage==PARITY_WRITE?4:offsets[j->cursor]);
        if(j->stage==PARITY_WRITE)op.value=j->descriptor.control2|AXD_DMA64_PARITY;
        else if(j->stage==WRITE_WORDS)switch(j->cursor){
            case 0:op.value=j->descriptor.address_low;break;case 1:op.value=j->descriptor.address_high;break;
            case 2:op.value=j->descriptor.control1;break;default:op.value=j->descriptor.control2;break;
        }
        emit=1;break;
    case PARITY_CHECK:j->stage=odd(j->parity_sample)?PARITY_WRITE:ADVANCE_INDEX;break;
    case ADVANCE_INDEX:j->index=(j->index+1u)&(j->context.rx.ring.count-1u);j->stage=BUFFER_DONE;break;
    case BUFFER_DONE:
        j->completed_buffers++;j->stage=j->completed_buffers==j->needed?PUBLISH_OWNER:ALLOCATE;break;
    case ALLOCATION_FAILURE:
        if(!j->completed_buffers && j->context.rx.ring.count)j->stage=IDLE_STATUS_READ;
        else {j->return_value=j->completed_buffers?1u:0u;j->stage=NO_BUFFER_STORE;}break;
    case IDLE_STATUS_READ:case IDLE_STATUS_SY:case IDLE_PRODUCER_READ:case IDLE_PRODUCER_SY:
        if(!owner(j,j->context.rx.status_owned,AXD_RXF_CHANNEL_OWNER))break;
        op.resource=AXD_RXF_CHANNEL;op.identity=j->context.rx.status_identity;
        op.kind=j->stage==IDLE_STATUS_READ || j->stage==IDLE_PRODUCER_READ?AXD_RXF_READ32:AXD_RXF_FULL_BARRIER;
        op.offset=j->stage==IDLE_STATUS_READ?0x10u:j->stage==IDLE_PRODUCER_READ?4u:0;emit=1;break;
    case IDLE_TEST:j->return_value=((j->status_sample^j->producer_sample)&j->context.rx.ring.pointer_mask)!=0;j->stage=NO_BUFFER_STORE;break;
    case NO_BUFFER_STORE:j->context.no_buffer_count+=j->needed-j->completed_buffers;j->no_buffer_committed=1;j->stage=PUBLISH_OWNER;break;
    case PUBLISH_OWNER:{
        struct axd_dma64_ring ring=j->context.rx.ring;ring.out=j->index;
        if(!owner(j,j->context.rx.status_owned,AXD_RXF_CHANNEL_OWNER))break;
        if(!ring.count)j->producer=ring.pointer_base;
        else if(axd_dma64_producer(&ring,&j->producer)!=AXD_DMA64_OK){fail(j,AXD_RXF_BOUNDS);break;}
        j->stage=PUBLISH_ST;break;
    }
    case PUBLISH_ST:case PUBLISH_WRITE:
        op.kind=j->stage==PUBLISH_ST?AXD_RXF_STORE_BARRIER:AXD_RXF_WRITE32;op.resource=AXD_RXF_CHANNEL;
        op.identity=j->context.rx.status_identity;op.offset=j->stage==PUBLISH_ST?0:4;op.value=j->stage==PUBLISH_ST?0:j->producer;emit=1;break;
    case STORE_OUT:j->context.rx.ring.out=j->index;j->out_committed=1;j->stage=RETURN;break;
    case RETURN:done(j,j->return_value);break;
    default:fail(j,AXD_RXF_BAD_ARGUMENT);break;
    }
    if(!emit)return j->phase;
    before=j->stage;
    if(io==NULL){fail(j,AXD_RXF_BAD_ARGUMENT);return j->phase;}
    j->attempted_ops++;
    if(io(opaque,&op,&reply)){fail(j,AXD_RXF_IO_ERROR);return j->phase;}
    j->completed_ops++;
    switch(before){
    case ALLOCATE:
        j->packet=reply;if(reply)j->allocated_buffers++;
        j->stage=reply?AUX_READ:ALLOCATION_FAILURE;break;
    case AUX_READ:j->aux=reply;j->stage=FLAGS_READ;break;
    case FLAGS_READ:j->packet_flags=reply;j->stage=FLAGS_WRITE;break;
    case ALIGN_DATA_READ:case LINEAR_DATA_READ:case ZERO_DATA_READ:case MAP_DATA_READ:j->data=reply;j->stage=before+1;break;
    case ALIGN_NONLINEAR_READ:j->nonlinear=reply;j->stage=ALIGN_LENGTH_READ;break;
    case ALIGN_LENGTH_READ:j->length=reply;j->stage=TRIM_PLAN;break;
    case TRIM_OS:j->stage=ZERO_DATA_READ;break;
    case LINEAR_TAIL_WRITE:j->stage=ZERO_DATA_WRITE;break;
    case TRANSLATE:j->physical=reply;j->stage=TRACK_GATE;break;
    case SLOT_WRITE:j->slot_writes++;j->stage=DESCRIPTOR_PREPARE;break;
    case OLD_TAIL_LINK:j->list_links++;j->stage=STORE_TAIL;break;
    case WRITE_WORDS:
        j->descriptor_writes++;if(++j->cursor==4){j->cursor=0;j->stage=j->context.flags&1u?READ_WORDS:ADVANCE_INDEX;}break;
    case READ_WORDS:j->parity_reads++;j->parity_sample^=reply;j->last_read=reply;if(++j->cursor==4)j->stage=PARITY_CHECK;break;
    case PARITY_WRITE:j->descriptor_writes++;j->stage=ADVANCE_INDEX;break;
    case IDLE_STATUS_READ:j->status_sample=reply;j->stage=IDLE_STATUS_SY;break;
    case IDLE_PRODUCER_READ:j->producer_sample=reply;j->stage=IDLE_PRODUCER_SY;break;
    case PUBLISH_WRITE:j->producer_written=1;j->stage=STORE_OUT;break;
    default:j->stage=before+1;break;
    }
    if(op.kind==AXD_RXF_READ32)j->last_read=reply;
    return j->phase;
}
enum axd_rx_fill_phase axd_dma_rx_fill_cancel(struct axd_dma_rx_fill *j)
{if(j==NULL)return AXD_RXF_INVALID;if(j->phase==AXD_RXF_RUNNING)j->phase=AXD_RXF_CANCELLED;return j->phase;}
