/* SPDX-License-Identifier: MIT */
#include "../driver/phy_scheduler.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independent table-child oracle retained from test_phy_init, with only the
 * outer transport tag changed. No production output supplies expectations.
 * Table payloads are original repeated fixtures. Every read has an explicit
 * supplied reply; none comes from writes or an unspecified zero fallback.
 */
struct event {unsigned path,kind;uint32_t offset,value,reply;};
struct script {struct event events[512];size_t count;};
#define N(a) (sizeof(a)/sizeof((a)[0]))
#define D(k,o,v,r) {AXD_PS_D11,AXD_D11_##k,o,v,r}
#define ST D(STORE_BARRIER,0,0,0)
#define SY D(FULL_BARRIER,0,0,0)
#define H(o,v) D(WRITE16,o,v,0)
#define W(o,v) D(WRITE32,o,v,0)
#define R(o,v) D(READ32,o,0,v)
#define Q(o,v) D(READ16,o,0,v)
#define MOD(s,r,v) ST,H(0x3fc,s),ST,Q(0x3fe,r),SY,H(0x3fe,v)
#define READ(s,r) ST,H(0x3fc,s),Q(0x3fe,r),SY
#define P(s,v) ST,W(0x3fc,((uint32_t)(v)<<16)|(s))
#define C(k,o,v,r) {AXD_PS_CONTROL,AXD_SPG_##k,o,v,r}
#define CMOD(o,r,v,back) C(READ32,o,0,r),C(FULL_BARRIER,o,0,0),C(STORE_BARRIER,o,0,0),C(WRITE32,o,v,0),C(READ32,o,0,back),C(FULL_BARRIER,o,0,0)
static const struct event prefix[]={MOD(0x19e,0xa5a7,0xa5a5),MOD(0x1b0,0x1234,0x9234),READ(0,0xfeed12ab)};
static const struct event control[]={CMOD(0x658,0xdeadbeef,0,0x12345678),CMOD(0x65c,0xabcabcde,0xaa8abcde,0x55667788),CMOD(0x6c,0xa5a5f0f3,0xa5a50003,0xdecafbad)};
static const struct event middle[]={
    MOD(0x16e,0x4000,0x4002),MOD(0x16e,0x5002,0x5003),MOD(0x16e,0x6003,0x6013),
    P(0x16f,2000),P(0x170,2000),READ(0x19e,0xface5532),
    MOD(0x19e,0xa55a,0xa55a),MOD(0x10f7,0xffff,0xffef),MOD(0x1120,0x123f,0x123b)
};
static const struct event tail[]={MOD(0x19e,0x1230,0x1232),MOD(0x400,0xabcf,0xabcd)};
static uint8_t table0[32],table1[256],table2[4];
static void append(struct script *s,const struct event *events,size_t count)
{assert(s->count+count<=N(s->events));memcpy(s->events+s->count,events,count*sizeof(*events));s->count+=count;}
static struct script full_script(void)
{
    struct script s={0};unsigned i;
    const struct event h0[]={R(0x120,0),SY,READ(0x19e,2),P(0xd,16),P(0xe,0x1000)};
    const struct event h1[]={R(0x120,0),SY,READ(0x19e,0xaf02),P(0xd,16),P(0xe,0x1040)};
    const struct event h2[]={R(0x120,0),SY,READ(0x19e,0x7702),P(0xd,16),P(0xe,0x1020)};
    const struct event d0[]={P(0x10,0x1122),P(0xf,0x3344)};
    const struct event d1[]={P(0x10,0xa1b2),P(0xf,0xc3d4)};
    const struct event d2[]={P(0x10,0x5566),P(0xf,0x7788)};
    append(&s,prefix,N(prefix));append(&s,control,N(control));append(&s,middle,N(middle));
    append(&s,h0,N(h0));for(i=0;i<8;i++)append(&s,d0,N(d0));
    append(&s,h1,N(h1));for(i=0;i<64;i++)append(&s,d1,N(d1));
    append(&s,h2,N(h2));append(&s,d2,N(d2));append(&s,tail,N(tail));
    assert(s.count==412);return s;
}
static const struct axd_si_core catalog[]={{0x800,0x18000000,0x18100000,0},{0x812,0x18200000,0x18300000,0}};
static struct axd_phy_init_context table_context(void)
{
    struct axd_phy_init_context c={0};
    c.chip_id=63178;c.hardware.phy.core=130;c.hardware.phy.minor=1;
    c.hardware.phy.phy_type=0xb;c.hardware.phy.phy_revision=51;c.hardware.phy.phy_core_count=1;
    c.hardware.phy.bookkeeping=0x1234;c.hardware.write_threshold=32;
    c.hardware.mac.core=130;c.hardware.mac.minor=1;c.hardware.mac_context_valid=1;
    c.hardware.mac.main_control=3;c.hardware.mac.chip_id=63178;
    c.control.si.interconnect=1;c.control.si.core_count=N(catalog);
    c.control.si.catalog=catalog;c.control.si.catalog_length=N(catalog);
    c.control.state.current_index=1;c.control.state.current_map=0x22000000;
    c.control.state.primary_wrapper=0x22100000;
    c.control.state.cache[0].core=0x20000000;c.control.state.cache[1].core=0x22000000;
    c.control.gpio_reservation_known=1;c.control.gpio_reservation=3;
    c.control_state_known=1;
    c.control.chipcommon_revision=34;
    c.otp_word=(struct axd_wake_u32){0x0b00,1};
    c.tables[0]=(struct axd_phy_init_span){table0,sizeof(table0),1};
    c.tables[1]=(struct axd_phy_init_span){table1,sizeof(table1),1};
    c.tables[2]=(struct axd_phy_init_span){table2,sizeof(table2),1};
    return c;
}
static struct axd_phy_scheduler_context context(void)
{
    struct axd_phy_scheduler_context c={0};c.table=table_context();
    c.allocator=(struct axd_wake_u32){0x7777,1};
    c.cache.reuse_state=(struct axd_wake_u32){255,1};c.cache.reuse_bytes=(struct axd_wake_u32){32,1};
    c.cache.reuse_buffer=(struct axd_wake_u32){0xcafe,1};c.cache.entries=(struct axd_wake_u32){3,1};
    c.cache.table_pointer=(struct axd_wake_u32){0,1};c.cache.table_bytes=0xdead;c.cache.table_used=0xbeef;
    c.flags3b0=(struct axd_wake_u32){0,1};c.chanspec=(struct axd_wake_u32){0x1001,1};
    c.bandwidth_state=(struct axd_wake_u32){0x1000,1};c.hardware_chanspec=(struct axd_wake_u32){0x1801,1};
    c.interference2g=(struct axd_wake_u32){0x12345678,1};c.interference5g=(struct axd_wake_u32){0x89abcdef,1};
    c.interference_current=(struct axd_wake_u32){0xfeedface,1};
    c.forced_interference=(struct axd_wake_u8){1,0};c.clocks=(struct axd_wake_u8){1,1};
    c.phy_core_count_known=1;c.rxgcrs_enabled=(struct axd_wake_u8){1,1};
    c.channel_state_pointer=(struct axd_wake_u32){0xabcd,1};c.channel_state_chanspec=0x5555;
    c.slice_unit=(struct axd_wake_u32){1,1};c.slice_up=(struct axd_wake_u32){0x80,1};
    c.btcx_byte18=0x55;c.rxgcrs_bytea8=0xee;c.hirssi_byte16=3;c.hirssi_byte17=4;
    c.hirssi_half18=5;c.hirssi_half1a=6;
    memset(c.tpc_min,0xa5,sizeof(c.tpc_min));memset(c.tpc_max,0x5a,sizeof(c.tpc_max));return c;
}
static struct axd_phy_registry registry(void)
{
    struct axd_phy_registry r;struct axd_wake_u32 contexts[16];unsigned i;
    for(i=0;i<16;i++)contexts[i]=(struct axd_wake_u32){0x1000+i*16,1};
    assert(!axd_phy_registry_target51(&r,contexts));return r;
}
static struct axd_phy_registry one(enum axd_phy_init_callback cb)
{
    struct axd_phy_registry r;struct axd_wake_u32 label={0x1234,1};axd_phy_registry_clear(&r);
    assert(!axd_phy_registry_add(&r,cb,&label,(uint32_t)cb));return r;
}
struct fixture {struct axd_phy_scheduler *job;const struct script *script;size_t cursor,fail_at,cancel_at;unsigned prefix;};
static struct fixture fixture(struct axd_phy_scheduler *j,const struct script *s)
{struct fixture f={0};f.job=j;f.script=s;f.fail_at=f.cancel_at=(size_t)-1;return f;}
static int io(void *opaque,const struct axd_phy_scheduler_operation *op,struct axd_phy_scheduler_reply *reply)
{
    struct fixture *f=opaque;const struct event *e;size_t n=f->cursor++;unsigned kind;uint32_t offset,value;
    if(n>=f->script->count)fprintf(stderr,"unexpected event%zu path%u callback%u stage%u\n",n,(unsigned)op->path,(unsigned)f->job->current,f->job->stage);
    assert(n<f->script->count);e=&f->script->events[n];
    if(op->path==AXD_PS_D11){kind=op->d11.kind;offset=op->d11.offset;value=op->d11.value;}
    else if(op->path==AXD_PS_CONTROL){kind=op->control.kind;offset=op->control.offset;value=op->control.value;
        assert(op->control.mapping==0x20000000 && f->job->context.table.control.state.current_index==1);
        assert(f->job->context.table.control.state.current_map==0x22000000);
    }else if(op->path==AXD_PS_SI){kind=op->si.kind;offset=op->si.offset;value=op->si.value;assert(op->si.wrapper==0x22100000);}
    else {assert(op->path==AXD_PS_ALLOCATE_ZERO);kind=0;offset=op->bytes;value=op->allocator;}
    if(op->path!=e->path || kind!=e->kind || offset!=e->offset || value!=e->value)
        fprintf(stderr,"event%zu actual%u/%u/%x/%x expected%u/%u/%x/%x\n",n,(unsigned)op->path,kind,offset,value,e->path,e->kind,e->offset,e->value);
    assert(op->path==e->path && kind==e->kind && offset==e->offset && value==e->value);
    if(f->job->current==AXD_INIT_TABLE)assert(!memcmp(&f->job->context.table,&f->job->table_job.context,sizeof(f->job->context.table)));
    if(f->job->current==AXD_INIT_ANTDIV && f->job->mac_job.phase==AXD_PSM_RUNNING)
        assert(!memcmp(&f->job->context.table.hardware.mac,&f->job->mac_job.context,sizeof(f->job->mac_job.context)));
    if(f->job->current==AXD_INIT_RADIO){
        assert(!memcmp(&f->job->context.radio,&f->job->radio_job.context.radio,sizeof(f->job->context.radio)));
        assert(!memcmp(&f->job->context.radio_power,&f->job->radio_job.context,sizeof(f->job->context.radio_power)));
        assert(!memcmp(&f->job->context.table.hardware,&f->job->radio_job.context.radio.hardware,sizeof(f->job->context.table.hardware)));
        assert(f->job->context.radio.pi47e==0 && f->job->context.radio.chanmgr_data10==0);
    }
    if(f->job->current==AXD_INIT_RADAR){
        assert(!memcmp(&f->job->context.channel,&f->job->channel_job.context,sizeof(f->job->context.channel)));
        assert(!memcmp(&f->job->context.table.hardware,&f->job->channel_job.context.hardware,sizeof(f->job->context.table.hardware)));
    }
    if(f->prefix){
        if(n==0)assert(f->job->context.cache.reuse_state.value==255 && f->job->context.cache.reuse_buffer.value==0xcafe);
        if(n==1)assert(f->job->context.cache.reuse_state.value==0 && f->job->context.cache.reuse_buffer.value==0x8000 && f->job->context.cache.table_bytes==12 && f->job->context.cache.table_pointer.value==0);
        if(n>=2)assert(f->job->context.cache.table_pointer.value==0x9000 && f->job->context.cache.table_used==0);
    }
    if(n==f->cancel_at)assert(axd_phy_scheduler_cancel(f->job)==AXD_PS_CANCELLED);
    if(n==f->fail_at)return -1;
    reply->value=e->reply;reply->control.value=e->reply;return 0;
}
static void run(struct axd_phy_scheduler *j,struct fixture *f)
{
    unsigned steps=0;
    while(j->phase==AXD_PS_RUNNING){size_t before=f->cursor;axd_phy_scheduler_step(j,io,f);assert(f->cursor-before<=1);assert(++steps<6000);}
}
static void terminal(struct axd_phy_scheduler *j,struct fixture *f)
{
    struct axd_phy_scheduler copy=*j;size_t before=f->cursor;
    assert(axd_phy_scheduler_step(j,io,f)==copy.phase && axd_phy_scheduler_step(j,NULL,NULL)==copy.phase);
    assert(axd_phy_scheduler_cancel(j)==copy.phase && f->cursor==before && !memcmp(&copy,j,sizeof(copy)));
}
static struct script prefix_script(void)
{
    static const struct event events[]={
        {AXD_PS_ALLOCATE_ZERO,0,32,0x7777,0x8000},{AXD_PS_ALLOCATE_ZERO,0,12,0x7777,0x9000},
        ST,H(0x3e6,0),{AXD_PS_SI,AXD_SICO_READ32,0x408,0,0x40},{AXD_PS_SI,AXD_SICO_FULL_BARRIER,0,0,0}
    };
    struct script s={0};append(&s,events,N(events));return s;
}
static void insertion(void)
{
    struct axd_phy_registry r=registry(),before;unsigned i;
    const unsigned expected[]={1,3,4,5,6,7,8,9,10,11,12,13,16,19,20,21};
    assert(r.init_count==16 && r.total_count==16);
    for(i=0;i<16;i++){assert((unsigned)r.entries[i].callback==expected[i] && r.entries[i].priority==expected[i]);assert(r.entries[i].context.value==0x1000+i*16);}
    axd_phy_registry_clear(&r);r.total_count=2;
    r.entries[0]=(struct axd_phy_registry_entry){AXD_INIT_UNKNOWN,{0xaabb,1},77};
    r.entries[1]=(struct axd_phy_registry_entry){AXD_INIT_UNKNOWN,{0xccdd,1},88};
    {struct axd_wake_u32 label={1,1};assert(!axd_phy_registry_add(&r,AXD_INIT_TYPE,&label,5));}
    assert(r.init_count==1 && r.total_count==3 && r.entries[1].context.value==0xaabb && r.entries[2].context.value==0xccdd);
    /* Own-entry context alias is snapshotted before the memmove; equal priority
     * goes BEFORE the existing entry, not stable insertion after it. */
    assert(!axd_phy_registry_add(&r,AXD_INIT_PAPD,&r.entries[1].context,5));
    assert(r.entries[0].callback==AXD_INIT_PAPD && r.entries[0].context.value==0xaabb && r.entries[1].callback==AXD_INIT_TYPE);
    for(i=r.total_count;i<23;i++){struct axd_wake_u32 label={i+1,1};assert(!axd_phy_registry_add(&r,AXD_INIT_TYPE,&label,255));}
    before=r;assert(axd_phy_registry_add(&r,AXD_INIT_TYPE,&r.entries[0].context,1)==-22 && !memcmp(&r,&before,sizeof(r)));
    assert(axd_phy_registry_add(&r,AXD_INIT_TYPE,&r.entries[0].context,256)==-1 && !memcmp(&r,&before,sizeof(r)));
    assert(axd_phy_registry_target51(NULL,NULL)==-1);
}
static void full_prefix(void)
{
    struct axd_phy_scheduler j;struct axd_phy_scheduler_context c=context();struct axd_phy_registry r=registry();
    struct script s=prefix_script();struct fixture f=fixture(&j,&s);unsigned mode;size_t cut;
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f.prefix=1;run(&j,&f);
    assert(j.phase==AXD_PS_DEPENDENCY && j.dependency==AXD_PS_RADIO_CHILD && j.current==AXD_INIT_RADIO);
    assert(f.cursor==6 && j.callbacks_completed==5 && !j.return_valid && !j.slice_up_stored);
    assert(j.context.interference_current.value==0x12345678 && j.bandwidth_check_result==-20);
    assert(j.context.table.hardware.phy.bookkeeping==0x1234);terminal(&j,&f);
    for(cut=0;cut<6;cut++)for(mode=0;mode<3;mode++){
        assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);f.prefix=1;
        if(mode!=1)f.fail_at=cut;if(mode!=0)f.cancel_at=cut;run(&j,&f);
        assert(j.phase==(mode==1?AXD_PS_CANCELLED:AXD_PS_FAILED));assert(j.error==(mode==1?AXD_PS_NO_ERROR:AXD_PS_IO_ERROR));
        assert(f.cursor==cut+1 && j.attempted_ops==cut+1 && j.completed_ops==cut+(mode==1));
        if(cut==0)assert(j.context.cache.reuse_state.value==255 && j.context.cache.reuse_buffer.value==0xcafe);
        if(cut==1)assert(j.context.cache.table_pointer.value==0 && j.context.cache.table_used==0xbeef && j.context.cache.table_bytes==12);
        assert(!j.slice_up_stored && !j.return_valid);terminal(&j,&f);
    }
    c=context();c.bandwidth_state.value=0x1800;assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PS_DEPENDENCY && j.dependency==AXD_PS_BANDWIDTH_SET && f.cursor==4 && j.callbacks_completed==2);
    c=context();s=prefix_script();s.events[0].reply=0;
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.dependency==AXD_PS_FATAL_ALLOCATION && f.cursor==1 && j.allocation_observed==1 && j.context.cache.reuse_state.value==255);
    c=context();c.allocator.known=0;assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.dependency==AXD_PS_ALLOCATOR && f.cursor==0);
    c=context();c.cache.reuse_state.value=0;c.cache.table_pointer.value=0x9000;c.cache.entries.known=0;c.cache.reuse_bytes.known=2;c.allocator.known=2;
    r=one(AXD_INIT_CACHE);s.count=0;assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PS_TRACE_COMPLETE && f.cursor==0 && j.context.slice_up.value==0x82);
}
static void software_and_dependencies(void)
{
    struct axd_phy_scheduler j;struct axd_phy_scheduler_context c=context();struct axd_phy_registry r;struct script s={0};struct fixture f;unsigned i;
    const enum axd_phy_init_callback closed[]={AXD_INIT_TYPE,AXD_INIT_NOISE_RESET,AXD_INIT_TPC,AXD_INIT_RADAR,AXD_INIT_PAPD,AXD_INIT_RXGCRS,AXD_INIT_BTCX,AXD_INIT_HIRSSI};
    axd_phy_registry_clear(&r);c.table.hardware.phy.phy_core_count=4;
    for(i=0;i<N(closed);i++){struct axd_wake_u32 label={0x1000+i,1};assert(!axd_phy_registry_add(&r,closed[i],&label,(unsigned)closed[i]));}
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PS_TRACE_COMPLETE && j.callbacks_completed==N(closed) && j.return_valid && j.return_value==0 && j.slice_up_stored);
    for(i=0;i<4;i++)assert(j.context.tpc_min[i]==0x32 && j.context.tpc_max[i]==0x32);
    assert(j.context.rxgcrs_bytea8==0 && j.context.channel_state_chanspec==0x1001 && j.context.btcx_byte18==255);
    assert(j.context.hirssi_half18==65535 && j.context.hirssi_half1a==65535 && !j.context.hirssi_byte16 && !j.context.hirssi_byte17);
    terminal(&j,&f);
    c=j.context;{struct axd_phy_registry copy=j.registry;assert(!axd_phy_scheduler_begin(&j,&j.registry,&j.context));assert(!memcmp(&c,&j.context,sizeof(c)) && !memcmp(&copy,&j.registry,sizeof(copy)));}
    axd_phy_scheduler_cancel(&j);
    r=one(AXD_INIT_PAPD);r.entries[0].context.known=2;assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.phase==AXD_PS_TRACE_COMPLETE);
    c=context();r=one(AXD_INIT_RXGCRS);c.channel_state_pointer.known=0;
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PS_DEPENDENCY && j.context.rxgcrs_bytea8==0 && j.context.channel_state_chanspec==0x5555);
    c=context();r=one(AXD_INIT_NOISE);c.chanspec.value=0xc001;c.forced_interference.value=1;c.interference_override5g=(struct axd_wake_u32){7,1};
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.dependency==AXD_PS_NOISE_MODE && j.context.interference_current.value==0 && !j.callbacks_completed);
    c.flags3b0.value=0x800;c.chanspec.known=2;c.forced_interference.known=2;
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.phase==AXD_PS_TRACE_COMPLETE);
    c=context();r=one(AXD_INIT_RADAR);c.chanspec.value=0x4001;assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.dependency==AXD_PS_RADAR_CHILD);
    c=context();r=one(AXD_INIT_CHANSPEC);assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.dependency==AXD_PS_CHANSPEC_CHILD);
    c=context();r=one(AXD_INIT_UNKNOWN);assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.dependency==AXD_PS_UNKNOWN_CALLBACK);
    axd_phy_registry_clear(&r);c=context();c.slice_unit.value=33;assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.context.slice_up.value==0x80 && j.slice_up_stored);
    c.slice_unit.value=257;assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.context.slice_up.value==0x82);
}
static void software_prefix_cancellation(void)
{
    struct axd_phy_scheduler j;struct axd_phy_scheduler_context c=context();struct axd_phy_registry r=one(AXD_INIT_TPC);unsigned steps=0;
    assert(!axd_phy_scheduler_begin(&j,&r,&c));while(j.context.tpc_min[0]!=0x32){assert(j.phase==AXD_PS_RUNNING && ++steps<20);axd_phy_scheduler_step(&j,NULL,NULL);}
    assert(j.context.tpc_max[0]==0x5a && !j.callbacks_completed);axd_phy_scheduler_cancel(&j);assert(j.context.tpc_max[0]==0x5a);
    r=one(AXD_INIT_HIRSSI);assert(!axd_phy_scheduler_begin(&j,&r,&c));steps=0;
    while(j.context.hirssi_half18!=65535){assert(++steps<20);axd_phy_scheduler_step(&j,NULL,NULL);}
    assert(j.context.hirssi_half1a==6 && j.context.hirssi_byte16==3);axd_phy_scheduler_cancel(&j);
    axd_phy_registry_clear(&r);assert(!axd_phy_scheduler_begin(&j,&r,&c));steps=0;
    while(!j.slice_up_stored){assert(++steps<20);axd_phy_scheduler_step(&j,NULL,NULL);}
    assert(!j.return_valid && j.context.slice_up.value==0x82);axd_phy_scheduler_cancel(&j);assert(!j.return_valid);
    c=context();r=registry();{struct script s=prefix_script();struct fixture f=fixture(&j,&s);assert(!axd_phy_scheduler_begin(&j,&r,&c));steps=0;
        while(j.context.cache.reuse_state.value!=0){assert(++steps<30);axd_phy_scheduler_step(&j,io,&f);}
        assert(f.cursor==1 && j.context.cache.reuse_buffer.value==0xcafe);axd_phy_scheduler_cancel(&j);assert(j.context.cache.reuse_buffer.value==0xcafe);}
}
static void table_child(void)
{
    struct axd_phy_scheduler j;struct axd_phy_scheduler_context c=context();struct axd_phy_registry r=one(AXD_INIT_TABLE);
    struct script s=full_script();struct fixture f;size_t cut;unsigned mode;
    c.table.hardware.shared_bus_word=1;c.table.hardware.write_threshold=65535;
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PS_TRACE_COMPLETE && j.callbacks_completed==1 && f.cursor==412 && j.table_job.tables_completed==3);
    assert(j.context.table.hardware.phy.bookkeeping==0 && j.context.table.control.state.current_index==1);terminal(&j,&f);
    for(cut=0;cut<s.count;cut++)for(mode=0;mode<3;mode++){
        assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);if(mode!=1)f.fail_at=cut;if(mode!=0)f.cancel_at=cut;run(&j,&f);
        assert(j.phase==(mode==1?AXD_PS_CANCELLED:AXD_PS_FAILED) && !j.callbacks_completed && !j.slice_up_stored);
        assert(j.error==(mode==1?AXD_PS_NO_ERROR:AXD_PS_IO_ERROR));assert(j.completed_ops==cut+(mode==1));
        assert(!memcmp(&j.context.table,&j.table_job.context,sizeof(j.context.table)));terminal(&j,&f);
    }
}
static void antdiv(void)
{
    static const struct event inactive[]={R(0x120,0xabc00002),SY};
    static const struct event active[]={R(0x120,0xabc00003),SY};
    static const struct event nested[]={R(0x120,0xabc00003),SY,R(0x120,0xfeed0002),SY,R(0x120,0xface0002),SY};
    struct axd_phy_scheduler j;struct axd_phy_scheduler_context c=context();struct axd_phy_registry r=one(AXD_INIT_ANTDIV);
    struct script s={0};struct fixture f;append(&s,inactive,N(inactive));
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PS_TRACE_COMPLETE && f.cursor==2 && j.antdiv_result==0);
    s.count=0;c.clocks.value=0;assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.phase==AXD_PS_TRACE_COMPLETE && j.antdiv_result==-11);
    c=context();c.table.hardware.mac_context_valid=0;append(&s,active,N(active));
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);assert(j.dependency==AXD_PS_MAC_CHILD && f.cursor==2);
    c=context();c.table.hardware.mac.main_depth=2;
    s.count=0;append(&s,nested,N(nested));
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PS_TRACE_COMPLETE && f.cursor==6 && j.context.table.hardware.mac.main_depth==2);
    for(unsigned mode=0;mode<3;mode++)for(size_t cut=0;cut<6;cut++){
        assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);if(mode!=1)f.fail_at=cut;if(mode!=0)f.cancel_at=cut;run(&j,&f);
        assert(j.phase==(mode==1?AXD_PS_CANCELLED:AXD_PS_FAILED));assert(j.completed_ops==cut+(mode==1));terminal(&j,&f);
    }
}
static void radio_child(void)
{
    static const struct event events[]={P(0x4728,0),P(0x4720,0x3ff),P(0x415,0),P(0x40e,0),
        P(0x40c,0x2000),P(0x408,0),P(0x417,0),P(0x416,0xd),P(0x408,7),D(DELAY_US,0,1000,0),P(0x408,6)};
    struct axd_phy_scheduler j;struct axd_phy_scheduler_context c=context();struct axd_phy_registry r=one(AXD_INIT_RADIO);
    struct axd_radio_control_context *a=&c.radio;struct script s={0};struct fixture f;unsigned mode;size_t cut;
    a->phy_pointer=(struct axd_wake_u32){0x1000,1};a->public_pointer=(struct axd_wake_u32){0x2000,1};
    a->shared_pointer=(struct axd_wake_u32){0x3000,1};a->radio_info=(struct axd_wake_u32){0x4000,1};
    a->dispatch=(struct axd_wake_u32){0x5000,1};a->callback_context=(struct axd_wake_u32){0x6000,1};a->callback_pi=a->phy_pointer;
    a->ac_pointer=(struct axd_wake_u32){0x7000,1};a->chanmgr_pointer=(struct axd_wake_u32){0x8000,1};a->chanmgr_data_pointer=(struct axd_wake_u32){0x9000,1};
    /* Synthetic identity is an explicit test branch, never a board inference. */
    a->radio_id=(struct axd_wake_u32){0x50e0,1};a->radio_revision.known=0;a->pi47e=7;a->chanmgr_data10=9;
    c.table.hardware.shared_bus_word=1;c.table.hardware.write_threshold=65535;
    append(&s,events,N(events));assert(s.count==21);
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PS_DEPENDENCY && j.dependency==AXD_PS_RADIO_CHILD && f.cursor==21 && !j.callbacks_completed);
    assert(j.context.radio.pi47e==0 && j.context.radio.chanmgr_data10==0);terminal(&j,&f);
    for(cut=0;cut<s.count;cut++)for(mode=0;mode<3;mode++){
        assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);if(mode!=1)f.fail_at=cut;if(mode!=0)f.cancel_at=cut;run(&j,&f);
        assert(j.phase==(mode==1?AXD_PS_CANCELLED:AXD_PS_FAILED) && !j.callbacks_completed);
        assert(j.completed_ops==cut+(mode==1));assert(!memcmp(&j.context.table.hardware,&j.radio_job.context.radio.hardware,sizeof(j.context.table.hardware)));terminal(&j,&f);
    }
}
static void radar_child(void)
{
    static const struct event events[]={P(0x25c,0),P(0x25e,0xb4a3),P(0x260,0x3237),P(0x261,0x14),
        P(0x25b,8),P(0x262,200),P(0x263,25),P(0x262,600),P(0x250,0xa419)};
    struct axd_phy_scheduler j;struct axd_phy_scheduler_context c=context();struct axd_phy_registry r=one(AXD_INIT_RADAR);
    struct script s={0};struct fixture f;struct axd_wake_u32 label={0x5555,1};unsigned mode;size_t cut;
    c.chanspec=(struct axd_wake_u32){0x5047,1};c.radar_enabled=(struct axd_wake_u8){1,0};
    c.channel.radar_state_pointer=(struct axd_wake_u32){0x1234,1};
    c.channel.thresholds[4].first=(struct axd_wake_u32){0xaaaa,1};c.channel.thresholds[4].second=(struct axd_wake_u32){0xbbbb,1};
    c.channel.current_band=(struct axd_wake_u32){0,1};c.channel.pi47c=0xcccc;
    c.table.hardware.shared_bus_word=1;c.table.hardware.write_threshold=65535;
    assert(!axd_phy_registry_add(&r,AXD_INIT_BTCX,&label,20));append(&s,events,N(events));
    assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);run(&j,&f);
    assert(j.phase==AXD_PS_TRACE_COMPLETE && j.callbacks_completed==2 && j.slice_up_stored && f.cursor==18);
    assert(j.context.channel.state170==0xaaaa && j.context.channel.state172==0xbbbb && j.context.channel.pi47c==0xcccc && j.context.btcx_byte18==255);
    assert(j.context.table.hardware.phy.bookkeeping==0x123d);terminal(&j,&f);
    for(cut=0;cut<s.count;cut++)for(mode=0;mode<3;mode++){
        assert(!axd_phy_scheduler_begin(&j,&r,&c));f=fixture(&j,&s);if(mode!=1)f.fail_at=cut;if(mode!=0)f.cancel_at=cut;run(&j,&f);
        assert(j.phase==(mode==1?AXD_PS_CANCELLED:AXD_PS_FAILED) && !j.callbacks_completed && !j.slice_up_stored);
        assert(j.context.btcx_byte18==0x55 && j.context.channel.state170==0xaaaa && j.context.channel.state172==0xbbbb);
        assert(j.completed_ops==cut+(mode==1));terminal(&j,&f);
    }
}
int main(void)
{
    for(size_t i=0;i<sizeof(table0);i+=4){table0[i]=0x44;table0[i+1]=0x33;table0[i+2]=0x22;table0[i+3]=0x11;}
    for(size_t i=0;i<sizeof(table1);i+=4){table1[i]=0xd4;table1[i+1]=0xc3;table1[i+2]=0xb2;table1[i+3]=0xa1;}
    table2[0]=0x88;table2[1]=0x77;table2[2]=0x66;table2[3]=0x55;
    insertion();full_prefix();software_and_dependencies();software_prefix_cancellation();table_child();antdiv();radio_child();radar_child();
    puts("PHY scheduler: registry order, real prefix/table children, software stores and callback exits passed");return 0;
}
