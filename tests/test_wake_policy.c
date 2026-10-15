/* SPDX-License-Identifier: MIT */
#include "../driver/wake_policy.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define U8(v) ((struct axd_wake_u8){1,(v)})
#define U32(v) ((struct axd_wake_u32){(v),1})
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static const enum axd_wake_field globals[] = {
    AXD_WAKE_W19C, AXD_WAKE_W19B, AXD_WAKE_WB4, AXD_WAKE_B4_C,
    AXD_WAKE_W2FC, AXD_WAKE_2FC_4, AXD_WAKE_2FC_4_C,
    AXD_WAKE_PUBLIC, AXD_WAKE_PUBLIC58, AXD_WAKE_W530, AXD_WAKE_RRM14,
    AXD_WAKE_RRM_C, AXD_WAKE_W22C, AXD_WAKE_ASSOC4, AXD_WAKE_ASSOC_WORD,
    AXD_WAKE_W199, AXD_WAKE_W19A, AXD_WAKE_W198, AXD_WAKE_W300,
    AXD_WAKE_W336, AXD_WAKE_W568, AXD_WAKE_W188, AXD_WAKE_W18,
    AXD_WAKE_OVERRIDE, AXD_WAKE_W1A0, AXD_WAKE_W1E0
};
struct field_case { size_t offset; enum axd_wake_field field; unsigned wide, positive; };
#define BYTE(n,f,p) {offsetof(struct axd_wake_inputs,n),f,0,p}
#define WORD(n,f,p) {offsetof(struct axd_wake_inputs,n),f,1,p}
static const struct field_case fields[] = {
    BYTE(w19c,AXD_WAKE_W19C,1), BYTE(w19b,AXD_WAKE_W19B,1),
    WORD(wb4,AXD_WAKE_WB4,0), BYTE(b4_c,AXD_WAKE_B4_C,1),
    WORD(w2fc,AXD_WAKE_W2FC,0), WORD(object2fc_4,AXD_WAKE_2FC_4,0),
    WORD(object2fc_4_c,AXD_WAKE_2FC_4_C,4), WORD(public_pointer,AXD_WAKE_PUBLIC,0),
    BYTE(public58,AXD_WAKE_PUBLIC58,0), WORD(w530,AXD_WAKE_W530,0),
    WORD(rrm14,AXD_WAKE_RRM14,0), WORD(rrm_c,AXD_WAKE_RRM_C,4),
    WORD(w22c,AXD_WAKE_W22C,0), WORD(assoc4,AXD_WAKE_ASSOC4,0),
    WORD(assoc_word,AXD_WAKE_ASSOC_WORD,1), BYTE(w199,AXD_WAKE_W199,1),
    BYTE(w19a,AXD_WAKE_W19A,1), BYTE(w198,AXD_WAKE_W198,1),
    BYTE(w300,AXD_WAKE_W300,1), BYTE(w336,AXD_WAKE_W336,1),
    WORD(w568,AXD_WAKE_W568,1), WORD(w188,AXD_WAKE_W188,1),
    WORD(w18,AXD_WAKE_W18,0), WORD(w1a0,AXD_WAKE_W1A0,1),
    WORD(w1e0,AXD_WAKE_W1E0,0)
};
static struct axd_wake_inputs baseline(void)
{
    struct axd_wake_inputs s = {0};
    unsigned i;
    s.w19c=s.w19b=s.b4_c=s.w199=s.w19a=s.w198=s.w300=s.w336=U8(0);
    s.public58=U8(1);
    s.wb4=U32(0x2000); s.w2fc=U32(0x3000); s.object2fc_4=U32(0x3100);
    s.object2fc_4_c=U32(3); s.public_pointer=U32(0x1000);
    s.w530=U32(0x4000); s.rrm14=U32(0x4100); s.rrm_c=U32(3);
    s.w22c=U32(0x5000); s.assoc4=U32(0x5100); s.assoc_word=U32(0);
    s.w568=s.w188=s.w1a0=U32(0); s.w18=U32(0x6000); s.w1e0=U32(0x7000);
    for (i=0;i<AXD_WAKE_BSS_COUNT;i++) s.bss[i].pointer=U32(0);
    return s;
}
static void access_is(const struct axd_wake_result *r, unsigned n,
                      enum axd_wake_field field, unsigned slot, unsigned pass)
{
    assert(n<r->accesses);
    assert(r->trace[n].field==field && r->trace[n].slot==slot && r->trace[n].pass==pass);
}
static unsigned first_global(enum axd_wake_field field)
{
    unsigned i;
    for (i=0;i<COUNT(globals);i++) if (globals[i]==field) return i;
    assert(0); return 0;
}
static void terminal(const struct axd_wake_result *r, enum axd_wake_decision decision,
                     enum axd_wake_field reason, unsigned slot, unsigned pass)
{
    assert(r->decision==decision && r->reason==reason && r->slot==slot && r->pass==pass);
    if (reason!=AXD_WAKE_NONE) access_is(r,r->accesses-1,reason,slot,pass);
}
static void globals_and_availability(void)
{
    struct axd_wake_result r;
    struct axd_wake_inputs s=baseline(), saved=s;
    unsigned i,pass,slot,n;
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
    terminal(&r,AXD_WAKE_SLEEP,AXD_WAKE_NONE,255,0);
    assert(memcmp(&s,&saved,sizeof(s))==0);
    assert(r.accesses==COUNT(globals)+48);
    for(i=0;i<COUNT(globals);i++) access_is(&r,i,globals[i],255,0);
    n=(unsigned)COUNT(globals);
    for(pass=1;pass<=3;pass++) for(slot=0;slot<16;slot++)
        access_is(&r,n++,AXD_WAKE_BSS_POINTER,slot,pass);
    for(i=0;i<COUNT(fields);i++) {
        const struct field_case *f=&fields[i];
        unsigned bad;
        for(bad=0;bad<=2;bad+=2) {
            s=baseline();
            if(f->wide) ((struct axd_wake_u32 *)((unsigned char *)&s+f->offset))->known=(uint8_t)bad;
            else ((struct axd_wake_u8 *)((unsigned char *)&s+f->offset))->known=(uint8_t)bad;
            assert(axd_wake_policy_evaluate(&s,U32(0),&r)==(bad?AXD_WAKE_INVALID:AXD_WAKE_DEPENDENCY));
            terminal(&r,bad?AXD_WAKE_INVALID:AXD_WAKE_DEPENDENCY,f->field,255,0);
            assert(r.accesses==first_global(f->field)+1);
        }
        if(f->positive) {
            s=baseline();
            if(f->wide) ((struct axd_wake_u32 *)((unsigned char *)&s+f->offset))->value=0x80000000u;
            else ((struct axd_wake_u8 *)((unsigned char *)&s+f->offset))->value=255;
            assert(axd_wake_policy_evaluate(&s,(struct axd_wake_u32){0,0},&r)==
                   (first_global(f->field)<first_global(AXD_WAKE_OVERRIDE)?AXD_WAKE_STAY:AXD_WAKE_DEPENDENCY));
            /* A supplied current override permits later w1a0 to decide. */
            assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_STAY);
            terminal(&r,AXD_WAKE_STAY,f->field,255,0);
            assert(r.accesses==first_global(f->field)+1);
        }
    }
    s=baseline();
    assert(axd_wake_policy_evaluate(&s,(struct axd_wake_u32){0,0},&r)==AXD_WAKE_DEPENDENCY);
    terminal(&r,AXD_WAKE_DEPENDENCY,AXD_WAKE_OVERRIDE,255,0);
    assert(axd_wake_policy_evaluate(&s,(struct axd_wake_u32){0,2},&r)==AXD_WAKE_INVALID);
    assert(axd_wake_policy_evaluate(&s,U32(0x80000000),&r)==AXD_WAKE_STAY);
    terminal(&r,AXD_WAKE_STAY,AXD_WAKE_OVERRIDE,255,0);
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
    /* Earliest positive does not require any later known values. */
    memset(&s,0,sizeof(s)); s.w19c=U8(0x80);
    assert(axd_wake_policy_evaluate(&s,(struct axd_wake_u32){0,2},&r)==AXD_WAKE_STAY);
    assert(r.accesses==1);
}
static void pointers_and_thresholds(void)
{
    struct axd_wake_inputs s;
    struct axd_wake_result r;
    const size_t offsets[]={offsetof(struct axd_wake_inputs,object2fc_4),
        offsetof(struct axd_wake_inputs,public_pointer),offsetof(struct axd_wake_inputs,w530),
        offsetof(struct axd_wake_inputs,rrm14),offsetof(struct axd_wake_inputs,w22c),
        offsetof(struct axd_wake_inputs,assoc4),offsetof(struct axd_wake_inputs,w18),
        offsetof(struct axd_wake_inputs,w1e0)};
    const uint32_t thresholds[]={0,1,3,4,0x80000000u,0xffffffffu};
    unsigned i,j;
    for(i=0;i<COUNT(offsets);i++) {
        s=baseline(); ((struct axd_wake_u32 *)((unsigned char *)&s+offsets[i]))->value=0;
        assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_INVALID);
    }
    s=baseline(); s.wb4=U32(0); s.b4_c.known=2;
    s.w2fc=U32(0); s.object2fc_4.known=s.object2fc_4_c.known=2;
    s.public58=U8(0); s.w530.known=s.rrm14.known=s.rrm_c.known=2;
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
    assert(r.accesses==COUNT(globals)+48-6);
    for(j=0;j<2;j++) for(i=0;i<COUNT(thresholds);i++) {
        s=baseline();
        if(j==0) s.object2fc_4_c=U32(thresholds[i]); else s.rrm_c=U32(thresholds[i]);
        assert(axd_wake_policy_evaluate(&s,U32(0),&r)==(thresholds[i]>3?AXD_WAKE_STAY:AXD_WAKE_SLEEP));
        if(thresholds[i]>3) assert(r.reason==(j?AXD_WAKE_RRM_C:AXD_WAKE_2FC_4_C));
    }
    assert(axd_wake_policy_evaluate(NULL,U32(0),&r)==AXD_WAKE_INVALID);
    assert(r.accesses==0 && r.reason==AXD_WAKE_NONE);
    assert(axd_wake_policy_evaluate(&s,U32(0),NULL)==AXD_WAKE_INVALID);
}
static struct axd_wake_bss bss(uint32_t pointer)
{
    const struct axd_wake_bss b={U32(pointer),U32(0x9000),U32(0),
                                U8(0),U8(0),U8(0),U8(0)};
    return b;
}
static void bss_passes(void)
{
    struct axd_wake_inputs s;
    struct axd_wake_result r;
    unsigned slot,i,n;
    for(slot=0;slot<16;slot++) {
        s=baseline(); s.bss[slot]=bss(0x8000); s.bss[slot].byte6=U8(255); s.bss[slot].byte4=U8(0x80);
        assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_STAY);
        terminal(&r,AXD_WAKE_STAY,AXD_WAKE_BSS4,slot,1);
        assert(r.accesses==COUNT(globals)+slot+3);
        s.bss[slot].byte6=U8(0); s.bss[slot].byte8=U8(255); s.primary=U32(0);
        assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_STAY);
        terminal(&r,AXD_WAKE_STAY,AXD_WAKE_PRIMARY,slot,2);
        assert(r.accesses==COUNT(globals)+17+slot+4);
        s.primary=U32(0x8000); s.bss[slot].byte10=U8(0x80); s.bss[slot].state8=U32(0x8000001c);
        assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_STAY);
        terminal(&r,AXD_WAKE_STAY,AXD_WAKE_BSS_STATE8,slot,3);
        /* Matching primary is accepted in pass3, with full-word state. */
        assert(r.accesses==COUNT(globals)+17+19+slot+6);
    }
    s=baseline(); s.bss[0]=bss(0x8000); s.primary=U32(0x8000);
    s.bss[0].byte8=s.bss[0].byte10=U8(1); s.bss[0].state8=U32(4);
    s.bss[15]=bss(0x8100); s.bss[15].byte6=s.bss[15].byte4=U8(1);
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_STAY);
    terminal(&r,AXD_WAKE_STAY,AXD_WAKE_BSS4,15,1); /* Pass1 beats earlier-slot pass3. */
    s.bss[15].byte6=U8(0); s.bss[15].byte8=U8(1);
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_STAY);
    terminal(&r,AXD_WAKE_STAY,AXD_WAKE_PRIMARY,15,2);
    s=baseline(); s.primary=U32(0x8000);
    for(i=0;i<16;i++) {
        s.bss[i]=bss(0x8000); s.bss[i].byte8=s.bss[i].byte10=U8(1);
    }
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
    assert(r.accesses==218 && r.accesses<AXD_WAKE_TRACE_CAPACITY);
    n=(unsigned)COUNT(globals);
    for(i=0;i<16;i++) {
        access_is(&r,n++,AXD_WAKE_BSS_POINTER,i,1); access_is(&r,n++,AXD_WAKE_BSS6,i,1);
    }
    for(i=0;i<16;i++) {
        access_is(&r,n++,AXD_WAKE_BSS_POINTER,i,2); access_is(&r,n++,AXD_WAKE_BSS6,i,2);
        access_is(&r,n++,AXD_WAKE_BSS8,i,2); access_is(&r,n++,AXD_WAKE_PRIMARY,i,2);
    }
    for(i=0;i<16;i++) {
        access_is(&r,n++,AXD_WAKE_BSS_POINTER,i,3); access_is(&r,n++,AXD_WAKE_BSS6,i,3);
        access_is(&r,n++,AXD_WAKE_BSS8,i,3); access_is(&r,n++,AXD_WAKE_BSS10,i,3);
        access_is(&r,n++,AXD_WAKE_BSS98,i,3); access_is(&r,n++,AXD_WAKE_BSS_STATE8,i,3);
    }
    assert(n==r.accesses);
}
static void bss_missing_and_skips(void)
{
    struct axd_wake_inputs s;
    struct axd_wake_result r;
    const enum axd_wake_field reasons[]={AXD_WAKE_BSS_POINTER,AXD_WAKE_BSS6,
        AXD_WAKE_BSS4,AXD_WAKE_BSS8,AXD_WAKE_PRIMARY,AXD_WAKE_BSS10,
        AXD_WAKE_BSS98,AXD_WAKE_BSS_STATE8};
    unsigned i;
    for(i=0;i<COUNT(reasons);i++) {
        s=baseline(); s.bss[15]=bss(0x8000); s.primary=U32(0x8000);
        s.bss[15].byte8=s.bss[15].byte10=U8(1);
        switch(reasons[i]) {
        case AXD_WAKE_BSS_POINTER: s.bss[15].pointer.known=0; break;
        case AXD_WAKE_BSS6: s.bss[15].byte6.known=0; break;
        case AXD_WAKE_BSS4: s.bss[15].byte6=U8(1); s.bss[15].byte4.known=0; break;
        case AXD_WAKE_BSS8: s.bss[15].byte8.known=0; break;
        case AXD_WAKE_PRIMARY: s.primary.known=0; break;
        case AXD_WAKE_BSS10: s.bss[15].byte10.known=0; break;
        case AXD_WAKE_BSS98: s.bss[15].assoc98.known=0; break;
        case AXD_WAKE_BSS_STATE8: s.bss[15].state8.known=0; break;
        default: assert(0);
        }
        assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_DEPENDENCY);
        terminal(&r,AXD_WAKE_DEPENDENCY,reasons[i],15,i<3?1:(i<5?2:3));
    }
    s=baseline(); s.bss[0]=bss(0x8000); s.primary=U32(0x8000);
    s.bss[0].byte8=s.bss[0].byte10=U8(1); s.bss[0].state8=U32(0x1c);
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
    s.bss[0].assoc98=U32(0); s.bss[0].state8.known=2;
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
    s.bss[0].byte10=U8(0); s.bss[0].assoc98.known=2;
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
    s.bss[0].byte8=U8(0); s.bss[0].byte10.known=s.primary.known=2;
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
    s.bss[0].byte6=U8(255); s.bss[0].byte4=U8(0); s.bss[0].byte8.known=2;
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
    s.bss[0].pointer=U32(0); s.bss[0].byte6.known=2;
    assert(axd_wake_policy_evaluate(&s,U32(0),&r)==AXD_WAKE_SLEEP);
}
int main(void)
{
    globals_and_availability(); pointers_and_thresholds(); bss_passes(); bss_missing_and_skips();
    puts("wake policy tests passed");
    return 0;
}
