/* SPDX-License-Identifier: MIT */
#ifndef AXD_TEST_PHY_CHANSPEC_TRACE_H
#define AXD_TEST_PHY_CHANSPEC_TRACE_H
#include <assert.h>
#include <stddef.h>
#include "../../driver/phy_chanspec.h"
/* Original synthetic source-ordered oracle for callback13 module0. Every
 * expectation is written from recovered instruction order and immediates;
 * production output never generates it. Replies are explicit and fresh. */
struct cs_event {enum axd_phy_chanspec_path path;unsigned kind;uint32_t offset,value,reply;};
struct cs_script {struct cs_event *events;size_t count,capacity;};
struct cs_options {unsigned cores,mac_enabled,cca_sample,sync_guard,setup_guard,core_reply,phymode,module2_cca,bit13_clear,twod_clear;};
static void cs_add(struct cs_script *s,enum axd_phy_chanspec_path p,unsigned k,uint32_t o,uint32_t v,uint32_t r)
{assert(s->count<s->capacity);s->events[s->count++]=(struct cs_event){p,k,o,v,r};}
static void cs_d(struct cs_script *s,enum axd_d11_kind k,uint32_t o,uint32_t v,uint32_t r){cs_add(s,AXD_CS_D11,k,o,v,r);}
static void cs_st(struct cs_script *s){cs_d(s,AXD_D11_STORE_BARRIER,0,0,0);}
static void cs_sy(struct cs_script *s){cs_d(s,AXD_D11_FULL_BARRIER,0,0,0);}
static void cs_delay(struct cs_script *s,uint32_t us){cs_d(s,AXD_D11_DELAY_US,0,us,0);}
static void cs_read(struct cs_script *s,uint16_t sel,uint32_t sample)
{cs_st(s);cs_d(s,AXD_D11_WRITE16,0x3fc,sel,0);cs_d(s,AXD_D11_READ16,0x3fe,0,sample);cs_sy(s);}
static void cs_modify(struct cs_script *s,uint16_t sel,uint16_t mask,uint16_t value)
{
    cs_st(s);cs_d(s,AXD_D11_WRITE16,0x3fc,sel,0);cs_st(s);
    cs_d(s,AXD_D11_READ16,0x3fe,0,0x1a5a5);cs_sy(s);
    cs_d(s,AXD_D11_WRITE16,0x3fe,(0xa5a5u&~mask)|(value&mask),0);
}
static void cs_radio(struct cs_script *s,uint16_t sel,uint16_t mask,uint16_t value)
{
    if(sel==0xffff)return;
    cs_st(s);cs_d(s,AXD_D11_WRITE16,0x3d8,sel|0x8000u,0);cs_d(s,AXD_D11_READ16,0x3da,0,0x1a5a5);cs_sy(s);
    cs_st(s);cs_d(s,AXD_D11_WRITE16,0x3d8,sel,0);cs_st(s);
    cs_d(s,AXD_D11_WRITE16,0x3da,(0xa5a5u&~mask)|(value&mask),0);
}
/* Actual si_core_cflags RMW on the selected wrapper's word408. */
static void cs_cflags(struct cs_script *s,uint32_t mask,uint32_t value)
{
    const uint32_t old=0x13572468u;
    cs_add(s,AXD_CS_SI,AXD_SICO_READ32,0x408,0,old);cs_add(s,AXD_CS_SI,AXD_SICO_FULL_BARRIER,0,0,0);
    cs_add(s,AXD_CS_SI,AXD_SICO_STORE_BARRIER,0,0,0);cs_add(s,AXD_CS_SI,AXD_SICO_WRITE32,0x408,(old&~mask)|value,0);
    cs_add(s,AXD_CS_SI,AXD_SICO_READ32,0x408,0,0x2468ace0u);cs_add(s,AXD_CS_SI,AXD_SICO_FULL_BARRIER,0,0,0);
}
static void cs_resetcca(struct cs_script *s,uint16_t sample)
{
    cs_d(s,AXD_D11_READ16,0x7c0,0,sample);cs_sy(s);
    if(!(sample&2))cs_delay(s,1);
    cs_cflags(s,2,2);cs_delay(s,1);cs_st(s);cs_d(s,AXD_D11_WRITE16,0x7c0,6,0);cs_delay(s,1);
    cs_modify(s,1,0x4000,0x4000);cs_delay(s,1);cs_modify(s,1,0x4000,0);cs_delay(s,10);
    cs_st(s);cs_d(s,AXD_D11_WRITE16,0x7c0,2,0);cs_cflags(s,2,0);cs_delay(s,2);
    cs_st(s);cs_d(s,AXD_D11_WRITE16,0x7c0,sample,0);cs_modify(s,0x400,2,0);
}
/* Module0 through its final RMW, for chanspec0xd024 (5GHz, BW1000). */
static void cs_module0(struct cs_script *s,const struct cs_options *o)
{
    static const uint16_t a[3]={1,0x200,0x40},b[3]={0x100,0x400,0x800};
    static const uint16_t r173[6]={0x10,0x20,0x80,0x400,0x200,0x800},radio[6]={2,0x200,1,0x400,0x100,0x80};
    unsigned c,k;
    cs_d(s,AXD_D11_READ32,0x120,0,o->mac_enabled?1u:0x400u);cs_sy(s);
    for(c=0;c<o->cores;c++)for(k=0;k<3;k++)cs_modify(s,(uint16_t)(0x725+c*0x200),a[k],0);
    for(c=0;c<o->cores;c++)for(k=0;k<3;k++)cs_modify(s,(uint16_t)(0x601+c*0x200),b[k],0);
    for(k=0;k<6;k++)cs_modify(s,0x173,r173[k],0);
    for(c=0;c<o->cores;c++){cs_modify(s,(uint16_t)(0x601+c*0x200),0x1000,0x1000);cs_modify(s,(uint16_t)(0x601+c*0x200),0x80,0x80);}
    cs_modify(s,0x173,0x40,0);cs_modify(s,0x46a,0x1e,0x1e);
    cs_read(s,0x19e,o->sync_guard);cs_modify(s,0x19e,2,2);cs_resetcca(s,(uint16_t)o->cca_sample);
    cs_modify(s,0x19e,2,(uint16_t)(o->sync_guard&2u));
    for(c=0;c<o->cores;c++)for(k=0;k<6;k++)cs_radio(s,c==0?0x88:c==1?0x288:0xffff,radio[k],0);
    cs_modify(s,0x16b,0x1e0,0);
    cs_read(s,0x19e,o->setup_guard);cs_modify(s,0x19e,1,1);
    cs_read(s,0xb,o->core_reply);
    cs_cflags(s,0x8c0,0x40);cs_delay(s,2);cs_modify(s,3,0x100,0x100);
}
/* Original synthetic tune table: 86 rows (rev0 count), arbitrary channels and
 * per-channel bytes; never the vendor table. Row20 is channel36 at 5180MHz. */
