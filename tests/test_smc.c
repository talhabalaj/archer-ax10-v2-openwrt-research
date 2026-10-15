/* SPDX-License-Identifier: MIT */
#include "../driver/smc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct event {enum axd_d11_kind kind;uint32_t offset,value,reply;};
struct fixture {struct event events[128];unsigned n,cursor,fail,cancel,expect_ac,check_bookkeeping;struct axd_smc *job;};
static void add(struct fixture *f,enum axd_d11_kind kind,uint32_t offset,uint32_t value,uint32_t reply)
{struct event e={kind,offset,value,reply};assert(f->n<128);f->events[f->n++]=e;}
static void st(struct fixture *f){add(f,AXD_D11_STORE_BARRIER,0,0,0);}
static void sy(struct fixture *f){add(f,AXD_D11_FULL_BARRIER,0,0,0);}
static void packed(struct fixture *f,uint16_t selector,uint16_t value)
{st(f);add(f,AXD_D11_WRITE32,0x3fc,selector|((uint32_t)value<<16),0);}
static void direct(struct fixture *f,uint32_t offset,uint16_t value)
{st(f);add(f,AXD_D11_WRITE16,offset,value,0);}
static void modify(struct fixture *f,uint16_t selector,uint16_t mask,uint32_t read)
{
    direct(f,0x3fc,selector);st(f);add(f,AXD_D11_READ16,0x3fe,0,read);sy(f);
    add(f,AXD_D11_WRITE16,0x3fe,((uint16_t)read&~(uint32_t)mask)|mask,0);
}
static void reset1(struct fixture *f)
{modify(f,0x1409,0x8000,0x10203);modify(f,0x1410,2,0x8001);modify(f,0x140b,1,0);modify(f,0x140b,2,0xa501);}
static void prefix(struct fixture *f,int flush)
{
    if(flush){add(f,AXD_D11_READ16,0x3e0,0,0xdead);sy(f);}
    packed(f,0x140b,0);add(f,AXD_D11_READ16,0x7c0,0,0x11234);sy(f);direct(f,0x7c0,0x12b4);direct(f,0x7c0,0x1234);
}
static void table(struct fixture *f)
{
    add(f,AXD_D11_READ32,0x120,0,0);sy(f);
    direct(f,0x3fc,0x19e);add(f,AXD_D11_READ16,0x3fe,0,2);sy(f);
    packed(f,0xd,47);packed(f,0xe,0);
    packed(f,0x11,0x2211);direct(f,0x3fe,0x4433);direct(f,0x3fe,0x6655);
    packed(f,0x11,0x8877);direct(f,0x3fe,0xaa99);direct(f,0x3fe,0xccbb);
}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;assert(f->cursor<f->n);e=&f->events[f->cursor++];
    assert(op->kind==e->kind && op->offset==e->offset && op->value==e->value);*reply=e->reply;
    assert(f->job->context.loaded==2 || f->job->context.loaded==0);
    if(f->check_bookkeeping)assert(f->job->context.hardware.phy.bookkeeping==0);
    if(f->expect_ac && f->cursor>f->n-24)assert(f->job->context.ac_downloaded==1);
    if(f->cursor==f->cancel)axd_smc_cancel(f->job);
    return f->cursor==f->fail?-1:0;
}
static struct axd_smc_context context(void)
{
    struct axd_smc_context c={0};struct axd_wake_u32 p={.value=0x1000,.known=1};
    c.hardware.phy.core=130;c.hardware.phy.minor=1;c.hardware.phy.phy_type=0xb;c.hardware.phy.phy_revision=51;c.hardware.phy.phy_core_count=2;c.hardware.phy.bookkeeping=42;
    c.hardware.mac.core=130;c.hardware.mac.minor=1;c.hardware.mac_context_valid=1;
    c.band=p;p.value+=0x100;c.phy_pointer=p;p.value+=0x100;c.smc_info=p;p.value+=0x100;c.dispatch=p;
    p.value+=0x100;c.callback_context=p;c.callback_pi=c.phy_pointer;p.value+=0x100;c.reset_callback=p;p.value+=0x100;c.download_callback=p;
    return c;
}
static void run(struct axd_smc *j,struct fixture *f)
{
    unsigned steps=0;f->job=j;
    while(j->phase==AXD_SMC_RUNNING){unsigned old=f->cursor;axd_smc_step(j,io,f);assert(f->cursor-old<=1);assert(++steps<2000);}
    {unsigned old=f->cursor;assert(axd_smc_step(j,io,f)==j->phase);assert(f->cursor==old);}
}
static const uint8_t image_data[14]={0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee};
static void success_paths(void)
{
    struct axd_smc j;struct axd_smc_context c=context();struct axd_smc_image image={image_data,sizeof(image_data),14};struct fixture f={0};
    prefix(&f,0);table(&f);reset1(&f);f.expect_ac=1;
    assert(!axd_smc_begin(&j,AXD_SMC_LOAD,&image,&c));run(&j,&f);
    assert(j.phase==AXD_SMC_TRACE_COMPLETE && f.cursor==f.n && j.image_rows==2);
    assert(j.context.loaded==1 && j.context.ac_downloaded==1 && j.context.hardware.phy.bookkeeping==0);
    assert(j.reset0_completed==1 && j.reset1_completed==1 && j.download_completed && j.loaded_committed);
    c.loaded=2;c.download_callback.known=0;f=(struct fixture){0};reset1(&f);
    assert(!axd_smc_begin(&j,AXD_SMC_LOAD,NULL,&c));run(&j,&f);
    assert(j.phase==AXD_SMC_TRACE_COMPLETE && f.cursor==24 && j.context.loaded==1);
    assert(j.reset0_completed==0 && j.download_completed==0 && j.context.ac_downloaded==0);
    c=context();f=(struct fixture){0};packed(&f,0x140b,0);
    assert(!axd_smc_begin(&j,AXD_SMC_RESET0,NULL,&c));run(&j,&f);
    assert(j.phase==AXD_SMC_TRACE_COMPLETE && j.context.loaded==0 && j.context.hardware.phy.bookkeeping==42);
    c.hardware.shared_bus_word=1;c.hardware.write_threshold=43;f=(struct fixture){0};f.check_bookkeeping=1;add(&f,AXD_D11_READ16,0x3e0,0,0xdead);sy(&f);packed(&f,0x140b,0);
    assert(!axd_smc_begin(&j,AXD_SMC_RESET0,NULL,&c));run(&j,&f);
    assert(j.phase==AXD_SMC_TRACE_COMPLETE && j.context.hardware.phy.bookkeeping==0);
    assert(!axd_smc_begin(&j,AXD_SMC_RESET0,&j.image,&j.context));assert(j.context.hardware.write_threshold==43);
}
static void faults(void)
{
    struct axd_smc j;struct axd_smc_context c=context();struct axd_smc_image image={image_data,sizeof(image_data),14};struct fixture original={0};unsigned mode,i;
    prefix(&original,0);table(&original);reset1(&original);original.expect_ac=1;
    for(mode=0;mode<3;mode++)for(i=1;i<=original.n;i++) {
        struct fixture f=original;if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_smc_begin(&j,AXD_SMC_LOAD,&image,&c));run(&j,&f);
        assert(f.cursor==i && j.phase==(mode==1?AXD_SMC_CANCELLED:AXD_SMC_FAILED));
        assert(j.context.loaded==0 && !j.loaded_committed);
        if(i>original.n-24)assert(j.context.ac_downloaded==1);
        else assert(j.context.ac_downloaded==0);
        if(mode!=1)assert(j.error==AXD_SMC_IO_ERROR);
    }
}
static void loaded_and_flush_faults(void)
{
    struct axd_smc j;struct axd_smc_context c=context();unsigned mode,i;
    for(mode=0;mode<3;mode++)for(i=1;i<=24;i++) {
        struct fixture f={0};reset1(&f);c.loaded=2;
        if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_smc_begin(&j,AXD_SMC_LOAD,NULL,&c));run(&j,&f);
        assert(f.cursor==i && j.phase==(mode==1?AXD_SMC_CANCELLED:AXD_SMC_FAILED));
        assert(j.context.loaded==2 && !j.loaded_committed);
    }
    c=context();c.hardware.shared_bus_word=1;c.hardware.write_threshold=43;
    for(mode=0;mode<3;mode++)for(i=1;i<=4;i++) {
        struct fixture f={0};f.check_bookkeeping=1;
        add(&f,AXD_D11_READ16,0x3e0,0,0xdead);sy(&f);packed(&f,0x140b,0);
        if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_smc_begin(&j,AXD_SMC_RESET0,NULL,&c));run(&j,&f);
        assert(f.cursor==i && j.phase==(mode==1?AXD_SMC_CANCELLED:AXD_SMC_FAILED));
        assert(j.context.hardware.phy.bookkeeping==0);
    }
}
static void dependencies(void)
{
    struct axd_smc j;struct axd_smc_context c=context();struct fixture f={0};struct axd_smc_image image={image_data,11,14};
    c.band.known=0;assert(!axd_smc_begin(&j,AXD_SMC_LOAD,NULL,&c));run(&j,&f);assert(j.phase==AXD_SMC_DEPENDENCY && j.last_field==AXD_SMC_BAND && f.cursor==0);
    c=context();c.callback_pi.value++;assert(!axd_smc_begin(&j,AXD_SMC_LOAD,NULL,&c));run(&j,&f);assert(j.phase==AXD_SMC_FAILED && j.error==AXD_SMC_BINDING && f.cursor==0);
    c=context();c.reset_callback.value=0;assert(!axd_smc_begin(&j,AXD_SMC_RESET1,NULL,&c));run(&j,&f);assert(j.phase==AXD_SMC_FAILED && j.error==AXD_SMC_BINDING && j.last_field==AXD_SMC_RESET_CALLBACK);
    c=context();f=(struct fixture){0};prefix(&f,0);assert(!axd_smc_begin(&j,AXD_SMC_LOAD,NULL,&c));run(&j,&f);assert(j.phase==AXD_SMC_DEPENDENCY && j.last_field==AXD_SMC_IMAGE && f.cursor==f.n);
    f=(struct fixture){0};prefix(&f,0);assert(!axd_smc_begin(&j,AXD_SMC_LOAD,&image,&c));run(&j,&f);assert(j.phase==AXD_SMC_FAILED && j.error==AXD_SMC_BOUNDS && f.cursor==f.n);
    f=(struct fixture){0};prefix(&f,0);c.download_callback.known=0;assert(!axd_smc_begin(&j,AXD_SMC_LOAD,&image,&c));run(&j,&f);assert(j.phase==AXD_SMC_DEPENDENCY && j.last_field==AXD_SMC_DOWNLOAD_CALLBACK && f.cursor==f.n);
}
int main(void){assert(!axd_smc_hw_supported(127));assert(axd_smc_hw_supported(128));assert(axd_smc_hw_supported(130));success_paths();faults();loaded_and_flush_faults();dependencies();puts("SMC: exact reset/load/table48 ordering, strict reads, deferred bindings, aliases and all callback exits passed");return 0;}
