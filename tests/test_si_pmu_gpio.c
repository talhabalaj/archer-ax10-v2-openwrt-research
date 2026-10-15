/* SPDX-License-Identifier: MIT */
#include "../driver/si_pmu_gpio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct event {enum axd_si_pmu_gpio_kind kind;uint32_t mapping,offset,value,reply;};
struct fixture {struct event e[64];unsigned n,cursor,fail,cancel,unknown_irq,mutate_irq;struct axd_si_pmu_gpio *job;};
static void add(struct fixture *f,enum axd_si_pmu_gpio_kind kind,uint32_t mapping,uint32_t offset,uint32_t value,uint32_t reply)
{struct event e={kind,mapping,offset,value,reply};assert(f->n<64);f->e[f->n++]=e;}
static void rmw(struct fixture *f,uint32_t map,uint32_t offset,uint32_t old,uint32_t value,uint32_t result)
{
    add(f,AXD_SPG_READ32,map,offset,0,old);add(f,AXD_SPG_FULL_BARRIER,map,offset,0,0);
    add(f,AXD_SPG_STORE_BARRIER,map,offset,0,0);add(f,AXD_SPG_WRITE32,map,offset,value,0);
    add(f,AXD_SPG_READ32,map,offset,0,result);add(f,AXD_SPG_FULL_BARRIER,map,offset,0,0);
}
static int io(void *opaque,const struct axd_si_pmu_gpio_operation *op,struct axd_si_pmu_gpio_reply *reply)
{
    struct fixture *f=opaque;const struct event *e;assert(f->cursor<f->n);e=&f->e[f->cursor++];
    assert(op->kind==e->kind);
    if(op->kind==AXD_SPG_SELECT){
        if(op->select.kind==AXD_SI_MAP){assert(op->select.resource==AXD_SI_CORE && op->select.address==e->offset && op->select.size==0x1000);}
        else {assert(op->select.kind==AXD_SI_PCI_WRITE32 && op->select.address==e->offset && op->select.value==e->value);}
    } else if(op->kind==AXD_SPG_IRQ_DISABLE || op->kind==AXD_SPG_IRQ_RESTORE) {
        assert(op->callback==(op->kind==AXD_SPG_IRQ_DISABLE?0x111:0x222));assert(op->callback_context==0x333);assert(op->state);
        assert(op->value==e->value);
        reply->irq_state_known=(uint8_t)!f->unknown_irq;reply->irq_state=*op->state;
        if(f->mutate_irq && op->kind==AXD_SPG_IRQ_DISABLE){reply->irq_state.current_index=1;reply->irq_state.current_map=0x8800;}
    } else {assert(op->mapping==e->mapping && op->offset==e->offset && op->value==e->value);}
    reply->value=e->reply;
    if(f->cursor==f->cancel)axd_si_pmu_gpio_cancel(f->job);
    return f->cursor==f->fail?-1:0;
}
static const struct axd_si_core catalog[3]={{0x800,0x100000,0,0},{0x827,0x200000,0,0},{0x812,0x300000,0,0}};
static struct axd_si_pmu_gpio_context context(void)
{
    struct axd_si_pmu_gpio_context c={0};c.si.interconnect=1;c.si.core_count=3;c.si.catalog=catalog;c.si.catalog_length=3;
    c.state.current_index=2;c.state.current_map=0x9000;c.state.cache[0].core=0x1000;c.state.cache[1].core=0x2000;
    c.chipcommon_revision=35;c.capabilities_ext=0x40;c.gpio_reservation_known=1;c.gpio_reservation=0x00f0;
    return c;
}
static void run(struct axd_si_pmu_gpio *j,struct fixture *f)
{
    unsigned steps=0;f->job=j;
    while(j->phase==AXD_SPG_RUNNING){unsigned old=f->cursor;axd_si_pmu_gpio_step(j,io,f);assert(f->cursor-old<=1);assert(++steps<1000);}
    {unsigned old=f->cursor;assert(axd_si_pmu_gpio_step(j,io,f)==j->phase);assert(f->cursor==old);}
}
static void pmu(void)
{
    struct axd_si_pmu_gpio j;struct axd_si_pmu_gpio_context c=context();struct fixture f={0};
    rmw(&f,0x2000,0x658,0x12345678,0,0xaabbccdd);rmw(&f,0x2000,0x65c,0xdeadbeef,0xde5dbeef,0x12345678);
    assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_VREG,0,0x01f00000,0x00500000,&c));run(&j,&f);
    assert(j.phase==AXD_SPG_TRACE_COMPLETE && f.cursor==12 && j.result==0x12345678 && j.register_completed==2);
    assert(j.context.state.current_index==2 && j.context.state.current_map==0x9000);
    c.chipcommon_revision=34;f=(struct fixture){0};rmw(&f,0x1000,0x658,0,7,1);rmw(&f,0x1000,0x65c,0,0xabcdef01,0xabcdef02);
    assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_VREG,7,0,0xabcdef01,&c));run(&j,&f);assert(j.result==0xabcdef02 && f.cursor==12);
    /* PMU missing yields actual findcoreidx33, then AI's >31 return0. */
    c=context();c.si.core_count=1;f=(struct fixture){0};assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_VREG,0,1,1,&c));run(&j,&f);assert(j.phase==AXD_SPG_TRACE_COMPLETE && f.cursor==0 && j.result==0 && j.register_completed==2);
}
static void gpio(void)
{
    struct axd_si_pmu_gpio j;struct axd_si_pmu_gpio_context c=context();struct fixture f={0};unsigned priority;
    for(priority=0;priority<3;priority++){
        f=(struct fixture){0};rmw(&f,0x1000,0x6c,0x1234ffff,priority==0?0x123400f0u:(priority==1?0x1234ff0fu:0x12340000u),0x33334444);
        assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,priority,0xffff,0,&c));run(&j,&f);assert(j.phase==AXD_SPG_TRACE_COMPLETE && f.cursor==6 && j.result==0x33334444);
    }
    {const uint32_t expected[3]={0xdead00e3,0xdeadbeff,0xdfad00f3};
        for(priority=0;priority<3;priority++){
            f=(struct fixture){0};rmw(&f,0x1000,0x6c,0xdeadbeef,expected[priority],0x76543210);
            assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,priority,0xffff,0x010000f3,&c));run(&j,&f);
            assert(j.phase==AXD_SPG_TRACE_COMPLETE && f.cursor==6 && j.result==0x76543210);
        }
    }
    c.gpio_reservation_known=0;f=(struct fixture){0};assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,0,1,0,&c));run(&j,&f);assert(j.phase==AXD_SPG_DEPENDENCY && j.dependency==AXD_SPG_GPIO_RESERVATION && f.cursor==0);
    f=(struct fixture){0};add(&f,AXD_SPG_READ32,0x1000,0x6c,0,0x1234);add(&f,AXD_SPG_FULL_BARRIER,0x1000,0x6c,0,0);
    assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,0,0,0,&c));run(&j,&f);assert(j.phase==AXD_SPG_TRACE_COMPLETE && j.result==0x1234 && f.cursor==2);
}
static void map_and_faults(void)
{
    struct axd_si_pmu_gpio j;struct axd_si_pmu_gpio_context c=context();struct fixture original={0};unsigned i,mode;
    rmw(&original,0x2000,0x658,0x12345678,0,0xaabbccdd);rmw(&original,0x2000,0x65c,0xdeadbeef,0xde5dbeef,0x12345678);
    for(mode=0;mode<3;mode++)for(i=1;i<=12;i++){struct fixture f=original;if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_VREG,0,0x01f00000,0x00500000,&c));run(&j,&f);
        assert(f.cursor==i && j.phase==(mode==1?AXD_SPG_CANCELLED:AXD_SPG_FAILED));
        if(mode!=1)assert(j.error==AXD_SPG_IO_ERROR);
    }
    c.state.cache[1].core=0;
    for(mode=0;mode<3;mode++){struct fixture f={0};add(&f,AXD_SPG_SELECT,0,0x200000,0,mode==2?0:0x7000);
        if(mode==0)f.cancel=1;else if(mode==1)f.fail=1;
        assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_VREG,0,1,0,&c));run(&j,&f);
        assert(f.cursor==1 && j.phase==(mode==0?AXD_SPG_CANCELLED:AXD_SPG_FAILED));
        assert(j.context.state.cache[1].core==(mode==0?0x7000u:0));assert(j.context.state.current_index==2);
    }
}
static struct fixture ordinary(int disable,int mutation)
{
    struct fixture f={0};
    if(disable)add(&f,AXD_SPG_IRQ_DISABLE,0,0,0,0x55);
    add(&f,AXD_SPG_SELECT,0,0x80,0x100000,0);add(&f,AXD_SPG_SELECT,0,0xac,0,0);
    rmw(&f,mutation?0x8800:0x9000,0x6c,0xffff1234,0xffff0000,0x7777);
    add(&f,AXD_SPG_SELECT,0,0x80,mutation?0x200000:0x300000,0);add(&f,AXD_SPG_SELECT,0,0xac,0,0);
    if(!mutation)add(&f,AXD_SPG_IRQ_RESTORE,0,0,disable?0x55:0,0);
    return f;
}
static void irq_faults(void)
{
    struct axd_si_pmu_gpio j;struct axd_si_pmu_gpio_context c=context();unsigned mode,i;
    c.si.bus=1;c.si.pci_config_owned=1;c.si.bus_core_id=0x804;c.bus_core_revision=12;c.bus_core_index=2;
    c.disable_callback=0x111;c.restore_callback=0x222;c.irq_context=0x333;c.irq_core_id=0x812;
    for(mode=0;mode<3;mode++)for(i=1;i<=12;i++) {struct fixture f=ordinary(1,0);if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,0,0xffff,0,&c));run(&j,&f);
        assert(j.phase==(mode==1?AXD_SPG_CANCELLED:AXD_SPG_FAILED) && f.cursor==i);
        if(mode!=1 && (i==1 || i==12))assert(!j.state_valid);
        if(mode==1 && (i==1 || i==12))assert(j.state_valid);
    }
    {struct fixture f=ordinary(1,1);f.mutate_irq=1;
        assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,0,0xffff,0,&c));run(&j,&f);
        assert(j.phase==AXD_SPG_TRACE_COMPLETE && f.cursor==f.n && j.saved_index==1);
        assert(j.context.state.current_index==1 && j.context.state.current_map==0x8800 && j.restore_called==0);
    }
    {struct fixture f=ordinary(0,0);c.disable_callback=0;
        assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,0,0xffff,0,&c));run(&j,&f);
        assert(j.phase==AXD_SPG_TRACE_COMPLETE && f.cursor==f.n && j.disable_called==0 && j.restore_called==1);
    }
}
static void pci_and_irq(void)
{
    struct axd_si_pmu_gpio j;struct axd_si_pmu_gpio_context c=context();struct fixture f={0};
    c.si.bus=1;c.si.bus_core_id=0x83c;c.gpio_reservation_known=0;rmw(&f,0xc000,0x6c,0x1234ffff,0x12340000,0x4444);
    assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,0,0xffff,0,&c));run(&j,&f);assert(j.phase==AXD_SPG_TRACE_COMPLETE && f.cursor==6 && j.context.state.current_index==2);
    c=context();c.si.bus=1;c.si.pci_config_owned=1;c.si.bus_core_id=0x804;c.bus_core_revision=12;c.bus_core_index=2;
    c.disable_callback=0x111;c.restore_callback=0x222;c.irq_context=0x333;c.irq_core_id=0x812;
    f=(struct fixture){0};add(&f,AXD_SPG_IRQ_DISABLE,0,0,0,0x55);
    add(&f,AXD_SPG_SELECT,0,0x80,0x100000,0);add(&f,AXD_SPG_SELECT,0,0xac,0,0);
    rmw(&f,0x9000,0x6c,0xffff1234,0xffff0000,0x7777);
    add(&f,AXD_SPG_SELECT,0,0x80,0x300000,0);add(&f,AXD_SPG_SELECT,0,0xac,0,0);
    add(&f,AXD_SPG_IRQ_RESTORE,0,0,0x55,0);
    assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,0,0xffff,0,&c));run(&j,&f);
    assert(j.phase==AXD_SPG_TRACE_COMPLETE && f.cursor==f.n && j.disable_called==1 && j.restore_called==1 && j.context.state.current_index==2);
    f=(struct fixture){0};f.unknown_irq=1;add(&f,AXD_SPG_IRQ_DISABLE,0,0,0,0x55);
    assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,0,0xffff,0,&c));run(&j,&f);assert(j.phase==AXD_SPG_DEPENDENCY && !j.state_valid && f.cursor==1);
    c=context();c.si.interconnect=0;f=(struct fixture){0};assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,2,1,0,&c));run(&j,&f);assert(j.phase==AXD_SPG_DEPENDENCY && j.dependency==AXD_SPG_SB);
    c.si.interconnect=2;assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,2,1,0,&c));run(&j,&f);assert(j.phase==AXD_SPG_TRACE_COMPLETE && j.result==0);
    assert(!axd_si_pmu_gpio_begin(&j,AXD_SPG_GPIO,2,0,0,&j.context));assert(j.context.si.interconnect==2);
}
int main(void){pmu();gpio();map_and_faults();pci_and_irq();irq_faults();puts("SI PMU/GPIO: exact register/filter/map/PCI/IRQ traces, freshness and callback exits passed");return 0;}
