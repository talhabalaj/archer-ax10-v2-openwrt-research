/* SPDX-License-Identifier: MIT */
#include "../driver/bf_clean.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define U8(v) ((struct axd_wake_u8){1,(v)})
#define U32(v) ((struct axd_wake_u32){(v),1})
#define U16(v) ((struct axd_bf_u16){(v),1})
#define N(a) (sizeof(a)/sizeof((a)[0]))
#define P AXD_BF_PRIMARY
#define S AXD_BF_SECONDARY
#define G AXD_BF_GLOBAL_INDEX
struct event {
    unsigned metadata;
    enum axd_bf_field field;
    enum axd_bf_list_id list;
    uint32_t index, value;
    struct axd_d11_operation io;
    uint32_t reply;
};
#define M(f,l,i,v) {1,f,l,i,v,{0,0,0},0}
#define MG(f,v) M(f,AXD_BF_NO_LIST,G,v)
#define IO(k,o,v,r) {0,0,0,0,0,{k,o,v},r}
#define R(o,v) IO(AXD_D11_READ32,o,0,v)
#define W(o,v) IO(AXD_D11_WRITE32,o,v,0)
#define SY IO(AXD_D11_FULL_BARRIER,0,0,0)
#define ST IO(AXD_D11_STORE_BARRIER,0,0,0)
#define R16(o,v) IO(AXD_D11_READ16,o,0,v)
/* Source-derived literal main/extended PSM traces, with reason8 already held.
 * Main enable completes before extended suspend, not a combined hold. */
static const struct event active[] = {
    MG(AXD_BF_T_WT,0x2000), MG(AXD_BF_WT_HW,0x3000),
    R(0x120,3),SY,R(0x120,3),SY,R(0x128,0),SY,ST,W(0x120,0x04000002),
    R(0x128,1),SY,R(0x128,1),SY,R(0x120,2),SY,
    M(AXD_BF_BITMAP,P,0,0),M(AXD_BF_PRIMARY_LAST,P,0,0),
    MG(AXD_BF_WT_HW,0x3000),R(0x120,2),SY,ST,W(0x120,0x04000003),
    ST,W(0x128,1),R(0x120,3),SY,R(0x128,0),SY,
    ST,W(0x160,0x10010),R(0x160,0xabcdef),SY,R16(0x164,2),SY,
    MG(AXD_BF_WT_PUBLIC,0x4000),MG(AXD_BF_PUBLIC_EXT,1),
    MG(AXD_BF_WT_HW,0x3000),R(0x1b4,3),SY,R(0x1b4,3),SY,R(0x1c0,0),SY,
    ST,W(0x1b4,2),R(0x1c0,1),SY,R(0x1c0,1),SY,R(0x1b4,2),SY,
    M(AXD_BF_SECONDARY_COUNT,S,0,0),MG(AXD_BF_WT_HW,0x3000),
    R(0x1b4,2),SY,R(0x1b4,0x80000000),SY,ST,W(0x1b4,3),ST,W(0x1c0,1),
    R(0x1b4,3),SY,R(0x1c0,0),SY
};
#define INV_LEAF MG(AXD_BF_T_WT,0x2000),MG(AXD_BF_WT21,255),MG(AXD_BF_WT_PUBLIC,0x4000),MG(AXD_BF_PUBLIC_CORE,130)
#define SET_LEAF MG(AXD_BF_T_WT,0x2000),MG(AXD_BF_WT21,255),MG(AXD_BF_WT_PUBLIC,0x4000),MG(AXD_BF_PUBLIC_EXT,1),MG(AXD_BF_PUBLIC_CORE,130)
/* Primary and secondary cover valid peer, bitmap skip, null slot, null cubby.
 * Bounds and primary/secondary count reloads are spelled out, not C-derived. */
