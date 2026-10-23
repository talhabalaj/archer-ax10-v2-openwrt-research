/* SPDX-License-Identifier: MIT */
#include "rx_status.h"
enum stage {
    ENTRY, RUN_LIST, LIST_RETURN, NODE, READ_DATA, READ_LENGTH, TEST_LENGTH,
    DELAY, DELAY_RETURN, ACCEPT_TAIL, ACCEPT_COUNT, READ_ACCEPT_NEXT, SET_CURRENT,
    DROP_READ_NEXT, DROP_LINK, DROP_WRITE_LINK, DROP_HEAD, DROP_CLEAR,
    POOL_BIND, POOL_NODE, POOL_CALLBACK, CALLBACK_GATE, POOL_INDEX,
    INDEX_GATE, POOL_ARGUMENT, CALLBACK_BIND, RUN_OFFLOAD, CLEAR_INDEX, POOL_COUNT,
    COUNT_INCREMENT, POOL_COUNT_WRITE, POOL_NEXT, POOL_NEXT_GATE,
    POOL_HIGHWATER, POOL_HEAD, HIGHWATER_SELECT, HIGHWATER_WRITE,
    RETURN_LINK, RETURN_HEAD, DROP_NEXT, DROP_COUNT, RESULT_HEAD, RESULT_TAIL, FINISH
};
struct relay { struct axd_rx_status *job; axd_rx_status_io_fn io; void *opaque; };
static void fail(struct axd_rx_status *j,enum axd_rx_status_error error)
{ j->phase=AXD_RS_FAILED;j->error=error; }
static void dependency(struct axd_rx_status *j,enum axd_rx_status_dependency reason)
{ j->phase=AXD_RS_DEPENDENCY;j->dependency=reason; }
static int perform(struct relay *r,const struct axd_rx_status_operation *op,struct axd_rx_status_reply *reply)
{
    if(r->io==NULL){fail(r->job,AXD_RS_BAD_ARGUMENT);return -1;}
    r->job->attempted_ops++;
    if(r->io(r->opaque,op,reply)!=0){fail(r->job,AXD_RS_IO_ERROR);return -1;}
    r->job->completed_ops++;return 0;
}
static int list_io(void *opaque,const struct axd_rx_status_list_operation *op,uint32_t *reply)
{
    struct relay *r=opaque;struct axd_rx_status_operation converted={0};
    struct axd_rx_status_reply out={0};
    r->job->context.list=r->job->list_job.context;
    converted.kind=AXD_RS_LIST;converted.list=*op;
    if(perform(r,&converted,&out)!=0)return -1;
    *reply=out.value;return 0;
}
static int offload_io(void *opaque,const struct axd_offload_status_operation *op,struct axd_offload_status_reply *reply)
{
    struct relay *r=opaque;struct axd_rx_status_operation converted={0};
    struct axd_rx_status_reply out={0};
    r->job->context.offload=r->job->offload_job.context;
    converted.kind=AXD_RS_OFFLOAD;converted.offload=*op;
    if(perform(r,&converted,&out)!=0)return -1;
    *reply=out.offload;return 0;
}
static int node(struct axd_rx_status *j,struct axd_rx_status_operation *op,
                enum axd_rx_status_kind kind,uint32_t label,uint32_t offset,uint32_t value)
{
    if(!j->context.nodes_owned){dependency(j,AXD_RS_NODE_OWNER);return 0;}
    if(label==0){fail(j,AXD_RS_BINDING);return 0;}
    op->kind=kind;op->identity=label;op->offset=offset;op->value=value;return 1;
}
static int pool(struct axd_rx_status *j,struct axd_rx_status_operation *op,
                unsigned write,uint32_t offset,uint32_t value)
{
    if(!j->context.pool_owned){dependency(j,AXD_RS_POOL_OWNER);return 0;}
    op->kind=write?AXD_RS_POOL_WRITE32:AXD_RS_POOL_READ32;
    op->identity=j->context.pool.value;op->offset=offset;op->value=value;return 1;
}
int axd_rx_status_begin(struct axd_rx_status *j,const struct axd_rx_status_output *output,
                         const struct axd_rx_status_context *context)
{
    static const struct axd_rx_status empty={0};struct axd_rx_status_output out;struct axd_rx_status_context c;
    if(j==NULL)return -1;
    if(output!=NULL){out=*output;output=&out;}if(context!=NULL){c=*context;context=&c;}
    *j=empty;fail(j,AXD_RS_BAD_ARGUMENT);
    if(output==NULL || context==NULL || c.nodes_owned>1 || c.data_owned>1 || c.pool_owned>1 || c.os_owned>1)return -1;
    j->context=c;j->output=out;
    if(c.work_limit==0 || c.work_limit>65535){fail(j,AXD_RS_BOUNDS);return -1;}
    j->phase=AXD_RS_RUNNING;j->error=AXD_RS_NO_ERROR;return 0;
}
enum axd_rx_status_phase axd_rx_status_step(struct axd_rx_status *j,axd_rx_status_io_fn io,void *opaque)
{
    struct relay r={j,io,opaque};struct axd_rx_status_operation op={0};
    unsigned emit=0,before;struct axd_rx_status_reply out={0};uint32_t reply;
    if(j==NULL)return AXD_RS_INVALID;
    if(j->phase!=AXD_RS_RUNNING)return j->phase;
    switch(j->stage){
    case ENTRY:
        if(axd_dma_rx_status_list_begin(&j->list_job,0,&j->context.list)!=0)fail(j,AXD_RS_CHILD_ERROR);
        else j->stage=RUN_LIST;
        break;
    case RUN_LIST:{
        enum axd_rx_status_list_phase p=axd_dma_rx_status_list_step(&j->list_job,list_io,&r);
        j->context.list=j->list_job.context;
        if(j->phase!=AXD_RS_RUNNING)break;
        if(p==AXD_RX_STATUS_LIST_DONE)j->stage=LIST_RETURN;
        else if(p==AXD_RX_STATUS_LIST_DEPENDENCY)dependency(j,AXD_RS_LIST_DEPENDENCY);
        else if(p==AXD_RX_STATUS_LIST_CANCELLED)j->phase=AXD_RS_CANCELLED;
        else if(p!=AXD_RX_STATUS_LIST_RUNNING)fail(j,AXD_RS_CHILD_ERROR);
        break;
    }
    case LIST_RETURN:
        j->head=j->current=j->list_job.return_value;j->stage=j->current!=0?NODE:RESULT_HEAD;break;
    case NODE:
        if(j->visited_nodes==j->context.work_limit){fail(j,AXD_RS_BOUNDS);break;}
        j->visited_nodes++;j->poll_remaining=200;j->stage=READ_DATA;break;
    case READ_DATA:emit=node(j,&op,AXD_RS_NODE_READ32,j->current,0,0);break;
    case READ_LENGTH:
        if(!j->context.data_owned)dependency(j,AXD_RS_DATA_OWNER);
        else if(j->data==0)fail(j,AXD_RS_BINDING);
        else{op.kind=AXD_RS_DATA_READ16;op.identity=j->data;emit=1;}
        break;
    case TEST_LENGTH:j->stage=j->length_sample!=0?ACCEPT_TAIL:DELAY;break;
    case DELAY:
        if(!j->context.os_owned)dependency(j,AXD_RS_OS_OWNER);
        else{op.kind=AXD_RS_DELAY_US;op.value=1;emit=1;}
        break;
    case DELAY_RETURN:j->stage=--j->poll_remaining==0?DROP_READ_NEXT:READ_LENGTH;break;
    case ACCEPT_TAIL:j->accepted_tail=j->current;j->stage=ACCEPT_COUNT;break;
    case ACCEPT_COUNT:j->output.count++;j->stage=READ_ACCEPT_NEXT;break;
    case READ_ACCEPT_NEXT:emit=node(j,&op,AXD_RS_NODE_READ32,j->current,0x1c,0);break;
    case SET_CURRENT:j->current=j->next;j->stage=j->current!=0?NODE:RESULT_HEAD;break;
    case DROP_READ_NEXT:emit=node(j,&op,AXD_RS_NODE_READ32,j->current,0x1c,0);break;
    case DROP_LINK:j->stage=j->accepted_tail!=0?DROP_WRITE_LINK:DROP_HEAD;break;
    case DROP_WRITE_LINK:emit=node(j,&op,AXD_RS_NODE_WRITE32,j->accepted_tail,0x1c,j->next);break;
    case DROP_HEAD:
        if(j->accepted_tail==0)j->head=j->next;
        j->byte_cursor=0;j->stage=DROP_CLEAR;break;
    case DROP_CLEAR:emit=node(j,&op,AXD_RS_NODE_WRITE8,j->current,0x1c+j->byte_cursor,0);break;
    case POOL_BIND:
        if(j->context.pool.known==0){dependency(j,AXD_RS_POOL_BINDING);break;}
        if(j->context.pool.known!=1){fail(j,AXD_RS_BAD_ARGUMENT);break;}
        if(j->context.pool.value==0){fail(j,AXD_RS_BINDING);break;}
        j->pool_node=j->current;j->stage=POOL_NODE;break;
    case POOL_NODE:
        if(j->pool_visits==j->context.work_limit){fail(j,AXD_RS_BOUNDS);break;}
        j->pool_visits++;j->stage=POOL_CALLBACK;break;
    case POOL_CALLBACK:emit=pool(j,&op,0,0x14,0);break;
    case CALLBACK_GATE:j->stage=j->callback!=0?POOL_INDEX:POOL_COUNT;break;
    case POOL_INDEX:emit=node(j,&op,AXD_RS_INDEX_READ16,j->pool_node,0x18,0);break;
    case INDEX_GATE:j->stage=j->pool_index!=UINT16_MAX?POOL_ARGUMENT:POOL_COUNT;break;
    case POOL_ARGUMENT:emit=pool(j,&op,0,0x10,0);break;
    case CALLBACK_BIND:
        if(j->context.offload_callback.known==0 || j->context.offload_argument.known==0){dependency(j,AXD_RS_REGISTERED_POOL_CALLBACK);break;}
        if(j->context.offload_callback.known!=1 || j->context.offload_argument.known!=1){fail(j,AXD_RS_BAD_ARGUMENT);break;}
        if(j->callback!=j->context.offload_callback.value || j->callback_argument==0 ||
           j->callback_argument!=j->context.offload_argument.value){dependency(j,AXD_RS_REGISTERED_POOL_CALLBACK);break;}
        /* The caller sign-extends the original 16-bit index. */
        if(axd_offload_status_begin(&j->offload_job,j->pool_index<0x8000?(int32_t)j->pool_index:(int32_t)j->pool_index-65536,
                                      &j->context.offload)!=0)fail(j,AXD_RS_CHILD_ERROR);
        else j->stage=RUN_OFFLOAD;
        break;
    case RUN_OFFLOAD:{
        enum axd_offload_status_phase p=axd_offload_status_step(&j->offload_job,offload_io,&r);
        j->context.offload=j->offload_job.context;
        if(j->phase!=AXD_RS_RUNNING)break;
        if(p==AXD_OS_TRACE_COMPLETE){j->byte_cursor=0;j->stage=CLEAR_INDEX;}
        else if(p==AXD_OS_DEPENDENCY)dependency(j,AXD_RS_OFFLOAD_PATH);
        else if(p==AXD_OS_CANCELLED)j->phase=AXD_RS_CANCELLED;
        else if(p!=AXD_OS_RUNNING)fail(j,AXD_RS_CHILD_ERROR);
        break;
    }
    case CLEAR_INDEX:emit=node(j,&op,AXD_RS_NODE_WRITE8,j->pool_node,0x18+j->byte_cursor,0xff);break;
    case POOL_COUNT:emit=pool(j,&op,0,4,0);break;
    case COUNT_INCREMENT:j->pool_count++;j->stage=POOL_COUNT_WRITE;break;
    case POOL_COUNT_WRITE:emit=pool(j,&op,1,4,j->pool_count);break;
    case POOL_NEXT:emit=node(j,&op,AXD_RS_NODE_READ32,j->pool_node,0x1c,0);break;
    case POOL_NEXT_GATE:
        if(j->data!=0){j->pool_node=j->data;j->stage=POOL_NODE;}else j->stage=POOL_HIGHWATER;
        break;
    case POOL_HIGHWATER:emit=pool(j,&op,0,0xc,0);break;
    case POOL_HEAD:emit=pool(j,&op,0,0,0);break;
    case HIGHWATER_SELECT:
        if((j->pool_count^0x80000000u)>(j->pool_highwater^0x80000000u))j->pool_highwater=j->pool_count;
        j->stage=HIGHWATER_WRITE;break;
    case HIGHWATER_WRITE:emit=pool(j,&op,1,0xc,j->pool_highwater);break;
    case RETURN_LINK:emit=node(j,&op,AXD_RS_NODE_WRITE32,j->pool_node,0x1c,j->pool_head);break;
    case RETURN_HEAD:emit=pool(j,&op,1,0,j->current);break;
    case DROP_NEXT:j->current=j->next;j->stage=DROP_COUNT;break;
    case DROP_COUNT:j->context.error_count++;j->returned_nodes++;j->stage=j->current!=0?NODE:RESULT_HEAD;break;
    case RESULT_HEAD:j->output.head=j->head;j->result_stores|=1u;j->stage=RESULT_TAIL;break;
    case RESULT_TAIL:j->output.tail=j->accepted_tail;j->result_stores|=2u;j->stage=FINISH;break;
    case FINISH:j->phase=AXD_RS_TRACE_COMPLETE;break;
    default:fail(j,AXD_RS_BAD_ARGUMENT);break;
    }
    if(!emit)return j->phase;
    before=j->stage;
    if(perform(&r,&op,&out)!=0)return j->phase;
    reply=out.value;
    switch(before){
    case READ_DATA:case POOL_NEXT:j->data=reply;break;
    case READ_LENGTH:j->length_sample=(uint16_t)reply;j->length_reads++;break;
    case DELAY:j->delays++;break;
    case READ_ACCEPT_NEXT:case DROP_READ_NEXT:j->next=reply;break;
    case POOL_CALLBACK:j->callback=reply;break;
    case POOL_INDEX:j->pool_index=(uint16_t)reply;break;
    case POOL_ARGUMENT:j->callback_argument=reply;break;
    case POOL_COUNT:j->pool_count=reply;break;
    case POOL_HIGHWATER:j->pool_highwater=reply;break;
    case POOL_HEAD:j->pool_head=reply;break;
    default:break;
    }
    if(before==DROP_CLEAR){if(++j->byte_cursor==4)j->stage=POOL_BIND;}
    else if(before==CLEAR_INDEX){if(++j->byte_cursor==2)j->stage=POOL_COUNT;}
    else j->stage=before+1;
    return j->phase;
}
enum axd_rx_status_phase axd_rx_status_cancel(struct axd_rx_status *j)
{
    if(j==NULL)return AXD_RS_INVALID;
    if(j->phase==AXD_RS_RUNNING){
        j->phase=AXD_RS_CANCELLED;
        if(j->stage==RUN_LIST)axd_dma_rx_status_list_cancel(&j->list_job);
        else if(j->stage==RUN_OFFLOAD)axd_offload_status_cancel(&j->offload_job);
    }
    return j->phase;
}
