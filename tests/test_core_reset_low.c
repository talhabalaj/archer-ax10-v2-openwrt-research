/* SPDX-License-Identifier: MIT */
#include "../driver/core_reset_low.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct expect {unsigned kind;uint32_t map,sub,offset,value,read;};
struct fixture {struct expect e[256];unsigned n,cursor,fail,cancel;struct axd_core_reset_low *job;};
static void add(struct fixture *f,unsigned k,uint32_t map,uint32_t sub,uint32_t off,uint32_t val,uint32_t read) {struct expect e={k,map,sub,off,val,read};assert(f->n<256);f->e[f->n++]=e;}
static void a(struct fixture *f,unsigned map,unsigned kind,unsigned off,unsigned value,unsigned read){add(f,AXD_LOW_AI,map,kind,off,value,read);}
static void r(struct fixture *f,unsigned map,unsigned off,unsigned read){a(f,map,AXD_LOW_AI_READ32,off,0,read);a(f,map,AXD_LOW_AI_FULL_BARRIER,0,0,0);}
static void w(struct fixture *f,unsigned map,unsigned off,unsigned val){a(f,map,AXD_LOW_AI_STORE_BARRIER,0,0,0);a(f,map,AXD_LOW_AI_WRITE32,off,val,0);}
/* Independent literal fast-path protocol from retained assembly; distinct read
 * samples deliberately prevent write echo or default-zero transports. */
