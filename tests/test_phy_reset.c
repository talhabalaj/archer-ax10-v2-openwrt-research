/* SPDX-License-Identifier: MIT */
#include "../driver/phy_reset.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independent literal oracle: D11=1, current wrapper=2, unit1 wrapper=3.
 * Samples deliberately differ from written values and from later samples. */
struct event { enum axd_d11_kind kind; unsigned region; uint32_t offset, value, reply; };
#define N(a) (sizeof(a) / sizeof((a)[0]))
#define ST {AXD_D11_STORE_BARRIER,1,0,0,0}
#define SY {AXD_D11_FULL_BARRIER,1,0,0,0}
#define W16(o,v) {AXD_D11_WRITE16,1,o,v,0}
#define W32(o,v) {AXD_D11_WRITE32,1,o,v,0}
#define R16(o,v) {AXD_D11_READ16,1,o,0,v}
#define R32(o,v) {AXD_D11_READ32,1,o,0,v}
#define D(v) {AXD_D11_DELAY_US,0,0,v,0}
#define CS {AXD_D11_STORE_BARRIER,2,0,0,0}
#define CY {AXD_D11_FULL_BARRIER,2,0,0,0}
#define CR(v) {AXD_D11_READ32,2,0x408,0,v}
#define CW(v) {AXD_D11_WRITE32,2,0x408,v,0}
static const struct event snapshot[] = {
    R32(0x120,0),SY,
    ST,W16(0x3fc,0x645),R16(0x3fe,0xdeadf123),SY,
    R32(0x120,0),SY
};
static const struct event prepare[] = {
    CR(0xffff0000),CY,CS,CW(0xffff080e),CR(0xbadc0ffe),CY,D(2),
    CR(0xa5a5ffff),CY,CS,CW(0xa5a5fdff),CR(0x12345678),CY,
    CR(0xdeadbeef),CY,CS,CW(0xdeadbee1),CR(0x87654321),CY,D(1),
    CR(0x80000001),CY,CS,CW(0x80000005),CR(0xf0000000),CY,D(1)
};
static const struct event pulse[] = {
    R16(0x7c0,0xfeed12b4),SY,ST,W16(0x7c0,0x12b4),
    ST,W16(0x3fc,0x1409),R16(0x3fe,0xdead4567),SY,
    ST,W16(0x3fe,0xc567),ST,W16(0x7c0,0x1234),D(100)
};
static const struct event analog[] = {ST,W16(0x3e6,0)};
static const struct event restore[] = {
    R32(0x120,0),SY,
    ST,W16(0x3fc,0x645),ST,R16(0x3fe,0xbeef1234),SY,W16(0x3fe,0x1123),
    R32(0x120,0),SY
};
struct script { struct event events[256]; size_t count, prep, pulse, analog, restore, bf; };
static void append(struct script *s,const struct event *e,size_t n)
{
    assert(s->count+n<=N(s->events));
    memcpy(s->events+s->count,e,n*sizeof(*e));s->count+=n;
}
static struct script script_for(unsigned tssi,unsigned analog_present)
{
    struct script s;
    memset(&s,0,sizeof(s));
    if(tssi) append(&s,snapshot,N(snapshot));
    s.prep=s.count;append(&s,prepare,N(prepare));
    s.pulse=s.count;append(&s,pulse,N(pulse));
    s.analog=s.count;
    if(analog_present) append(&s,analog,N(analog));
    s.restore=s.count;
    if(tssi) append(&s,restore,N(restore));
    s.bf=s.count;return s;
}
struct fixture {
    struct axd_phy_reset *job;
    const struct script *script;
    size_t seen,fault;
    unsigned error,cancel;
};
static struct axd_phy_reset_context context(void)
{
    struct axd_phy_reset_context c;
    memset(&c,0,sizeof(c));
    c.band_type=0xb;c.band_revision=51;c.public_revision.value=51;c.public_revision.known=1;
    c.idle.tssi.hardware.phy.core=130;c.idle.tssi.hardware.phy.minor=1;
    c.idle.tssi.hardware.phy.phy_type=0xb;c.idle.tssi.hardware.phy.phy_revision=51;
    c.idle.tssi.hardware.phy.phy_core_count=1;c.idle.tssi.hardware.phy.bookkeeping=0x5aa5;
    c.idle.tssi.hardware.phy.saved_tssi[0]=0x155;c.idle.tssi.hardware.phy.saved_tssi[1]=0x2aa;
    c.idle.tssi.hardware.phy.snapshot_valid=3;
    c.idle.tssi.hardware.mac.core=130;c.idle.tssi.hardware.mac.minor=1;
    c.idle.tssi.hardware.mac.chip_id=63178;c.idle.tssi.hardware.mac.main_control=3;
    c.idle.tssi.hardware.mac.wake_reasons=8;c.idle.tssi.hardware.mac.band_present=1;
    c.idle.tssi.hardware.mac_context_valid=1;
    c.idle.tssi.saved[0]=0x111;c.idle.tssi.saved[1]=0x222;
    c.idle.tssi.saved[2]=0x333;c.idle.tssi.saved[3]=0x444;c.idle.tssi.valid=15;
    c.clock.core=130;c.clock.minor=1;c.clock.phy_type=0xb;c.clock.phy_revision=51;
    c.clock.chanspec=0x2836;c.clock.bandwidth_state=0x5555;c.clock.chip_id=63178;
    c.clock.interconnect=1;c.clock.current_owned=1;c.clock.unit1_owned=1;
    c.phy_pointer.value=0x1000;c.phy_pointer.known=1;
    c.analog_module.value=0x2000;c.analog_module.known=1;
    c.analog_table.value=0x3000;c.analog_table.known=1;
    c.analog_context.value=0x7000;c.analog_context.known=1;
    c.analog_context_phy.value=c.phy_pointer.value;c.analog_context_phy.known=1;
    c.idle_module.value=0x8000;c.idle_module.known=1;
    c.idle_table.value=0x9000;c.idle_table.known=1;
    c.idle_context.value=0xa000;c.idle_context.known=1;
    c.idle_context_phy.value=c.phy_pointer.value;c.idle_context_phy.known=1;
    c.idle_callback.value=1;c.idle_callback.known=1;
    c.analog_callback.value=1;c.analog_callback.known=1;
    c.bf_flags.known=1;
    c.wlc_identity=0x4000;c.hardware_identity=0x5000;
    c.bf.mac=c.idle.tssi.hardware.mac;c.bf.wlc_identity=c.wlc_identity;
    c.bf.hardware_identity=c.hardware_identity;c.bf.txbf_identity=0x6000;
    return c;
}
static int io(void *opaque,const struct axd_phy_reset_operation *op,uint32_t *reply)
{
    struct fixture *f=opaque;
    const struct event *e;
    assert(f->seen<f->script->count);e=&f->script->events[f->seen++];
    assert(op->kind==e->kind && (unsigned)op->region==e->region);
    assert(op->offset==e->offset && op->value==e->value);
    if(f->seen==f->fault && f->cancel)
        assert(axd_phy_reset_cancel(f->job)==AXD_RESET_CANCELLED);
    if(f->seen==f->fault && f->error) return -1;
    *reply=e->reply;return 0;
}
static void terminal(struct fixture *f)
{
    struct axd_phy_reset before=*f->job;
    size_t seen=f->seen;
    enum axd_reset_phase p=f->job->phase;
    assert(axd_phy_reset_step(f->job,io,f)==p);
    assert(axd_phy_reset_step(f->job,NULL,NULL)==p);
    assert(axd_phy_reset_cancel(f->job)==p);
    assert(f->seen==seen && memcmp(&before,f->job,sizeof(before))==0);
}
static void run(struct fixture *f)
{
    size_t steps=0;
    while(f->job->phase==AXD_RESET_RUNNING) {
        size_t seen=f->seen;
        struct axd_idle_tssi idle=f->job->idle_job;
        struct axd_phy_clock clock=f->job->clock_job;
        struct axd_phy_register phy=f->job->phy_job;
        struct axd_bf_clean bf=f->job->bf_job;
        assert(++steps<f->script->count*30+1000);
        axd_phy_reset_step(f->job,io,f);
        assert(f->seen-seen<=1 && f->job->attempted_ops==f->seen);
        assert(f->job->completed_ops==f->seen-
            (f->error && f->seen==f->fault ? 1u : 0u));
        /* Child algorithms have separate literal suites. Here compare only
         * propagation at their public boundary, including software-only steps
         * and the last failing/cancelled callback, never derive I/O from them. */
        if(memcmp(&idle,&f->job->idle_job,sizeof(idle))!=0)
            assert(memcmp(&f->job->context.idle,&f->job->idle_job.context,
                           sizeof(f->job->context.idle))==0);
        if(memcmp(&clock,&f->job->clock_job,sizeof(clock))!=0)
            assert(memcmp(&f->job->context.clock,&f->job->clock_job.context,
                           sizeof(f->job->context.clock))==0);
        if(memcmp(&phy,&f->job->phy_job,sizeof(phy))!=0)
            assert(memcmp(&f->job->context.idle.tssi.hardware.phy,&f->job->phy_job.context,
                           sizeof(f->job->phy_job.context))==0);
        if(memcmp(&bf,&f->job->bf_job,sizeof(bf))!=0) {
            assert(memcmp(&f->job->context.bf,&f->job->bf_job.context,
                           sizeof(f->job->context.bf))==0);
            assert(memcmp(&f->job->context.idle.tssi.hardware.mac,&f->job->bf_job.context.mac,
                           sizeof(f->job->bf_job.context.mac))==0);
        }
    }
    terminal(f);
}
static void success(struct axd_phy_reset_context c,const struct script *s,unsigned tssi,
                    unsigned analog_present)
{
    struct axd_phy_reset j;
    struct fixture f={&j,s,0,0,0,0};
    assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    assert(j.phase==AXD_RESET_TRACE_COMPLETE && j.error==AXD_RESET_NO_ERROR);
    assert(f.seen==s->count && j.completed_ops==s->count);
    assert(j.snapshot_completed==tssi && j.restore_completed==tssi);
    assert(j.prepare_completed && j.pulse_completed && j.bandwidth_recorded);
    assert(j.analog_completed==analog_present && !j.bf_completed);
    assert(j.context.clock.bandwidth_state==0x2800 && j.context.clock.phy_clock==1);
    assert(j.context.idle.tssi.saved[0]==(tssi ? 0x123 : 0));
    assert(j.context.idle.tssi.saved[1]==0 && j.context.idle.tssi.saved[2]==0);
    assert(j.context.idle.tssi.saved[3]==0 && j.context.idle.tssi.valid==tssi);
    assert(j.context.idle.tssi.hardware.phy.saved_tssi[0]==0x155);
    assert(j.context.idle.tssi.hardware.phy.saved_tssi[1]==0x2aa);
    assert(j.context.idle.tssi.hardware.phy.snapshot_valid==3);
    assert(j.context.idle.phy_depth==0 && j.context.idle.tssi.hardware.mac.main_depth==0);
    assert(j.context.idle.tssi.hardware.mac.wake_reasons==8);
    assert(j.context.idle.tssi.hardware.mac.main_control==3);
}
static void basic_and_failures(void)
{
    struct axd_phy_reset_context c=context();
    struct script s=script_for(1,1);
    struct axd_phy_reset j;
    struct fixture f={&j,&s,0,0,0,0};
    size_t p;
    unsigned type;
    success(c,&s,1,1);
    for(p=1;p<=s.count;p++) for(type=1;type<=3;type++) {
        assert(axd_phy_reset_begin(&j,&c)==0);
        f.seen=0;f.fault=p;f.error=type&1u;f.cancel=(type>>1)&1u;run(&f);
        assert(f.seen==p);
        assert(j.phase==(f.error ? AXD_RESET_FAILED : AXD_RESET_CANCELLED));
        assert(j.error==(f.error ? AXD_RESET_IO_ERROR : AXD_RESET_NO_ERROR));
        assert(j.snapshot_completed==(p>s.prep));
        assert(j.prepare_completed==(p>s.pulse));
        assert(j.pulse_completed==(p>s.analog));
        assert(j.bandwidth_recorded==(p>s.analog));
        assert(j.analog_completed==(p>s.restore));
        assert(!j.restore_completed && !j.bf_completed);
        assert(j.context.clock.phy_clock==(p>s.prep+7));
        assert(j.context.clock.bandwidth_state==(p>s.analog ? 0x2800 : 0x5555));
        /* Snapshot result commits only at its read's successful SY, even if
         * cancelled there. Cancellation at final wrapper SY is not DONE. */
        assert(j.context.idle.tssi.valid==(p>6 || (p==6 && !f.error)));
        assert(j.context.idle.tssi.saved[0]==
            (p>6 || (p==6 && !f.error) ? 0x123 : 0));
        assert(j.context.idle.tssi.saved[1]==0 && j.context.idle.tssi.saved[2]==0);
        assert(j.context.idle.tssi.saved[3]==0);
        assert(j.context.idle.tssi.hardware.phy.bookkeeping==
            (p>4 || (p==4 && !f.error) ? 0 : 0x5aa5));
        assert(j.context.idle.phy_depth==
            ((p>2 && p<=6) || (p>s.restore+2 && p<=s.restore+8) ? 1 : 0));
        assert(j.context.idle.tssi.hardware.mac.main_control==3);
        assert(j.context.idle.tssi.hardware.mac.wake_reasons==
            ((p>2 && p<=6) || (p>s.restore+2 && p<=s.restore+8) ? 10 : 8));
    }
    c.public_revision.value=50;c.idle_module.known=c.idle_table.known=2;
    c.idle_context.known=c.idle_context_phy.known=c.idle_callback.known=2;
    s=script_for(0,1);success(c,&s,0,1);
    c.public_revision.value=0xffff;s=script_for(0,0);c.analog_callback.value=0;success(c,&s,0,0);
    c=context();c.idle_callback.value=0;s=script_for(0,1);
    c.idle_context.known=c.idle_context_phy.known=2;
    c.idle.tssi.hardware.mac.core=0;c.idle.tssi.hardware.mac_context_valid=255;
    c.idle.tssi.hardware.policy.public_up_known=2;c.idle.phy_depth=255;
    /* Both actual idle wrappers are reached and return absent-callback no-op. */
    assert(axd_phy_reset_begin(&j,&c)==0);
    f.script=&s;f.seen=f.fault=f.error=f.cancel=0;run(&f);
    assert(j.phase==AXD_RESET_TRACE_COMPLETE && f.seen==s.count);
    assert(j.snapshot_completed && j.restore_completed && j.context.idle.tssi.valid==0);
    assert(j.context.idle.phy_depth==255 && j.context.idle.tssi.hardware.mac.core==0);
    c=context();c.clock.minor=0;c.idle.tssi.hardware.phy.minor=0;
    c.idle.tssi.hardware.mac.minor=0;s=script_for(1,1);success(c,&s,1,1);
}
static void stop_case(struct axd_phy_reset_context c,const struct script *s,size_t count,
                      enum axd_reset_phase phase,enum axd_reset_field field)
{
    struct axd_phy_reset j;
    struct fixture f={&j,s,0,0,0,0};
    assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    assert(j.phase==phase && j.last_field==field && f.seen==count);
    assert(j.context.clock.bandwidth_state==(j.bandwidth_recorded ? 0x2800 : 0x5555));
    if(phase==AXD_RESET_FAILED) assert(j.error!=AXD_RESET_NO_ERROR);
}
static void metadata_and_dependencies(void)
{
    struct axd_phy_reset_context c=context();
    struct script s=script_for(1,1),without=script_for(0,1);
    struct axd_phy_reset j;
    struct fixture f={&j,&s,0,0,0,0};
    c.public_revision.known=0;stop_case(c,&s,0,AXD_RESET_DEPENDENCY,AXD_RESET_PUBLIC_REVISION);
    c=context();c.public_revision.known=2;
    stop_case(c,&s,0,AXD_RESET_FAILED,AXD_RESET_PUBLIC_REVISION);
    c=context();c.phy_pointer.known=0;
    stop_case(c,&s,0,AXD_RESET_DEPENDENCY,AXD_RESET_PHY_POINTER);
    c=context();c.phy_pointer.value=0;
    stop_case(c,&s,0,AXD_RESET_FAILED,AXD_RESET_PHY_POINTER);
    c=context();c.idle_callback.known=0;
    stop_case(c,&s,0,AXD_RESET_DEPENDENCY,AXD_RESET_IDLE_CALLBACK);
    c=context();c.idle_callback.value=2;
    stop_case(c,&s,0,AXD_RESET_FAILED,AXD_RESET_IDLE_CALLBACK);
    c=context();c.idle_module.known=0;
    stop_case(c,&s,0,AXD_RESET_DEPENDENCY,AXD_RESET_IDLE_MODULE);
    c=context();c.idle_module.value=0;
    stop_case(c,&s,0,AXD_RESET_FAILED,AXD_RESET_IDLE_MODULE);
    c=context();c.idle_table.known=0;
    stop_case(c,&s,0,AXD_RESET_DEPENDENCY,AXD_RESET_IDLE_TABLE);
    c=context();c.idle_table.value=0;
    stop_case(c,&s,0,AXD_RESET_FAILED,AXD_RESET_IDLE_TABLE);
    c=context();c.idle_callback.value=0;c.idle_module.known=0;
    stop_case(c,&s,0,AXD_RESET_DEPENDENCY,AXD_RESET_IDLE_MODULE);
    /* No snapshot gate: pointer becomes required only AFTER pulse+100us,
     * before software BW store, even when analog callback is absent. */
    c=context();c.public_revision.value=50;c.phy_pointer.known=0;c.analog_callback.value=0;
    stop_case(c,&without,without.analog,AXD_RESET_DEPENDENCY,AXD_RESET_PHY_POINTER);
    c.phy_pointer.known=1;c.phy_pointer.value=0;
    stop_case(c,&without,without.analog,AXD_RESET_FAILED,AXD_RESET_PHY_POINTER);
    c=context();c.analog_module.known=0;
    stop_case(c,&s,s.analog,AXD_RESET_DEPENDENCY,AXD_RESET_ANALOG_MODULE);
    c=context();c.analog_module.value=0;
    stop_case(c,&s,s.analog,AXD_RESET_FAILED,AXD_RESET_ANALOG_MODULE);
    c=context();c.analog_table.known=0;
    stop_case(c,&s,s.analog,AXD_RESET_DEPENDENCY,AXD_RESET_ANALOG_TABLE);
    c=context();c.analog_table.value=0;
    stop_case(c,&s,s.analog,AXD_RESET_FAILED,AXD_RESET_ANALOG_TABLE);
    c=context();c.analog_callback.known=0;
    stop_case(c,&s,s.analog,AXD_RESET_DEPENDENCY,AXD_RESET_ANALOG_CALLBACK);
    c=context();c.analog_callback.value=2;
    stop_case(c,&s,s.analog,AXD_RESET_FAILED,AXD_RESET_ANALOG_CALLBACK);
    c=context();c.analog_context.known=0;
    stop_case(c,&s,s.analog,AXD_RESET_DEPENDENCY,AXD_RESET_ANALOG_CONTEXT);
    c=context();c.analog_context.value=0;
    stop_case(c,&s,s.analog,AXD_RESET_FAILED,AXD_RESET_ANALOG_CONTEXT);
    c=context();c.analog_context_phy.known=0;
    stop_case(c,&s,s.analog,AXD_RESET_DEPENDENCY,AXD_RESET_ANALOG_CONTEXT_PHY);
    c=context();c.analog_context_phy.value=0;
    stop_case(c,&s,s.analog,AXD_RESET_FAILED,AXD_RESET_ANALOG_CONTEXT_PHY);
    c=context();c.analog_context_phy.value=0x1004;
    stop_case(c,&s,s.analog,AXD_RESET_FAILED,AXD_RESET_ANALOG_CONTEXT_PHY);
    {
        struct script absent=script_for(1,0);
        c=context();c.analog_callback.value=0;
        c.analog_context.known=c.analog_context_phy.known=2;
        success(c,&absent,1,0);
    }
    c=context();c.bf_flags.known=0;
    stop_case(c,&s,s.count,AXD_RESET_DEPENDENCY,AXD_RESET_BF_FLAGS);
    c=context();c.bf_flags.value=2; /* Bit0 clear skips invalid later fields. */
    c.public_bf.known=2;c.txbf_pointer.known=2;c.txbf_enabled.known=2;success(c,&s,1,1);
    c=context();c.bf_flags.value=1;c.public_bf.known=0;
    stop_case(c,&s,s.count,AXD_RESET_DEPENDENCY,AXD_RESET_PUBLIC_BF);
    c.public_bf.known=1;c.public_bf.value=0;c.txbf_pointer.known=2;success(c,&s,1,1);
    c=context();c.bf_flags.value=1;c.public_bf.known=1;c.public_bf.value=1;
    c.txbf_pointer.known=0;stop_case(c,&s,s.count,AXD_RESET_DEPENDENCY,AXD_RESET_TXBF_POINTER);
    c.txbf_pointer.known=1;c.txbf_pointer.value=0;
    stop_case(c,&s,s.count,AXD_RESET_FAILED,AXD_RESET_TXBF_POINTER);
    c.txbf_pointer.value=0x6000;c.txbf_enabled.known=0;
    stop_case(c,&s,s.count,AXD_RESET_DEPENDENCY,AXD_RESET_TXBF_ENABLED);
    c.txbf_enabled.known=1;c.txbf_enabled.value=0;success(c,&s,1,1);
    c=context();c.clock.current_owned=0;
    assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    assert(j.phase==AXD_RESET_DEPENDENCY && f.seen==N(snapshot));
    assert(j.snapshot_completed && !j.prepare_completed && !j.pulse_completed);
    assert(j.context.idle.tssi.valid==1 && j.context.idle.tssi.saved[0]==0x123);
    assert(j.clock_job.dependency==AXD_PHY_CLOCK_CURRENT_OWNER);
    c=context();c.clock.rsdb=1;c.clock.unit1_owned=0;
    f.seen=0;assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    assert(j.phase==AXD_RESET_DEPENDENCY && f.seen==s.prep+20);
    assert(j.context.clock.phy_clock==1 && !j.context.clock.workaround_done);
    assert(j.snapshot_completed && !j.prepare_completed && !j.pulse_completed);
}
static void active_and_two_cores(void)
{
    static const struct event suspend[] = {
        R32(0x120,3),SY,R32(0x120,3),SY,R32(0x128,0),SY,
        ST,W32(0x120,0x04000002),R32(0x128,1),SY,R32(0x128,1),SY,R32(0x120,2),SY
    };
    static const struct event enable[] = {
        R32(0x120,2),SY,ST,W32(0x120,0x04000003),ST,W32(0x128,1),
        R32(0x120,3),SY,R32(0x128,0),SY,
        ST,W32(0x160,0x10010),R32(0x160,0xdeadbeef),SY,R16(0x164,2),SY
    };
    static const struct event snap2[] = {ST,W16(0x3fc,0x845),R16(0x3fe,0xfeed0a55),SY};
    static const struct event rest2[] = {
        ST,W16(0x3fc,0x845),ST,R16(0x3fe,0xfeedabcd),SY,W16(0x3fe,0xaa55)
    };
    struct axd_phy_reset_context c=context();
    struct script s;
    struct axd_phy_reset j;
    struct fixture f={&j,&s,0,0,0,0};
    unsigned cores;
    for(cores=1;cores<=2;cores++) {
        memset(&s,0,sizeof(s));c.idle.tssi.hardware.phy.phy_core_count=(uint8_t)cores;
        append(&s,suspend,N(suspend));if(cores==2) append(&s,snap2,N(snap2));
        append(&s,snapshot+2,4);append(&s,enable,N(enable));
        s.prep=s.count;append(&s,prepare,N(prepare));
        s.pulse=s.count;append(&s,pulse,N(pulse));s.analog=s.count;
        append(&s,analog,N(analog));s.restore=s.count;
        append(&s,suspend,N(suspend));if(cores==2) append(&s,rest2,N(rest2));
        append(&s,restore+2,6);append(&s,enable,N(enable));s.bf=s.count;
        f.seen=0;assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
        assert(j.phase==AXD_RESET_TRACE_COMPLETE && f.seen==s.count);
        assert(j.snapshot_completed && j.restore_completed && j.analog_completed);
        assert(j.context.idle.tssi.valid==(1u<<cores)-1);
        assert(j.context.idle.tssi.saved[0]==0x123);
        assert(j.context.idle.tssi.saved[1]==(cores==2 ? 0x255 : 0));
        assert(j.context.idle.tssi.hardware.mac.main_control==3);
        assert(j.context.idle.tssi.hardware.mac.main_depth==0);
        assert(j.context.idle.tssi.hardware.mac.wake_reasons==8 && j.context.idle.phy_depth==0);
        {
            size_t p;
            unsigned type;
            for(p=1;p<=s.count;p++) for(type=1;type<=3;type++) {
                assert(axd_phy_reset_begin(&j,&c)==0);f.seen=0;f.fault=p;
                f.error=type&1u;f.cancel=(type>>1)&1u;run(&f);
                assert(f.seen==p && !j.restore_completed && !j.bf_completed);
                assert(j.snapshot_completed==(p>s.prep));
                assert(j.prepare_completed==(p>s.pulse));
                assert(j.pulse_completed==(p>s.analog));
                assert(j.bandwidth_recorded==(p>s.analog));
                assert(j.phase==(f.error ? AXD_RESET_FAILED : AXD_RESET_CANCELLED));
                assert(j.error==(f.error ? AXD_RESET_IO_ERROR : AXD_RESET_NO_ERROR));
            }
            f.fault=f.error=f.cancel=0;
        }
    }
}
static void rsdb_and_policy(void)
{
    static const struct event unit1[] = {
        {AXD_D11_READ32,3,0x408,0,0xcafeffff},
        {AXD_D11_FULL_BARRIER,3,0,0,0},
        {AXD_D11_STORE_BARRIER,3,0,0,0},
        {AXD_D11_WRITE32,3,0x408,0xcafeffef,0},
        {AXD_D11_READ32,3,0x408,0,0x13579bdf},
        {AXD_D11_FULL_BARRIER,3,0,0,0}
    };
    struct axd_phy_reset_context c=context();
    struct script s;
    struct axd_phy_reset j;
    struct fixture f={&j,&s,0,0,0,0};
    size_t p,unit_end;
    unsigned type;
    memset(&s,0,sizeof(s));append(&s,snapshot,N(snapshot));s.prep=s.count;
    append(&s,prepare,20);append(&s,unit1,N(unit1));unit_end=s.count;
    append(&s,prepare+20,N(prepare)-20);s.pulse=s.count;
    append(&s,pulse,N(pulse));s.analog=s.count;append(&s,analog,N(analog));
    s.restore=s.count;append(&s,restore,N(restore));s.bf=s.count;
    c.clock.rsdb=1;assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    assert(j.phase==AXD_RESET_TRACE_COMPLETE && j.context.clock.workaround_done==1);
    for(p=1;p<=s.count;p++) for(type=1;type<=3;type++) {
        assert(axd_phy_reset_begin(&j,&c)==0);f.seen=0;f.fault=p;
        f.error=type&1u;f.cancel=(type>>1)&1u;run(&f);
        assert(j.phase==(f.error ? AXD_RESET_FAILED : AXD_RESET_CANCELLED));
        assert(j.error==(f.error ? AXD_RESET_IO_ERROR : AXD_RESET_NO_ERROR));
        assert(j.context.clock.workaround_done==
            (p>unit_end || (p==unit_end && !f.error)));
    }
    /* An unknown public-down decision is reached only after callback and
     * guard-exit stores; no fictitious cleanup/flags-prepare follows. */
    c=context();c.idle.tssi.hardware.mac.wake_reasons=0;
    c.idle.tssi.hardware.mac.band_present=0;
    memset(&s,0,sizeof(s));append(&s,snapshot,2);
    { const struct event force[]={ST,W32(0x120,0x04000003)};
      append(&s,force,N(force)); }
    append(&s,snapshot+2,4);f.seen=f.fault=f.error=f.cancel=0;
    assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    assert(j.phase==AXD_RESET_DEPENDENCY && j.pending_callouts==AXD_PSM_WAKE_CONTROL);
    assert(f.seen==s.count && !j.snapshot_completed && !j.prepare_completed);
    assert(j.context.idle.tssi.valid==1 && j.context.idle.tssi.saved[0]==0x123);
    assert(j.context.idle.phy_depth==0 && j.context.idle.tssi.hardware.mac.wake_reasons==0);
    /* Explicit public-down makes the same real policy path finish without
     * fabricated reads. Later restore reuses the cache advanced by snapshot. */
    c.idle.tssi.hardware.policy.public_up_known=1;
    append(&s,snapshot+6,2);s.prep=s.count;append(&s,prepare,N(prepare));
    s.pulse=s.count;append(&s,pulse,N(pulse));s.analog=s.count;
    append(&s,analog,N(analog));s.restore=s.count;append(&s,restore,N(restore));s.bf=s.count;
    f.seen=0;assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    assert(j.phase==AXD_RESET_TRACE_COMPLETE && !j.pending_callouts && f.seen==s.count);
    assert(j.snapshot_completed && j.restore_completed && j.context.idle.phy_depth==0);
    assert(j.context.idle.tssi.hardware.mac.main_control==0x04000003);
    assert(j.context.idle.tssi.hardware.mac.wake_reasons==0);
}
static void all_core_snapshots(void)
{
    static const struct event table[] = {
        R32(0x120,2),SY,ST,W16(0x3fc,0x19e),R16(0x3fe,2),SY,
        ST,W32(0x3fc,0x0029000d),ST,W32(0x3fc,0xffff000e),
        ST,W16(0x3fc,0xf),R16(0x3fe,0xfeedbeef),SY,
        ST,W16(0x3fc,0x10),R16(0x3fe,0xdead1234),SY
    };
    static const struct event snap2[] = {ST,W16(0x3fc,0x845),R16(0x3fe,0xfeed0a55),SY};
    static const struct event rest2[] = {
        ST,W16(0x3fc,0x845),ST,R16(0x3fe,0xfeedabcd),SY,W16(0x3fe,0xaa55)
    };
    struct axd_phy_reset_context c=context();
    struct script s;
    struct axd_phy_reset j;
    struct fixture f={&j,&s,0,0,0,0};
    unsigned cores,type;
    for(cores=3;cores<=4;cores++) {
        size_t table_end[2],direct1,direct0,p;
        unsigned t;
        c.idle.tssi.hardware.phy.phy_core_count=(uint8_t)cores;
        memset(&s,0,sizeof(s));append(&s,snapshot,2);
        for(t=0;t<cores-2;t++) {
            size_t start=s.count;
            append(&s,table,N(table));
            if(t) s.events[start+12].reply=0xfeed5678;
            table_end[t]=s.count;
        }
        append(&s,snap2,N(snap2));direct1=s.count;
        append(&s,snapshot+2,4);direct0=s.count;
        append(&s,snapshot+6,2);s.prep=s.count;
        append(&s,prepare,N(prepare));s.pulse=s.count;
        append(&s,pulse,N(pulse));s.analog=s.count;
        append(&s,analog,N(analog));s.restore=s.count;
        append(&s,restore,2);append(&s,rest2,N(rest2));
        append(&s,restore+2,8);s.bf=s.count;
        f.seen=f.fault=f.error=f.cancel=0;
        assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
        assert(j.phase==AXD_RESET_TRACE_COMPLETE && f.seen==s.count);
        assert(j.snapshot_completed && j.restore_completed);
        assert(j.context.idle.tssi.valid==(1u<<cores)-1);
        assert(j.context.idle.tssi.saved[0]==0x123 && j.context.idle.tssi.saved[1]==0x255);
        assert(j.context.idle.tssi.saved[cores-1]==0x2ef);
        assert(j.context.idle.tssi.saved[2]==(cores==4 ? 0x278 : 0x2ef));
        for(p=1;p<=s.count;p++) for(type=1;type<=3;type++) {
            unsigned valid=0;
            assert(axd_phy_reset_begin(&j,&c)==0);f.seen=0;f.fault=p;
            f.error=type&1u;f.cancel=(type>>1)&1u;run(&f);
            assert(j.phase==(f.error ? AXD_RESET_FAILED : AXD_RESET_CANCELLED));
            assert(j.error==(f.error ? AXD_RESET_IO_ERROR : AXD_RESET_NO_ERROR));
            for(t=0;t<cores-2;t++) if(p>table_end[t]) valid|=1u<<(cores-1-t);
            if(p>direct1 || (p==direct1 && !f.error)) valid|=2;
            if(p>direct0 || (p==direct0 && !f.error)) valid|=1;
            assert(j.context.idle.tssi.valid==valid);
            assert(j.context.idle.tssi.saved[0]==((valid&1) ? 0x123 : 0));
            assert(j.context.idle.tssi.saved[1]==((valid&2) ? 0x255 : 0));
            assert(j.context.idle.tssi.saved[cores-1]==((valid&(1u<<(cores-1))) ? 0x2ef : 0));
            assert(j.context.idle.tssi.saved[2]==((valid&4) ? (cores==4 ? 0x278 : 0x2ef) : 0));
        }
    }
}
static void late_idle_bindings(void)
{
    static const struct event suspend[] = {
        R32(0x120,3),SY,R32(0x120,3),SY,R32(0x128,0),SY,
        ST,W32(0x120,0x04000002),R32(0x128,1),SY,R32(0x128,1),SY,R32(0x120,2),SY
    };
    struct axd_phy_reset_context c;
    struct script s;
    struct axd_phy_reset j;
    struct fixture f={&j,&s,0,0,0,0};
    unsigned active,which;
    for(active=0;active<=1;active++) for(which=0;which<7;which++) {
        c=context();memset(&s,0,sizeof(s));
        append(&s,active ? suspend : snapshot,active ? N(suspend) : 2);
        switch(which) {
        case 0:c.idle_context.known=0;break;
        case 1:c.idle_context.value=0;break;
        case 2:c.idle_context.known=2;break;
        case 3:c.idle_context_phy.known=0;break;
        case 4:c.idle_context_phy.value=0;break;
        case 5:c.idle_context_phy.value=0x1004;break;
        default:c.idle_context_phy.known=2;break;
        }
        f.seen=0;assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
        assert(f.seen==s.count && j.last_field==AXD_RESET_IDLE_CALLBACK);
        assert(j.phase==((which==0 || which==3) ? AXD_RESET_DEPENDENCY : AXD_RESET_FAILED));
        assert(j.error==((which==0 || which==3) ? AXD_RESET_NO_ERROR : AXD_RESET_IDLE_ERROR));
        assert(j.idle_job.error==((which==0 || which==3) ? AXD_IDLE_NO_ERROR : AXD_IDLE_CALLBACK_BINDING));
        assert(j.idle_job.binding_field==(which<3 ? AXD_IDLE_CONTEXT : AXD_IDLE_CONTEXT_PHY));
        assert(j.idle_job.binding_accesses==(which<3 ? 1u : 2u));
        assert(!j.snapshot_completed && !j.prepare_completed && !j.pending_callouts);
        assert(j.context.idle.phy_depth==1 && j.idle_job.entered && !j.idle_job.exited);
        assert(!j.idle_job.callback_completed && !j.context.idle.tssi.valid);
        assert(j.context.idle.tssi.hardware.mac.main_depth==active);
        assert(j.context.idle.tssi.hardware.mac.wake_reasons==(active ? 14u : 10u));
        assert(j.context.idle.tssi.hardware.mac.main_control==(active ? 2u : 3u));
        assert(j.context.clock.phy_clock==0 && j.context.clock.bandwidth_state==0x5555);
        assert(j.context.idle.tssi.hardware.phy.bookkeeping==0x5aa5);
    }
}
static void bf_composition(void)
{
    const struct axd_wake_u8 bitmap[1]={{.known=1,.value=0}};
    struct axd_bf_metadata m;
    struct axd_phy_reset_context c=context();
    struct script s=script_for(1,1);
    struct axd_phy_reset j;
    struct fixture f={&j,&s,0,0,0,0};
    static const struct event mac_noop[]={R32(0x120,0),SY,R32(0x120,0),SY};
    size_t p;
    unsigned type;
    memset(&m,0,sizeof(m));m.txbf_wlc.value=0x4000;m.txbf_wlc.known=1;
    m.wt_hardware.value=0x5000;m.wt_hardware.known=1;
    m.wt_public.value=0x7000;m.wt_public.known=1;m.primary_last.known=1;
    m.primary.bitmap=bitmap;m.primary.bitmap_length=1;m.public_extended.known=1;
    c.bf_flags.value=1;c.public_bf.value=2;c.public_bf.known=1;
    c.txbf_pointer.value=0x6000;c.txbf_pointer.known=1;
    c.txbf_enabled.value=0xff;c.txbf_enabled.known=1;c.bf.metadata=&m;
    /* BF's runtime MAC/policy must come from prior reset children, rather
     * than this unused stale caller copy. Ownership identities remain valid. */
    memset(&c.bf.mac,0,sizeof(c.bf.mac));c.bf.policy.public_up_known=2;
    append(&s,mac_noop,N(mac_noop));
    assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    assert(j.phase==AXD_RESET_TRACE_COMPLETE && j.bf_completed && f.seen==s.count);
    assert(j.bf_job.primary_iterations==1 && !j.bf_job.invalidation_calls);
    assert(j.bf_job.context.mac.core==130 && j.bf_job.context.mac.wake_reasons==8);
    for(p=s.bf+1;p<=s.count;p++) for(type=1;type<=3;type++) {
        assert(axd_phy_reset_begin(&j,&c)==0);f.seen=0;f.fault=p;
        f.error=type&1u;f.cancel=(type>>1)&1u;run(&f);
        assert(f.seen==p && !j.bf_completed && j.restore_completed);
        assert(j.phase==(f.error ? AXD_RESET_FAILED : AXD_RESET_CANCELLED));
        assert(j.error==(f.error ? AXD_RESET_IO_ERROR : AXD_RESET_NO_ERROR));
    }
    c.bf.txbf_identity=0x6004;f.seen=f.fault=f.error=f.cancel=0;
    assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    assert(j.phase==AXD_RESET_FAILED && j.error==AXD_RESET_BINDING && f.seen==s.bf);
}
static void validation_alias_and_null(void)
{
    struct axd_phy_reset_context c=context(),bad;
    struct axd_phy_reset j;
    struct script s=script_for(1,1);
    struct fixture f={&j,&s,0,0,0,0};
#define BAD(field,value) do {bad=c;bad.field=value;assert(axd_phy_reset_begin(&j,&bad)==-1);assert(!j.attempted_ops);}while(0)
    BAD(band_type,4);BAD(band_revision,47);BAD(band_revision,129);
    BAD(clock.core,129);BAD(clock.minor,2);BAD(clock.phy_type,4);BAD(clock.phy_revision,50);
    BAD(clock.chip_id,43217);BAD(clock.interconnect,2);BAD(clock.current_owned,2);
    BAD(idle.tssi.hardware.phy.core,129);BAD(idle.tssi.hardware.phy.minor,0);
    BAD(idle.tssi.hardware.phy.phy_type,4);BAD(idle.tssi.hardware.phy.phy_revision,50);
    BAD(idle.tssi.hardware.phy.phy_core_count,0);
#undef BAD
    assert(axd_phy_reset_begin(NULL,&c)==-1 && axd_phy_reset_begin(&j,NULL)==-1);
    assert(axd_phy_reset_step(NULL,NULL,NULL)==AXD_RESET_INVALID);
    assert(axd_phy_reset_cancel(NULL)==AXD_RESET_INVALID);
    assert(axd_phy_reset_begin(&j,&c)==0);run(&f);
    /* Restart through job-owned context must copy before zeroing the job;
     * ENTRY clears prior locals and reacquires the snapshot from explicit I/O. */
    assert(axd_phy_reset_begin(&j,&j.context)==0);f.seen=0;run(&f);
    assert(j.phase==AXD_RESET_TRACE_COMPLETE && f.seen==s.count);
    assert(j.context.idle.tssi.valid==1 && j.context.idle.tssi.saved[0]==0x123);
    assert(axd_phy_reset_begin(&j,&c)==0);
    assert(axd_phy_reset_cancel(&j)==AXD_RESET_CANCELLED);terminal(&f);
    assert(j.context.idle.tssi.valid==15 && j.context.idle.tssi.saved[0]==0x111);
    assert(axd_phy_reset_begin(&j,&c)==0);
    while(j.phase==AXD_RESET_RUNNING) axd_phy_reset_step(&j,NULL,NULL);
    assert(j.phase==AXD_RESET_FAILED && j.error==AXD_RESET_BAD_ARGUMENT);
    assert(!j.attempted_ops && !j.completed_ops && j.context.idle.tssi.valid==0);
}
int main(void)
{
    basic_and_failures();metadata_and_dependencies();active_and_two_cores();rsdb_and_policy();
    all_core_snapshots();late_idle_bindings();
    bf_composition();validation_alias_and_null();
    puts("PHY reset: literal order, independent gates, partial contexts and every callback exit passed");
    return 0;
}
