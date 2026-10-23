/* SPDX-License-Identifier: MIT */
#ifndef AXD_TEST_PHY_NOISE_TRACE_H
#define AXD_TEST_PHY_NOISE_TRACE_H
#include "../../driver/phy_noise.h"
#include <assert.h>
#include <string.h>
/* Original synthetic instruction-derived oracle. Expectations are independent
 * of production output; all physical read samples are supplied explicitly. */
#define AXD_NF_N(a) (sizeof(a)/sizeof((a)[0]))
struct axd_noise_fixture_event {enum axd_d11_kind kind;uint32_t offset,value,reply;};
struct axd_noise_fixture_script {struct axd_noise_fixture_event events[1024];size_t count;};
#define AXD_NF_ST {AXD_D11_STORE_BARRIER,0,0,0}
#define AXD_NF_SY {AXD_D11_FULL_BARRIER,0,0,0}
#define AXD_NF_W(o,v) {AXD_D11_WRITE32,o,v,0}
#define AXD_NF_H(o,v) {AXD_D11_WRITE16,o,v,0}
#define AXD_NF_R(o,v) {AXD_D11_READ32,o,0,v}
#define AXD_NF_Q(o,v) {AXD_D11_READ16,o,0,v}
#define AXD_NF_P(s,v) AXD_NF_ST,AXD_NF_W(0x3fc,((uint32_t)(v)<<16)|(s))
#define AXD_NF_M(s,m,v) AXD_NF_ST,AXD_NF_H(0x3fc,s),AXD_NF_ST,AXD_NF_Q(0x3fe,0xabcdba98),AXD_NF_SY,AXD_NF_H(0x3fe,((0xba98u&~(m))|((v)&(m))))
#define AXD_NF_READ(s,v) AXD_NF_ST,AXD_NF_H(0x3fc,s),AXD_NF_Q(0x3fe,v),AXD_NF_SY
#define AXD_NF_SH(a,v) AXD_NF_ST,AXD_NF_W(0x160,0x10000|((a)>>2)),AXD_NF_R(0x160,0xabcdef13),AXD_NF_SY,AXD_NF_ST,AXD_NF_H(0x164+((a)&2),v)
static const struct axd_noise_fixture_event axd_noise_fixture_hwaci_off[]={AXD_NF_M(0x5a1,0x8000,0),AXD_NF_M(0x5a0,1,0),AXD_NF_M(0x5ab,6,0)};
static const struct axd_noise_fixture_event axd_noise_fixture_hwaci_on[]={
    AXD_NF_M(0x5a0,1,1),AXD_NF_M(0x5a1,0x8000,0x8000),AXD_NF_M(0x5c1,0xc000,0),AXD_NF_M(0x5ab,6,4),
    AXD_NF_M(0x5b4,1,0),AXD_NF_M(0x5ab,0x4000,0x4000),AXD_NF_M(0x5b2,0x40,0),AXD_NF_M(0x5a0,0xf0,0x80),
    AXD_NF_M(0x5a0,0xf00,0x800),AXD_NF_M(0x5a1,0xf,2),AXD_NF_M(0x5a1,0xf0,0x20),AXD_NF_M(0x5a2,0xffff,1),
    AXD_NF_M(0x5a3,0xffff,1),AXD_NF_P(0x5a8,0xe848),AXD_NF_P(0x5a9,1),AXD_NF_M(0x5b2,0x780,0x300),AXD_NF_M(0x5b2,0x7800,0x1800),
    AXD_NF_M(0x5c1,6,2),AXD_NF_M(0x5ab,1,1),AXD_NF_M(0x5ab,0x8000,0x8000),AXD_NF_M(0x5ab,0x1c0,0x140),AXD_NF_M(0x5ab,0x38,0x28),
    AXD_NF_M(0x5cb,3,1),AXD_NF_M(0x5cb,0x30,0x10),AXD_NF_M(0x5cb,0xc,0),AXD_NF_M(0x5cb,0xc0,0x80),AXD_NF_M(0x5cb,0x3800,0x2800),
    AXD_NF_M(0x5cb,0x700,0x500),AXD_NF_M(0x5b3,4,0),AXD_NF_P(0x4718,0x451),AXD_NF_P(0x4719,0xd34),
    AXD_NF_P(0x5740,0x14),AXD_NF_P(0x5741,0),AXD_NF_P(0x5742,10),AXD_NF_P(0x5743,0x14),AXD_NF_P(0x5744,0),AXD_NF_P(0x5745,10),
    AXD_NF_P(0x5746,2),AXD_NF_P(0x5747,0),AXD_NF_P(0x5748,0xff9c),AXD_NF_P(0x5749,2),AXD_NF_P(0x574a,0),AXD_NF_P(0x574b,0xff9c),
    AXD_NF_P(0x4714,0),AXD_NF_P(0x4715,0),AXD_NF_P(0x4716,0),AXD_NF_P(0x4717,0),AXD_NF_P(0x471a,0),AXD_NF_P(0x471b,0),AXD_NF_P(0x471c,0x200),AXD_NF_P(0x471d,0x200),AXD_NF_P(0x471e,0x451),AXD_NF_P(0x471f,0xd34),
    AXD_NF_M(0x5a1,0x8000,0x8000),AXD_NF_M(0x5a0,1,1),AXD_NF_M(0x1092,1,1),AXD_NF_M(0x1092,1,0),AXD_NF_M(0x5ab,0x4000,0x4000)};
