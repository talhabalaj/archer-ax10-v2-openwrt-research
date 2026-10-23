/* SPDX-License-Identifier: MIT */
#include "../driver/radio_power.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/radio_power_trace.h"
#include "fixtures/radio_regtbl_trace.h"
struct fixture {const struct script *s;struct axd_radio_regtbl *j;
    struct axd_radio_power *power;size_t cursor,fail,cancel;};
static int io(void *opaque,const struct axd_d11_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct event *e;
    assert(f->cursor<f->s->count);e=&f->s->events[f->cursor++];
    if(op->kind!=e->kind || op->offset!=e->offset || op->value!=e->value)
        fprintf(stderr,"regtbl event%zu actual%u/%x/%x expected%u/%x/%x\n",f->cursor,(unsigned)op->kind,op->offset,op->value,(unsigned)e->kind,e->offset,e->value);
    assert(op->kind==e->kind && op->offset==e->offset && op->value==e->value);
    if(f->j && f->cursor==1 && f->j->context.hardware.shared_bus_word==1)
        assert(f->j->context.hardware.phy.bookkeeping==43);
    if(f->power && f->power->regtbl_job.attempted_ops){
        assert(!memcmp(&f->power->context.regtbl,&f->power->regtbl_job.context,sizeof(f->power->context.regtbl)));
        assert(!memcmp(&f->power->context.radio.hardware,&f->power->regtbl_job.context.hardware,sizeof(f->power->context.radio.hardware)));
    }
    *reply=e->reply;
    if(f->cursor==f->cancel){if(f->power)axd_radio_power_cancel(f->power);else axd_radio_regtbl_cancel(f->j);}
    return f->cursor==f->fail?-1:0;
}
static size_t run(struct axd_radio_regtbl *j,const struct script *s,size_t fail,size_t cancel)
{
    struct fixture f={s,j,NULL,0,fail,cancel};unsigned steps=0;
    while(j->phase==AXD_RRT_RUNNING){size_t old=f.cursor;axd_radio_regtbl_step(j,io,&f);assert(f.cursor-old<=1 && ++steps<2000000);}
    {struct axd_radio_regtbl before=*j;size_t old=f.cursor;
        axd_radio_regtbl_step(j,io,&f);axd_radio_regtbl_cancel(j);assert(old==f.cursor && !memcmp(&before,j,sizeof(*j)));}
    assert(j->attempted_ops==f.cursor && j->completed_ops==f.cursor-(fail && f.cursor==fail));return f.cursor;
}
int main(void)
{
    struct script s={0};struct axd_radio_regtbl j;struct axd_radio_regtbl_context c;
    unsigned k,mode;size_t i,n;
    s.capacity=100000;s.events=malloc(s.capacity*sizeof(*s.events));assert(s.events);
    c=rrt_context(0);rrt_full(&s,2,0,0,&c);n=s.count;
    assert(!axd_radio_regtbl_begin(&j,&c));assert(run(&j,&s,0,0)==n);
    assert(j.phase==AXD_RRT_TRACE_COMPLETE && j.table_completed==235 && j.modify_completed==7);
    for(i=1;i<=n;i++)for(mode=0;mode<3;mode++){
        assert(!axd_radio_regtbl_begin(&j,&c));assert(run(&j,&s,mode==1?0:i,mode==0?0:i)==i);
        assert(j.phase==(mode==1?AXD_RRT_CANCELLED:AXD_RRT_FAILED));
        if(mode!=1)assert(j.error==AXD_RRT_IO_ERROR);
    }
    for(k=0;k<4;k++){
        c=rrt_context(k==0);c.hardware.phy.phy_core_count=(uint8_t)(k+1);
        if(k==1){c.flag471.value=1;c.tpc_two_range5g.value=1;}
        if(k==2)c.papd_byte0.value=1;
        if(k==3)c.papd_byte4.value=1;
        s.count=0;rrt_full(&s,k+1,k==1,k!=0,&c);
        assert(!axd_radio_regtbl_begin(&j,&c));assert(run(&j,&s,0,0)==s.count && j.phase==AXD_RRT_TRACE_COMPLETE);
        {unsigned zero_total=0,z;for(z=0;z<8;z++)zero_total+=c.zeros.entries[z].count;
            assert(j.table_completed==(k?491u:244u+zero_total));}
    }
    /* Cold path: sampled error/cancel prefixes across EXT writes and zero loop. */
    c=rrt_context(1);s.count=0;rrt_full(&s,2,0,0,&c);n=s.count;
    for(i=1;i<=n;i+=(i<64?1:n/300+1))for(mode=0;mode<3;mode++){
        assert(!axd_radio_regtbl_begin(&j,&c));assert(run(&j,&s,mode==1?0:i,mode==0?0:i)==i);
        assert(j.phase==(mode==1?AXD_RRT_CANCELLED:AXD_RRT_FAILED));
    }
    /* Row counts are exact pinned globals: a longer span is rejected, not truncated. */
    {struct axd_phy_table_write_request extra[10];memcpy(extra,c.tables.entries,9*sizeof(*extra));extra[9]=extra[8];
        c.tables=(struct axd_radio_regtbl_span){extra,10,1};assert(!axd_radio_regtbl_begin(&j,&c));
        s.count=0;rrt_full(&s,2,0,0,&c);run(&j,&s,0,0);assert(j.phase==AXD_RRT_FAILED && j.error==AXD_RRT_BOUNDS);}
    /* Entire actual registered radio-on now returns through the real child. */
    {struct axd_radio_power p;struct axd_radio_power_context q=context();struct fixture f={&s,NULL,&p,0,0,0};unsigned steps=0;
        q.regtbl=rrt_context(1);s.count=0;full(&s,2,0);rrt_full(&s,2,0,0,&q.regtbl);
        assert(!axd_radio_power_begin(&p,AXD_RPOWER_ON_REGISTERED,0,&q));
        while(p.phase==AXD_RPOWER_RUNNING){size_t old=f.cursor;axd_radio_power_step(&p,io,&f);assert(f.cursor-old<=1 && ++steps<2000000);}
        assert(p.phase==AXD_RPOWER_TRACE_COMPLETE && p.regtbl_completed && p.switch_completed && f.cursor==s.count);
        q.radio.chanmgr_data10=1;f.cursor=0;steps=0;
        assert(!axd_radio_power_begin(&p,AXD_RPOWER_SWITCH_ON,0,&q));
        while(p.phase==AXD_RPOWER_RUNNING){size_t old=f.cursor;axd_radio_power_step(&p,io,&f);assert(f.cursor-old<=1 && ++steps<2000000);}
        assert(p.phase==AXD_RPOWER_DEPENDENCY && p.dependency==AXD_RPOWER_CHANSPEC_TAIL && p.regtbl_completed && !p.switch_completed && !p.context.radio.pi47e && f.cursor==s.count);
    }
    c=rrt_context(0);c.load_static.known=0;s.count=0;rrt_full(&s,2,0,0,&c);
    assert(!axd_radio_regtbl_begin(&j,&c));run(&j,&s,0,0);assert(j.phase==AXD_RRT_DEPENDENCY && j.last_field==AXD_RRT_LOAD_STATIC);
    c=rrt_context(1);c.tables.known=0;assert(!axd_radio_regtbl_begin(&j,&c));run(&j,&s,0,0);assert(j.last_field==AXD_RRT_TABLES && j.phase==AXD_RRT_DEPENDENCY);
    c=rrt_context(0);c.tpc_pointer.value=0;assert(!axd_radio_regtbl_begin(&j,&c));assert(!run(&j,&s,0,0) && j.error==AXD_RRT_BINDING);
    c=rrt_context(0);c.chanspec.value=0x9024;c.tpc_pointer.known=0;
    assert(!axd_radio_regtbl_begin(&j,&c));assert(run(&j,&s,0,0)==s.count && j.phase==AXD_RRT_TRACE_COMPLETE);
    c=rrt_context(0);c.hardware.shared_bus_word=1;c.hardware.write_threshold=65535;
    assert(!axd_radio_regtbl_begin(&j,&c));assert(run(&j,&s,1,0)==1 && j.error==AXD_RRT_IO_ERROR && j.context.hardware.phy.bookkeeping==43);
    /* Original guard0 must be restored after a separate fresh RMW sample. */
    c=rrt_context(0);s.count=0;rrt_full(&s,2,0,0,&c);
    {unsigned modifies=0;
        for(i=0;i<s.count;i++){
            if(s.events[i].kind==AXD_D11_READ16 && s.events[i].reply==0x1232)s.events[i].reply=0x1230;
            if(s.events[i].kind==AXD_D11_WRITE16 && s.events[i].offset==0x3fc && s.events[i].value==0x19e &&
                i+4<s.count && s.events[i+1].kind==AXD_D11_STORE_BARRIER && ++modifies==2)s.events[i+4].value=0xa5a5;
        }
    }
    assert(!axd_radio_regtbl_begin(&j,&c));assert(run(&j,&s,0,0)==s.count && j.phase==AXD_RRT_TRACE_COMPLETE && j.guard_sample==0x1230);
    /* A successful epacal byte0 short-circuits an unknown byte4 exactly. */
    c=rrt_context(0);c.papd_byte0.value=1;c.papd_byte4.known=0;s.count=0;rrt_full(&s,2,0,1,&c);
    assert(!axd_radio_regtbl_begin(&j,&c));assert(run(&j,&s,0,0)==s.count && j.phase==AXD_RRT_TRACE_COMPLETE);
    c=rrt_context(0);s.count=0;rrt_full(&s,2,0,0,&c);
    assert(!axd_radio_regtbl_begin(&j,&c));assert(run(&j,&s,0,0)==s.count && j.coreunit==1);
    c.bmac_word150.value=0x80000000;assert(!axd_radio_regtbl_begin(&j,&c));
    assert(run(&j,&s,0,0)==s.count && j.coreunit==2);
    c.bmac_word150.known=0;assert(!axd_radio_regtbl_begin(&j,&c));
    assert(!run(&j,&s,0,0) && j.phase==AXD_RRT_DEPENDENCY && j.last_field==AXD_RRT_COREUNIT);
    c.bmac_word150.known=1;c.bmac_device_pointer.value=0;assert(!axd_radio_regtbl_begin(&j,&c));
    assert(!run(&j,&s,0,0) && j.error==AXD_RRT_BINDING && j.last_field==AXD_RRT_BMAC_DEVICE);
    c=j.context;assert(!axd_radio_regtbl_begin(&j,&j.context) && !memcmp(&j.context,&c,sizeof(c)));axd_radio_regtbl_cancel(&j);
    assert(axd_radio_regtbl_begin(NULL,&c)<0 && axd_radio_regtbl_step(NULL,NULL,NULL)==AXD_RRT_INVALID && axd_radio_regtbl_cancel(NULL)==AXD_RRT_INVALID);
    free(s.events);puts("radio regtbl: actual cold/warm source order, real children and callback exits passed");return 0;
}
