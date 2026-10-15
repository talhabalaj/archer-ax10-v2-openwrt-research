/* SPDX-License-Identifier: MIT */
#include "../driver/core_reset.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Original synthetic oracle, derived from retained corereset/helper assembly
 * before reading the coordinator. Replies are explicit and independent of
 * writes; typed child resources and queue identities are checked throughout. */
#define N(a) (sizeof(a) / sizeof((a)[0]))
struct event {
    unsigned path, kind, region, queue, secondary;
    uint32_t offset, value, reply;
};
struct script { struct event events[1024]; size_t count; };
static void append(struct script *s,const struct event *e,size_t n)
{
    assert(s->count+n<=N(s->events));
    memcpy(s->events+s->count,e,n*sizeof(*e));s->count+=n;
}
enum test_path { T_SI=1,T_D11,T_DMA,T_WL,T_PHY,T_PLL,T_LOW };
#define E(p,k,r,q,s,o,v,a) {p,k,r,q,s,o,v,a}
#define SR(o,a) E(T_SI,AXD_D11_READ32,0,0,0,o,0,a)
#define SY E(T_SI,AXD_D11_FULL_BARRIER,0,0,0,0,0,0)
#define SS E(T_SI,AXD_D11_STORE_BARRIER,0,0,0,0,0,0)
#define SW(o,v) E(T_SI,AXD_D11_WRITE32,0,0,0,o,v,0)
#define DS E(T_D11,AXD_D11_STORE_BARRIER,0,0,0,0,0,0)
#define DY E(T_D11,AXD_D11_FULL_BARRIER,0,0,0,0,0,0)
#define DH(o,a) E(T_D11,AXD_D11_READ16,0,0,0,o,0,a)
#define DW(o,v) E(T_D11,AXD_D11_WRITE32,0,0,0,o,v,0)
#define WC(k,o,v,a) E(T_WL,AXD_D11_##k,AXD_CLOCK_CURRENT_D11,0,0,o,v,a)
#define PC(k,o,v,a) E(T_PLL,AXD_D11_##k,AXD_CLOCK_CURRENT_D11,0,0,o,v,a)
static const struct event down[] = {SR(0x408,0x80000003),SY};
static const struct event up[] = {SR(0x408,0xa5a50001),SY,SR(0x800,0xdead0000),SY};
static const struct event reset_active[] = {SR(0x408,0xa5a50001),SY,SR(0x800,0xdead0001),SY};
static const struct event mctrl_reset[] = {DS,DW(0x120,0x04000400)};
static const struct event mctrl_mirror[] = {DS,DW(0x324,0x04000c00)};
static const struct event ext_reset[] = {DS,DW(0x1b4,0x400)};
static const struct event fast[] = {
    WC(STORE_BARRIER,0,0,0),WC(READ32,0x1e0,0,0x80000010),
    WC(FULL_BARRIER,0,0,0),WC(WRITE32,0x1e0,0x80000012,0),
    WC(DELAY_US,0,64,0),WC(READ32,0x1e0,0,0x20000),WC(FULL_BARRIER,0,0,0)
};
static const struct event dynamic[] = {
    WC(STORE_BARRIER,0,0,0),WC(READ32,0x1e0,0,0xa5a50003),
    WC(FULL_BARRIER,0,0,0),WC(WRITE32,0x1e0,0xa5a50001,0)
};
static const struct event pll[] = {
    PC(READ32,0x1e0,0,0),PC(FULL_BARRIER,0,0,0),
    PC(STORE_BARRIER,0,0,0),PC(READ32,0x1e0,0,0x20),
    PC(FULL_BARRIER,0,0,0),PC(WRITE32,0x1e0,0x320,0),
    PC(READ32,0x1e0,0,0x03000000),PC(FULL_BARRIER,0,0,0),
    PC(READ32,0x1e0,0,0),PC(FULL_BARRIER,0,0,0)
};
static const struct event discovery[] = {DH(0x3e0,0xdead0d77),DY,DH(0x3e0,0xbeefaa33),DY};
static const struct event ac_flags[] = {
    SR(0x408,0xa5a503ff),SY,SS,SW(0x408,0xa5a501ff),SR(0x408,0x11110000),SY,
    SR(0x408,0x1234567f),SY,SS,SW(0x408,0x12345679),SR(0x408,0x22220000),SY,
    SR(0x408,0xdeadbe01),SY,SS,SW(0x408,0xdeadbe05),SR(0x408,0x33330000),SY
};
static void tx_disabled(struct script *s,unsigned q,unsigned secondary)
{
    const struct event e[]={
        E(T_DMA,AXD_D11_READ32,AXD_DMA_TX_CHANNEL,q,secondary,0x10,0,0x0fffffff),
        E(T_DMA,AXD_D11_FULL_BARRIER,AXD_DMA_TX_CHANNEL,q,secondary,0,0,0)};
    append(s,e,N(e));
}
static void rx_disabled(struct script *s,unsigned q)
{
    const struct event e[]={
        E(T_DMA,AXD_D11_STORE_BARRIER,AXD_DMA_RX_CHANNEL,q,0,0,0,0),
        E(T_DMA,AXD_D11_WRITE32,AXD_DMA_RX_CHANNEL,q,0,0,0,0),
        E(T_DMA,AXD_D11_READ32,AXD_DMA_RX_CHANNEL,q,0,0x10,0,0x01234567),
        E(T_DMA,AXD_D11_FULL_BARRIER,AXD_DMA_RX_CHANNEL,q,0,0,0,0)};
    append(s,e,N(e));
}
#define PH(k,r,o,v,a) E(T_PHY,AXD_D11_##k,r,0,0,o,v,a)
#define P_ST PH(STORE_BARRIER,AXD_RESET_D11,0,0,0)
#define P_SY PH(FULL_BARRIER,AXD_RESET_D11,0,0,0)
#define P_W(o,v) PH(WRITE16,AXD_RESET_D11,o,v,0)
#define P_R(o,a) PH(READ16,AXD_RESET_D11,o,0,a)
#define P_D(v) PH(DELAY_US,AXD_RESET_NO_REGION,0,v,0)
#define P_CS PH(STORE_BARRIER,AXD_RESET_CURRENT_WRAPPER,0,0,0)
#define P_CY PH(FULL_BARRIER,AXD_RESET_CURRENT_WRAPPER,0,0,0)
#define P_CR(v) PH(READ32,AXD_RESET_CURRENT_WRAPPER,0x408,0,v)
#define P_CW(v) PH(WRITE32,AXD_RESET_CURRENT_WRAPPER,0x408,v,0)
/* Public-revision0 skips the outer TSSI calls, while actual band51 still
 * requires the complete preparation, pulse, bandwidth and analog path. */
