/* SPDX-License-Identifier: MIT */
#include "../driver/si_core.h"
#include <assert.h>
#include <stdio.h>
struct expected {enum axd_si_core_kind kind;uint32_t offset,value,read;};
struct fixture {struct expected ops[8];unsigned n,cursor,fail,cancel;struct axd_si_core_job *job;};
static void add(struct fixture *f,enum axd_si_core_kind k,uint32_t off,uint32_t value,uint32_t read)
{struct expected e={k,off,value,read};assert(f->n<8);f->ops[f->n++]=e;}
static int io(void *opaque,const struct axd_si_core_operation *op,uint32_t *read)
{
    struct fixture *f=opaque;const struct expected *e;assert(f->cursor<f->n);e=&f->ops[f->cursor++];
    assert(op->wrapper==0x12340000);assert(op->kind==e->kind);assert(op->offset==e->offset);assert(op->value==e->value);*read=e->read;
    if(f->cancel==f->cursor)axd_si_core_cancel(f->job);
    return f->fail==f->cursor?-1:0;
}
static void run(struct axd_si_core_job *j,struct fixture *f)
{
    unsigned steps=0;f->job=j;
    while(j->phase==AXD_SICO_RUNNING){unsigned old=f->cursor;axd_si_core_step(j,io,f);assert(f->cursor-old<=1);assert(++steps<30);}
    {unsigned old=f->cursor;assert(axd_si_core_step(j,io,f)==j->phase);assert(f->cursor==old);}
}
static struct axd_si_core_context context(void)
{struct axd_si_core_context c={1,0x63178,0x812,0x12340000,1};return c;}
static struct fixture flags(int readonly)
{
    struct fixture f={0};add(&f,AXD_SICO_READ32,0x408,0,0xabcdef31);add(&f,AXD_SICO_FULL_BARRIER,0,0,0);
    if(!readonly){add(&f,AXD_SICO_STORE_BARRIER,0,0,0);add(&f,AXD_SICO_WRITE32,0x408,0xabcded31,0);add(&f,AXD_SICO_READ32,0x408,0,0x87654321);add(&f,AXD_SICO_FULL_BARRIER,0,0,0);}
    return f;
}
static void flag_paths(void)
{
    struct axd_si_core_job j;struct axd_si_core_context c=context();struct fixture f=flags(0);unsigned i,mode;
    assert(!axd_si_core_begin(&j,AXD_SICO_FLAGS,0x300,0x100,&c));run(&j,&f);assert(j.phase==AXD_SICO_DONE && j.result==0x87654321 && f.cursor==f.n);
    assert(!axd_si_core_begin(&j,AXD_SICO_FLAGS,0,0,&j.context));f=flags(1);run(&j,&f);assert(j.result==0xabcdef31 && f.cursor==f.n);
    f=flags(0);f.ops[3].value=0xabcdef71;
    assert(!axd_si_core_begin(&j,AXD_SICO_FLAGS,0,0x40,&c));run(&j,&f);assert(j.phase==AXD_SICO_DONE && f.cursor==f.n);
    for(mode=0;mode<3;mode++)for(i=1;i<=6;i++) {f=flags(0);if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;assert(!axd_si_core_begin(&j,AXD_SICO_FLAGS,0x300,0x100,&c));run(&j,&f);assert(j.phase==(mode==1?AXD_SICO_CANCELLED:AXD_SICO_FAILED));assert(f.cursor==i);if(mode!=1)assert(j.error==AXD_SICO_IO_ERROR);if(mode==1 && i==1)assert(j.last_read==0xabcdef31);}
}
static void up_paths(void)
{
    struct axd_si_core_job j;struct axd_si_core_context c=context();unsigned i;
    for(i=0;i<4;i++) {struct fixture f={0};uint32_t control=i==0?3:0x111;uint32_t reset=i==2?3:4;
        add(&f,AXD_SICO_READ32,0x408,0,control);add(&f,AXD_SICO_FULL_BARRIER,0,0,0);
        if(i){add(&f,AXD_SICO_READ32,0x800,0,reset);add(&f,AXD_SICO_FULL_BARRIER,0,0,0);}
        assert(!axd_si_core_begin(&j,AXD_SICO_IS_UP,0,0,&c));run(&j,&f);assert(j.phase==AXD_SICO_DONE && j.result==(i!=0 && i!=2));assert(f.cursor==f.n);
    }
}
static void up_faults(void)
{
    struct axd_si_core_job j;struct axd_si_core_context c=context();unsigned mode,i;
    for(mode=0;mode<3;mode++)for(i=1;i<=4;i++) {
        struct fixture f={0};add(&f,AXD_SICO_READ32,0x408,0,1);add(&f,AXD_SICO_FULL_BARRIER,0,0,0);
        add(&f,AXD_SICO_READ32,0x800,0,0);add(&f,AXD_SICO_FULL_BARRIER,0,0,0);
        if(mode!=1)f.fail=i;if(mode!=0)f.cancel=i;
        assert(!axd_si_core_begin(&j,AXD_SICO_IS_UP,0,0,&c));run(&j,&f);
        assert(j.phase==(mode==1?AXD_SICO_CANCELLED:AXD_SICO_FAILED) && f.cursor==i);
    }
}
static void skips(void)
{
    struct axd_si_core_job j;struct axd_si_core_context c=context();struct fixture f={0};unsigned i;
    const struct {uint32_t chip,core;int exempt;} cases[]={
        {0x63178,0x827,1},{0xcf26,0x50b,1},{0xcf12,0x50b,1},{0xcf1a,0x50b,1},{0xcf26,0x812,0},{0x63178,0x50b,0}};
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {c=context();c.chip_id=cases[i].chip;c.core_id=cases[i].core;if(cases[i].exempt)c.wrapper_known=0;
        f=cases[i].exempt?(struct fixture){0}:flags(1);assert(!axd_si_core_begin(&j,AXD_SICO_FLAGS,0,0,&c));run(&j,&f);assert(j.phase==AXD_SICO_DONE && f.cursor==f.n);
    }
    for(i=0;i<2;i++) {c=context();c.interconnect=i?2:0;f=(struct fixture){0};assert(!axd_si_core_begin(&j,AXD_SICO_IS_UP,0,0,&c));run(&j,&f);assert(j.phase==(i?AXD_SICO_DONE:AXD_SICO_DEPENDENCY));assert(f.cursor==0);}
    for(i=0;i<2;i++) {c=context();if(i)c.primary_wrapper=0;else c.wrapper_known=0;f=(struct fixture){0};assert(!axd_si_core_begin(&j,AXD_SICO_IS_UP,0,0,&c));run(&j,&f);assert(j.phase==AXD_SICO_DEPENDENCY && f.cursor==0);}
}
int main(void){flag_paths();up_paths();up_faults();skips();puts("SI core helpers: strict cflags/isup, exemptions, binding dependencies, aliases and callback exits passed");return 0;}