static const struct axd_noise_fixture_event axd_noise_fixture_hwaci_shm[]={AXD_NF_ST,AXD_NF_W(0x160,0x100ac),AXD_NF_R(0x160,0x76543210),AXD_NF_SY,AXD_NF_Q(0x166,0x123455a2),AXD_NF_SY,AXD_NF_SH(0x2b2,0x55aa)};
static const struct axd_noise_fixture_event axd_noise_fixture_preempt_off[]={AXD_NF_M(0x141,0x800,0),AXD_NF_M(0x1040,1,0),AXD_NF_P(0x47dc,0),AXD_NF_SH(0x3a2,0),AXD_NF_READ(0x1040,0x12345678)};
static const struct axd_noise_fixture_event axd_noise_fixture_filter_on[]={AXD_NF_P(0x47d0,0xae00),AXD_NF_P(0x47d1,0x4d00),AXD_NF_P(0x47d2,0x1e),AXD_NF_P(0x47d3,0x10),AXD_NF_P(0x47d6,0x1a),AXD_NF_P(0x47d7,0x1c),AXD_NF_P(0x47d8,0x1e),AXD_NF_P(0x47d9,10),
    AXD_NF_P(0x573a,0x2f00),AXD_NF_P(0x5738,0xffff),AXD_NF_P(0x5739,0xffff),AXD_NF_P(0x47d0,28000),AXD_NF_P(0x47d1,40000),AXD_NF_P(0x47d8,0x12),AXD_NF_P(0x47d9,0xe),AXD_NF_P(0x47dc,0x1f),AXD_NF_SH(0x3a2,0x1f)};
