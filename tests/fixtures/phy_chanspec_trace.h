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
struct cs_options {unsigned cores,mac_enabled,cca_sample,sync_guard,setup_guard,core_reply,phymode,module2_cca;};
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
/* Module1 (setup_tpc) is a rev>46 return; module2 radio50e0 runs resetcca first. */
static void cs_module2(struct cs_script *s,const struct cs_options *o){cs_resetcca(s,(uint16_t)o->module2_cca);}
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
    return c;
}
#endif
