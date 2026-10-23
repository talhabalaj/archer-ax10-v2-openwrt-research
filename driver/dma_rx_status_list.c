/* SPDX-License-Identifier: MIT */
#include "dma_rx_status_list.h"
enum stage {ENTRY,STATUS_READ,STATUS_SY,STATUS_STORE,PLAN,NODE_READ,HEAD_STORE,
    DESCRIPTOR_READ,ITERATE,DETACH,STORE_INDEX,CLEAR_TAIL,STORE_AVAILABLE,RETURN};
static void fail(struct axd_dma_rx_status_list *j,enum axd_rx_status_list_error e)
{j->phase=AXD_RX_STATUS_LIST_FAILED;j->error=e;}
static void done(struct axd_dma_rx_status_list *j,uint32_t value)
{j->return_value=value;j->return_valid=1;j->phase=AXD_RX_STATUS_LIST_DONE;}
int axd_dma_rx_status_list_begin(struct axd_dma_rx_status_list *j,uint32_t force_all,
    const struct axd_rx_status_list_context *context)
{
    struct axd_rx_status_list_context c;static const struct axd_dma_rx_status_list empty={0};
    if(j==NULL)return -1;
    if(context!=NULL){c=*context;context=&c;}
    *j=empty;fail(j,AXD_RX_STATUS_LIST_BAD_ARGUMENT);
    if(context==NULL || c.status_owned>1 || c.descriptor_owned>1 || c.node_owned>1)return -1;
    j->context=c;j->force_all=force_all;
    if(c.core!=130 || c.minor>1){fail(j,AXD_RX_STATUS_LIST_UNSUPPORTED);return -1;}
    if(c.ring.count!=0 && (axd_dma64_ring_check(&c.ring)!=AXD_DMA64_OK ||
       (!force_all && c.cached_current>=c.ring.count))){fail(j,AXD_RX_STATUS_LIST_BOUNDS);return -1;}
    j->phase=AXD_RX_STATUS_LIST_RUNNING;j->error=AXD_RX_STATUS_LIST_NO_ERROR;return 0;
}
enum axd_rx_status_list_phase axd_dma_rx_status_list_step(struct axd_dma_rx_status_list *j,
    axd_rx_status_list_io_fn io,void *context)
{
    struct axd_rx_status_list_operation op={0};uint32_t value=0,index,used,available;unsigned emit=0;
    if(j==NULL)return AXD_RX_STATUS_LIST_INVALID;
    if(j->phase!=AXD_RX_STATUS_LIST_RUNNING)return j->phase;
    switch(j->stage){
    case ENTRY:
        if(!j->context.ring.count)done(j,0);
        else {j->index=j->context.ring.in;j->stage=!j->force_all && j->index==j->context.cached_current?STATUS_READ:PLAN;}
        break;
    case STATUS_READ:case STATUS_SY:
        if(!j->context.status_owned){j->phase=AXD_RX_STATUS_LIST_DEPENDENCY;break;}
        op.kind=j->stage==STATUS_READ?AXD_RX_STATUS_LIST_STATUS_READ32:AXD_RX_STATUS_LIST_FULL_BARRIER;
        op.identity=j->context.status_identity;op.offset=j->stage==STATUS_READ?0x10u:0;emit=1;break;
    case STATUS_STORE:
        if(axd_dma64_status_index(&j->context.ring,j->status_sample,&index)!=AXD_DMA64_OK){fail(j,AXD_RX_STATUS_LIST_BOUNDS);break;}
        j->context.cached_current=(uint16_t)index;j->stage=PLAN;break;
    case PLAN:
        j->end=j->force_all?j->context.ring.out:j->context.cached_current;
        j->planned_entries=(j->end-j->index)&(j->context.ring.count-1u);
        j->original_head=j->context.head;j->node=j->original_head;
        if(!j->planned_entries)done(j,0);else j->stage=NODE_READ;
        break;
    case NODE_READ:case DETACH:
        if(!j->context.node_owned){j->phase=AXD_RX_STATUS_LIST_DEPENDENCY;break;}
        op.identity=j->stage==NODE_READ?j->node:j->last_node;
        if(!op.identity){fail(j,AXD_RX_STATUS_LIST_BINDING);break;}
        op.kind=j->stage==NODE_READ?AXD_RX_STATUS_LIST_NODE_NEXT_READ32:AXD_RX_STATUS_LIST_NODE_NEXT_WRITE8;
        op.offset=0x1cu+(j->stage==DETACH?j->tail_bytes_cleared:0u);emit=1;break;
    case HEAD_STORE:j->context.head=j->next_sample;j->head_stores++;j->stage=DESCRIPTOR_READ;break;
    case DESCRIPTOR_READ:
        if(!j->context.descriptor_owned){j->phase=AXD_RX_STATUS_LIST_DEPENDENCY;break;}
        if(j->context.descriptor_bytes<12u || j->index>(j->context.descriptor_bytes-12u)/16u){fail(j,AXD_RX_STATUS_LIST_BOUNDS);break;}
        op.kind=AXD_RX_STATUS_LIST_DESCRIPTOR_READ32;op.identity=j->context.descriptor_identity;op.offset=j->index*16u+8u;emit=1;break;
    case ITERATE:
        j->index=(j->index+1u)&(j->context.ring.count-1u);
        if(j->index==j->end)j->stage=DETACH;else {j->node=j->next_sample;j->stage=NODE_READ;}
        break;
    case STORE_INDEX:j->context.ring.in=j->end;j->index_committed=1;j->stage=CLEAR_TAIL;break;
    case CLEAR_TAIL:if(!j->context.head){j->context.tail=0;j->tail_cleared=1;}j->stage=STORE_AVAILABLE;break;
    case STORE_AVAILABLE:
        if(axd_dma64_ring_counts(&j->context.ring,&used,&available)!=AXD_DMA64_OK)fail(j,AXD_RX_STATUS_LIST_BOUNDS);
        else {j->context.available=available;j->available_committed=1;j->stage=RETURN;}break;
    case RETURN:done(j,j->original_head);break;
    default:fail(j,AXD_RX_STATUS_LIST_BAD_ARGUMENT);break;
    }
    if(emit){
        unsigned before=j->stage;
        if(io==NULL){fail(j,AXD_RX_STATUS_LIST_BAD_ARGUMENT);return j->phase;}
        j->attempted_ops++;
        if(io(context,&op,&value)){fail(j,AXD_RX_STATUS_LIST_IO_ERROR);return j->phase;}
        j->completed_ops++;
        if(before==STATUS_READ)j->status_sample=value;
        else if(before==NODE_READ){j->next_sample=value;j->last_node=j->node;}
        else if(before==DESCRIPTOR_READ){j->descriptor_sample=value;j->traversed_entries++;}
        else if(before==DETACH)j->tail_bytes_cleared++;
        j->stage=before==DETACH && j->tail_bytes_cleared<4?DETACH:before+1;
    }
    return j->phase;
}
enum axd_rx_status_list_phase axd_dma_rx_status_list_cancel(struct axd_dma_rx_status_list *j)
{if(j==NULL)return AXD_RX_STATUS_LIST_INVALID;if(j->phase==AXD_RX_STATUS_LIST_RUNNING)j->phase=AXD_RX_STATUS_LIST_CANCELLED;return j->phase;}
