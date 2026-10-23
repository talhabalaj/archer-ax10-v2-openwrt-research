/* SPDX-License-Identifier: MIT */
#ifndef AXD_TEST_PHY_INIT_TRACE_H
#define AXD_TEST_PHY_INIT_TRACE_H
/* Original independent table/PMU/GPIO oracle shared by both nesting levels.
 * Paths1/2 are the common D11/control transport ABI at those levels. */
struct axd_test_table_event {unsigned path,kind;uint32_t offset,value,reply;};
struct axd_test_table_script {struct axd_test_table_event events[512];size_t count;};
#define N(a) (sizeof(a)/sizeof((a)[0]))
#define D(k,o,v,r) {1,AXD_D11_##k,o,v,r}
#define ST D(STORE_BARRIER,0,0,0)
#define SY D(FULL_BARRIER,0,0,0)
#define H(o,v) D(WRITE16,o,v,0)
#define W(o,v) D(WRITE32,o,v,0)
#define R(o,v) D(READ32,o,0,v)
#define Q(o,v) D(READ16,o,0,v)
#define MOD(s,r,v) ST,H(0x3fc,s),ST,Q(0x3fe,r),SY,H(0x3fe,v)
#define READ(s,r) ST,H(0x3fc,s),Q(0x3fe,r),SY
#define P(s,v) ST,W(0x3fc,((uint32_t)(v)<<16)|(s))
#define C(k,o,v,r) {2,AXD_SPG_##k,o,v,r}
#define CMOD(o,r,v,back) C(READ32,o,0,r),C(FULL_BARRIER,o,0,0),C(STORE_BARRIER,o,0,0),C(WRITE32,o,v,0),C(READ32,o,0,back),C(FULL_BARRIER,o,0,0)
static const struct axd_test_table_event axd_test_table_prefix[]={MOD(0x19e,0xa5a7,0xa5a5),MOD(0x1b0,0x1234,0x9234),READ(0,0xfeed12ab)};
static const struct axd_test_table_event axd_test_table_control[]={CMOD(0x658,0xdeadbeef,0,0x12345678),CMOD(0x65c,0xabcabcde,0xaa8abcde,0x55667788),CMOD(0x6c,0xa5a5f0f3,0xa5a50003,0xdecafbad)};
static const struct axd_test_table_event axd_test_table_middle[]={
    MOD(0x16e,0x4000,0x4002),MOD(0x16e,0x5002,0x5003),MOD(0x16e,0x6003,0x6013),
    P(0x16f,2000),P(0x170,2000),READ(0x19e,0xface5532),
    MOD(0x19e,0xa55a,0xa55a),MOD(0x10f7,0xffff,0xffef),MOD(0x1120,0x123f,0x123b)
};
static const struct axd_test_table_event axd_test_table_tail[]={MOD(0x19e,0x1230,0x1232),MOD(0x400,0xabcf,0xabcd)};
static uint8_t axd_test_table0[32],axd_test_table1[256],axd_test_table2[4];
static void axd_test_table_append(struct axd_test_table_script *s,const struct axd_test_table_event *events,size_t count)
{assert(s->count+count<=N(s->events));memcpy(s->events+s->count,events,count*sizeof(*events));s->count+=count;}
static struct axd_test_table_script axd_test_table_script_build(void)
{
    struct axd_test_table_script s={0};unsigned i;
    const struct axd_test_table_event h0[]={R(0x120,0),SY,READ(0x19e,2),P(0xd,16),P(0xe,0x1000)};
    const struct axd_test_table_event h1[]={R(0x120,0),SY,READ(0x19e,0xaf02),P(0xd,16),P(0xe,0x1040)};
    const struct axd_test_table_event h2[]={R(0x120,0),SY,READ(0x19e,0x7702),P(0xd,16),P(0xe,0x1020)};
    const struct axd_test_table_event d0[]={P(0x10,0x1122),P(0xf,0x3344)};
    const struct axd_test_table_event d1[]={P(0x10,0xa1b2),P(0xf,0xc3d4)};
    const struct axd_test_table_event d2[]={P(0x10,0x5566),P(0xf,0x7788)};
    axd_test_table_append(&s,axd_test_table_prefix,N(axd_test_table_prefix));axd_test_table_append(&s,axd_test_table_control,N(axd_test_table_control));axd_test_table_append(&s,axd_test_table_middle,N(axd_test_table_middle));
    axd_test_table_append(&s,h0,N(h0));for(i=0;i<8;i++)axd_test_table_append(&s,d0,N(d0));
    axd_test_table_append(&s,h1,N(h1));for(i=0;i<64;i++)axd_test_table_append(&s,d1,N(d1));
    axd_test_table_append(&s,h2,N(h2));axd_test_table_append(&s,d2,N(d2));axd_test_table_append(&s,axd_test_table_tail,N(axd_test_table_tail));
    assert(s.count==412);return s;
}
static const struct axd_si_core axd_test_table_catalog[]={{0x800,0x18000000,0x18100000,0},{0x812,0x18200000,0x18300000,0}};
static void axd_test_table_payloads_init(void)
{
    unsigned i;
    for(i=0;i<sizeof(axd_test_table0);i+=4){axd_test_table0[i]=0x44;axd_test_table0[i+1]=0x33;axd_test_table0[i+2]=0x22;axd_test_table0[i+3]=0x11;}
    for(i=0;i<sizeof(axd_test_table1);i+=4){axd_test_table1[i]=0xd4;axd_test_table1[i+1]=0xc3;axd_test_table1[i+2]=0xb2;axd_test_table1[i+3]=0xa1;}
    axd_test_table2[0]=0x88;axd_test_table2[1]=0x77;axd_test_table2[2]=0x66;axd_test_table2[3]=0x55;
}
static struct axd_phy_init_context axd_test_table_context(void)
{
    struct axd_phy_init_context c={0};axd_test_table_payloads_init();
    c.chip_id=63178;c.hardware.phy.core=130;c.hardware.phy.minor=1;
    c.hardware.phy.phy_type=0xb;c.hardware.phy.phy_revision=51;c.hardware.phy.phy_core_count=1;
    c.hardware.phy.bookkeeping=0x1234;c.hardware.write_threshold=32;
    c.hardware.mac.core=130;c.hardware.mac.minor=1;c.hardware.mac_context_valid=1;
    c.hardware.mac.main_control=3;c.hardware.mac.chip_id=63178;
    c.control.si.interconnect=1;c.control.si.core_count=N(axd_test_table_catalog);
    c.control.si.catalog=axd_test_table_catalog;c.control.si.catalog_length=N(axd_test_table_catalog);
    c.control.state.current_index=1;c.control.state.current_map=0x22000000;
    c.control.state.primary_wrapper=0x22100000;
    c.control.state.cache[0].core=0x20000000;c.control.state.cache[1].core=0x22000000;
    c.control.gpio_reservation_known=1;c.control.gpio_reservation=3;
    c.control_state_known=1;
    c.control.chipcommon_revision=34;
    c.otp_word=(struct axd_wake_u32){0x0b00,1};
    c.tables[0]=(struct axd_phy_init_span){axd_test_table0,sizeof(axd_test_table0),1};
    c.tables[1]=(struct axd_phy_init_span){axd_test_table1,sizeof(axd_test_table1),1};
    c.tables[2]=(struct axd_phy_init_span){axd_test_table2,sizeof(axd_test_table2),1};
    return c;
}
#endif
