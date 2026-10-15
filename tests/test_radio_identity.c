/* SPDX-License-Identifier: MIT */
#include "../driver/radio_identity.h"
#include <assert.h>
#include <stdio.h>
struct event {enum axd_d11_kind kind;uint32_t offset,value,reply;};
static const struct event expected[]={
    {AXD_D11_STORE_BARRIER,0,0,0},{AXD_D11_WRITE16,0x3d8,0,0},
    {AXD_D11_READ16,0x3da,0,0xffffabcd},{AXD_D11_FULL_BARRIER,0,0,0},
    {AXD_D11_STORE_BARRIER,0,0,0},{AXD_D11_WRITE16,0x3d8,1,0},
    {AXD_D11_READ16,0x3da,0,0x12345678},{AXD_D11_FULL_BARRIER,0,0,0}};
struct fixture {unsigned cursor,fail,cancel;struct axd_radio_identity *job;};
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;assert(f->cursor<8);e=&expected[f->cursor++];
    assert(op->kind==e->kind && op->offset==e->offset && op->value==e->value);*reply=e->reply;
    assert(f->job->context.radio_id==0x1111 && f->job->context.radio_revision==0x22 && f->job->context.radio_version==0x33);
    if(f->cursor==f->cancel)axd_radio_identity_cancel(f->job);
    return f->cursor==f->fail?-1:0;
}
static struct axd_radio_identity_context context(void)
{
    struct axd_radio_identity_context c={0};c.core=130;c.minor=1;
    c.phy_pointer=(struct axd_wake_u32){0x1000,1};c.public_pointer=(struct axd_wake_u32){0x2000,1};
    c.callback_context=(struct axd_wake_u32){0x3000,1};c.callback_pi=c.phy_pointer;
    c.radio_id=0x1111;c.radio_revision=0x22;c.radio_version=0x33;return c;
}
static void run(struct axd_radio_identity *j,struct fixture *f)
{
    unsigned steps=0;f->job=j;
    while(j->phase==AXD_RID_RUNNING){unsigned old=f->cursor;axd_radio_identity_step(j,io,f);assert(f->cursor-old<=1);assert(++steps<50);}
    {unsigned old=f->cursor;assert(axd_radio_identity_step(j,io,f)==j->phase);assert(f->cursor==old);}
}
static void queries(void)
{
    struct axd_radio_identity j;struct axd_radio_identity_context c=context();unsigned command;
    for(command=AXD_RID_QUERY_DIRECT;command<=AXD_RID_QUERY_PARSE_AC;command++){
        struct fixture f={0};assert(!axd_radio_identity_begin(&j,(enum axd_radio_identity_command)command,0,&c));run(&j,&f);
        assert(j.phase==AXD_RID_TRACE_COMPLETE && f.cursor==8 && j.result_valid && j.result_code==0xabcd5678);
        assert(j.samples_observed==3 && j.barriers_completed==3);
        if(command==AXD_RID_QUERY_PARSE_AC)assert(j.context.radio_id==0x5678 && j.context.radio_revision==0xcd && j.context.radio_version==0 && j.decoded_stores==7);
        else assert(j.decoded_stores==0 && j.context.radio_id==0x1111);
    }
    c.public_pointer.known=0;{struct fixture f={0};assert(!axd_radio_identity_begin(&j,AXD_RID_QUERY_DIRECT,0,&c));run(&j,&f);assert(j.phase==AXD_RID_TRACE_COMPLETE);}
    {struct fixture f={0};assert(!axd_radio_identity_begin(&j,AXD_RID_QUERY_PARSE_AC,0,&c));run(&j,&f);assert(j.phase==AXD_RID_DEPENDENCY && f.cursor==8 && j.result_valid && j.decoded_stores==0);}
}
static void parsers(void)
{
    struct axd_radio_identity j;struct axd_radio_identity_context c=context();struct fixture f={0};
    assert(!axd_radio_identity_begin(&j,AXD_RID_PARSE_AC,0xabcd5678,&c));run(&j,&f);assert(f.cursor==0 && j.context.radio_id==0x5678 && j.context.radio_revision==0xcd && j.context.radio_version==0);
    assert(!axd_radio_identity_begin(&j,AXD_RID_PARSE_COMMON,0xabcd5678,&c));run(&j,&f);assert(f.cursor==0 && j.context.radio_id==0xbcd5 && j.context.radio_revision==0xa && j.context.radio_version==8);
    assert(!axd_radio_identity_begin(&j,AXD_RID_PARSE_AC,0xabcd5678,&c));
    while(j.decoded_stores==0)axd_radio_identity_step(&j,NULL,NULL);
    assert(j.context.radio_id==0x5678 && j.context.radio_revision==0x22 && j.context.radio_version==0x33);
    axd_radio_identity_cancel(&j);assert(axd_radio_identity_step(&j,NULL,NULL)==AXD_RID_CANCELLED && j.decoded_stores==1);
    assert(!axd_radio_identity_begin(&j,AXD_RID_PARSE_COMMON,0xabcd5678,&j.context));assert(j.context.radio_id==0x5678);
}
static void faults(void)
{
    struct axd_radio_identity j;struct axd_radio_identity_context c=context();unsigned mode,i;
    for(mode=0;mode<3;mode++)for(i=1;i<=8;i++){
        struct fixture f={0};if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_radio_identity_begin(&j,AXD_RID_QUERY_PARSE_AC,0,&c));run(&j,&f);
        assert(j.phase==(mode==1?AXD_RID_CANCELLED:AXD_RID_FAILED) && f.cursor==i && j.decoded_stores==0 && !j.result_valid);
        if(mode!=1)assert(j.error==AXD_RID_IO_ERROR);
        if(mode==1 && i>=3)assert(j.high_sample==0xabcd && (j.samples_observed&1));
        if(mode==1 && i>=7)assert(j.low_sample==0x5678 && j.samples_observed==3);
        if(mode==1 && i==8)assert(j.barriers_completed==3);
    }
}
static void bindings(void)
{
    struct axd_radio_identity j;struct axd_radio_identity_context c=context();struct fixture f={0};
    c.callback_context.known=0;assert(!axd_radio_identity_begin(&j,AXD_RID_QUERY_REGISTERED,0,&c));run(&j,&f);assert(j.phase==AXD_RID_DEPENDENCY && j.last_field==AXD_RID_CALLBACK_CONTEXT && f.cursor==0);
    c=context();c.callback_pi.value++;assert(!axd_radio_identity_begin(&j,AXD_RID_QUERY_REGISTERED,0,&c));run(&j,&f);assert(j.phase==AXD_RID_FAILED && j.error==AXD_RID_BINDING && f.cursor==0);
    c=context();c.phy_pointer.value=0;assert(!axd_radio_identity_begin(&j,AXD_RID_QUERY_DIRECT,0,&c));run(&j,&f);assert(j.phase==AXD_RID_FAILED && j.error==AXD_RID_BINDING);
    c=context();c.public_pointer.value=0;assert(!axd_radio_identity_begin(&j,AXD_RID_PARSE_AC,0,&c));run(&j,&f);assert(j.phase==AXD_RID_FAILED && j.error==AXD_RID_BINDING && !j.decoded_stores);
    c=context();c.core=129;assert(axd_radio_identity_begin(&j,AXD_RID_QUERY_DIRECT,0,&c)!=0 && j.error==AXD_RID_UNSUPPORTED);
}
int main(void){queries();parsers();faults();bindings();puts("Radio identity: exact high-first samples, distinct AC/common decode, bindings and callback exits passed");return 0;}