static void ai(struct fixture *f,unsigned map,unsigned bits,unsigned reset,int special)
{
    r(f,map,0x804,0);w(f,map,0x800,1);a(f,map,AXD_LOW_AI_DELAY_US,0,10,0);
    r(f,map,0x804,0);w(f,map,0x408,bits|reset|3);r(f,map,0x408,0xabc3);
    if(special){a(f,map,AXD_LOW_AI_DELAY_US,0,1,0);w(f,map,0x408,0xabc1);}
    r(f,map,0x804,0);r(f,map,0x800,0);w(f,map,0x408,bits|(special?3:1));r(f,map,0x408,0x5a03);
    a(f,map,AXD_LOW_AI_DELAY_US,0,1,0);if(special){w(f,map,0x408,0x5a01);a(f,map,AXD_LOW_AI_DELAY_US,0,1,0);}
}
static void up(struct fixture *f,int isup){add(f,AXD_LOW_CORE,0,AXD_SICO_READ32,0x408,0,isup?1:3);add(f,AXD_LOW_CORE,0,AXD_SICO_FULL_BARRIER,0,0,0);if(isup){add(f,AXD_LOW_CORE,0,AXD_SICO_READ32,0x800,0,0);add(f,AXD_LOW_CORE,0,AXD_SICO_FULL_BARRIER,0,0,0);}}
static void tail(struct fixture *f){add(f,AXD_LOW_D11,0,AXD_D11_STORE_BARRIER,0,0,0);add(f,AXD_LOW_D11,0,AXD_D11_WRITE32,0x120,0x400,0);add(f,AXD_LOW_D11,0,AXD_D11_STORE_BARRIER,0,0,0);add(f,AXD_LOW_D11,0,AXD_D11_WRITE16,0xa08,0,0);add(f,AXD_LOW_D11,0,AXD_D11_STORE_BARRIER,0,0,0);add(f,AXD_LOW_D11,0,AXD_D11_WRITE16,0xa06,0,0);}
static int io(void *opaque,const struct axd_core_reset_low_operation *o,uint32_t *read)
{
    struct fixture *f=opaque;struct expect *e;assert(f->cursor<f->n);e=&f->e[f->cursor++];assert(o->kind==e->kind);
    if(o->kind==AXD_LOW_AI){assert(o->wrapper_mapping==e->map);assert((unsigned)o->ai.kind==e->sub);assert(o->ai.offset==e->offset);assert(o->ai.value==e->value);}
    else if(o->kind==AXD_LOW_CORE){assert(o->core.wrapper==0x200);assert((unsigned)o->core.kind==e->sub);assert(o->core.offset==e->offset);assert(o->core.value==e->value);}
    else if(o->kind==AXD_LOW_D11){assert(f->job->context.mctrl.psm.main_control==0x400);assert((unsigned)o->d11.kind==e->sub);assert(o->d11.offset==e->offset);assert(o->d11.value==e->value);}
    else {assert(o->select.kind==AXD_SI_MAP);assert(o->select.address==e->offset);}
    *read=e->read;
    if(f->cursor==f->cancel)axd_core_reset_low_cancel(f->job);
    return f->cursor==f->fail?-1:0;
}
static const struct axd_si_core catalog[2]={{0x812,0x1000,0x2000,0},{0x849,0x3000,0x4000,0}};
static struct axd_core_reset_low_context context(void)
{
    struct axd_core_reset_low_context c={0};c.hw_present=c.reset_callback_present=c.w_present=1;c.revision=130;c.flags=4;
    c.si.interconnect=1;c.si.core_count=2;c.si.catalog=catalog;c.si.catalog_length=2;
    c.state.current_map=0x111;c.state.primary_wrapper=0x100;c.state.cache[0].core=0x111;c.state.cache[0].primary=0x100;c.state.cache[1].core=0x222;c.state.cache[1].primary=0x200;
    c.mctrl.psm.core=130;c.mctrl.psm.minor=1;return c;
}
static void run(struct axd_core_reset_low *j,struct fixture *f){unsigned steps=0;f->job=j;while(j->phase==AXD_LOW_RUNNING){unsigned old=f->cursor;axd_core_reset_low_step(j,io,f);assert(f->cursor-old<=1);assert(++steps<20000);}assert(axd_core_reset_low_step(j,io,f)==j->phase);}
static void full(int isup,int secondary)
{
    struct axd_core_reset_low j;struct fixture f={0};struct axd_core_reset_low_context c=context();struct axd_si_core cat[2];memcpy(cat,catalog,sizeof(cat));
    if(secondary){cat[1].secondary_physical=0x5000;c.state.cache[1].secondary=0x300;c.si.catalog=cat;}
    ai(&f,0x100,4,0x20,1);up(&f,isup);if(!isup){if(secondary)ai(&f,0x300,0,0,0);ai(&f,0x200,0,0,0);}tail(&f);
    assert(!axd_core_reset_low_begin(&j,&c));run(&j,&f);assert(j.phase==AXD_LOW_DONE);assert(f.cursor==f.n);assert(j.context.state.current_index==0);assert(j.context.state.primary_wrapper==0x100);assert(j.context.mctrl.psm.main_control==0x400);
    /* Context alias initialization must snapshot before job clear. */
    assert(!axd_core_reset_low_begin(&j,&j.context));assert(j.context.state.primary_wrapper==0x100);
}
static void faults(void)
{
    struct axd_core_reset_low j;struct fixture template={0};struct axd_core_reset_low_context c=context();unsigned i,mode;
    ai(&template,0x100,4,0x20,1);up(&template,0);ai(&template,0x200,0,0,0);tail(&template);
    for(mode=0;mode<3;mode++)for(i=1;i<=template.n;i++) {struct fixture f=template;if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;assert(!axd_core_reset_low_begin(&j,&c));run(&j,&f);assert(f.cursor==i);assert(j.phase==(mode==1?AXD_LOW_CANCELLED:AXD_LOW_FAILED));if(i>template.n-6)assert(j.context.mctrl.psm.main_control==0x400);}
}
static void zero_mapping(void)
{
    struct axd_core_reset_low j;struct axd_core_reset_low_context c=context();struct fixture f={0};ai(&f,0x100,4,0x20,1);c.state.cache[1].core=0;add(&f,AXD_LOW_SELECT,0,0,0x3000,0,0);
    assert(!axd_core_reset_low_begin(&j,&c));run(&j,&f);assert(j.phase==AXD_LOW_DONE);assert(f.cursor==f.n);assert(j.context.state.current_index==1);assert(j.context.state.current_map==0);assert(j.context.mctrl.psm.main_control==0);
}
static void flag_paths(void)
{
    struct axd_core_reset_low j;unsigned i;
    const struct {unsigned rev,w,w138,bits,reset,special;} cases[]={
        {129,1,2,0x20,0,1},{130,0,0,0,0x20,1},
        {130,1,1,0,0x20,1},{130,1,2,0x20,0x20,1},
        {131,0,0,0x20,0x20,1},{133,0,0,0x20,0x20,0}};
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        struct fixture f={0};struct axd_core_reset_low_context c=context();
        c.si.core_count=c.si.catalog_length=1;c.revision=(uint16_t)cases[i].rev;
        c.flags=0x20;c.w_present=(uint8_t)cases[i].w;c.w138=cases[i].w138;
        ai(&f,0x100,cases[i].bits,cases[i].reset,(int)cases[i].special);
        assert(!axd_core_reset_low_begin(&j,&c));run(&j,&f);
        assert(j.phase==AXD_LOW_DONE && f.cursor==f.n);
        assert(j.effective_bits==cases[i].bits && j.effective_reset_bits==cases[i].reset);
    }
}
static void mapping_cancel(void)
{
    struct axd_core_reset_low j;struct axd_core_reset_low_context c=context();struct fixture f={0};
    ai(&f,0x100,4,0x20,1);c.state.cache[1].core=0;
    add(&f,AXD_LOW_SELECT,0,0,0x3000,0,0x555);f.cancel=f.n;
    assert(!axd_core_reset_low_begin(&j,&c));run(&j,&f);
    assert(j.phase==AXD_LOW_CANCELLED && f.cursor==f.n);
    assert(j.context.state.cache[1].core==0x555);
    assert(j.context.state.current_index==0 && j.context.state.current_map==0x111);
}
struct pci_fixture {unsigned cursor;uint32_t wrapper;};
static int pci_io(void *opaque,const struct axd_si_operation *op,uint32_t *read)
{
    struct pci_fixture *f=opaque;(void)read;
    assert(op->kind==AXD_SI_PCI_WRITE32 && op->size==4);
    assert(op->address==(f->cursor?0xac:0x80));assert(op->value==(f->cursor?f->wrapper:0x3000));
    assert(++f->cursor<=2);return 0;
}
static void secondary_pci(void)
{
    struct axd_si_select j;struct axd_core_reset_low_context c=context();struct axd_si_core cat[2];struct pci_fixture f={0,0x5000};unsigned steps=0;
    memcpy(cat,catalog,sizeof(cat));cat[1].secondary_physical=0x5000;c.si.catalog=cat;c.si.bus=1;c.si.pci_config_owned=1;
    assert(!axd_si_select_begin_wrapper(&j,AXD_SI_BY_INDEX,1,0,1,&c.si,&c.state));
    while(j.phase==AXD_SI_RUNNING){axd_si_select_step(&j,pci_io,&f);assert(++steps<40);}
    assert(j.phase==AXD_SI_TRACE_COMPLETE && f.cursor==2 && j.state.current_index==1);
    f.cursor=0;f.wrapper=0x4000;steps=0;
    assert(!axd_si_select_begin_wrapper(&j,AXD_SI_BY_INDEX,1,0,0,&j.context,&j.state));
    while(j.phase==AXD_SI_RUNNING){axd_si_select_step(&j,pci_io,&f);assert(++steps<40);}
    assert(j.phase==AXD_SI_TRACE_COMPLETE && f.cursor==2);
}
int main(void)
{
    struct axd_core_reset_low j;struct fixture f={0};struct axd_core_reset_low_context c=context();full(1,0);full(0,0);full(0,1);faults();zero_mapping();flag_paths();mapping_cancel();secondary_pci();
    c.hw_present=0;assert(!axd_core_reset_low_begin(&j,&c));run(&j,&f);assert(j.phase==AXD_LOW_DONE && f.cursor==0);c.hw_present=1;c.reset_callback_present=0;assert(!axd_core_reset_low_begin(&j,&c));run(&j,&f);assert(j.phase==AXD_LOW_DONE);
    puts("core-reset lower wrapper: independent fast/generic/secondary traces, zero-map and all callback faults/cancellation passed");return 0;
}