static uint8_t cs_tune_rows[0x56*16];
static inline void cs_tune_frequency(unsigned mhz){cs_tune_rows[16*20+2]=(uint8_t)mhz;cs_tune_rows[16*20+3]=(uint8_t)(mhz>>8);}
static void cs_tune_build(void)
{
    unsigned r,k;
    for(r=0;r<0x56;r++){
        unsigned ch=r==20?36:200+r,mhz=r==20?5180:4000+r;
        cs_tune_rows[16*r]=(uint8_t)ch;cs_tune_rows[16*r+1]=(uint8_t)(ch>>8);
        cs_tune_rows[16*r+2]=(uint8_t)mhz;cs_tune_rows[16*r+3]=(uint8_t)(mhz>>8);
        for(k=4;k<16;k++)cs_tune_rows[16*r+k]=(uint8_t)(0x31+k*7+r);
    }
}
/* Generated from the independent Python transcription in check_radio20704_pll.py */
/* (reference 54000000, shift0, mode0, initial block (0x1111*(k+3))&0xffff). */
static const struct {uint16_t mhz; uint16_t values[73];} cs_pll_expected[]={
    {5180,{0x3333,0x4444,0x5555,0x6666,0x7777,0x8888,0x9999,0xaaaa,0xbbbb,0xcccc,0xdddd,0x9,0xffff,0x18e,0x5,0x2ee,0x3ee,0xffce,0x32,0x7776,0x1,0x9998,0xaaa9,0xbbba,0xcccb,0xdddc,0xeeed,0xfffe,0x14c4,0x2220,0x3331,0x4442,0x5553,0x6664,0x7775,0x143c,0x0,0x0,0x96e0,0xe127,0x9,0xa,0x1,0x3,0x5,0xba,0xba,0xff,0xd1,0x1d,0x1,0x1,0x4,0x4,0x41,0x0,0x1,0x0,0x0,0x221e,0x332f,0x3,0x5551,0x6662,0x7773,0x28,0x9995,0x1,0xfff,0xccc8,0xddd9,0xeeea,0xfffb}},
    {5580,{0x3333,0x4444,0x5555,0x6666,0x7777,0x8888,0x9999,0xaaaa,0xbbbb,0xcccc,0xdddd,0xa,0xffff,0x124,0x8,0x514,0x614,0xffce,0x32,0x7776,0x1,0x9998,0xaaa9,0xbbba,0xcccb,0xdddc,0xeeed,0xfffe,0x170f,0x2220,0x3331,0x4442,0x5553,0x6664,0x7775,0x15cc,0x0,0x0,0x9078,0x70cb,0x9,0xa,0x1,0x3,0x5,0xcb,0xcb,0xff,0xe4,0x35,0x1a,0x1a,0x1d,0x4,0x41,0x0,0x1,0x0,0x0,0x221e,0x332f,0x3,0x5551,0x6662,0x7773,0x28,0x9995,0x1,0xfff,0xccc8,0xddd9,0xeeea,0xfffb}},
    {5550,{0x3333,0x4444,0x5555,0x6666,0x7777,0x8888,0x9999,0xaaaa,0xbbbb,0xcccc,0xdddd,0xa,0xffff,0x124,0x8,0x514,0x614,0xffce,0x32,0x7776,0x1,0x9998,0xaaa9,0xbbba,0xcccb,0xdddc,0xeeed,0xfffe,0x170f,0x2220,0x3331,0x4442,0x5553,0x6664,0x7775,0x15ae,0x0,0x0,0x8fb1,0x997a,0x9,0xa,0x1,0x3,0x5,0xcd,0xcd,0xff,0xe6,0x2d,0x12,0x12,0x15,0x4,0x41,0x0,0x1,0x0,0x0,0x221e,0x332f,0x3,0x5551,0x6662,0x7773,0x28,0x9995,0x1,0xfff,0xccc8,0xddd9,0xeeea,0xfffb}},
    {6000,{0x3333,0x4444,0x5555,0x6666,0x7777,0x8888,0x9999,0xaaaa,0xbbbb,0xcccc,0xdddd,0xa,0xffff,0x124,0x8,0x190,0x290,0xffce,0x32,0x7776,0x1,0x9998,0xaaa9,0xbbba,0xcccb,0xdddc,0xeeed,0xfffe,0x18ad,0x2220,0x3331,0x4442,0x5553,0x6664,0x7775,0x1770,0x0,0x0,0x9b58,0x3738,0x9,0xa,0x1,0x3,0x5,0xaf,0xaf,0xff,0xc5,0x37,0x1c,0x1c,0x1f,0x4,0x41,0x0,0x1,0x0,0x0,0x221e,0x332f,0x3,0x5551,0x6662,0x7773,0x28,0x9995,0x1,0xfff,0xccc8,0xddd9,0xeeea,0xfffb}},
};
/* Original synthetic PLL field descriptors and caller-owned value block. */
static uint16_t cs_pll_values[73],cs_pll_selectors[73],cs_pll_masks[73];
static uint8_t cs_pll_shifts[73];
static void cs_pll_build(void)
{
    unsigned k;
    for(k=0;k<73;k++){cs_pll_values[k]=(uint16_t)(0x1111u*(k+3));cs_pll_selectors[k]=(uint16_t)(0x500+k);
        cs_pll_masks[k]=k%3?0x0ff0:0xffff;cs_pll_shifts[k]=(uint8_t)(k%5);}
}
static void cs_pll_writes(struct cs_script *s,unsigned mhz)
{
    unsigned e,k;
    for(e=0;e<sizeof(cs_pll_expected)/sizeof(cs_pll_expected[0]) && cs_pll_expected[e].mhz!=mhz;e++);
    assert(e<sizeof(cs_pll_expected)/sizeof(cs_pll_expected[0]));
    for(k=0;k<73;k++)cs_radio(s,(uint16_t)(0x500+k),k%3?0x0ff0:0xffff,(uint16_t)(cs_pll_expected[e].values[k]<<(k%5)));
    cs_radio(s,0x9f6,0x4000,0x4000);cs_radio(s,0x9f6,0x8000,0x8000);
    /* VCO calibration (20704 entry tail-calls the 20709 body). */
    cs_radio(s,0x9f9,0x8000,0x8000);cs_radio(s,0x841,0x300,0x300);cs_radio(s,0x84f,0x7ff,0);cs_radio(s,0x85c,1,1);
    cs_radio(s,0x841,0x40,0x40);cs_radio(s,0x841,0xc00,0xc00);cs_radio(s,0x841,1,1);cs_radio(s,0x841,0x80,0);
    cs_delay(s,10);cs_radio(s,0x841,0x80,0x80);cs_radio(s,0x82a,1,0);cs_delay(s,10);cs_radio(s,0x82a,1,1);
}
/* Module1 (setup_tpc) is a rev>46 return. Module2 radio50e0: resetcca, then
 * radio20704 5GHz: LOGEN pulse, band bytes, 88a/9f6, bit13, per-core rows,
 * tail and 2d, written from recovered immediates, then the PLL field writes
 * from the independent Python value tables. Stops at VCO calibration. */
