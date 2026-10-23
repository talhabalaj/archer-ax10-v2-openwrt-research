/* SPDX-License-Identifier: MIT */
#include "bmac_rx_fill.h"
enum stage {HARDWARE,WLC,PUBLIC,PUBLIC_READ,SELECT,DI_BIND,CHILD_BIND,CHILD_BEGIN,RUN_CHILD,RETURN};
struct relay {struct axd_bmac_rx_fill *j;axd_bmac_rx_fill_io_fn io;void *opaque;};
static void fail(struct axd_bmac_rx_fill *j,enum axd_bmac_rx_fill_error e)
{j->phase=AXD_BRF_FAILED;j->error=e;}
static void dep(struct axd_bmac_rx_fill *j,enum axd_bmac_rx_fill_dependency d)
{j->phase=AXD_BRF_DEPENDENCY;j->dependency=d;}
static int fact(struct axd_bmac_rx_fill *j,enum axd_bmac_rx_fill_field field,struct axd_wake_u32 v)
{
    j->last_field=field;j->metadata_accesses++;
    if(!v.known)dep(j,AXD_BRF_METADATA);else if(v.known!=1)fail(j,AXD_BRF_BAD_ARGUMENT);
    else if(!v.value)fail(j,AXD_BRF_BINDING);else return 1;return 0;
}
static int perform(struct relay *r,const struct axd_bmac_rx_fill_operation *op,uint32_t *reply)
{
    if(!r->io){fail(r->j,AXD_BRF_BAD_ARGUMENT);return -1;}r->j->attempted_ops++;
    if(r->io(r->opaque,op,reply)){fail(r->j,AXD_BRF_IO_ERROR);return -1;}
    r->j->completed_ops++;return 0;
}
static int normal_io(void *opaque,const struct axd_rx_fill_operation *op,uint32_t *reply)
{
    struct relay *r=opaque;struct axd_bmac_rx_fill_operation converted={0};
    r->j->context.normal=r->j->normal_job.context;converted.kind=AXD_BRF_NORMAL;converted.normal=*op;return perform(r,&converted,reply);
}
static int status_io(void *opaque,const struct axd_rx_status_fill_operation *op,uint32_t *reply)
{
    struct relay *r=opaque;struct axd_bmac_rx_fill_operation converted={0};
    r->j->context.status=r->j->status_job.context;converted.kind=AXD_BRF_STATUS;converted.status=*op;return perform(r,&converted,reply);
}
int axd_bmac_rx_fill_begin(struct axd_bmac_rx_fill *j,uint32_t fifo,const struct axd_bmac_rx_fill_context *context)
{
    struct axd_bmac_rx_fill_context c;const struct axd_bmac_rx_fill empty={0};
    if(j==NULL)return -1;if(context!=NULL){c=*context;context=&c;}*j=empty;fail(j,AXD_BRF_BAD_ARGUMENT);
    if(context==NULL || c.public_owned>1)return -1;j->context=c;j->fifo=fifo;j->phase=AXD_BRF_RUNNING;j->error=AXD_BRF_NO_ERROR;return 0;
}
enum axd_bmac_rx_fill_phase axd_bmac_rx_fill_step(struct axd_bmac_rx_fill *j,axd_bmac_rx_fill_io_fn io,void *opaque)
{
    struct relay r={j,io,opaque};struct axd_bmac_rx_fill_operation op={0};uint32_t reply=0;
    if(j==NULL)return AXD_BRF_INVALID;if(j->phase!=AXD_BRF_RUNNING)return j->phase;
    switch(j->stage){
    case HARDWARE:if(fact(j,AXD_BRF_HARDWARE,j->context.hardware))j->stage=WLC;break;
    case WLC:if(fact(j,AXD_BRF_WLC,j->context.wlc))j->stage=PUBLIC;break;
    case PUBLIC:if(fact(j,AXD_BRF_PUBLIC,j->context.pub))j->stage=PUBLIC_READ;break;
    case PUBLIC_READ:
        if(!j->context.public_owned){dep(j,AXD_BRF_PUBLIC_OWNER);break;}
        op.kind=AXD_BRF_PUBLIC_READ8;op.identity=j->context.pub.value;op.offset=0x1c0;
        if(!perform(&r,&op,&reply)){j->eligibility_sample=(uint8_t)reply;j->stage=SELECT;}break;
    case SELECT:
        j->selected_status=j->fifo==3 && (j->eligibility_sample&1u);
        if(j->fifo>=AXD_BMAC_RX_FILL_MAX_FIFOS)fail(j,AXD_BRF_BOUNDS);else j->stage=DI_BIND;break;
    case DI_BIND:
        if(j->context.selected_fifo!=j->fifo){fail(j,AXD_BRF_BINDING);break;}
        if(fact(j,AXD_BRF_SELECTED_DI,j->context.selected_di))j->stage=CHILD_BIND;break;
    case CHILD_BIND:{
        unsigned known=j->selected_status?j->context.status_known:j->context.normal_known;
        struct axd_wake_u32 binding=j->selected_status?j->context.status_binding:j->context.normal_binding;
        if(!known){dep(j,j->selected_status?AXD_BRF_STATUS_CONTEXT:AXD_BRF_NORMAL_CONTEXT);break;}
        if(known!=1){fail(j,AXD_BRF_BAD_ARGUMENT);break;}
        if(!fact(j,j->selected_status?AXD_BRF_STATUS_BINDING:AXD_BRF_NORMAL_BINDING,binding))break;
        if(binding.value!=j->context.selected_di.value){fail(j,AXD_BRF_BINDING);break;}
        j->stage=CHILD_BEGIN;break;
    }
    case CHILD_BEGIN:
        if(j->selected_status?axd_dma_rx_status_fill_begin(&j->status_job,&j->context.status):axd_dma_rx_fill_begin(&j->normal_job,&j->context.normal))fail(j,AXD_BRF_CHILD_ERROR);
        else j->stage=RUN_CHILD;break;
    case RUN_CHILD:
        if(j->selected_status){
            enum axd_rx_status_fill_phase p=axd_dma_rx_status_fill_step(&j->status_job,status_io,&r);
            j->context.status=j->status_job.context;if(j->phase!=AXD_BRF_RUNNING)break;
            if(p==AXD_RSF_TRACE_COMPLETE){j->child_return_value=j->status_job.return_value;j->child_return_valid=j->status_job.return_valid;j->stage=RETURN;}
            else if(p==AXD_RSF_DEPENDENCY)dep(j,AXD_BRF_STATUS_CHILD);
            else if(p==AXD_RSF_CANCELLED)j->phase=AXD_BRF_CANCELLED;
            else if(p!=AXD_RSF_RUNNING)fail(j,AXD_BRF_CHILD_ERROR);
        }else{
            enum axd_rx_fill_phase p=axd_dma_rx_fill_step(&j->normal_job,normal_io,&r);
            j->context.normal=j->normal_job.context;if(j->phase!=AXD_BRF_RUNNING)break;
            if(p==AXD_RXF_TRACE_COMPLETE){j->child_return_value=j->normal_job.return_value;j->child_return_valid=j->normal_job.return_valid;j->stage=RETURN;}
            else if(p==AXD_RXF_DEPENDENCY)dep(j,AXD_BRF_NORMAL_CHILD);
            else if(p==AXD_RXF_CANCELLED)j->phase=AXD_BRF_CANCELLED;
            else if(p!=AXD_RXF_RUNNING)fail(j,AXD_BRF_CHILD_ERROR);
        }break;
    case RETURN:j->phase=AXD_BRF_TRACE_COMPLETE;break;
    default:fail(j,AXD_BRF_BAD_ARGUMENT);break;
    }
    return j->phase;
}
enum axd_bmac_rx_fill_phase axd_bmac_rx_fill_cancel(struct axd_bmac_rx_fill *j)
{
    if(j==NULL)return AXD_BRF_INVALID;
    if(j->phase==AXD_BRF_RUNNING){j->phase=AXD_BRF_CANCELLED;if(j->stage==RUN_CHILD){if(j->selected_status)axd_dma_rx_status_fill_cancel(&j->status_job);else axd_dma_rx_fill_cancel(&j->normal_job);}}
    return j->phase;
}
