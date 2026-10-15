/* SPDX-License-Identifier: MIT */
#include "../driver/radio_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct event {enum axd_d11_kind kind;uint32_t offset,value,reply;};
struct fixture {struct event events[256];unsigned n,cursor,fail,cancel;struct axd_radio_control *job;int check_counter;uint16_t expected_counter;};
static void add(struct fixture *f,enum axd_d11_kind kind,uint32_t offset,uint32_t value,uint32_t reply)
{assert(f->n<256);f->events[f->n++]=(struct event){kind,offset,value,reply};}
static void st(struct fixture *f){add(f,AXD_D11_STORE_BARRIER,0,0,0);}
static void sy(struct fixture *f){add(f,AXD_D11_FULL_BARRIER,0,0,0);}
static void packed(struct fixture *f,uint16_t selector,uint16_t value)
{st(f);add(f,AXD_D11_WRITE32,0x3fc,selector|((uint32_t)value<<16),0);}
static void rw(struct fixture *f,uint16_t selector,uint16_t value)
{st(f);add(f,AXD_D11_WRITE16,0x3d8,selector,0);st(f);add(f,AXD_D11_WRITE16,0x3da,value,0);}
static void rr(struct fixture *f,uint16_t selector,uint16_t read_offset,uint32_t sample)
{st(f);add(f,AXD_D11_WRITE16,0x3d8,selector|read_offset,0);add(f,AXD_D11_READ16,0x3da,0,sample);sy(f);}
static void rm(struct fixture *f,uint16_t selector,uint16_t mask,uint16_t value,uint32_t sample)
{rr(f,selector,0x8000,sample);rw(f,selector,(uint16_t)(((uint16_t)sample&~mask)|(value&mask)));}
static void phy_modify(struct fixture *f)
{st(f);add(f,AXD_D11_WRITE16,0x3fc,0x408,0);st(f);add(f,AXD_D11_READ16,0x3fe,0,0x1a55b);sy(f);add(f,AXD_D11_WRITE16,0x3fe,0xa559,0);}
static void off_prefix(struct fixture *f)
{
    packed(f,0x473e,0);packed(f,0x4739,0);packed(f,0x473a,0);packed(f,0x4725,0x1fff);
    packed(f,0x4729,0);packed(f,0x4721,0xffff);packed(f,0x4728,0);packed(f,0x4720,0x3ff);phy_modify(f);
}
static void off_tail(struct fixture *f){packed(f,0x417,0);packed(f,0x416,1);}
static void on_prefix(struct fixture *f)
{
    packed(f,0x4728,0);packed(f,0x4720,0x3ff);packed(f,0x415,0);packed(f,0x40e,0);
    packed(f,0x40c,0x2000);packed(f,0x408,0);packed(f,0x417,0);packed(f,0x416,0xd);packed(f,0x408,7);
    add(f,AXD_D11_DELAY_US,0,1000,0);packed(f,0x408,6);
}
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;struct event e;assert(f->cursor<f->n);e=f->events[f->cursor++];
    assert(op->kind==e.kind && op->offset==e.offset && op->value==e.value);*reply=e.reply;
    if(f->check_counter)assert(f->job->context.hardware.phy.bookkeeping==f->expected_counter);
    if(f->cursor==f->cancel)axd_radio_control_cancel(f->job);
    return f->cursor==f->fail?-1:0;
}
static struct axd_radio_control_context context(void)
{
    struct axd_radio_control_context c={0};
    c.hardware.phy=(struct axd_phy_context){.core=130,.minor=1,.phy_type=0xb,.phy_revision=51,.phy_core_count=2,.bookkeeping=42};
    c.phy_pointer=(struct axd_wake_u32){0x1000,1};c.public_pointer=(struct axd_wake_u32){0x2000,1};c.shared_pointer=(struct axd_wake_u32){0x3000,1};
    c.radio_info=(struct axd_wake_u32){0x4000,1};c.dispatch=(struct axd_wake_u32){0x5000,1};
    c.callback_context=(struct axd_wake_u32){0x6000,1};c.callback_pi=c.phy_pointer;
    c.ac_pointer=(struct axd_wake_u32){0x7000,1};c.chanmgr_pointer=(struct axd_wake_u32){0x8000,1};c.chanmgr_data_pointer=(struct axd_wake_u32){0x9000,1};
    c.radio_id=(struct axd_wake_u32){0x50e0,1};c.radio_revision=(struct axd_wake_u32){0,1};c.chip_id=(struct axd_wake_u32){63178,1};
    c.radio_read_offset=(struct axd_wake_u32){0x8000,1};c.init_callback=(struct axd_wake_u32){0,1};c.shared_bus_revision=(struct axd_wake_u32){4,1};
    c.pi47e=7;c.chanmgr_data10=9;return c;
}
static void run(struct axd_radio_control *j,struct fixture *f)
{
    unsigned count=0;f->job=j;
    while(j->phase==AXD_RADIO_CTL_RUNNING){unsigned old=f->cursor;axd_radio_control_step(j,io,f);assert(f->cursor-old<=1);assert(++count<5000);}
    {unsigned old=f->cursor;assert(axd_radio_control_step(j,io,f)==j->phase);assert(f->cursor==old);}
}
static void complete_off(void)
{
    struct axd_radio_control j;struct axd_radio_control_context c=context();struct fixture f={0};
    c.radio_revision.known=0;c.radio_read_offset.known=0;off_prefix(&f);off_tail(&f);
    assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));run(&j,&f);
    assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && f.cursor==f.n && f.n==26);
    assert(j.context.pi47e==0 && j.context.chanmgr_data10==9 && j.context.hardware.phy.bookkeeping==0 && j.software_stores==2);
    c=context();c.radio_id.value=0x2069;c.radio_revision.value=0x20;f=(struct fixture){0};off_prefix(&f);
    rm(&f,0xc,0x80,0,0x1ff80);rm(&f,0x20c,0x80,0,0xffff);off_tail(&f);
    assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));run(&j,&f);
    assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && f.cursor==42 && j.core_index==2 && j.result==0xffff);
    c.radio_revision.value=0x40;c.radio_read_offset.known=0;f=(struct fixture){0};off_prefix(&f);off_tail(&f);
    assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && f.cursor==26);
    assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&j.context));assert(j.context.radio_id.value==0x2069);
}
static const uint8_t synthetic_prefregs[]={0x34,0x12,0xcd,0xab,0x00,0x01,0xff,0xff,0xff,0xff,0,0};
static void on_and_common(void)
{
    struct axd_radio_control j;struct axd_radio_control_context c=context();struct fixture f={0};
    c.prefregs=synthetic_prefregs;c.prefregs_length=sizeof(synthetic_prefregs);
    on_prefix(&f);rw(&f,0x1234,0xabcd);rw(&f,0x100,0xffff);rm(&f,0x88b,0x3f,1,0x1a5a5);
    assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_ON_REGISTERED,&c));run(&j,&f);
    assert(j.phase==AXD_RADIO_CTL_DEPENDENCY && j.dependency==AXD_RADIO_CTL_LOGEN_PLL_CONTINUATION && f.cursor==f.n);
    assert(j.context.pi47e==0 && j.context.chanmgr_data10==0 && j.prefreg_writes==2 && j.prefreg_cursor==8 && j.context.hardware.phy.bookkeeping==0);
    c=context();f=(struct fixture){0};c.public_pointer.known=0;c.callback_context.known=0;
    assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_COMMON_INIT,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && !f.cursor && j.context.pi47e==7);
    c.init_callback.value=0x1234;assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_COMMON_INIT,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_DEPENDENCY && j.dependency==AXD_RADIO_CTL_COMMON_CALLBACK);
    c=context();c.callback_pi.known=0;assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_ON_REGISTERED,&c));run(&j,&f);
    assert(j.phase==AXD_RADIO_CTL_DEPENDENCY && j.last_field==AXD_RADIO_CTL_CALLBACK_PI && j.context.chanmgr_data10==0 && j.context.pi47e==7);
}
static void leaves(void)
{
    struct axd_radio_control j;struct axd_radio_control_context c=context();struct fixture f={0};
    rr(&f,1,0x8000,0x12345);assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_READ,1,0,0,&c));run(&j,&f);
    assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && j.result_valid && j.result==0x2345 && j.context.hardware.phy.bookkeeping==0);
    f=(struct fixture){0};rr(&f,0x8123,0x8000,0xffff);
    assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_READ,0x8123,0,0,&c));run(&j,&f);assert(j.result==0xffff);
    c.phy_pointer.known=c.public_pointer.known=c.radio_read_offset.known=c.shared_pointer.known=0;f=(struct fixture){0};
    assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_READ,0xffff,0,0,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && j.result==0xffff && !f.cursor && j.context.hardware.phy.bookkeeping==42);
    c=context();c.hardware.shared_bus_word=1;c.hardware.write_threshold=43;f=(struct fixture){0};
    add(&f,AXD_D11_READ32,0x120,0,0x12345678);sy(&f);rw(&f,0x234,0x456);
    assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_WRITE,0x234,0,0x456,&c));run(&j,&f);
    assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && j.flush_reads==1 && j.context.hardware.phy.bookkeeping==0);
    c.hardware.phy.bookkeeping=0xffff;c.hardware.write_threshold=1;f=(struct fixture){0};f.check_counter=1;f.expected_counter=0;rw(&f,0x234,0x456);
    assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_WRITE,0x234,0,0x456,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && !j.flush_reads);
    c=context();c.hardware.shared_bus_word=2;c.shared_bus_revision.value=3;f=(struct fixture){0};rw(&f,0x234,0x456);add(&f,AXD_D11_READ16,0x3e0,0,0x12345);sy(&f);
    assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_WRITE,0x234,0,0x456,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && j.flush_reads==1 && j.context.hardware.phy.bookkeeping==42);
    c.shared_bus_revision.known=0;f=(struct fixture){0};rw(&f,0x234,0x456);assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_WRITE,0x234,0,0x456,&c));run(&j,&f);
    assert(j.phase==AXD_RADIO_CTL_DEPENDENCY && j.last_field==AXD_RADIO_CTL_BUS_REVISION && f.cursor==4);
    c=context();c.hardware.shared_bus_word=1;c.hardware.write_threshold=43;f=(struct fixture){0};add(&f,AXD_D11_READ32,0x120,0,123);sy(&f);
    assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_MODIFY,0xffff,0x400,0,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && j.result==0xffff && f.cursor==2 && j.context.hardware.phy.bookkeeping==0);
    c=context();c.hardware.shared_bus_word=1;c.hardware.write_threshold=0;f=(struct fixture){0};
    rr(&f,0x23,0x8000,0xffaa);add(&f,AXD_D11_READ32,0x120,0,12);sy(&f);rw(&f,0x23,0xff5a);
    assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_MODIFY,0x23,0xf0,0x50,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_TRACE_COMPLETE && f.cursor==10);
}
static void dependencies_and_bounds(void)
{
    struct axd_radio_control j;struct axd_radio_control_context c=context();struct fixture f={0};
    c.radio_id.known=0;off_prefix(&f);assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_DEPENDENCY && j.last_field==AXD_RADIO_CTL_ID && f.cursor==22);
    c=context();c.chip_id.known=0;f=(struct fixture){0};off_prefix(&f);off_tail(&f);assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_DEPENDENCY && j.last_field==AXD_RADIO_CTL_CHIP && f.cursor==26);
    c.chip_id=(struct axd_wake_u32){0x4345,1};f.cursor=0;assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_DEPENDENCY && j.dependency==AXD_RADIO_CTL_OTHER_OFF_TAIL);
    c=context();c.radio_id.value=0x2069;f=(struct fixture){0};assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_ON,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_DEPENDENCY && j.dependency==AXD_RADIO_CTL_OTHER_ON_DISPATCH && j.context.pi47e==0 && !f.cursor);
    c=context();c.prefregs=synthetic_prefregs;c.prefregs_length=3;f=(struct fixture){0};on_prefix(&f);assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_ON,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_FAILED && j.error==AXD_RADIO_CTL_BOUNDS && f.cursor==21);
    c.prefregs=NULL;f.cursor=0;assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_ON,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_DEPENDENCY && j.last_field==AXD_RADIO_CTL_PREFREGS && f.cursor==21);
    c.radio_revision.value=4;f.cursor=0;assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_ON,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_FAILED && j.error==AXD_RADIO_CTL_BINDING && f.cursor==21);
    c=context();c.phy_pointer.value=0;f=(struct fixture){0};assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_FAILED && j.error==AXD_RADIO_CTL_BINDING && !f.cursor && j.context.pi47e==7);
    c=context();c.callback_pi.value++;assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_ON_REGISTERED,&c));run(&j,&f);assert(j.phase==AXD_RADIO_CTL_FAILED && j.context.chanmgr_data10==0 && j.context.pi47e==7);
    c=context();assert(axd_radio_register_begin(&j,AXD_RADIO_CTL_READ,0,1,0,&c));assert(axd_radio_register_begin(&j,AXD_RADIO_CTL_WRITE,0,1,0,&c));
    assert(axd_radio_control_begin(NULL,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));assert(axd_radio_control_step(NULL,NULL,NULL)==AXD_RADIO_CTL_INVALID);assert(axd_radio_control_cancel(NULL)==AXD_RADIO_CTL_INVALID);
}
static void faults(void)
{
    struct axd_radio_control j;struct axd_radio_control_context c=context();struct fixture original={0};unsigned mode,i;
    c.prefregs=synthetic_prefregs;c.prefregs_length=sizeof(synthetic_prefregs);on_prefix(&original);rw(&original,0x1234,0xabcd);rw(&original,0x100,0xffff);rm(&original,0x88b,0x3f,1,0x1a5a5);
    for(mode=0;mode<3;mode++)for(i=1;i<=original.n;i++) {
        struct fixture f=original;if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_ON_REGISTERED,&c));run(&j,&f);
        assert(f.cursor==i && j.phase==(mode==1?AXD_RADIO_CTL_CANCELLED:AXD_RADIO_CTL_FAILED));
        assert(j.context.pi47e==0 && j.context.chanmgr_data10==0 && j.prefreg_writes==(i>29?2u:i>25?1u:0u));
        if(mode!=1)assert(j.error==AXD_RADIO_CTL_IO_ERROR);
    }
    c=context();c.hardware.shared_bus_word=1;c.hardware.write_threshold=43;original=(struct fixture){0};add(&original,AXD_D11_READ32,0x120,0,12);sy(&original);rw(&original,0x234,0x456);
    for(mode=0;mode<3;mode++)for(i=1;i<=original.n;i++) {
        struct fixture f=original;if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_WRITE,0x234,0,0x456,&c));run(&j,&f);
        assert(f.cursor==i && j.phase==(mode==1?AXD_RADIO_CTL_CANCELLED:AXD_RADIO_CTL_FAILED));
        assert(j.context.hardware.phy.bookkeeping==((i>2 || (i==2 && mode==1))?0:43));
    }
    c=context();original=(struct fixture){0};off_prefix(&original);off_tail(&original);
    for(mode=0;mode<3;mode++)for(i=1;i<=original.n;i++) {
        struct fixture f=original;if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));run(&j,&f);
        assert(f.cursor==i && j.phase==(mode==1?AXD_RADIO_CTL_CANCELLED:AXD_RADIO_CTL_FAILED));
        assert(j.context.pi47e==0 && j.context.hardware.phy.bookkeeping==((i>22 || (i==22 && mode==1))?0:42));
    }
    c=context();c.hardware.shared_bus_word=1;c.hardware.write_threshold=100;
    {struct fixture f={0};st(&f);f.fail=1;f.check_counter=1;f.expected_counter=43;
        assert(!axd_radio_control_begin(&j,AXD_RADIO_CTL_AC_SWITCH_OFF,&c));run(&j,&f);
        assert(j.phase==AXD_RADIO_CTL_FAILED && j.context.hardware.phy.bookkeeping==43 && j.context.pi47e==0);}
    c=context();original=(struct fixture){0};rr(&original,1,0x8000,0x12345);
    for(mode=0;mode<3;mode++)for(i=1;i<=original.n;i++) {
        struct fixture f=original;if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_READ,1,0,0,&c));run(&j,&f);
        assert(f.cursor==i && j.phase==(mode==1?AXD_RADIO_CTL_CANCELLED:AXD_RADIO_CTL_FAILED));
        assert(j.result_valid==(i==4 && mode==1));
        assert(j.context.hardware.phy.bookkeeping==((i==4 && mode==1)?0:42));
    }
    c.hardware.shared_bus_word=2;c.shared_bus_revision.value=3;original=(struct fixture){0};rw(&original,0x234,0x456);add(&original,AXD_D11_READ16,0x3e0,0,0xffff);sy(&original);
    for(mode=0;mode<3;mode++)for(i=1;i<=original.n;i++) {
        struct fixture f=original;if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_radio_register_begin(&j,AXD_RADIO_CTL_WRITE,0x234,0,0x456,&c));run(&j,&f);
        assert(f.cursor==i && j.phase==(mode==1?AXD_RADIO_CTL_CANCELLED:AXD_RADIO_CTL_FAILED));
        assert(j.context.hardware.phy.bookkeeping==42);
    }
}
int main(void)
{complete_off();on_and_common();leaves();dependencies_and_bounds();faults();puts("radio-control tests passed");return 0;}