static void cs_module2(struct cs_script *s,const struct cs_options *o)
{
    static const uint16_t band[11][2]={{0x9f5,0xf},{0x1f4,0xf},{0x3f4,0xf},{0x6e,0xf000},{0x26e,0xf000},
        {0x73,6},{0x273,6},{0x2d,0xf},{0x22d,0xf},{0x1c6,0x78},{0x3c6,0x78}};
    static const unsigned shift[11]={0,0,0,12,12,1,1,0,0,3,3};
    static const uint16_t core0[25][3]={
        {0x1f1,1,0},{0x1f1,0x200,0},{0x1f6,0x400,0x400},{0x1f1,0x100,0},{0x1f6,0x800,0x800},
        {0x1f2,0x100,0x100},{0x1f2,0x400,0x400},{0x1f1,0x400,0},{0x74,0x100,0},{0x74,0x4000,0x4000},
        {0x73,1,1},{0x2e,3,0},{0x2e,4,0},{0x29,1,1},{0x1df,0x1000,0},{0x18,8,8},{0x1c6,4,4},
        {0x29,4,4},{0x1c6,2,0},{0x2d,0x20,0},{0x1c6,1,0},{0x1c9,0x38,0x28},{0x1c9,0x7000,0x5000},
        {0x1c9,7,5},{0x1f3,0x1c00,0xc00}};
    const uint8_t *row=cs_tune_rows+16*20;unsigned k,c;
    /* Module2 loops use the count phycorestate recomputed from the PHY0b reply. */
    const unsigned cores=(o->core_reply&7u)>>((o->phymode&0xfff7u)!=0);
    cs_resetcca(s,(uint16_t)o->module2_cca);
    cs_modify(s,0x728,0x100,0x100);cs_delay(s,1);cs_modify(s,0x728,0x100,0);
    for(k=0;k<11;k++)cs_radio(s,band[k][0],band[k][1],(uint16_t)((unsigned)row[4+k]<<shift[k]));
    cs_radio(s,0x88a,0x80,0x80);cs_radio(s,0x9f6,1,0);cs_radio(s,0x9f6,0x2000,o->bit13_clear?0:0x2000);
    for(c=0;c<cores;c++)for(k=0;k<25;k++)cs_radio(s,c<2?(uint16_t)(core0[k][0]+0x200*c):0xffff,core0[k][1],core0[k][2]);
    cs_radio(s,0x88a,0x38,0x18);cs_radio(s,0x88a,0xe000,0);cs_radio(s,0x9f6,0x4000,0x4000);cs_radio(s,0x9f5,0xe000,0xa000);
    for(c=0;c<cores;c++)cs_radio(s,c==0?0x2d:c==1?0x22d:0xffff,0x10,o->twod_clear?0:0x10);
    cs_pll_writes(s,(unsigned)(row[2]|(row[3]<<8)));
}
static struct axd_phy_chanspec_context cs_context(void)
{
    struct axd_phy_chanspec_context c={0};struct axd_radio_control_context *a=&c.radio;
    c.hardware.phy=(struct axd_phy_context){.core=130,.minor=1,.phy_type=0xb,.phy_revision=51,.phy_core_count=2,.bookkeeping=42};
    a->phy_pointer=(struct axd_wake_u32){0x1000,1};a->ac_pointer=(struct axd_wake_u32){0x7000,1};
    a->public_pointer=(struct axd_wake_u32){0x2000,1};a->shared_pointer=(struct axd_wake_u32){0x3000,1};
    a->chanmgr_pointer=(struct axd_wake_u32){0x8000,1};a->chanmgr_data_pointer=(struct axd_wake_u32){0x9000,1};
    a->radio_read_offset=(struct axd_wake_u32){0x8000,1};a->chip_id=(struct axd_wake_u32){63178,1};
    a->radio_id=(struct axd_wake_u32){0x50e0,1};a->radio_revision=(struct axd_wake_u32){0,1};
    c.si=(struct axd_si_core_context){1,0x63178,0x812,0x12340000,1};c.si_known=1;
    c.chanspec=(struct axd_wake_u32){0xd024,1};c.common_pointer=(struct axd_wake_u32){0xc000,1};
    c.radar_pointer=(struct axd_wake_u32){0xd000,1};c.radar_info=(struct axd_wake_u32){0xe000,1};
    c.band_phy_pointer=a->phy_pointer;c.band_phy_type=(struct axd_wake_u32){11,1};
    c.bmac_chanspec=(struct axd_wake_u32){0xd024,1};
    c.bwinit_slot=(struct axd_wake_u32){0xf100,1};c.ac_bwinit_label=(struct axd_wake_u32){0xf100,1};
    c.phymode=(struct axd_wake_u32){0,1};c.shared_current_pi=a->phy_pointer;
    c.temp_pointer=(struct axd_wake_u32){0xf200,1};c.temp_state=(struct axd_wake_u32){0xf300,1};
    cs_tune_build();c.sih_package=(struct axd_wake_u32){0x84000000u,1};
    c.tune_rows=cs_tune_rows;c.tune_count=0x56;c.tune_table=AXD_CS_TUNE_REV0_63178;c.tune_known=1;
    cs_pll_build();c.pll_config=(struct axd_radio20704_pll_config){54000000u,0,0};c.pll_known=1;
    c.pll_selectors=cs_pll_selectors;c.pll_masks=cs_pll_masks;c.pll_shifts=cs_pll_shifts;c.pll_values=cs_pll_values;
    return c;
}
#endif
