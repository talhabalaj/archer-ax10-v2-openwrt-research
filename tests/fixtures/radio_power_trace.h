/* SPDX-License-Identifier: MIT */
#ifndef AXD_TEST_RADIO_POWER_TRACE_H
#define AXD_TEST_RADIO_POWER_TRACE_H
/* Original synthetic source-ordered oracle, shared by the focused fault suite
 * and full-vector probe. Production output never generates expectations.
 * Explicit50e0 is a scenario input, not a claim about the physical board. */
struct event {enum axd_d11_kind kind;uint32_t offset,value,reply;};
struct script {struct event *events;size_t count,capacity,calibration,cal0,cal1;};
static const uint8_t mode_data[]={1,2,3,0x55,4,5,6,7,8,9,0xaa,0xbb,11,12,13,14,15,16};
static const uint8_t rccal_data[]={0x12,0x34,0xaa,0xbb,0x56,7,0x89,10};
static const uint8_t prefregs[]={0xff,0xff};
static void add(struct script *s,enum axd_d11_kind k,uint32_t o,uint32_t v,uint32_t r)
{assert(s->count<s->capacity);s->events[s->count++]=(struct event){k,o,v,r};}
static void st(struct script *s){add(s,AXD_D11_STORE_BARRIER,0,0,0);}
static void sy(struct script *s){add(s,AXD_D11_FULL_BARRIER,0,0,0);}
static void delay(struct script *s,uint32_t us){add(s,AXD_D11_DELAY_US,0,us,0);}
static void packed(struct script *s,uint16_t sel,uint16_t val)
{st(s);add(s,AXD_D11_WRITE32,0x3fc,sel|((uint32_t)val<<16),0);}
static void read_radio(struct script *s,uint16_t sel,uint32_t sample)
{st(s);add(s,AXD_D11_WRITE16,0x3d8,sel|0x8000u,0);add(s,AXD_D11_READ16,0x3da,0,sample);sy(s);}
static void modify(struct script *s,uint16_t sel,uint16_t mask,uint16_t value)
{
    /* An explicit independent fresh reply for every actual read. The expected
     * data sample never derives from a preceding model write. */
    if(sel==0xffff)return;
    read_radio(s,sel,0x1a5a5);st(s);add(s,AXD_D11_WRITE16,0x3d8,sel,0);st(s);
    add(s,AXD_D11_WRITE16,0x3da,(0xa5a5u&~mask)|(value&mask),0);
}
static void logen(struct script *s,unsigned mode)
{
    unsigned a=1+mode,b=4+mode,c=7+mode,d=11+mode,e=14+mode;
    modify(s,0x88c,4,(uint16_t)(a<<2));modify(s,0x9f6,0x4000,(uint16_t)(a<<14));
    modify(s,0x88a,1,(uint16_t)a);modify(s,0x88a,0x100,(uint16_t)(a<<8));modify(s,0x88a,2,(uint16_t)(a<<1));modify(s,0x88a,4,0);
    modify(s,0x1f1,0x4000,(uint16_t)(b<<14));modify(s,0x1f1,0x8000,(uint16_t)(b<<15));modify(s,0x1f1,2,(uint16_t)(b<<1));modify(s,0x1f1,0x2000,(uint16_t)(b<<13));
    modify(s,0x1f2,1,(uint16_t)d);modify(s,0x1f2,0x40,(uint16_t)(b<<6));modify(s,0x1f2,0x800,(uint16_t)(b<<11));modify(s,0x1f2,0x1000,(uint16_t)(b<<12));
    modify(s,0x3f1,0x4000,(uint16_t)(c<<14));modify(s,0x3f1,0x8000,(uint16_t)(c<<15));modify(s,0x3f1,2,(uint16_t)(c<<1));modify(s,0x3f1,0x2000,(uint16_t)(c<<13));
    modify(s,0x3f2,1,(uint16_t)e);modify(s,0x3f2,0x40,(uint16_t)(c<<6));modify(s,0x3f2,0x800,(uint16_t)(c<<11));modify(s,0x3f2,0x1000,(uint16_t)(c<<12));
}
static void prefix(struct script *s)
{
    packed(s,0x4728,0);packed(s,0x4720,0x3ff);packed(s,0x415,0);packed(s,0x40e,0);packed(s,0x40c,0x2000);
    packed(s,0x408,0);packed(s,0x417,0);packed(s,0x416,0xd);packed(s,0x408,7);delay(s,1000);packed(s,0x408,6);modify(s,0x88b,0x3f,1);
}
static void full(struct script *s,unsigned cores,unsigned timeout)
{
    unsigned i;uint16_t rcal=timeout?50:32,cal=timeout?0x10a4:55928;
    prefix(s);logen(s,0);
    for(i=0;i<cores;i++){uint16_t q=i<2?(uint16_t)(i?0x3f2:0x1f2):0xffff;modify(s,q,0x20,0x20);modify(s,q,0x200,0x200);modify(s,i<2?(uint16_t)(i?0x2a3:0xa3):0xffff,0x8000,0x8000);}
    packed(s,0x417,0xd);packed(s,0x408,2);packed(s,0x4728,0x180);delay(s,1000);packed(s,0x417,4);packed(s,0x4728,0x80);packed(s,0x4720,0x180);
    modify(s,0x884,8,8);modify(s,0x9fa,0x100,0x100);modify(s,0x9fa,0x1e00,0x1000);delay(s,1);
    modify(s,0x80e,0xfc0,0);modify(s,0x80e,3,1);modify(s,0x80d,0x780,0);modify(s,0x80d,0x40,0);modify(s,0x80d,0x30,0);modify(s,0x80d,0xc,0);modify(s,0x80d,2,0);modify(s,0x80d,1,0);
    modify(s,0x80f,3,0);modify(s,0x80f,0x300,0x100);modify(s,0x9f9,0x2000,0x2000);modify(s,0x9f9,0x4000,0x4000);
    modify(s,0x83c,0x3f,50);modify(s,0x815,8,0);modify(s,0x83d,0x7f,50);modify(s,0x815,0x10,0x10);
    modify(s,0x9f9,0x4000,0);modify(s,0x9f9,0x2000,0);modify(s,0x88b,0x3f,1);modify(s,0x9f9,0x2000,0x2000);
    modify(s,0x87b,0x40,0);modify(s,0x87b,8,0);modify(s,0x87b,2,2);modify(s,0x87b,0x20,0x20);modify(s,0x815,0x40,0x40);
    delay(s,30);modify(s,0x812,0x4000,0x4000);modify(s,0x812,0x4000,0);delay(s,30);modify(s,0x87b,1,1);delay(s,25);
    for(i=0;i<(timeout?101u:1u);i++){read_radio(s,0x882,timeout?0:0x40);if(timeout)delay(s,100);}
    for(i=0;i<cores;i++){modify(s,i<2?(uint16_t)(i?0x256:0x56):0xffff,1,1);modify(s,i<2?(uint16_t)(i?0x26a:0x6a):0xffff,0x80,0x80);}
    if(!timeout){read_radio(s,0x882,0x2a);modify(s,0x87a,0x3f,0x2a);}
    modify(s,0x87b,0x40,0x40);modify(s,0x87b,8,8);modify(s,0x87b,2,0);modify(s,0x87b,1,0);modify(s,0x9f9,0x2000,0);modify(s,0x9f9,0x2000,0x2000);modify(s,0x9f9,0x4000,0x4000);
    modify(s,0x231,0x100,0x100);modify(s,0x3e9,1,1);modify(s,0x813,0x80,0x80);modify(s,0x87c,1,1);
    for(i=0;i<(timeout?101u:1u);i++){delay(s,1000);read_radio(s,0x87c,timeout?0:2);}
    if(!timeout)read_radio(s,0x87c,0x80);
    modify(s,0x83d,0x7f,rcal);modify(s,0x815,8,0);modify(s,0x83c,0x3f,rcal);modify(s,0x815,0x10,0x10);
    modify(s,0x231,0x100,0);modify(s,0x3e9,1,0);modify(s,0x813,0x80,0);modify(s,0x87c,1,0);modify(s,0x9f9,0x4000,0);modify(s,0x9f9,0x2000,0);
    s->calibration=s->count;modify(s,0x9f9,0x2000,0x2000);
    for(i=0;i<2;i++){
        unsigned k;
        modify(s,0x2ee,0x30,0x10);modify(s,0x2f0,0x40,i?0:0x40);modify(s,0x2f0,0x30,i?0x10:0);
        modify(s,0x2ee,0xff00,(uint16_t)(i?0x3400:0x1200));modify(s,0x2ef,0x1fff,(uint16_t)(i?0xa89:0x756));
        modify(s,0x2f0,1,0);delay(s,1);modify(s,0x2f0,1,1);delay(s,35);modify(s,0x2ee,1,0);modify(s,0x2ee,1,1);
        for(k=0;k<(timeout?10000u:1u);k++){delay(s,100);read_radio(s,0x2f3,timeout?0:1);}
        modify(s,0x2ee,1,0);read_radio(s,0x2f4,i?10:0x1ffe);read_radio(s,0x2f2,i?30:8);
        if(i)s->cal1=s->count;else s->cal0=s->count;
        modify(s,0x2f0,1,0);
    }
    for(i=0;i<cores;i++){modify(s,i<2?(uint16_t)(i?0x285:0x85):0xffff,0xffff,cal);modify(s,i<2?(uint16_t)(i?0x249:0x49):0xffff,0xffff,cal);modify(s,i<2?(uint16_t)(i?0x24a:0x4a):0xffff,0xffff,cal);}
    modify(s,0x9f9,0x2000,0);
    modify(s,0x83e,0x20,0x20);modify(s,0x83e,0x10,0x10);modify(s,0x837,0x20,0x20);modify(s,0x837,0x10,0x10);
    modify(s,0x83e,0x400,0x400);modify(s,0x83e,0x100,0x100);modify(s,0x83e,0x200,0x200);modify(s,0x83e,0x80,0x80);
    modify(s,0x818,0x2000,0x2000);modify(s,0x829,8,8);modify(s,0x829,2,2);modify(s,0x829,0x10,0x10);modify(s,0x827,0x10,0x10);modify(s,0x829,0x1e00,0x1e00);modify(s,0x831,8,8);modify(s,0x825,1,1);modify(s,0x833,4,4);modify(s,0x9f6,0x8000,0x8000);
}
static struct axd_radio_power_context context(void)
{
    struct axd_radio_power_context c={0};struct axd_radio_control_context *a=&c.radio;
    a->hardware.phy=(struct axd_phy_context){.core=130,.minor=1,.phy_type=0xb,.phy_revision=51,.phy_core_count=2,.bookkeeping=42};
    a->phy_pointer=(struct axd_wake_u32){0x1000,1};a->public_pointer=(struct axd_wake_u32){0x2000,1};a->shared_pointer=(struct axd_wake_u32){0x3000,1};a->radio_info=(struct axd_wake_u32){0x4000,1};a->dispatch=(struct axd_wake_u32){0x5000,1};a->callback_context=(struct axd_wake_u32){0x6000,1};a->callback_pi=a->phy_pointer;
    a->ac_pointer=(struct axd_wake_u32){0x7000,1};a->chanmgr_pointer=(struct axd_wake_u32){0x8000,1};a->chanmgr_data_pointer=(struct axd_wake_u32){0x9000,1};a->radio_id=(struct axd_wake_u32){0x50e0,1};a->radio_revision=(struct axd_wake_u32){0,1};a->radio_read_offset=(struct axd_wake_u32){0x8000,1};a->chip_id=(struct axd_wake_u32){63178,1};a->pi47e=7;
    a->prefregs=prefregs;a->prefregs_length=sizeof(prefregs);c.mode_data=mode_data;c.mode_length=sizeof(mode_data);c.rccal_data=rccal_data;c.rccal_length=sizeof(rccal_data);
    c.ac_radio_pointer=(struct axd_wake_u32){0xa000,1};c.ac_radio_data_pointer=(struct axd_wake_u32){0xb000,1};c.clock_word=(struct axd_wake_u32){16384,1};
    c.rccal_multiplier=0xdeadbeef;c.rccal_saved6=0xaaaa;c.rccal_saved8=0xbbbb;c.rccal_saved_a=0xcccc;return c;
}
#endif