static const struct event scan[] = {
    MG(AXD_BF_T_WT,0x2000),MG(AXD_BF_WT_HW,0x3000),R(0x120,0),SY,
    M(AXD_BF_BITMAP,P,0,13),M(AXD_BF_SLOT,P,0,0x5000),MG(AXD_BF_CUBBY_OFFSET,0x24),
    M(AXD_BF_PEER,P,0,0x6000),INV_LEAF,SET_LEAF,M(AXD_BF_PRIMARY_LAST,P,0,3),
    M(AXD_BF_BITMAP,P,1,13),M(AXD_BF_PRIMARY_LAST,P,1,3),
    M(AXD_BF_BITMAP,P,2,13),M(AXD_BF_SLOT,P,2,0),M(AXD_BF_PRIMARY_LAST,P,2,3),
    M(AXD_BF_BITMAP,P,3,13),M(AXD_BF_SLOT,P,3,0x5100),MG(AXD_BF_CUBBY_OFFSET,0x24),
    M(AXD_BF_PEER,P,3,0),M(AXD_BF_PRIMARY_LAST,P,3,3),
    MG(AXD_BF_WT_HW,0x3000),R(0x120,0),SY,MG(AXD_BF_WT_PUBLIC,0x4000),
    MG(AXD_BF_PUBLIC_EXT,1),MG(AXD_BF_WT_HW,0x3000),R(0x1b4,0),SY,
    M(AXD_BF_SECONDARY_COUNT,S,0,4),
    M(AXD_BF_BITMAP,S,0,13),M(AXD_BF_SLOT,S,0,0x5000),MG(AXD_BF_CUBBY_OFFSET,0x24),
    M(AXD_BF_PEER,S,0,0x6000),INV_LEAF,SET_LEAF,M(AXD_BF_SECONDARY_COUNT,S,0,4),
    M(AXD_BF_BITMAP,S,1,13),M(AXD_BF_BITMAP,S,2,13),M(AXD_BF_SLOT,S,2,0),
    M(AXD_BF_BITMAP,S,3,13),M(AXD_BF_SLOT,S,3,0x5100),MG(AXD_BF_CUBBY_OFFSET,0x24),
    M(AXD_BF_PEER,S,3,0),MG(AXD_BF_WT_HW,0x3000),R(0x1b4,0),SY
};
struct fixture {
    struct axd_bf_metadata m;
    struct axd_bf_clean_context c;
    struct axd_wake_u8 primary_bitmap[32], secondary_bitmap[32];
    struct axd_bf_slot primary_slots[256], secondary_slots[256];
};
struct trace {
    struct axd_bf_clean *job;
    const struct event *events;
    size_t count, cursor;
    uint64_t io_count, metadata_count;
    size_t fault;
    unsigned cancel, error;
};
static void setup(struct fixture *f)
{
    unsigned i;
    memset(f,0,sizeof(*f));
    f->c.mac.core=130; f->c.mac.minor=1; f->c.mac.chip_id=0xf6ca;
    f->c.mac.wake_reasons=8; f->c.mac.main_control=0x04000003; f->c.mac.extended_control=3;
    f->c.txbf_identity=0x1000; f->c.wlc_identity=0x2000; f->c.hardware_identity=0x3000;
    f->c.metadata=&f->m;
    f->m.txbf_wlc=U32(0x2000); f->m.wt_hardware=U32(0x3000); f->m.wt_public=U32(0x4000);
    f->m.cubby_offset=U32(0x24); f->m.primary_last=U32(0);
    f->m.wt21=U8(255); f->m.public_core=U16(130); f->m.public_extended=U8(1);
    f->m.secondary_count=U8(0);
    f->m.primary=(struct axd_bf_list){f->primary_bitmap,32,f->primary_slots,256};
    f->m.secondary=(struct axd_bf_list){f->secondary_bitmap,32,f->secondary_slots,256};
    for(i=0;i<32;i++) f->primary_bitmap[i]=f->secondary_bitmap[i]=U8(0);
    for(i=0;i<256;i++) {
        f->primary_slots[i].pointer=f->secondary_slots[i].pointer=U32(0);
        f->primary_slots[i].peer=f->secondary_slots[i].peer=U32(0);
    }
}
static void scan_setup(struct fixture *f)
{
    setup(f); f->m.primary_last=U32(3); f->m.secondary_count=U8(4);
    f->primary_bitmap[0]=f->secondary_bitmap[0]=U8(13);
    f->primary_slots[0].pointer=f->secondary_slots[0].pointer=U32(0x5000);
    f->primary_slots[0].peer=f->secondary_slots[0].peer=U32(0x6000);
    f->primary_slots[3].pointer=f->secondary_slots[3].pointer=U32(0x5100);
}
static int io(void *opaque, const struct axd_d11_operation *op, uint32_t *value)
{
    struct trace *t=opaque;
    size_t n=t->cursor++;
    if(t->events!=NULL) {
        const struct event *e;
        assert(n<t->count); e=&t->events[n];
        assert(!e->metadata && op->kind==e->io.kind && op->offset==e->io.offset && op->value==e->io.value);
        *value=e->reply;
    } else *value=0;
    t->io_count++;
    if(n==t->fault) {
        if(t->cancel) assert(axd_bf_clean_cancel(t->job)==AXD_BF_CANCELLED);
        if(t->error) return -1;
    }
    return 0;
}
static void step(struct axd_bf_clean *j, struct trace *t)
{
    uint64_t before_io=t->io_count, before_meta=j->metadata_accesses;
    axd_bf_clean_step(j,io,t);
    assert(t->io_count-before_io<=1 && j->metadata_accesses-before_meta<=1);
    assert((t->io_count-before_io)+(j->metadata_accesses-before_meta)<=1);
    if(j->metadata_accesses!=before_meta) {
        if(t->events!=NULL) {
            const struct event *e;
            assert(t->cursor<t->count); e=&t->events[t->cursor];
            assert(e->metadata && j->last_access.known==1);
            assert(j->last_access.field==e->field && j->last_access.list==e->list);
            assert(j->last_access.index==e->index && j->last_access.value==e->value);
        }
        t->metadata_count++; t->cursor++;
    }
    assert(j->metadata_accesses==t->metadata_count && j->attempted_ops==t->io_count);
    assert(j->pending_callouts==j->mac_job.pending_callouts);
}
static void run(struct axd_bf_clean *j, struct trace *t)
{
    unsigned steps=0;
    while(j->phase==AXD_BF_RUNNING) { assert(++steps<50000); step(j,t); }
}
static struct trace trace(struct axd_bf_clean *j,const struct event *e,size_t n)
{
    const struct trace t={j,e,n,0,0,0,(size_t)-1,0,0}; return t;
}
static void silent(struct axd_bf_clean *j,struct trace *t)
{
    struct axd_bf_clean before=*j;
    size_t cursor=t->cursor;
    assert(axd_bf_clean_step(j,io,t)==before.phase);
    assert(axd_bf_clean_cancel(j)==before.phase);
    assert(memcmp(j,&before,sizeof(*j))==0 && t->cursor==cursor);
}
static void literal_and_faults(void)
{
    struct fixture f;
    struct axd_bf_clean j;
    struct trace t;
    unsigned which,mode;
    size_t n,i;
    for(which=0;which<2;which++) {
        const struct event *e=which?scan:active; n=which?N(scan):N(active);
        if(which) scan_setup(&f); else setup(&f);
        assert(axd_bf_clean_begin(&j,&f.c)==0); t=trace(&j,e,n); run(&j,&t);
        assert(j.phase==AXD_BF_TRACE_COMPLETE && j.error==AXD_BF_NO_ERROR && t.cursor==n);
        assert(j.context.mac.main_depth==0 && j.context.mac.extended_depth==0);
        assert(j.context.mac.wake_reasons==8 && j.primary_iterations==(which?4u:1u));
        assert(j.secondary_iterations==(which?4u:0u));
        assert(j.invalidation_calls==(which?2u:0u) && j.enable_calls==j.invalidation_calls);
        if(which) assert(j.invalidate_result_valid && j.invalidate_result==255 && j.enable_value==255);
        silent(&j,&t);
        for(i=0;i<n;i++) {
            if(e[i].metadata) continue;
            for(mode=0;mode<3;mode++) {
                assert(axd_bf_clean_begin(&j,&f.c)==0); t=trace(&j,e,n);
                t.fault=i; t.error=mode!=1; t.cancel=mode!=0; run(&j,&t);
                assert(j.phase==(mode==1?AXD_BF_CANCELLED:AXD_BF_FAILED));
                assert(j.error==(mode==1?AXD_BF_NO_ERROR:AXD_BF_IO_ERROR));
                assert(t.cursor==i+1 && j.completed_ops==t.io_count-(mode!=1));
                assert(memcmp(&j.context.mac,&j.mac_job.context,sizeof(j.context.mac))==0);
                silent(&j,&t);
            }
        }
        /* External cancellation at each metadata boundary is terminal too. */
        for(i=0;i<n;i++) if(e[i].metadata) {
            assert(axd_bf_clean_begin(&j,&f.c)==0); t=trace(&j,e,n);
            while(t.cursor<=i) { assert(j.phase==AXD_BF_RUNNING); step(&j,&t); }
            assert(axd_bf_clean_cancel(&j)==AXD_BF_CANCELLED); silent(&j,&t);
        }
    }
}
static void generic_run(struct fixture *f, struct axd_bf_clean *j, struct trace *t)
{
    assert(axd_bf_clean_begin(j,&f->c)==0); *t=trace(j,NULL,0); run(j,t);
}
static void missing_and_invalid(void)
{
    struct fixture f;
    struct axd_bf_clean j;
    struct trace t;
    unsigned i;
    const enum axd_bf_field missing[]={AXD_BF_METADATA,AXD_BF_T_WT,AXD_BF_WT_HW,
        AXD_BF_BITMAP,AXD_BF_SLOT,AXD_BF_CUBBY_OFFSET,AXD_BF_PEER,
        AXD_BF_WT21,AXD_BF_WT_PUBLIC,AXD_BF_PUBLIC_CORE,AXD_BF_PUBLIC_EXT,
        AXD_BF_PRIMARY_LAST,AXD_BF_SECONDARY_COUNT};
    for(i=0;i<N(missing);i++) {
        scan_setup(&f);
        switch(missing[i]) {
        case AXD_BF_METADATA: f.c.metadata=NULL; break;
        case AXD_BF_T_WT: f.m.txbf_wlc.known=0; break;
        case AXD_BF_WT_HW: f.m.wt_hardware.known=0; break;
        case AXD_BF_BITMAP: f.primary_bitmap[0].known=0; break;
        case AXD_BF_SLOT: f.primary_slots[0].pointer.known=0; break;
        case AXD_BF_CUBBY_OFFSET: f.m.cubby_offset.known=0; break;
        case AXD_BF_PEER: f.primary_slots[0].peer.known=0; break;
        case AXD_BF_WT21: f.m.wt21.known=0; break;
        case AXD_BF_WT_PUBLIC: f.m.wt_public.known=0; break;
        case AXD_BF_PUBLIC_CORE: f.m.public_core.known=0; break;
        case AXD_BF_PUBLIC_EXT: f.m.public_extended.known=0; break;
        case AXD_BF_PRIMARY_LAST: f.m.primary_last.known=0; break;
        case AXD_BF_SECONDARY_COUNT: f.m.secondary_count.known=0; break;
        default: assert(0);
        }
        generic_run(&f,&j,&t); assert(j.phase==AXD_BF_DEPENDENCY && j.last_access.field==missing[i]);
        assert(j.last_access.known==0 && j.error==AXD_BF_NO_ERROR); silent(&j,&t);
        if(missing[i]==AXD_BF_PUBLIC_CORE) assert(!j.invalidate_result_valid && j.enable_calls==0);
    }
    setup(&f); f.m.txbf_wlc=U32(0x9999); generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_BINDING && t.io_count==0);
    setup(&f); f.m.wt_hardware=U32(0); generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_BINDING && t.io_count==0);
    scan_setup(&f); f.m.public_core=U16(127); generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_UNSUPPORTED && !j.invalidate_result_valid);
    scan_setup(&f); f.m.wt21.known=2; generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_BAD_ARGUMENT);
    /* WT21 zero really returns before public core; no fabricated SHM callout. */
    scan_setup(&f); f.m.wt21=U8(0); f.m.public_core.known=0; generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_TRACE_COMPLETE && j.invalidate_result_valid && j.invalidate_result==0 && j.enable_value==0);
    /* All skipped peers require neither slot storage, cubby offset nor leaf state. */
    setup(&f); f.m.primary.slots=f.m.secondary.slots=NULL; f.m.cubby_offset.known=f.m.wt21.known=0;
    f.m.secondary_count=U8(8); generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_TRACE_COMPLETE && j.invalidation_calls==0 && j.secondary_iterations==8);
    assert(j.attempted_ops==8); /* All four inactive PSM wrappers still ran. */
}
static void bounds_and_lifecycle(void)
{
    struct fixture f;
    struct axd_bf_clean j;
    struct trace t;
    setup(&f); f.m.primary_last=U32(255); f.m.secondary_count=U8(255);
    generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_TRACE_COMPLETE && j.primary_iterations==256 && j.secondary_iterations==255);
    setup(&f); f.m.primary_last=U32(0xffffffff); generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_BOUNDS && j.last_access.field==AXD_BF_PRIMARY_LAST);
    assert(j.primary_iterations==0 && j.metadata_accesses==4 && t.io_count==2);
    scan_setup(&f); f.m.primary_last=U32(0xffffffff); generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_BOUNDS && t.io_count==2);
    assert(j.invalidation_calls==1 && j.enable_calls==1 && j.invalidate_result_valid);
    assert(j.invalidate_result==255 && j.enable_value==255 && j.primary_iterations==0);
    setup(&f); f.m.primary.bitmap_length=0; generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_BOUNDS && j.last_access.field==AXD_BF_BITMAP);
    setup(&f); f.m.primary.bitmap=NULL; generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_DEPENDENCY && j.last_access.field==AXD_BF_BITMAP);
    setup(&f); f.primary_bitmap[0]=U8(1); f.m.primary.slot_length=0; generic_run(&f,&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_BOUNDS && j.last_access.field==AXD_BF_SLOT);
    /* No extended-feature input is assumed from the core number. */
    setup(&f); f.m.public_extended=U8(0); f.m.secondary_count.known=0;
    generic_run(&f,&j,&t); assert(j.phase==AXD_BF_TRACE_COMPLETE && t.io_count==4);
    assert(axd_bf_clean_begin(&j,&j.context)==0); t=trace(&j,NULL,0); run(&j,&t);
    assert(j.phase==AXD_BF_TRACE_COMPLETE && j.context.metadata==&f.m);
    setup(&f); assert(axd_bf_clean_begin(&j,&f.c)==0);
    axd_bf_clean_step(&j,NULL,NULL); axd_bf_clean_step(&j,NULL,NULL); axd_bf_clean_step(&j,NULL,NULL);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_BAD_ARGUMENT && j.attempted_ops==0);
    setup(&f); f.c.mac.core=129; assert(axd_bf_clean_begin(&j,&f.c)!=0 && j.error==AXD_BF_MAC_ERROR);
    setup(&f); f.c.txbf_identity=0; assert(axd_bf_clean_begin(&j,&f.c)!=0 && j.error==AXD_BF_BAD_ARGUMENT);
    assert(axd_bf_clean_begin(NULL,&f.c)!=0 && axd_bf_clean_begin(&j,NULL)!=0);
    assert(axd_bf_clean_step(NULL,NULL,NULL)==AXD_BF_INVALID && axd_bf_clean_cancel(NULL)==AXD_BF_INVALID);
    memset(&j,0,sizeof(j)); t=trace(&j,NULL,0); silent(&j,&t);
}
static void semantic_child_exits(void)
{
    static const struct event nested[]={MG(AXD_BF_T_WT,0x2000),MG(AXD_BF_WT_HW,0x3000),R(0x120,3),SY};
    struct fixture f;
    struct axd_bf_clean j;
    struct trace t;
    size_t first_outer=0;
    setup(&f); f.c.mac.main_depth=1;
    assert(axd_bf_clean_begin(&j,&f.c)==0); t=trace(&j,nested,N(nested)); run(&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_MAC_ERROR && t.cursor==N(nested));
    assert(j.context.mac.main_depth==2 && j.pending_callouts==AXD_PSM_FATAL);
    assert(j.primary_iterations==0 && j.invalidation_calls==0); silent(&j,&t);
    setup(&f); f.c.mac.wake_reasons=0;
    assert(axd_bf_clean_begin(&j,&f.c)==0); t=trace(&j,active,N(active)); run(&j,&t);
    while(first_outer<N(active) && active[first_outer].field!=AXD_BF_WT_PUBLIC) first_outer++;
    assert(j.phase==AXD_BF_DEPENDENCY && j.pending_callouts==AXD_PSM_WAKE_CONTROL);
    assert(t.cursor==first_outer && j.context.mac.main_depth==0 && j.context.mac.wake_reasons==0);
    assert(j.context.mac.extended_depth==0 && j.secondary_iterations==0); silent(&j,&t);
    setup(&f); f.c.mac.extended_depth=1;
    assert(axd_bf_clean_begin(&j,&f.c)==0); t=trace(&j,active,N(active)); run(&j,&t);
    assert(j.phase==AXD_BF_FAILED && j.error==AXD_BF_MAC_ERROR && j.pending_callouts==AXD_PSM_FATAL);
    assert(j.context.mac.main_depth==0 && j.context.mac.extended_depth==2 && j.secondary_iterations==0);
    silent(&j,&t);
}
int main(void)
{
    literal_and_faults(); missing_and_invalid(); bounds_and_lifecycle(); semantic_child_exits();
    puts("BF cleaner: literal metadata/MAC order, leaf gates, bounds and callback exits passed");
    return 0;
}
