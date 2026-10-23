/* SPDX-License-Identifier: MIT */
#include "dma_rx_status_fill.h"
enum stage {ENTRY,PLAN,POOL_BIND,POOL_HEAD,HEAD_TEST,NEXT_READ,HEAD_WRITE,POP_CLEAR,
    COUNT_READ,COUNT_WRITE,DATA_READ,DATA_CLEAR,MAP_DATA_READ,TRANSLATE,INDEX_WRITE,
    POST_CLEAR,LINK_GATE,LINK_WRITE,STORE_HEAD,STORE_TAIL,PREPARED,DESCRIPTOR_PREPARE,
    WRITE_WORDS,READ_WORDS,PARITY_CHECK,PARITY_WRITE,ADVANCE,COMPLETED,FAILURE_READ,
    FAILURE_WRITE,FAILURE_GATE,IDLE_STATUS_READ,IDLE_STATUS_SY,IDLE_PRODUCER_READ,
    IDLE_PRODUCER_SY,IDLE_DONE,NO_BUFFER_STORE,PUBLISH_OWNER,PUBLISH_ST,PUBLISH_WRITE,
    STORE_OUT,RETURN};
static void fail(struct axd_dma_rx_status_fill *j,enum axd_rx_status_fill_error e)
{j->phase=AXD_RSF_FAILED;j->error=e;}
static int owner(struct axd_dma_rx_status_fill *j,unsigned owned,enum axd_rx_status_fill_dependency d)
{if(owned)return 1;j->phase=AXD_RSF_DEPENDENCY;j->dependency=d;return 0;}
static void done(struct axd_dma_rx_status_fill *j,uint32_t v)
{j->return_value=v;j->return_valid=1;j->phase=AXD_RSF_TRACE_COMPLETE;}
static int pool(struct axd_dma_rx_status_fill *j,struct axd_rx_status_fill_operation *op,
    unsigned write,uint32_t offset,uint32_t value)
{
    if(!owner(j,j->context.pool_owned,AXD_RSF_POOL_OWNER))return 0;
    op->kind=write?AXD_RSF_WRITE32:AXD_RSF_READ32;op->resource=AXD_RSF_POOL;
    op->identity=j->context.pool.value;op->offset=offset;op->value=value;return 1;
}
static int node(struct axd_dma_rx_status_fill *j,struct axd_rx_status_fill_operation *op,
    enum axd_rx_status_fill_kind kind,uint32_t identity,uint32_t offset,uint32_t value)
{
    if(!owner(j,j->context.rx.node_owned,AXD_RSF_NODE_OWNER))return 0;
    if(!identity){fail(j,AXD_RSF_BINDING);return 0;}
    op->kind=kind;op->resource=AXD_RSF_NODE;op->identity=identity;op->offset=offset;op->value=value;return 1;
}
static unsigned odd(uint32_t v)
{v^=v>>16;v^=v>>8;v^=v>>4;v^=v>>2;v^=v>>1;return v&1u;}
int axd_dma_rx_status_fill_begin(struct axd_dma_rx_status_fill *j,const struct axd_rx_status_fill_context *context)
{
    struct axd_rx_status_fill_context c;static const struct axd_dma_rx_status_fill empty={0};
    if(j==NULL)return -1;
    if(context!=NULL){c=*context;context=&c;}
    *j=empty;fail(j,AXD_RSF_BAD_ARGUMENT);
    if(context==NULL || c.pool_owned>1 || c.data_owned>1 || c.os_owned>1 || c.rx.node_owned>1 || c.rx.descriptor_owned>1 || c.rx.status_owned>1)return -1;
    j->context=c;j->phase=AXD_RSF_RUNNING;j->error=AXD_RSF_NO_ERROR;return 0;
}
enum axd_rx_status_fill_phase axd_dma_rx_status_fill_step(struct axd_dma_rx_status_fill *j,axd_rx_status_fill_io_fn io,void *opaque)
{
    static const uint32_t offsets[]={8,12,0,4};
    struct axd_rx_status_fill_operation op={0};uint32_t reply=0;unsigned emit=0,before;
    if(j==NULL)return AXD_RSF_INVALID;
    if(j->phase!=AXD_RSF_RUNNING)return j->phase;
    switch(j->stage){
    case ENTRY:
        if(j->context.disabled)done(j,0);
        else if(j->context.rx.core!=130 || j->context.rx.minor>1)fail(j,AXD_RSF_UNSUPPORTED);
        else if(j->context.rx.ring.count==0?(j->context.rx.ring.in || j->context.rx.ring.out):axd_dma64_ring_check(&j->context.rx.ring)!=AXD_DMA64_OK)fail(j,AXD_RSF_BOUNDS);
        else j->stage=PLAN;
        break;
    case PLAN:{
        uint32_t used=j->context.rx.ring.count?(j->context.rx.ring.out-j->context.rx.ring.in)&(j->context.rx.ring.count-1u):0;
        j->target=j->context.receive_posts<j->context.rx.ring.count?j->context.receive_posts:j->context.rx.ring.count;
        j->index=j->context.rx.ring.out;
        if(j->target<used){done(j,1);break;}
        j->needed=j->target-used;j->tail=j->context.rx.tail;j->stage=j->needed?POOL_BIND:PUBLISH_OWNER;break;
    }
    case POOL_BIND:
        if(!j->context.pool.known){j->phase=AXD_RSF_DEPENDENCY;j->dependency=AXD_RSF_POOL_BINDING;}
        else if(j->context.pool.known!=1)fail(j,AXD_RSF_BAD_ARGUMENT);
        else if(!j->context.pool.value)fail(j,AXD_RSF_BINDING);
        else j->stage=POOL_HEAD;
        break;
    case POOL_HEAD:emit=pool(j,&op,0,0,0);break;
    case HEAD_TEST:if(j->node)j->stage=NEXT_READ;else j->stage=FAILURE_READ;break;
    case NEXT_READ:emit=node(j,&op,AXD_RSF_READ32,j->node,0x1c,0);break;
    case HEAD_WRITE:emit=pool(j,&op,1,0,j->next);break;
    case POP_CLEAR:emit=node(j,&op,AXD_RSF_WRITE8,j->node,0x1c+j->byte_cursor,0);break;
    case COUNT_READ:emit=pool(j,&op,0,4,0);break;
    case COUNT_WRITE:emit=pool(j,&op,1,4,j->pool_count-1u);break;
    case DATA_READ:case MAP_DATA_READ:emit=node(j,&op,AXD_RSF_READ32,j->node,0,0);break;
    case DATA_CLEAR:
        if(!owner(j,j->context.data_owned,AXD_RSF_DATA_OWNER))break;
        if(!j->data){fail(j,AXD_RSF_BINDING);break;}
        op.kind=AXD_RSF_WRITE32;op.resource=AXD_RSF_DATA;op.identity=j->data;emit=1;break;
    case TRANSLATE:
        if(!owner(j,j->context.os_owned,AXD_RSF_OS_OWNER) || !owner(j,j->context.data_owned,AXD_RSF_DATA_OWNER))break;
        if(!j->data){fail(j,AXD_RSF_BINDING);break;}
        op.kind=AXD_RSF_TRANSLATE;op.resource=AXD_RSF_DATA;op.identity=j->data;emit=1;break;
    case INDEX_WRITE:emit=node(j,&op,AXD_RSF_WRITE16,j->node,0x18,j->index);break;
    case POST_CLEAR:emit=node(j,&op,AXD_RSF_WRITE8,j->node,0x1c+j->byte_cursor,0);break;
    case LINK_GATE:j->stage=j->tail?LINK_WRITE:STORE_HEAD;break;
    case LINK_WRITE:emit=node(j,&op,AXD_RSF_WRITE32,j->tail,0x1c,j->node);break;
    case STORE_HEAD:j->context.rx.head=j->node;j->head_stores++;j->stage=STORE_TAIL;break;
    case STORE_TAIL:j->context.rx.tail=j->tail=j->node;j->tail_stores++;j->stage=PREPARED;break;
    case PREPARED:j->prepared_nodes++;j->stage=DESCRIPTOR_PREPARE;break;
    case DESCRIPTOR_PREPARE:{
        const struct axd_dma64_format format={j->physical,j->context.data_offset_low,j->context.data_offset_high,
            AXD_DMA64_SOF|(j->index==j->context.rx.ring.count-1u?AXD_DMA64_EOT:0),j->context.buffer_size,0};
        if(!owner(j,j->context.rx.descriptor_owned,AXD_RSF_DESCRIPTOR_OWNER))break;
        if(j->context.rx.descriptor_bytes<16 || j->index>(j->context.rx.descriptor_bytes-16u)/16u){fail(j,AXD_RSF_BOUNDS);break;}
        if(axd_dma64_format(&j->descriptor,&format)!=AXD_DMA64_OK){fail(j,AXD_RSF_BOUNDS);break;}
        j->cursor=0;j->parity_sample=0;j->stage=WRITE_WORDS;break;
    }
    case WRITE_WORDS:case READ_WORDS:case PARITY_WRITE:
        op.kind=j->stage==READ_WORDS?AXD_RSF_READ32:AXD_RSF_WRITE32;op.resource=AXD_RSF_DESCRIPTOR;
        op.identity=j->context.rx.descriptor_identity;op.offset=j->index*16u+(j->stage==PARITY_WRITE?4:offsets[j->cursor]);
        if(j->stage==PARITY_WRITE)op.value=j->descriptor.control2|AXD_DMA64_PARITY;
        else if(j->stage==WRITE_WORDS)switch(j->cursor){case 0:op.value=j->descriptor.address_low;break;case 1:op.value=j->descriptor.address_high;break;case 2:op.value=j->descriptor.control1;break;default:op.value=j->descriptor.control2;break;}
        emit=1;break;
    case PARITY_CHECK:j->stage=odd(j->parity_sample)?PARITY_WRITE:ADVANCE;break;
    case ADVANCE:j->index=(j->index+1u)&(j->context.rx.ring.count-1u);j->stage=COMPLETED;break;
    case COMPLETED:j->completed_nodes++;j->stage=j->completed_nodes==j->needed?PUBLISH_OWNER:POOL_HEAD;break;
    case FAILURE_READ:emit=pool(j,&op,0,8,0);break;
    case FAILURE_WRITE:emit=pool(j,&op,1,8,j->pool_failures+1u);break;
    case FAILURE_GATE:j->stage=j->completed_nodes || !j->context.rx.ring.count?NO_BUFFER_STORE:IDLE_STATUS_READ;break;
    case IDLE_STATUS_READ:case IDLE_STATUS_SY:case IDLE_PRODUCER_READ:case IDLE_PRODUCER_SY:
        if(!owner(j,j->context.rx.status_owned,AXD_RSF_CHANNEL_OWNER))break;
        op.resource=AXD_RSF_CHANNEL;op.identity=j->context.rx.status_identity;
        op.kind=j->stage==IDLE_STATUS_READ || j->stage==IDLE_PRODUCER_READ?AXD_RSF_READ32:AXD_RSF_FULL_BARRIER;
        op.offset=j->stage==IDLE_STATUS_READ?0x10u:j->stage==IDLE_PRODUCER_READ?4u:0;emit=1;break;
    case IDLE_DONE:j->idle_observed=1;j->stage=NO_BUFFER_STORE;break;
    case NO_BUFFER_STORE:j->context.no_buffer_count+=j->needed-j->completed_nodes;j->no_buffer_committed=1;j->stage=PUBLISH_OWNER;break;
    case PUBLISH_OWNER:{
        struct axd_dma64_ring ring=j->context.rx.ring;ring.out=j->index;
        if(!owner(j,j->context.rx.status_owned,AXD_RSF_CHANNEL_OWNER))break;
        if(!ring.count)j->producer=ring.pointer_base;
        else if(axd_dma64_producer(&ring,&j->producer)!=AXD_DMA64_OK){fail(j,AXD_RSF_BOUNDS);break;}
        j->stage=PUBLISH_ST;break;
    }
    case PUBLISH_ST:case PUBLISH_WRITE:
        op.kind=j->stage==PUBLISH_ST?AXD_RSF_STORE_BARRIER:AXD_RSF_WRITE32;op.resource=AXD_RSF_CHANNEL;
        op.identity=j->context.rx.status_identity;op.offset=j->stage==PUBLISH_ST?0:4;op.value=j->stage==PUBLISH_ST?0:j->producer;emit=1;break;
    case STORE_OUT:j->context.rx.ring.out=j->index;j->out_committed=1;j->stage=RETURN;break;
    case RETURN:done(j,0);break;
    default:fail(j,AXD_RSF_BAD_ARGUMENT);break;
    }
    if(!emit)return j->phase;
    before=j->stage;
    if(io==NULL){fail(j,AXD_RSF_BAD_ARGUMENT);return j->phase;}
    j->attempted_ops++;
    if(io(opaque,&op,&reply)){fail(j,AXD_RSF_IO_ERROR);return j->phase;}
    j->completed_ops++;
    switch(before){
    case POOL_HEAD:j->node=reply;j->stage=HEAD_TEST;break;
    case NEXT_READ:j->next=reply;j->stage=HEAD_WRITE;break;
    case HEAD_WRITE:j->taken_nodes++;j->byte_cursor=0;j->stage=POP_CLEAR;break;
    case POP_CLEAR:j->pop_bytes++;if(++j->byte_cursor==4){j->byte_cursor=0;j->stage=COUNT_READ;}break;
    case COUNT_READ:j->pool_count=reply;j->stage=COUNT_WRITE;break;
    case DATA_READ:case MAP_DATA_READ:j->data=reply;j->stage=before+1;break;
    case TRANSLATE:j->physical=reply;j->stage=INDEX_WRITE;break;
    case INDEX_WRITE:j->index_writes++;j->byte_cursor=0;j->stage=POST_CLEAR;break;
    case POST_CLEAR:j->post_bytes++;if(++j->byte_cursor==4){j->byte_cursor=0;j->stage=LINK_GATE;}break;
    case LINK_WRITE:j->list_links++;j->stage=STORE_TAIL;break;
    case WRITE_WORDS:j->descriptor_writes++;if(++j->cursor==4){j->cursor=0;j->stage=j->context.flags&1u?READ_WORDS:ADVANCE;}break;
    case READ_WORDS:j->parity_reads++;j->parity_sample^=reply;if(++j->cursor==4)j->stage=PARITY_CHECK;break;
    case PARITY_WRITE:j->descriptor_writes++;j->stage=ADVANCE;break;
    case FAILURE_READ:j->pool_failures=reply;j->stage=FAILURE_WRITE;break;
    case IDLE_STATUS_READ:j->status_sample=reply;j->stage=IDLE_STATUS_SY;break;
    case IDLE_PRODUCER_READ:j->producer_sample=reply;j->stage=IDLE_PRODUCER_SY;break;
    case PUBLISH_WRITE:j->producer_written=1;j->stage=STORE_OUT;break;
    default:j->stage=before+1;break;
    }
    if(op.kind==AXD_RSF_READ32)j->last_read=reply;
    return j->phase;
}
enum axd_rx_status_fill_phase axd_dma_rx_status_fill_cancel(struct axd_dma_rx_status_fill *j)
{if(j==NULL)return AXD_RSF_INVALID;if(j->phase==AXD_RSF_RUNNING)j->phase=AXD_RSF_CANCELLED;return j->phase;}
