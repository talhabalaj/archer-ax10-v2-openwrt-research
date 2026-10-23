/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixtures/phy_chanspec_trace.h"
struct fixture {const struct cs_script *s;struct axd_phy_chanspec *j;size_t cursor,fail,cancel;};
static int io(void *opaque,const struct axd_phy_chanspec_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;const struct cs_event *e;unsigned kind;uint32_t offset,value;
    assert(f->cursor<f->s->count);e=&f->s->events[f->cursor++];
    kind=op->path==AXD_CS_D11?(unsigned)op->d11.kind:(unsigned)op->si.kind;
    offset=op->path==AXD_CS_D11?op->d11.offset:op->si.offset;
    value=op->path==AXD_CS_D11?op->d11.value:op->si.value;
    if(op->path!=e->path || kind!=e->kind || offset!=e->offset || value!=e->value)
        fprintf(stderr,"chanspec event%zu actual%u/%u/%x/%x expected%u/%u/%x/%x\n",f->cursor,
            (unsigned)op->path,kind,offset,value,(unsigned)e->path,e->kind,e->offset,e->value);
    assert(op->path==e->path && kind==e->kind && offset==e->offset && value==e->value);
    if(op->path==AXD_CS_SI)assert(op->si.wrapper==0x12340000);
    *reply=e->reply;
    if(f->cursor==f->cancel)axd_phy_chanspec_cancel(f->j);
    return f->cursor==f->fail?-1:0;
}
static size_t run(struct axd_phy_chanspec *j,const struct cs_script *s,size_t fail,size_t cancel)
{
    struct fixture f={s,j,0,fail,cancel};unsigned steps=0;
    while(j->phase==AXD_CS_RUNNING){size_t old=f.cursor;axd_phy_chanspec_step(j,io,&f);assert(f.cursor-old<=1 && ++steps<1000000);}
    {struct axd_phy_chanspec before=*j;size_t old=f.cursor;
        axd_phy_chanspec_step(j,io,&f);axd_phy_chanspec_cancel(j);assert(old==f.cursor && !memcmp(&before,j,sizeof(*j)));}
    assert(j->attempted_ops==f.cursor && j->completed_ops==f.cursor-(fail && f.cursor==fail));
    return f.cursor;
}
static const struct cs_options base={2,0,0x11,0x1232,1,2,0,0x12,0,0};
static size_t n0;
static size_t expect(struct cs_script *s,const struct cs_options *o){s->count=0;cs_module0(s,o);n0=s->count;cs_module2(s,o);return s->count;}
int main(void)
{
    struct cs_script s={0};struct axd_phy_chanspec j;struct axd_phy_chanspec_context c=cs_context();
    struct cs_options o=base;size_t i,n;unsigned mode;
    s.capacity=4096;s.events=malloc(s.capacity*sizeof(*s.events));assert(s.events);
    n=expect(&s,&o);
    assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n);
    assert(j.phase==AXD_CS_DEPENDENCY && j.dependency==AXD_CS_MODULE_SETUP_PHY && j.modules_completed==3);
    assert(j.cca_completed==2 && j.radio20704_argument==1);
    assert(j.sync_completed && j.corestate_completed && j.bandwidth_completed);
    assert(j.context.chanmgr_data11==0 && j.context.ac_flags_be==7 && j.context.radar_indicator==1);
    assert(j.context.chanmgr_byte29==1 && j.context.band_byte9==0 && j.context.core_bitmap18==3);
    assert(j.context.tempsense_byte5==0x33 && j.context.common_previous_chanspec==0xd024);
    assert(j.context.bwstate396==0x1000 && j.context.ac_bw_b8==0x1000 && j.context.hardware.phy.phy_core_count==2);
    /* Every event prefix: error, cancellation, coincident error (error wins). */
    for(i=1;i<=n;i++)for(mode=0;mode<3;mode++){
        assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,mode==1?0:i,mode==0?0:i)==i);
        assert(j.phase==(mode==1?AXD_CS_CANCELLED:AXD_CS_FAILED));
        if(mode!=1)assert(j.error==AXD_CS_IO_ERROR || j.error==AXD_CS_CHILD_ERROR);
    }
    /* cca sample with bit1 set omits the conditional 1us delay. */
    o=base;o.cca_sample=0x13;n=expect(&s,&o);assert(!axd_phy_chanspec_begin(&j,&c));
    assert(run(&j,&s,0,0)==n && j.dependency==AXD_CS_MODULE_SETUP_PHY);
    /* Fresh PHY0b sample drives the stored count: 7>>1 under nonzero phymode. */
    o=base;o.core_reply=0xff;o.phymode=1;n=expect(&s,&o);c=cs_context();c.phymode.value=1;
    assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n);
    assert(j.context.hardware.phy.phy_core_count==3 && j.context.core_bitmap18==7 && j.context.tempsense_byte5==0x77);
    /* phymode7 with another current PI repeats the identity-PI stores. */
    o=base;o.phymode=7;n=expect(&s,&o);c=cs_context();c.phymode.value=7;c.shared_current_pi.value=0x1111;
    assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n);
    /* A null current-PI word is only compared, never dereferenced. */
    c.shared_current_pi.value=0;assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n && j.dependency==AXD_CS_MODULE_SETUP_PHY);
    {unsigned stores=j.software_stores;c.shared_current_pi=c.radio.phy_pointer;
        assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n && j.software_stores==stores-1);}
    /* Corestate runs after sync: a one-core sample updates the count without
     * changing the already-executed two-core sync loops. */
    o=base;o.core_reply=1;n=expect(&s,&o);c=cs_context();
    assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n && j.context.hardware.phy.phy_core_count==1);
    /* Optional radar/temperature pointers. */
    o=base;n=expect(&s,&o);c=cs_context();c.radar_pointer.value=0;c.radar_info.known=0;c.temp_pointer.value=0;c.temp_state.known=0;
    assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n && !j.context.radar_indicator && !j.context.tempsense_byte5);
    c=cs_context();c.radar_info.value=0;assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n && !j.context.radar_indicator);
    c=cs_context();c.radar_info.known=0;assert(!axd_phy_chanspec_begin(&j,&c));
    assert(run(&j,&s,0,0)==0 && j.dependency==AXD_CS_METADATA && j.last_field==AXD_CS_RADAR_INFO);
    /* MAC-enabled sample reaches the real PSM suspend; absent MAC context is explicit. */
    o=base;o.mac_enabled=1;n=expect(&s,&o);c=cs_context();
    assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==2 && j.dependency==AXD_CS_MAC_CHILD && j.mac_enabled);
    /* Gates after the bandwidth store. */
    o=base;n=expect(&s,&o);
    {size_t before_cflags=n0-(6+1+6);
        c=cs_context();c.bwinit_slot.value=0;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==before_cflags+6 && j.dependency==AXD_CS_PHY_INIT_FALLBACK && j.context.bwstate396==0x1000);
        c=cs_context();c.bwinit_slot.value=0xf999;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==before_cflags+6 && j.dependency==AXD_CS_RESOURCE_CHILD);
        c=cs_context();c.band_phy_type.value=4;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==before_cflags && j.dependency==AXD_CS_BMAC_PHY_RESET);
        c=cs_context();c.band_phy_pointer.value=0x1004;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==before_cflags && j.phase==AXD_CS_FAILED && j.error==AXD_CS_BINDING);
    }
    /* Module2 dispatches on the explicit radio ID; no ID is inferred. */
    o=base;n=expect(&s,&o);
    c=cs_context();c.radio.radio_id.value=0x2069;assert(!axd_phy_chanspec_begin(&j,&c));
    assert(run(&j,&s,0,0)==n0 && j.dependency==AXD_CS_RADIO2069_SETUP && j.modules_completed==2);
    c=cs_context();c.radio.radio_id.value=0x1234;assert(!axd_phy_chanspec_begin(&j,&c));
    assert(run(&j,&s,0,0)==n0 && j.dependency==AXD_CS_MODULE_SETUP_PHY && j.modules_completed==3);
    c=cs_context();c.radio.radio_id.known=0;assert(!axd_phy_chanspec_begin(&j,&c));
    assert(run(&j,&s,0,0)==n0 && j.dependency==AXD_CS_METADATA && j.last_field==AXD_CS_RADIO_ID);
    /* radio20704 tune-table binding, selection and frequency branches. The
     * prefix through module2's resetcca is n0 plus one resetcca trace. */
    {struct cs_script r={0};size_t nr,ncommon;r.capacity=512;r.events=malloc(r.capacity*sizeof(*r.events));assert(r.events);
        cs_resetcca(&r,0x12);nr=n0+r.count;free(r.events);ncommon=nr+13+13*8;
        o=base;n=expect(&s,&o);
        c=cs_context();c.tune_known=0;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==nr && j.dependency==AXD_CS_METADATA && j.last_field==AXD_CS_TUNE_TABLE);
        c=cs_context();c.tune_table=AXD_CS_TUNE_REV3_63178_5G;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==nr && j.error==AXD_CS_BINDING);
        c=cs_context();c.tune_count=0x48;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==nr && j.error==AXD_CS_BINDING);
        c=cs_context();c.chanspec.value=0xd028;c.bmac_chanspec=c.chanspec;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==nr && j.dependency==AXD_CS_MODULE_SETUP_PHY && j.modules_completed==3 && !j.tune_row);
        c=cs_context();c.sih_package.value=0x87000000u;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==nr && j.dependency==AXD_CS_MODULE_SETUP_PHY);
        c=cs_context();c.radio.radio_revision.value=4;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==nr && j.dependency==AXD_CS_MODULE_SETUP_PHY);
        /* Revisions above3 return before the selector reads the SI package word. */
        c.sih_package.known=0;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==nr && j.dependency==AXD_CS_MODULE_SETUP_PHY);
        c=cs_context();c.radio.radio_revision.value=3;c.tune_table=AXD_CS_TUNE_REV3_63178_5G;c.tune_count=0x48;
        assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n && j.dependency==AXD_CS_MODULE_SETUP_PHY);
        /* 47622 slice A package: same bit13 set for5580MHz, other table identity. */
        c=cs_context();cs_tune_frequency(0x15cc);c.sih_package.value=0x85000000u;c.tune_table=AXD_CS_TUNE_REV0_47622A;
        o=base;n=expect(&s,&o);assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n && j.dependency==AXD_CS_MODULE_SETUP_PHY);
        /* 47622 slice B package with a bitmap frequency (0x15ae): bit13 set. */
        c=cs_context();cs_tune_frequency(0x15ae);c.sih_package.value=0x86000000u;c.tune_table=AXD_CS_TUNE_REV0_47622B;
        o=base;n=expect(&s,&o);assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n && j.dependency==AXD_CS_MODULE_SETUP_PHY);
        c=cs_context();cs_tune_frequency(0x15cc);o=base;o.bit13_clear=1;n=expect(&s,&o);assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==n && j.dependency==AXD_CS_MODULE_SETUP_PHY);
        c=cs_context();cs_tune_frequency(6000);o=base;o.bit13_clear=1;o.twod_clear=1;n=expect(&s,&o);
        assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==n && j.tune_frequency==6000);
        c=cs_context();cs_tune_frequency(2412);assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==ncommon && j.dependency==AXD_CS_RADIO20704_2G);
        c=cs_context();cs_tune_frequency(6300);assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==ncommon && j.dependency==AXD_CS_RADIO20704_6G);
        cs_tune_frequency(5180);
    }
    /* pll_tune: binding gates, the actual mode>2 early return, refusal of a
     * degenerate reference, and the block left equal to the independent table. */
    {size_t npre;unsigned k;o=base;n=expect(&s,&o);npre=n-75u*8u-11u*8u-2u;
        c=cs_context();c.pll_known=0;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==npre && j.dependency==AXD_CS_METADATA && j.last_field==AXD_CS_PLL);
        c=cs_context();c.pll_values=NULL;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==npre && j.phase==AXD_CS_FAILED && j.error==AXD_CS_BINDING);
        /* Mode3: the same trace with exactly the75 PLL radio writes removed. */
        memmove(s.events+npre,s.events+npre+600,(s.count-npre-600)*sizeof(*s.events));s.count-=600;
        c=cs_context();c.pll_config.mode=3;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==n-600 && j.dependency==AXD_CS_MODULE_SETUP_PHY && j.pll_result==1 && !j.pll_writes);
        n=expect(&s,&o);
        for(k=0;k<73;k++)assert(cs_pll_values[k]==(uint16_t)(0x1111u*(k+3)));
        c=cs_context();c.pll_config.reference=1;assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==npre && j.phase==AXD_CS_FAILED && j.error==AXD_CS_BINDING);
        c=cs_context();assert(!axd_phy_chanspec_begin(&j,&c));
        assert(run(&j,&s,0,0)==n && j.pll_writes==73 && j.dependency==AXD_CS_MODULE_SETUP_PHY);
        for(k=0;k<73;k++)assert(cs_pll_values[k]==cs_pll_expected[0].values[k]);
    }
    /* Unknown SI mapping stops at the first actual cflags (inside resetcca). */
    c=cs_context();c.si_known=0;assert(!axd_phy_chanspec_begin(&j,&c));run(&j,&s,0,0);
    assert(j.phase==AXD_CS_DEPENDENCY && j.dependency==AXD_CS_METADATA && j.last_field==AXD_CS_SI_MAPPING);
    /* Required nulls fail; unsupported identity rejected at begin. */
    c=cs_context();c.common_pointer.value=0;assert(!axd_phy_chanspec_begin(&j,&c));assert(run(&j,&s,0,0)==0 && j.error==AXD_CS_BINDING);
    c=cs_context();c.hardware.phy.phy_revision=50;assert(axd_phy_chanspec_begin(&j,&c)<0 && j.error==AXD_CS_UNSUPPORTED);
    c=cs_context();assert(!axd_phy_chanspec_begin(&j,&c));c=j.context;
    assert(!axd_phy_chanspec_begin(&j,&j.context) && !memcmp(&j.context,&c,sizeof(c)));axd_phy_chanspec_cancel(&j);
    assert(axd_phy_chanspec_begin(NULL,&c)<0 && axd_phy_chanspec_step(NULL,NULL,NULL)==AXD_CS_INVALID && axd_phy_chanspec_cancel(NULL)==AXD_CS_INVALID);
    free(s.events);puts("PHY51 chanspec modules0-2: literal sync/resetcca/corestate/bandwidth/radio20704/PLL/VCO traces, gates and callback exits passed");return 0;
}
