/* SPDX-License-Identifier: MIT */
#include "dma_tx_post.h"
enum stage {
    ENTRY, FULL_AVAILABLE, FULL_COUNTER, FULL_RETURN, INPUT_BURST, INPUT_EOT,
    MEMORY_OWNER, WRITE_WORDS, READ_WORDS, PARITY_CHECK, PARITY_WRITE,
    STORE_OUT, CHANNEL_OWNER, CHANNEL_BARRIER, CHANNEL_WRITE, STORE_AVAILABLE, RETURN
};
static void fail(struct axd_dma_tx_post *j, enum axd_dma_tx_post_error error)
{ j->phase=AXD_DTP_FAILED;j->error=error; }
static unsigned odd(uint32_t value)
{
    value^=value>>16;value^=value>>8;value^=value>>4;value^=value>>2;value^=value>>1;
    return value&1u;
}
int axd_dma_tx_post_begin(struct axd_dma_tx_post *j,
    const struct axd_dma_tx_post_request *request,const struct axd_dma_tx_post_context *context)
{
    struct axd_dma_tx_post_request r;
    struct axd_dma_tx_post_context c;
    const struct axd_dma_tx_post empty={0};
    if(j==NULL)return -1;
    if(request!=NULL){r=*request;request=&r;}
    if(context!=NULL){c=*context;context=&c;}
    *j=empty;fail(j,AXD_DTP_BAD_ARGUMENT);
    if(request==NULL || context==NULL || c.descriptor_owned>1 || c.channel_owned>1)return -1;
    j->request=r;j->context=c;
    if(c.ring.count<2 || c.ring.count>32768 || (c.ring.count&(c.ring.count-1u))!=0 ||
       c.ring.in>=c.ring.count || c.ring.out>=c.ring.count){fail(j,AXD_DTP_BOUNDS);return -1;}
    j->slot=c.ring.out;j->next=(c.ring.out+1u)&(c.ring.count-1u);
    j->stage=ENTRY;j->phase=AXD_DTP_RUNNING;j->error=AXD_DTP_NO_ERROR;return 0;
}
enum axd_dma_tx_post_phase axd_dma_tx_post_step(struct axd_dma_tx_post *j,
    axd_dma_tx_post_io_fn io,void *context)
{
    static const uint32_t offsets[4]={8,12,0,4};
    struct axd_dma_tx_post_operation op={0};
    uint32_t reply=0,used,available;
    unsigned emit=0;
    if(j==NULL)return AXD_DTP_INVALID;
    if(j->phase!=AXD_DTP_RUNNING)return j->phase;
    switch(j->stage){
    case ENTRY:j->stage=j->next==j->context.ring.in ? FULL_AVAILABLE : INPUT_BURST;break;
    case FULL_AVAILABLE:j->context.available=0;j->availability_committed=1;j->stage=FULL_COUNTER;break;
    case FULL_COUNTER:j->context.no_descriptor_count++;j->stage=FULL_RETURN;break;
    case FULL_RETURN:j->return_value=UINT32_MAX;j->return_valid=1;j->phase=AXD_DTP_DONE;break;
    case INPUT_BURST:
        if(j->context.burstsize_control)j->request.descriptor.control1|=AXD_DMA64_NOT_PCIE;
        j->stage=INPUT_EOT;break;
    case INPUT_EOT:
        if(j->slot==j->context.ring.count-1u)j->request.descriptor.control1|=AXD_DMA64_EOT;
        j->stage=MEMORY_OWNER;break;
    case MEMORY_OWNER:
        if(!j->context.descriptor_owned)j->phase=AXD_DTP_DEPENDENCY;
        else if(!j->context.descriptor_identity)fail(j,AXD_DTP_BINDING);
        else if(j->context.descriptor_bytes<16 || j->slot>(j->context.descriptor_bytes-16u)/16u)
            fail(j,AXD_DTP_BOUNDS);
        else j->stage=WRITE_WORDS;
        break;
    case WRITE_WORDS:
        op.kind=AXD_DTP_MEMORY_WRITE32;op.resource=AXD_DTP_DESCRIPTOR;
        op.identity=j->context.descriptor_identity;op.offset=j->slot*16u+offsets[j->cursor];
        switch(j->cursor){
        case 0:op.value=j->request.descriptor.address_low;break;
        case 1:op.value=j->request.descriptor.address_high;break;
        case 2:op.value=j->request.descriptor.control1;break;
        default:op.value=j->request.descriptor.control2;break;
        }
        emit=1;break;
    case READ_WORDS:
        op.kind=AXD_DTP_MEMORY_READ32;op.resource=AXD_DTP_DESCRIPTOR;
        op.identity=j->context.descriptor_identity;op.offset=j->slot*16u+offsets[j->cursor];emit=1;break;
    case PARITY_CHECK:j->stage=odd(j->parity_sample) ? PARITY_WRITE : STORE_OUT;break;
    case PARITY_WRITE:
        op.kind=AXD_DTP_MEMORY_WRITE32;op.resource=AXD_DTP_DESCRIPTOR;
        op.identity=j->context.descriptor_identity;op.offset=j->slot*16u+4u;
        op.value=j->request.descriptor.control2|AXD_DMA64_PARITY;emit=1;break;
    case STORE_OUT:
        j->context.ring.out=j->next;j->cursor_committed=1;
        j->stage=j->request.commit ? CHANNEL_OWNER : STORE_AVAILABLE;break;
    case CHANNEL_OWNER:
        if(!j->context.channel_owned)j->phase=AXD_DTP_DEPENDENCY;
        else if(!j->context.channel_identity)fail(j,AXD_DTP_BINDING);
        else if(axd_dma64_producer(&j->context.ring,&j->producer)!=AXD_DMA64_OK)fail(j,AXD_DTP_BOUNDS);
        else j->stage=CHANNEL_BARRIER;
        break;
    case CHANNEL_BARRIER:
        op.kind=AXD_DTP_STORE_BARRIER;op.resource=AXD_DTP_CHANNEL;op.identity=j->context.channel_identity;emit=1;break;
    case CHANNEL_WRITE:
        op.kind=AXD_DTP_CHANNEL_WRITE32;op.resource=AXD_DTP_CHANNEL;
        op.identity=j->context.channel_identity;op.offset=4;op.value=j->producer;emit=1;break;
    case STORE_AVAILABLE:
        /* The leaf's index arithmetic does not inspect the pointer mask here. */
        used=(j->context.ring.out-j->context.ring.in)&(j->context.ring.count-1u);
        available=j->context.ring.count-used-1u;
        j->context.available=available;j->availability_committed=1;j->stage=RETURN;break;
    case RETURN:j->return_value=0;j->return_valid=1;j->phase=AXD_DTP_DONE;break;
    default:fail(j,AXD_DTP_BAD_ARGUMENT);break;
    }
    if(emit){
        unsigned stage=j->stage;
        if(io==NULL){fail(j,AXD_DTP_BAD_ARGUMENT);return j->phase;}
        j->attempted_ops++;
        if(io(context,&op,&reply)!=0){fail(j,AXD_DTP_IO_ERROR);return j->phase;}
        j->completed_ops++;
        if(stage==WRITE_WORDS){
            j->descriptor_writes++;j->cursor++;
            if(j->cursor==4){j->cursor=0;j->stage=(j->context.control_flags&1u) ? READ_WORDS : STORE_OUT;}
        }else if(stage==READ_WORDS){
            j->parity_reads++;j->last_read=reply;j->parity_sample^=reply;j->cursor++;
            if(j->cursor==4)j->stage=PARITY_CHECK;
        }else if(stage==PARITY_WRITE){j->descriptor_writes++;j->stage=STORE_OUT;}
        else if(stage==CHANNEL_BARRIER)j->stage=CHANNEL_WRITE;
        else {j->producer_written=1;j->stage=STORE_AVAILABLE;}
    }
    return j->phase;
}
enum axd_dma_tx_post_phase axd_dma_tx_post_cancel(struct axd_dma_tx_post *j)
{
    if(j==NULL)return AXD_DTP_INVALID;
    if(j->phase==AXD_DTP_RUNNING)j->phase=AXD_DTP_CANCELLED;
    return j->phase;
}
