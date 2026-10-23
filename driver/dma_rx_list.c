/* SPDX-License-Identifier: MIT */
#include "dma_rx_list.h"
enum stage {
    ENTRY, STATUS_READ, STATUS_SY, STATUS_STORE, PLAN, PACKET_READ, HEAD_STORE,
    DESCRIPTOR_READ, ITERATE, DETACH, STORE_INDEX, STORE_AVAILABLE,
    LINKED_GATE, LINKED_INDEX, LINKED_AVAILABLE, CLEAR_TAIL, RETURN
};
static void fail(struct axd_dma_rx_list *j,enum axd_rx_list_error e)
{j->phase=AXD_RX_LIST_FAILED;j->error=e;}
static void done(struct axd_dma_rx_list *j,uint32_t value)
{j->return_value=value;j->return_valid=1;j->phase=AXD_RX_LIST_DONE;}
int axd_dma_rx_list_begin(struct axd_dma_rx_list *j,uint32_t budget,uint32_t force_all,
                           const struct axd_rx_list_context *context)
{
    struct axd_rx_list_context c;
    static const struct axd_dma_rx_list empty={0};
    if(j==NULL)return -1;
    if(context!=NULL){c=*context;context=&c;}
    *j=empty;fail(j,AXD_RX_LIST_BAD_ARGUMENT);
    if(context==NULL || c.status_owned>1 || c.descriptor_owned>1 || c.packet_owned>1)return -1;
    j->context=c;j->budget=budget;j->force_all=force_all;
    if(c.core!=130 || c.minor>1){fail(j,AXD_RX_LIST_UNSUPPORTED);return -1;}
    if(c.ring.count!=0 && axd_dma64_ring_check(&c.ring)!=AXD_DMA64_OK){fail(j,AXD_RX_LIST_BOUNDS);return -1;}
    j->phase=AXD_RX_LIST_RUNNING;j->error=AXD_RX_LIST_NO_ERROR;return 0;
}
enum axd_rx_list_phase axd_dma_rx_list_step(struct axd_dma_rx_list *j,
                                             axd_rx_list_io_fn io,void *context)
{
    struct axd_rx_list_operation op={0};uint32_t value=0,index,used,available;
    unsigned emit=0;
    if(j==NULL)return AXD_RX_LIST_INVALID;
    if(j->phase!=AXD_RX_LIST_RUNNING)return j->phase;
    switch(j->stage){
    case ENTRY:
        if(j->context.ring.count==0)done(j,0);
        else {j->index=j->context.ring.in;j->stage=j->force_all ? PLAN:STATUS_READ;}
        break;
    case STATUS_READ:case STATUS_SY:
        if(!j->context.status_owned){j->phase=AXD_RX_LIST_DEPENDENCY;break;}
        op.kind=j->stage==STATUS_READ ? AXD_RX_LIST_STATUS_READ32:AXD_RX_LIST_FULL_BARRIER;
        op.identity=j->context.status_identity;op.offset=j->stage==STATUS_READ ? 0x10u:0;emit=1;break;
    case STATUS_STORE:
        if(axd_dma64_status_index(&j->context.ring,j->status_sample,&index)!=AXD_DMA64_OK){fail(j,AXD_RX_LIST_BOUNDS);break;}
        j->context.cached_current=(uint16_t)index;j->stage=PLAN;break;
    case PLAN:
        j->end=j->context.ring.out;
        if(!j->force_all){
            uint32_t count=(j->context.cached_current-j->index)&(j->context.ring.count-1u);
            if(count>j->budget)count=j->budget;
            j->end=(j->index+count)&(j->context.ring.count-1u);
        }
        j->planned_entries=(j->end-j->index)&(j->context.ring.count-1u);
        j->original_head=j->context.head;j->packet=j->original_head;
        if(!j->planned_entries)done(j,0);else j->stage=PACKET_READ;
        break;
    case PACKET_READ:case DETACH:
        if(!j->context.packet_owned){j->phase=AXD_RX_LIST_DEPENDENCY;break;}
        op.identity=j->stage==PACKET_READ ? j->packet:j->last_packet;
        if(!op.identity){fail(j,AXD_RX_LIST_BINDING);break;}
        op.kind=j->stage==PACKET_READ ? AXD_RX_LIST_PACKET_NEXT_READ32:AXD_RX_LIST_PACKET_NEXT_WRITE32;
        op.offset=4;emit=1;break;
    case HEAD_STORE:j->context.head=j->next_sample;j->head_stores++;j->stage=DESCRIPTOR_READ;break;
    case DESCRIPTOR_READ:
        if(!j->context.descriptor_owned){j->phase=AXD_RX_LIST_DEPENDENCY;break;}
        if(j->context.descriptor_bytes<12u || j->index>(j->context.descriptor_bytes-12u)/16u){fail(j,AXD_RX_LIST_BOUNDS);break;}
        op.kind=AXD_RX_LIST_DESCRIPTOR_READ32;op.identity=j->context.descriptor_identity;
        op.offset=j->index*16u+8u;emit=1;break;
    case ITERATE:
        j->index=(j->index+1u)&(j->context.ring.count-1u);
        if(j->index==j->end)j->stage=DETACH;
        else {j->packet=j->next_sample;j->stage=PACKET_READ;}
        break;
    case STORE_INDEX:j->context.ring.in=j->end;j->index_committed=1;j->stage=STORE_AVAILABLE;break;
    case STORE_AVAILABLE:
        if(axd_dma64_ring_counts(&j->context.ring,&used,&available)!=AXD_DMA64_OK)fail(j,AXD_RX_LIST_BOUNDS);
        else {j->context.available=available;j->available_committed=1;j->stage=LINKED_GATE;}
        break;
    case LINKED_GATE:
        if(!j->context.linked_mode)j->stage=CLEAR_TAIL;
        else if(!j->context.linked_known || !j->context.linked_owned)j->phase=AXD_RX_LIST_DEPENDENCY;
        else if(j->context.linked_known>1 || j->context.linked_owned>1)fail(j,AXD_RX_LIST_BAD_ARGUMENT);
        else if(!j->context.linked_identity)fail(j,AXD_RX_LIST_BINDING);
        else j->stage=LINKED_INDEX;
        break;
    case LINKED_INDEX:j->context.linked_in=(uint16_t)j->end;j->linked_in_committed=1;j->stage=LINKED_AVAILABLE;break;
    case LINKED_AVAILABLE:j->context.linked_available=j->context.available;j->linked_available_committed=1;j->stage=CLEAR_TAIL;break;
    case CLEAR_TAIL:
        if(!j->context.head){j->context.tail=0;j->tail_cleared=1;}
        j->stage=RETURN;break;
    case RETURN:done(j,j->original_head);break;
    default:fail(j,AXD_RX_LIST_BAD_ARGUMENT);break;
    }
    if(emit){
        unsigned before=j->stage;
        if(io==NULL){fail(j,AXD_RX_LIST_BAD_ARGUMENT);return j->phase;}
        j->attempted_ops++;
        if(io(context,&op,&value)!=0){fail(j,AXD_RX_LIST_IO_ERROR);return j->phase;}
        j->completed_ops++;
        if(before==STATUS_READ)j->status_sample=value;
        else if(before==PACKET_READ){j->next_sample=value;j->last_packet=j->packet;}
        else if(before==DESCRIPTOR_READ){j->descriptor_sample=value;j->traversed_entries++;}
        else if(before==DETACH)j->tail_detached=1;
        j->stage=before+1;
    }
    return j->phase;
}
enum axd_rx_list_phase axd_dma_rx_list_cancel(struct axd_dma_rx_list *j)
{
    if(j==NULL)return AXD_RX_LIST_INVALID;
    if(j->phase==AXD_RX_LIST_RUNNING)j->phase=AXD_RX_LIST_CANCELLED;
    return j->phase;
}