static const struct event phy_full[] = {
    P_CR(0xffff0000),P_CY,P_CS,P_CW(0xffff080e),P_CR(0xbadc0ffe),P_CY,P_D(2),
    P_CR(0xa5a5ffff),P_CY,P_CS,P_CW(0xa5a5fdff),P_CR(0x12345678),P_CY,
    P_CR(0xdeadbeef),P_CY,P_CS,P_CW(0xdeadbee1),P_CR(0x87654321),P_CY,P_D(1),
    P_CR(0x80000001),P_CY,P_CS,P_CW(0x80000005),P_CR(0xf0000000),P_CY,P_D(1),
    P_R(0x7c0,0xfeed12b4),P_SY,P_ST,P_W(0x7c0,0x12b4),
    P_ST,P_W(0x3fc,0x1409),P_R(0x3fe,0xdead4567),P_SY,
    P_ST,P_W(0x3fe,0xc567),P_ST,P_W(0x7c0,0x1234),P_D(100),
    P_ST,P_W(0x3e6,0)
};
static struct axd_phy_reset_context phy_context(void)
{
    struct axd_phy_reset_context c;
    memset(&c,0,sizeof(c));c.band_type=0xb;c.band_revision=51;
    c.public_revision.known=1;c.public_revision.value=0;
    c.idle.tssi.hardware.phy.core=130;c.idle.tssi.hardware.phy.minor=1;
    c.idle.tssi.hardware.phy.phy_type=0xb;c.idle.tssi.hardware.phy.phy_revision=51;
    c.idle.tssi.hardware.phy.phy_core_count=1;
    c.idle.tssi.hardware.mac.core=130;c.idle.tssi.hardware.mac.minor=1;
    c.idle.tssi.hardware.mac.chip_id=63178;c.idle.tssi.hardware.mac_context_valid=1;
    c.clock.core=130;c.clock.minor=1;c.clock.phy_type=0xb;c.clock.phy_revision=51;
    c.clock.chanspec=0x2836;c.clock.chip_id=63178;
    c.clock.interconnect=1;c.clock.current_owned=1;c.clock.unit1_owned=1;
    c.phy_pointer.value=0x1000;c.phy_pointer.known=1;
    c.analog_module.value=0x2000;c.analog_module.known=1;
    c.analog_table.value=0x3000;c.analog_table.known=1;
    c.analog_context.value=0x7000;c.analog_context.known=1;
    c.analog_context_phy=c.phy_pointer;c.analog_callback.value=1;c.analog_callback.known=1;
    c.idle_module.known=c.idle_table.known=c.idle_callback.known=1;
    c.bf_flags.known=1;c.wlc_identity=0x4000;c.hardware_identity=0x5000;
    c.bf.wlc_identity=c.wlc_identity;c.bf.hardware_identity=c.hardware_identity;
    return c;
}
static const struct axd_si_core catalog[]={{0x812,0x19000000,0x19100000,0}};
static struct axd_core_reset_context context(void)
{
    struct axd_core_reset_context c;unsigned i;
    memset(&c,0,sizeof(c));
    c.clock.d11.core=130;c.clock.d11.minor=1;c.clock.d11.core_clock_valid=1;
    c.clock.d11.fast_clock=1;c.clock.d11.si_word20=1;c.clock.d11.si_capabilities=0x10000000;
    c.low.hw_present=1;c.low.w_present=1;c.low.revision=130;c.low.chip_id=63178;
    c.low.si.interconnect=1;c.low.si.catalog=catalog;c.low.si.core_count=N(catalog);
    c.low.si.catalog_length=N(catalog);c.low.si.bus_core_id=0x804;
    c.low.state.current_map=0x40000000;c.low.state.primary_wrapper=0x50000000;
    c.low.state.cache[0].core=0x40000000;c.low.state.cache[0].primary=0x50000000;
    c.control.psm.core=130;c.control.psm.minor=1;c.control.psm.chip_id=63178;
    c.control.psm.main_control=0x04000403;c.control.psm.extended_control=0x10203040;
    c.control.psm.main_depth=7;c.control.psm.extended_depth=11;
    c.control.psm.wake_reasons=0;c.control.psm.band_present=1;
    c.control.reset_halfword=0x7788;
    c.band.known=1;c.phy.phy_pointer.known=1;c.phy_shared.known=1;
    c.word164=0x11223344;c.word22c=0x55667788;c.tail_byte=0xa5;
    c.policy.public_up_known=1;c.policy.public_up=0;
    c.dma_slots=4;
    for(i=0;i<AXD_CORE_MAX_QUEUES;i++) {
        c.dma[i].primary.known=c.dma[i].secondary.known=1;
        c.dma[i].tx.core=c.dma[i].paired_tx.core=c.dma[i].rx.core=130;
        c.dma[i].tx.minor=c.dma[i].paired_tx.minor=c.dma[i].rx.minor=1;
        c.dma[i].tx.descriptor_count=c.dma[i].paired_tx.descriptor_count=c.dma[i].rx.descriptor_count=8;
        c.dma[i].tx.tx_cache=0x11112222;c.dma[i].paired_tx.tx_cache=0x33334444;
    }
    return c;
}
struct fixture {
    struct axd_core_reset *job;const struct script *script;
    size_t seen,fault;unsigned error,cancel;
    size_t control_start,extended_start,tail_start;
};
static struct fixture fixture(struct axd_core_reset *j,const struct script *s)
{
    struct fixture f;memset(&f,0,sizeof(f));f.job=j;f.script=s;
    f.control_start=f.extended_start=f.tail_start=(size_t)-1;return f;
}
static unsigned si_kind(enum axd_si_core_kind k)
{
    switch(k) {
    case AXD_SICO_READ32:return AXD_D11_READ32;
    case AXD_SICO_FULL_BARRIER:return AXD_D11_FULL_BARRIER;
    case AXD_SICO_STORE_BARRIER:return AXD_D11_STORE_BARRIER;
    case AXD_SICO_WRITE32:return AXD_D11_WRITE32;
    }
    assert(0);return 0;
}
static int io(void *opaque,const struct axd_core_reset_operation *op,
              struct axd_core_reset_reply *reply)
{
    struct fixture *f=opaque;const struct event *e;struct event got;
    assert(f->seen<f->script->count);e=&f->script->events[f->seen++];
    memset(&got,0,sizeof(got));got.reply=e->reply;
    memset(reply,0xa5,sizeof(*reply));reply->value=e->reply;reply->irq_state_known=0;
    switch(op->path) {
    case AXD_CORE_SI:
        got.path=T_SI;got.kind=si_kind(op->operation.si.kind);
        got.offset=op->operation.si.offset;got.value=op->operation.si.value;
        assert(op->operation.si.wrapper==f->job->context.low.state.primary_wrapper);break;
    case AXD_CORE_D11:
        got.path=T_D11;got.kind=op->operation.d11.kind;
        got.offset=op->operation.d11.offset;got.value=op->operation.d11.value;break;
    case AXD_CORE_DMA:
        got.path=T_DMA;got.kind=op->operation.dma.kind;got.region=op->operation.dma.region;
        got.queue=op->queue;got.secondary=op->secondary;
        got.offset=op->operation.dma.offset;got.value=op->operation.dma.value;
        assert(op->queue<f->job->context.dma_slots);
        assert(op->dma_identity==(op->secondary ? f->job->context.dma[op->queue].secondary.value :
                                                       f->job->context.dma[op->queue].primary.value));
        break;
    case AXD_CORE_WL:
        assert(op->operation.wl.path==AXD_WLC_D11);
        got.path=T_WL;got.kind=op->operation.wl.operation.d11.kind;
        got.region=op->operation.wl.operation.d11.region;
        got.offset=op->operation.wl.operation.d11.offset;got.value=op->operation.wl.operation.d11.value;break;
    case AXD_CORE_PHY:
        got.path=T_PHY;got.kind=op->operation.phy.kind;got.region=op->operation.phy.region;
        got.offset=op->operation.phy.offset;got.value=op->operation.phy.value;break;
    case AXD_CORE_PLL:
        got.path=T_PLL;got.kind=op->operation.pll.kind;got.region=op->operation.pll.region;
        got.offset=op->operation.pll.offset;got.value=op->operation.pll.value;break;
    case AXD_CORE_LOW:
        got.path=T_LOW;
        assert(op->operation.low.kind==AXD_LOW_AI);
        assert(op->operation.low.wrapper_mapping==0x50000000);
        got.region=op->operation.low.ai.wrapper;
        switch(op->operation.low.ai.kind) {
        case AXD_LOW_AI_STORE_BARRIER:got.kind=AXD_D11_STORE_BARRIER;break;
        case AXD_LOW_AI_FULL_BARRIER:got.kind=AXD_D11_FULL_BARRIER;break;
        case AXD_LOW_AI_WRITE32:got.kind=AXD_D11_WRITE32;break;
        case AXD_LOW_AI_READ32:got.kind=AXD_D11_READ32;break;
        case AXD_LOW_AI_DELAY_US:got.kind=AXD_D11_DELAY_US;break;
        }
        got.offset=op->operation.low.ai.offset;got.value=op->operation.low.ai.value;
        assert(!f->job->context.clock.d11.core_clock_valid && !f->job->low_completed);break;
    }
    if(memcmp(&got,e,sizeof(got))) {
        fprintf(stderr,"core event%zu got %u/%u/%u/%u/%u %x=%x wanted %u/%u/%u/%u/%u %x=%x\n",
            f->seen,got.path,got.kind,got.region,got.queue,got.secondary,got.offset,got.value,
            e->path,e->kind,e->region,e->queue,e->secondary,e->offset,e->value);
        assert(0 && "literal event mismatch");
    }
    if(f->seen==f->control_start) {
        unsigned i;assert(f->job->context.control.psm.main_control==0x04000400);
        for(i=0;i<9;i++)assert(!f->job->context.control.suspended_bitmap[i]);
        assert(!f->job->context.control.suspended_count && !f->job->context.control.reset_halfword);
        assert(!f->job->context.control.psm.wake_reasons && !f->job->context.control.psm.mute_override);
        assert(f->job->context.control.psm.main_depth==7 && f->job->context.control.psm.extended_depth==11);
    }
    if(f->seen==f->extended_start)assert(f->job->context.control.psm.extended_control==0x400);
    if(f->seen==f->tail_start) {
        assert(!f->job->context.word164 && !f->job->context.tail_byte);
        assert(f->job->context.word22c==0x55667788);
    }
    if(f->seen==f->fault && f->cancel)assert(axd_core_reset_cancel(f->job)==AXD_CORE_CANCELLED);
    return f->seen==f->fault && f->error ? -1 : 0;
}
static void terminal(struct fixture *f)
{
    struct axd_core_reset before=*f->job;size_t seen=f->seen;
    assert(axd_core_reset_step(f->job,io,f)==before.phase);
    assert(axd_core_reset_step(f->job,NULL,NULL)==before.phase);
    assert(axd_core_reset_cancel(f->job)==before.phase);
    assert(seen==f->seen && !memcmp(&before,f->job,sizeof(before)));
}
static void run(struct fixture *f)
{
    size_t steps=0;
    while(f->job->phase==AXD_CORE_RUNNING) {
        size_t seen=f->seen;
        struct axd_wl_clock wl=f->job->wl_job;
        struct axd_dma_reset dma=f->job->dma_job;
        struct axd_mctrl mctrl=f->job->mctrl_job;
        struct axd_psm_control psm=f->job->psm_job;
        struct axd_phy_reset phy=f->job->phy_job;
        struct axd_clock_control clock=f->job->pll_job;
        assert(++steps<f->script->count*40+3000);
        axd_core_reset_step(f->job,io,f);
        /* Child mutation is compared only at the propagation boundary; the
         * expected I/O and effects above remain independent literal fixtures. */
        if(memcmp(&wl,&f->job->wl_job,sizeof(wl)))
            assert(!memcmp(&f->job->context.clock.d11,&f->job->wl_job.context.d11,sizeof(wl.context.d11)));
        if(memcmp(&dma,&f->job->dma_job,sizeof(dma))) {
            const struct axd_core_dma_slot *slot=&f->job->context.dma[f->job->queue];
            const struct axd_dma_reset_context *actual=f->job->dma_job.direction==AXD_DMA_RX_RESET ? &slot->rx :
                f->job->secondary ? &slot->paired_tx : &slot->tx;
            assert(!memcmp(actual,&f->job->dma_job.context,sizeof(*actual)));
        }
        if(memcmp(&mctrl,&f->job->mctrl_job,sizeof(mctrl)))
            assert(!memcmp(&f->job->context.control,&f->job->mctrl_job.context,sizeof(mctrl.context)));
        if(memcmp(&psm,&f->job->psm_job,sizeof(psm)))
            assert(!memcmp(&f->job->context.control.psm,&f->job->psm_job.context,sizeof(psm.context)));
        if(memcmp(&phy,&f->job->phy_job,sizeof(phy)))
            assert(!memcmp(&f->job->context.phy,&f->job->phy_job.context,sizeof(phy.context)));
        if(memcmp(&clock,&f->job->pll_job,sizeof(clock)))
            assert(!memcmp(&f->job->context.clock.d11,&f->job->pll_job.context,sizeof(clock.context)));
        assert(f->seen-seen<=1 && f->job->attempted_ops==f->seen);
        assert(f->job->completed_ops==f->seen-(f->error && f->seen==f->fault ? 1u : 0u));
    }
    terminal(f);
}
static void execute(struct axd_core_reset *j,struct axd_core_reset_context c,
                    uint32_t flags,const struct script *s)
{
    struct fixture f=fixture(j,s);assert(axd_core_reset_begin(j,flags,&c)==0);run(&f);
    assert(j->phase==AXD_CORE_TRACE_COMPLETE && j->error==AXD_CORE_NO_ERROR);
    assert(f.seen==s->count);
}
static void noreset_paths(void)
{
    struct axd_core_reset_context c;struct axd_core_reset j;struct script s;
    unsigned fast_byte,gate;
    for(fast_byte=0;fast_byte<3;fast_byte++)for(gate=0;gate<3;gate++) {
        c=context();c.no_reset=0x80;c.clock.d11.fast_clock=fast_byte==2 ? 0x80 : (uint8_t)fast_byte;
        memset(&s,0,sizeof(s));
        if(!fast_byte)append(&s,fast,N(fast));
        append(&s,gate==0 ? down : gate==1 ? up : reset_active,gate==0 ? N(down) : N(up));
        append(&s,mctrl_reset,N(mctrl_reset));
        execute(&j,c,0x100,&s);
        assert(j.context.word164==0 && j.context.word22c==0x55667788 && j.context.tail_byte==0xa5);
        assert(j.context.clock.d11.core_clock_valid==1 && !j.low_completed && !j.control_reset_completed);
        assert(!j.phy_completed && !j.pll_completed);
        assert(j.original_fast==c.clock.d11.fast_clock && j.initial_up_valid && j.initial_up==(gate==1));
        assert(j.context.control.psm.main_control==0x04000400);
        assert(j.context.control.reset_halfword==0x7788);
    }
}
static void additional_coreup_gates(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;struct script s;
    unsigned bits;
    c.no_reset=1;
    for(bits=0;bits<4;bits++)if(bits!=1) {
        struct event gate[]={SR(0x408,0xdead0000u|bits),SY};
        memset(&s,0,sizeof(s));append(&s,gate,N(gate));append(&s,mctrl_reset,N(mctrl_reset));
        execute(&j,c,0,&s);assert(j.initial_up_valid && !j.initial_up);
    }
    c=context();
    memset(&s,0,sizeof(s));append(&s,up,N(up));append(&s,mctrl_reset,N(mctrl_reset));
    append(&s,fast,N(fast));append(&s,down,N(down));
    execute(&j,c,0,&s);assert(j.initial_up_valid && j.initial_up && j.pll_up_valid && !j.pll_up);
    assert(j.pll_completed && !j.pll_job.attempted_ops);
}
static void normal_paths_and_faults(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;
    struct fixture f;struct script s;size_t p;unsigned mode,i;
    memset(&s,0,sizeof(s));f=fixture(&j,&s);
    c.extended_supported=1;c.control.psm.mirror_enabled=1;c.control.psm.mirror_control=0x800;
    /* Main reset's mirror precedes the explicit extended write. */
    append(&s,down,N(down));f.control_start=s.count+1;
    append(&s,mctrl_reset,N(mctrl_reset));append(&s,mctrl_mirror,N(mctrl_mirror));
    f.extended_start=s.count+1;append(&s,ext_reset,N(ext_reset));
    append(&s,fast,N(fast));append(&s,up,N(up));append(&s,pll,N(pll));
    memset(c.control.suspended_bitmap,0xa5,9);c.control.suspended_count=31;
    c.control.psm.wake_reasons=0x13579;c.control.psm.mute_override=1;
    assert(axd_core_reset_begin(&j,0x100,&c)==0);run(&f);
    assert(j.phase==AXD_CORE_TRACE_COMPLETE && f.seen==s.count);
    assert(j.low_completed && j.control_reset_completed && !j.phy_completed && j.pll_completed);
    assert(j.context.clock.d11.core_clock_valid==1 && j.context.word22c==UINT32_MAX);
    assert(!j.context.word164 && !j.context.tail_byte && j.pll_up_valid && j.pll_up);
    assert(j.context.control.psm.main_depth==7 && j.context.control.psm.extended_depth==11);
    for(i=0;i<9;i++)assert(j.context.control.suspended_bitmap[i]==0);
    for(p=1;p<=s.count;p++)for(mode=1;mode<=3;mode++) {
        assert(axd_core_reset_begin(&j,0x100,&c)==0);
        f.seen=0;f.fault=p;f.error=mode&1;f.cancel=(mode>>1)&1;run(&f);
        assert(f.seen==p && j.phase==(f.error ? AXD_CORE_FAILED : AXD_CORE_CANCELLED));
        assert(j.error==(f.error ? AXD_CORE_IO_ERROR : AXD_CORE_NO_ERROR));
        assert(j.context.word22c==0x55667788 && j.context.word164==0x11223344 && j.context.tail_byte==0xa5);
        if(p>=f.control_start) {
            assert(j.context.control.psm.main_control==0x04000400);
            assert(j.context.control.psm.wake_reasons==0 && j.context.control.reset_halfword==0);
            assert(j.context.control.psm.main_depth==7 && j.context.control.psm.extended_depth==11);
        }
        if(p>=f.extended_start)assert(j.context.control.psm.extended_control==0x400);
        assert(j.context.control.psm.mirror_control==(p>=5 ? 0x04000c00u : 0x800u));
    }
}
static void original_fast_tail(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;
    struct fixture f;struct script s;size_t p;unsigned mode;
    c.clock.d11.fast_clock=0;c.clock.d11.pll_inhibit=1;
    memset(&s,0,sizeof(s));append(&s,fast,N(fast));append(&s,down,N(down));
    append(&s,mctrl_reset,N(mctrl_reset));append(&s,fast,N(fast));
    f=fixture(&j,&s);f.tail_start=s.count+1;append(&s,dynamic,N(dynamic));
    assert(axd_core_reset_begin(&j,0x100,&c)==0);run(&f);
    assert(j.phase==AXD_CORE_TRACE_COMPLETE && f.seen==s.count);
    assert(!j.context.clock.d11.fast_clock && !j.original_fast && j.context.word22c==UINT32_MAX);
    for(p=f.tail_start;p<=s.count;p++)for(mode=1;mode<=3;mode++) {
        assert(axd_core_reset_begin(&j,0x100,&c)==0);
        f.seen=0;f.fault=p;f.error=mode&1;f.cancel=(mode>>1)&1;run(&f);
        assert(j.phase==(f.error ? AXD_CORE_FAILED : AXD_CORE_CANCELLED));
        assert(j.context.word22c==0x55667788 && !j.context.word164 && !j.context.tail_byte);
    }
}
static void dma_paths(void)
{
    unsigned paired,presence,fifo3;struct axd_core_reset_context c;
    struct axd_core_reset j;struct script s;
    for(paired=0;paired<2;paired++)for(presence=0;presence<16;presence++)for(fifo3=0;fifo3<2;fifo3++) {
        unsigned i;
        c=context();c.no_reset=1;c.paired_dma=(uint8_t)paired;c.rx_fifo3=(uint8_t)(fifo3 ? 0x80 : 0);
        c.tx_count=4;c.control.suspended_count=4;c.control.suspended_bitmap[0]=15;
        c.control.psm.wake_reasons=0x18;
        memset(&s,0,sizeof(s));append(&s,up,N(up));
        for(i=0;i<4;i++) {
            c.dma[i].primary.value=presence&(1u<<i) ? 0x10000+i : 0;
            c.dma[i].secondary.value=(i&1) ? 0x20000+i : 0;
            if(c.dma[i].primary.value) {
                tx_disabled(&s,i,0);
                if(paired && c.dma[i].secondary.value)tx_disabled(&s,i,1);
            }
        }
        if(c.dma[0].primary.value)rx_disabled(&s,0);
        if(c.dma[3].primary.value && fifo3)rx_disabled(&s,3);
        append(&s,mctrl_reset,N(mctrl_reset));execute(&j,c,0,&s);
        for(i=0;i<4;i++) {
            unsigned cleared=(presence&(1u<<i)) && (!paired || (i&1));
            assert(!!(j.context.control.suspended_bitmap[0]&(1u<<i))==!cleared);
        }
        assert(j.context.control.suspended_count==4u-
            ((presence&2 ? 1u:0)+(presence&8 ? 1u:0)+(paired ? 0u:(presence&1 ? 1u:0)+(presence&4 ? 1u:0))));
        assert(j.context.control.psm.wake_reasons==(j.context.control.suspended_count ? 0x18u : 0x10u));
    }
}
static void zero_tx_and_reached_queue_bindings(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;struct script s;
    struct fixture f;unsigned i;
    c.no_reset=1;c.tx_count=0;c.rx_fifo3=7;
    for(i=0;i<4;i++)c.dma[i].primary.value=0x10000+i;
    memset(&s,0,sizeof(s));append(&s,up,N(up));rx_disabled(&s,0);rx_disabled(&s,3);
    append(&s,mctrl_reset,N(mctrl_reset));execute(&j,c,0,&s);
    c.dma[2].primary.known=0;
    memset(&s,0,sizeof(s));append(&s,up,N(up));rx_disabled(&s,0);
    f=fixture(&j,&s);assert(axd_core_reset_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_CORE_DEPENDENCY && j.last_field==AXD_CORE_PRIMARY_DMA && f.seen==s.count);
    c=context();c.no_reset=1;c.tx_count=1;c.paired_dma=1;
    c.dma[0].primary.value=0;c.dma[0].secondary.known=0;
    memset(&s,0,sizeof(s));append(&s,up,N(up));append(&s,mctrl_reset,N(mctrl_reset));
    execute(&j,c,0,&s); /* Missing secondary is genuinely unreached here. */
    c.dma[0].primary.value=0x10000;
    memset(&s,0,sizeof(s));append(&s,up,N(up));tx_disabled(&s,0,0);
    f=fixture(&j,&s);assert(axd_core_reset_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_CORE_DEPENDENCY && j.last_field==AXD_CORE_SECONDARY_DMA);
    c=context();c.clock.d11.fast_clock=0x80;c.clock.d11.si_capabilities=0;c.clock.d11.pll_inhibit=1;
    memset(&s,0,sizeof(s));append(&s,down,N(down));append(&s,mctrl_reset,N(mctrl_reset));
    execute(&j,c,0,&s);assert(j.original_fast==0x80 && j.context.word22c==UINT32_MAX);
    c.clock.d11.si_capabilities=0x10000000;append(&s,fast,N(fast));
    execute(&j,c,0,&s);assert(j.original_fast==0x80 && j.context.word22c==UINT32_MAX);
}
static void suspended_wake_paths(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;struct script s;
    struct fixture f;unsigned kind;
    for(kind=0;kind<5;kind++) {
        c=context();c.no_reset=1;c.tx_count=1;c.dma[0].primary.value=0x10000;
        c.dma[0].rx.descriptor_count=0;
        c.control.suspended_count=(uint8_t)(kind==0 ? 0 : 1);
        c.control.suspended_bitmap[0]=(uint8_t)(kind==1 ? 0 : 1);
        c.control.psm.wake_reasons=kind==2 ? 0x18 : 8;
        c.policy.public_up_known=kind!=4;
        memset(&s,0,sizeof(s));append(&s,up,N(up));tx_disabled(&s,0,0);
        if(kind!=4)append(&s,mctrl_reset,N(mctrl_reset));
        f=fixture(&j,&s);assert(axd_core_reset_begin(&j,0,&c)==0);run(&f);
        assert(f.seen==s.count);
        if(kind==4) {
            assert(j.phase==AXD_CORE_DEPENDENCY && j.context.control.psm.wake_reasons==0);
            assert(j.context.word164==0x11223344 && j.context.control.suspended_count==0);
            assert(!j.context.control.suspended_bitmap[0]);
        } else {
            assert(j.phase==AXD_CORE_TRACE_COMPLETE);
            assert(j.context.control.suspended_count==(kind<2 ? c.control.suspended_count : 0));
            assert(j.context.control.suspended_bitmap[0]==(kind<2 ? c.control.suspended_bitmap[0] : 0));
            assert(j.context.control.psm.wake_reasons==(kind<2 ? 8u : kind==2 ? 0x10u : 0u));
        }
    }
}
static void dma_and_wake_faults(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;struct script s;
    struct fixture f;size_t p;unsigned mode;
    const struct event wake[]={
        E(T_D11,AXD_D11_READ32,0,0,0,0x120,0,0x12340003),DY,DS,DW(0x120,0x403),
        DS,DW(0x120,0x400)};
    c.no_reset=1;c.paired_dma=1;c.tx_count=1;c.dma[0].primary.value=0x10000;
    c.dma[0].secondary.value=0x20000;c.dma[0].rx.descriptor_count=0;
    c.control.suspended_count=1;c.control.suspended_bitmap[0]=1;
    c.control.psm.wake_reasons=8;
    c.policy.public_up=1;c.policy.stay_awake_known=1;c.policy.stay_awake=0;
    memset(&s,0,sizeof(s));append(&s,up,N(up));tx_disabled(&s,0,0);tx_disabled(&s,0,1);
    append(&s,wake,N(wake));execute(&j,c,0,&s);
    assert(!j.context.control.psm.wake_reasons && !j.context.control.suspended_count);
    assert(!j.context.control.suspended_bitmap[0] && j.context.control.psm.main_control==0x400);
    for(p=1;p<=s.count;p++)for(mode=1;mode<=3;mode++) {
        f=fixture(&j,&s);f.fault=p;f.error=mode&1;f.cancel=(mode>>1)&1;
        assert(axd_core_reset_begin(&j,0,&c)==0);run(&f);
        assert(f.seen==p && j.phase==(f.error ? AXD_CORE_FAILED : AXD_CORE_CANCELLED));
        assert(j.error==(f.error ? AXD_CORE_IO_ERROR : AXD_CORE_NO_ERROR));
        if(p<=8) {
            assert(j.context.control.suspended_count==1 && j.context.control.suspended_bitmap[0]==1);
            assert(j.context.control.psm.wake_reasons==8);
        } else {
            assert(!j.context.control.suspended_count && !j.context.control.suspended_bitmap[0]);
            assert(!j.context.control.psm.wake_reasons);
        }
        assert(j.context.word22c==0x55667788 && !j.low_completed);
    }
}
static void forced_wake_band_binding(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;struct script s;
    struct fixture f;unsigned known;
    const struct event wake[]={E(T_D11,AXD_D11_READ32,0,0,0,0x120,0,0),DY,DS,DW(0x120,0x04000403)};
    for(known=0;known<2;known++) {
        c=context();c.no_reset=1;c.tx_count=1;c.dma[0].primary.value=0x10000;
        c.dma[0].rx.descriptor_count=0;c.control.suspended_count=1;
        c.control.suspended_bitmap[0]=1;c.control.psm.wake_reasons=8;
        c.control.psm.main_control=0x403;
        c.policy.public_up=1;c.policy.stay_awake_known=1;c.policy.stay_awake=1;
        c.band.known=(uint8_t)known;c.band.value=0;
        /* An inherited stale band-present1 must not override canonical null. */
        c.control.psm.band_present=1;
        memset(&s,0,sizeof(s));append(&s,up,N(up));tx_disabled(&s,0,0);append(&s,wake,N(wake));
        if(known)append(&s,mctrl_reset,N(mctrl_reset));
        f=fixture(&j,&s);assert(axd_core_reset_begin(&j,0,&c)==0);run(&f);
        assert(f.seen==s.count && j.context.control.psm.main_control==(known ? 0x04000400u : 0x04000403u));
        assert(j.phase==(known ? AXD_CORE_TRACE_COMPLETE : AXD_CORE_DEPENDENCY));
        assert(!j.context.control.suspended_count && !j.context.control.psm.wake_reasons);
        assert(j.context.word164==(known ? 0u : 0x11223344u));
        assert(j.context.word22c==0x55667788 && j.context.tail_byte==0xa5);
    }
}
static void discovery_and_phy(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;struct script s;
    struct fixture f;size_t p;unsigned mode;
    c.band.value=0x9000;c.phy.band_type=0;c.phy.band_revision=0;c.clock.d11.pll_inhibit=1;
    memset(&s,0,sizeof(s));append(&s,down,N(down));append(&s,discovery,N(discovery));
    append(&s,ac_flags,N(ac_flags));append(&s,mctrl_reset,N(mctrl_reset));append(&s,fast,N(fast));
    execute(&j,c,0,&s);assert(j.context.phy.band_type==0xb && j.context.phy.band_revision==51);
    for(p=3;p<=6;p++)for(mode=1;mode<=3;mode++) {
        f=fixture(&j,&s);f.fault=p;f.error=mode&1;f.cancel=(mode>>1)&1;
        assert(axd_core_reset_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_CORE_FAILED : AXD_CORE_CANCELLED));
        /* Stores occur AFTER their successful full barrier returns, as an
         * internal transition; cancellation at that barrier suppresses it. */
        assert(j.context.phy.band_type==(p>4 ? 0xbu : 0u));
        assert(j.context.phy.band_revision==0);
    }
    c.phy.band_revision=51;
    memset(&s,0,sizeof(s));append(&s,down,N(down));append(&s,mctrl_reset,N(mctrl_reset));append(&s,fast,N(fast));
    execute(&j,c,0,&s);assert(j.context.phy.band_type==0 && j.context.phy.band_revision==51);
    c.phy=phy_context();c.phy_shared.value=0xa000;c.phy_shared_clock=0x7f;
    memset(&s,0,sizeof(s));append(&s,down,N(down));append(&s,ac_flags,N(ac_flags));
    append(&s,mctrl_reset,N(mctrl_reset));append(&s,fast,N(fast));
    append(&s,phy_full,N(phy_full));
    execute(&j,c,0,&s);
    assert(j.phy_completed && j.context.phy_shared_clock==1);
    assert(j.context.phy.clock.bandwidth_state==0x2800 && j.context.phy.clock.phy_clock==1);
    for(p=1;p<=s.count;p++)for(mode=1;mode<=3;mode++) {
        f=fixture(&j,&s);f.fault=p;f.error=mode&1;f.cancel=(mode>>1)&1;
        assert(axd_core_reset_begin(&j,0,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_CORE_FAILED : AXD_CORE_CANCELLED));
        assert(j.error==(f.error ? AXD_CORE_IO_ERROR : AXD_CORE_NO_ERROR));
        assert(j.context.word22c==0x55667788 && !j.phy_completed);
        if(p>2)assert(j.context.phy_shared_clock==1);
    }
    c.phy_shared.value=0;execute(&j,c,0,&s);assert(j.context.phy_shared_clock==0x7f);
    c.phy_shared.known=0;
    memset(&s,0,sizeof(s));append(&s,down,N(down));f=fixture(&j,&s);
    assert(axd_core_reset_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_CORE_DEPENDENCY && j.last_field==AXD_CORE_PHY_SHARED && f.seen==s.count);
}
static void bindings_and_alias(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;struct script s;
    struct fixture f;unsigned which;
    memset(&s,0,sizeof(s));
    c.band.known=0;
    f=fixture(&j,&s);assert(axd_core_reset_begin(&j,UINT32_MAX,&c)==0);run(&f);
    assert(j.phase==AXD_CORE_DEPENDENCY && j.last_field==AXD_CORE_BAND);
    c.band.known=1;f=fixture(&j,&s);
    assert(axd_core_reset_begin(&j,UINT32_MAX,&c)==0);run(&f);
    assert(j.phase==AXD_CORE_FAILED && j.error==AXD_CORE_BINDING);
    for(which=0;which<3;which++) {
        c=context();c.no_reset=1;c.band.value=0x9000;c.band_flags=0x12345678;
        c.phy.phy_pointer.value=which==0 ? 0 : 0x1000;c.phy.phy_pointer.known=which!=2;
        memset(&s,0,sizeof(s));
        if(which!=2) {append(&s,down,N(down));append(&s,mctrl_reset,N(mctrl_reset));}
        f=fixture(&j,&s);assert(axd_core_reset_begin(&j,UINT32_MAX,&c)==0);run(&f);
        assert(j.phase==(which==2 ? AXD_CORE_DEPENDENCY : AXD_CORE_TRACE_COMPLETE));
        if(which!=2)assert(j.flags==(which==0 ? 0u : 0x12345678u));
    }
    c=context();c.no_reset=1;memset(&s,0,sizeof(s));append(&s,up,N(up));
    c.tx_count=73;c.dma_slots=72;f=fixture(&j,&s);
    assert(axd_core_reset_begin(&j,0,&c)==0);run(&f);
    assert(j.phase==AXD_CORE_FAILED && j.error==AXD_CORE_BOUNDS);
    c=context();c.no_reset=1;memset(&s,0,sizeof(s));append(&s,down,N(down));append(&s,mctrl_reset,N(mctrl_reset));
    execute(&j,c,0,&s);
    /* Snapshot own context BEFORE memset(job). Cache-match avoids the second
     * mctrl write, independently expected from the retained helper contract. */
    memset(&s,0,sizeof(s));append(&s,down,N(down));f=fixture(&j,&s);
    assert(axd_core_reset_begin(&j,0,&j.context)==0);run(&f);
    assert(j.phase==AXD_CORE_TRACE_COMPLETE && f.seen==s.count && j.context.word22c==0x55667788);
    assert(axd_core_reset_begin(NULL,0,&c)<0);
}
#define LA(k,o,v,a) E(T_LOW,AXD_D11_##k,0,0,0,o,v,a)
static const struct event lower_reset[] = {
    LA(READ32,0x804,0,0),LA(FULL_BARRIER,0,0,0),
    LA(STORE_BARRIER,0,0,0),LA(WRITE32,0x800,1,0),LA(DELAY_US,0,10,0),
    LA(READ32,0x804,0,0),LA(FULL_BARRIER,0,0,0),
    LA(STORE_BARRIER,0,0,0),LA(WRITE32,0x408,0x127,0),
    LA(READ32,0x408,0,0xc0de0007),LA(FULL_BARRIER,0,0,0),
    LA(DELAY_US,0,1,0),LA(STORE_BARRIER,0,0,0),LA(WRITE32,0x408,0xc0de0005,0),
    LA(READ32,0x804,0,0),LA(FULL_BARRIER,0,0,0),
    LA(READ32,0x800,0,1),LA(FULL_BARRIER,0,0,0),
    LA(READ32,0x804,0,0),LA(FULL_BARRIER,0,0,0),
    LA(STORE_BARRIER,0,0,0),LA(WRITE32,0x800,0,0),
    LA(READ32,0x804,0,0),LA(FULL_BARRIER,0,0,0),
    LA(READ32,0x800,0,0),LA(FULL_BARRIER,0,0,0),
    LA(STORE_BARRIER,0,0,0),LA(WRITE32,0x408,0x107,0),
    LA(READ32,0x408,0,0xa55a000b),LA(FULL_BARRIER,0,0,0),
    LA(DELAY_US,0,1,0),LA(STORE_BARRIER,0,0,0),
    LA(WRITE32,0x408,0xa55a0009,0),LA(DELAY_US,0,1,0)
};
static void real_lower_boundaries(void)
{
    struct axd_core_reset_context c=context();struct axd_core_reset j;
    struct script s;struct fixture f;size_t p;unsigned mode;
    c.low.reset_callback_present=1;c.clock.d11.pll_inhibit=1;
    memset(&s,0,sizeof(s));append(&s,down,N(down));append(&s,lower_reset,N(lower_reset));
    append(&s,mctrl_reset,N(mctrl_reset));append(&s,fast,N(fast));
    execute(&j,c,0x100,&s);assert(j.low_completed && j.context.clock.d11.core_clock_valid);
    assert(j.low_job.effective_bits==0x104 && j.low_job.effective_reset_bits==0x20);
    for(p=3;p<=2+N(lower_reset);p++)for(mode=1;mode<=3;mode++) {
        f=fixture(&j,&s);f.fault=p;f.error=mode&1;f.cancel=(mode>>1)&1;
        assert(axd_core_reset_begin(&j,0x100,&c)==0);run(&f);
        assert(j.phase==(f.error ? AXD_CORE_FAILED : AXD_CORE_CANCELLED));
        assert(!j.context.clock.d11.core_clock_valid && !j.low_completed);
        assert(j.context.word22c==0x55667788 && j.context.word164==0x11223344);
        assert(j.context.control.reset_halfword==0x7788);
        assert(j.context.low.state.primary_wrapper==0x50000000);
    }
}
int main(void)
{
    noreset_paths();additional_coreup_gates();normal_paths_and_faults();original_fast_tail();dma_paths();real_lower_boundaries();
    zero_tx_and_reached_queue_bindings();suspended_wake_paths();dma_and_wake_faults();forced_wake_band_binding();discovery_and_phy();bindings_and_alias();
    puts("core-reset independent traces, gates, partial effects and callback exits passed");return 0;
}