static const struct axd_noise_fixture_event axd_noise_fixture_hwobss_off[]={AXD_NF_P(0x1100,0x7000),AXD_NF_P(0x13a,0x3f),AXD_NF_P(0x358,0xc07f),AXD_NF_P(0x359,100),AXD_NF_M(0x35a,0xfff,100),AXD_NF_READ(0x13a,0xface4321),AXD_NF_SH(0x3b2,0x4321)};
static const struct axd_noise_fixture_event axd_noise_fixture_mclip_start[]={AXD_NF_R(0x120,0),AXD_NF_SY,AXD_NF_READ(0x19e,2)};
static const struct axd_noise_fixture_event axd_noise_fixture_table_prefix[]={AXD_NF_R(0x120,0),AXD_NF_SY,AXD_NF_READ(0x19e,2)};
static const struct axd_noise_fixture_event axd_noise_fixture_mclip_restore[]={AXD_NF_M(0x19e,2,2)};
static void axd_noise_fixture_append(struct axd_noise_fixture_script *s,const struct axd_noise_fixture_event *e,size_t n){assert(s->count+n<=AXD_NF_N(s->events));memcpy(s->events+s->count,e,n*sizeof(*e));s->count+=n;}
static struct axd_wake_u32 axd_noise_fixture_known(uint32_t v){const struct axd_wake_u32 r={v,1};return r;}
static struct axd_phy_noise_context axd_noise_fixture_context(unsigned cores)
{
    struct axd_phy_noise_context c={0};c.hardware.phy=(struct axd_phy_context){.core=130,.minor=1,.phy_type=11,.phy_revision=51,.phy_core_count=(uint8_t)cores,.bookkeeping=0x1234};
    c.hardware.shared_bus_word=1;c.hardware.write_threshold=65535;
    c.phy_pointer=axd_noise_fixture_known(0x1000);c.ac_pointer=axd_noise_fixture_known(0x2000);c.common_pointer=axd_noise_fixture_known(0x3000);c.data_pointer=axd_noise_fixture_known(0x4000);
    c.chanspec=axd_noise_fixture_known(0xd024);c.current_channel=axd_noise_fixture_known(0x5000);c.shared_mode=axd_noise_fixture_known(0x20);c.postfilter_shm=axd_noise_fixture_known(0x3a2);
    c.hwaci_2g_pointer=axd_noise_fixture_known(0x6000);c.hwaci_5g_pointer=axd_noise_fixture_known(0x7000);c.hwaci_2g_flag=(struct axd_wake_u8){1,0};c.hwaci_5g_flag=(struct axd_wake_u8){1,0x80};
    c.hwaci_adc_flag=(struct axd_wake_u8){1,0};c.hwaci_shm_offset=axd_noise_fixture_known(0x2b2);c.phy_depth=(struct axd_wake_u8){1,1};
    memset(c.hwaci_2g,0xaa,sizeof(c.hwaci_2g));memset(c.hwaci_5g,0xbb,sizeof(c.hwaci_5g));c.preempt_byte10=0xcc;c.preempt_halfe=0xdddd;
    c.channel.object=(struct axd_object_context){130,1,1};c.channel.hwobss_shm_offset=axd_noise_fixture_known(0x3b2);
    c.radio.phy_pointer=c.phy_pointer;c.radio.public_pointer=axd_noise_fixture_known(0x8000);c.radio.shared_pointer=axd_noise_fixture_known(0x9000);c.radio.radio_id=axd_noise_fixture_known(0x50e0);c.radio.radio_read_offset=axd_noise_fixture_known(0x8000);
    c.radio.shared_bus_revision=axd_noise_fixture_known(4);return c;
}
static void axd_noise_fixture_preempt_on(struct axd_noise_fixture_script *s,unsigned cores)
{
    const struct axd_noise_fixture_event a[]={AXD_NF_P(0x1040,0xf041)},b[]={AXD_NF_P(0x141,0xa00),AXD_NF_P(0x1041,0x2fff),AXD_NF_P(0x15e,0x3840),AXD_NF_P(0x1043,0x18),AXD_NF_READ(0x19e,0xfacea5a7),AXD_NF_M(0x19e,2,2)};
    const struct axd_noise_fixture_event tail[]={AXD_NF_M(0x19e,2,2),AXD_NF_READ(0x1040,0x12345678)};unsigned k;
    axd_noise_fixture_append(s,a,AXD_NF_N(a));for(k=0;k<cores;k++){const struct axd_noise_fixture_event e[]={AXD_NF_M(0x6f8+k*0x200,0x3ff,0x1df)};axd_noise_fixture_append(s,e,AXD_NF_N(e));}
    axd_noise_fixture_append(s,b,AXD_NF_N(b));axd_noise_fixture_append(s,axd_noise_fixture_filter_on,AXD_NF_N(axd_noise_fixture_filter_on));axd_noise_fixture_append(s,tail,AXD_NF_N(tail));
}
static void axd_noise_fixture_elna(struct axd_noise_fixture_script *s,unsigned cores,unsigned enabled)
{
    static const uint16_t selectors[]={0x75,0x75,0x75,0x70,0x76,0x70},masks[]={0x10,0x20,0x40,0x40,0x400,0x780};unsigned k,n;
    for(k=0;k<cores;k++)for(n=0;n<(enabled?6u:5u);n++){
        unsigned sel=selectors[n]+k*0x200;unsigned value=n==5?0x80:enabled?masks[n]:0;
        const struct axd_noise_fixture_event e[]={AXD_NF_ST,AXD_NF_H(0x3d8,sel|0x8000),AXD_NF_Q(0x3da,0xfacea5a5),AXD_NF_SY,AXD_NF_ST,AXD_NF_H(0x3d8,sel),AXD_NF_ST,AXD_NF_H(0x3da,(0xa5a5u&~masks[n])|(value&masks[n]))};axd_noise_fixture_append(s,e,AXD_NF_N(e));
    }
}
static void axd_noise_fixture_mclip(struct axd_noise_fixture_script *s,unsigned cores,unsigned enabled,unsigned mismatch)
{
    unsigned k,n;axd_noise_fixture_append(s,axd_noise_fixture_mclip_start,AXD_NF_N(axd_noise_fixture_mclip_start));
    for(k=0;k<cores;k++){
        const struct axd_noise_fixture_event h[]={AXD_NF_P(0xd,0x144+k*0x20),AXD_NF_P(0xe,8)};axd_noise_fixture_append(s,axd_noise_fixture_table_prefix,AXD_NF_N(axd_noise_fixture_table_prefix));axd_noise_fixture_append(s,h,AXD_NF_N(h));
        for(n=0;n<6;n++){unsigned value=enabled && n>=4 && !mismatch?100:0;const struct axd_noise_fixture_event e[]={AXD_NF_READ(0xf,0xfacebe00|value)};axd_noise_fixture_append(s,e,AXD_NF_N(e));}
        if(mismatch){axd_noise_fixture_append(s,axd_noise_fixture_table_prefix,AXD_NF_N(axd_noise_fixture_table_prefix));axd_noise_fixture_append(s,h,AXD_NF_N(h));for(n=0;n<6;n++){unsigned value=enabled && n>=4?100:0;const struct axd_noise_fixture_event e[]={AXD_NF_P(0xf,value)};axd_noise_fixture_append(s,e,AXD_NF_N(e));}}
    }
    axd_noise_fixture_append(s,axd_noise_fixture_mclip_restore,AXD_NF_N(axd_noise_fixture_mclip_restore));
}
static struct axd_noise_fixture_script axd_noise_fixture_full(unsigned cores,unsigned enabled,unsigned optional)
{
    struct axd_noise_fixture_script s={0};
    if(optional)axd_noise_fixture_append(&s,axd_noise_fixture_hwaci_shm,AXD_NF_N(axd_noise_fixture_hwaci_shm));axd_noise_fixture_append(&s,enabled?axd_noise_fixture_hwaci_on:axd_noise_fixture_hwaci_off,enabled?AXD_NF_N(axd_noise_fixture_hwaci_on):AXD_NF_N(axd_noise_fixture_hwaci_off));
    if(enabled)axd_noise_fixture_preempt_on(&s,cores);else axd_noise_fixture_append(&s,axd_noise_fixture_preempt_off,AXD_NF_N(axd_noise_fixture_preempt_off));
    axd_noise_fixture_append(&s,axd_noise_fixture_hwobss_off,AXD_NF_N(axd_noise_fixture_hwobss_off));axd_noise_fixture_elna(&s,cores,enabled);axd_noise_fixture_mclip(&s,cores,enabled,enabled);return s;
}
#endif
