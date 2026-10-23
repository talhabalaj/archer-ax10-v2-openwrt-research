/* SPDX-License-Identifier: MIT */
#include "wake_policy.h"
#include <stddef.h>

static int access_field(struct axd_wake_result *r, enum axd_wake_field field,
                        unsigned known, unsigned slot, unsigned pass)
{
    struct axd_wake_access a;
    r->reason = field;
    r->slot = (uint8_t)slot;
    r->pass = (uint8_t)pass;
    if (r->accesses == AXD_WAKE_TRACE_CAPACITY) {
        r->decision = AXD_WAKE_INVALID;
        return 0;
    }
    a.field = field; a.slot = (uint8_t)slot; a.pass = (uint8_t)pass;
    r->trace[r->accesses++] = a;
    if (known != 1) {
        r->decision = known == 0 ? AXD_WAKE_DEPENDENCY : AXD_WAKE_INVALID;
        return 0;
    }
    return 1;
}
static int required_pointer(struct axd_wake_result *r, struct axd_wake_u32 p,
                            enum axd_wake_field field)
{
    if (!access_field(r, field, p.known, AXD_WAKE_GLOBAL_SLOT, 0))
        return 0;
    if (p.value == 0) {
        r->decision = AXD_WAKE_INVALID;
        return 0;
    }
    return 1;
}
#define LOAD(v,f) do { if (!access_field(r,f,(v).known,AXD_WAKE_GLOBAL_SLOT,0)) return r->decision; } while (0)
#define POSITIVE(v,f) do { LOAD(v,f); if ((v).value != 0) { r->decision=AXD_WAKE_STAY; return r->decision; } } while (0)
#define REQUIRED(v,f) do { if (!required_pointer(r,v,f)) return r->decision; } while (0)
#define BLOAD(v,f) do { if (!access_field(r,f,(v).known,i,pass)) return r->decision; } while (0)

enum axd_wake_decision axd_wake_policy_evaluate(
    const struct axd_wake_inputs *s, struct axd_wake_u32 current_override,
    struct axd_wake_result *r)
{
    static const struct axd_wake_result empty={0};
    unsigned i, pass;
    if (r == NULL)
        return AXD_WAKE_INVALID;
    *r = empty;
    r->slot = AXD_WAKE_GLOBAL_SLOT;
    if (s == NULL)
        return r->decision;
    POSITIVE(s->w19c, AXD_WAKE_W19C);
    POSITIVE(s->w19b, AXD_WAKE_W19B);
    LOAD(s->wb4, AXD_WAKE_WB4);
    if (s->wb4.value != 0) {
        POSITIVE(s->b4_c, AXD_WAKE_B4_C);
    }
    LOAD(s->w2fc, AXD_WAKE_W2FC);
    if (s->w2fc.value != 0) {
        REQUIRED(s->object2fc_4, AXD_WAKE_2FC_4);
        LOAD(s->object2fc_4_c, AXD_WAKE_2FC_4_C);
        if (s->object2fc_4_c.value > 3u) {
            r->decision = AXD_WAKE_STAY;
            return r->decision;
        }
    }
    REQUIRED(s->public_pointer, AXD_WAKE_PUBLIC);
    LOAD(s->public58, AXD_WAKE_PUBLIC58);
    if (s->public58.value != 0) {
        REQUIRED(s->w530, AXD_WAKE_W530);
        REQUIRED(s->rrm14, AXD_WAKE_RRM14);
        LOAD(s->rrm_c, AXD_WAKE_RRM_C);
        if (s->rrm_c.value > 3u) {
            r->decision = AXD_WAKE_STAY;
            return r->decision;
        }
    }
    REQUIRED(s->w22c, AXD_WAKE_W22C);
    REQUIRED(s->assoc4, AXD_WAKE_ASSOC4);
    POSITIVE(s->assoc_word, AXD_WAKE_ASSOC_WORD);
    POSITIVE(s->w199, AXD_WAKE_W199);
    POSITIVE(s->w19a, AXD_WAKE_W19A);
    POSITIVE(s->w198, AXD_WAKE_W198);
    POSITIVE(s->w300, AXD_WAKE_W300);
    POSITIVE(s->w336, AXD_WAKE_W336);
    POSITIVE(s->w568, AXD_WAKE_W568);
    POSITIVE(s->w188, AXD_WAKE_W188);
    REQUIRED(s->w18, AXD_WAKE_W18);
    POSITIVE(current_override, AXD_WAKE_OVERRIDE);
    POSITIVE(s->w1a0, AXD_WAKE_W1A0);
    REQUIRED(s->w1e0, AXD_WAKE_W1E0);
    for (pass = 1; pass <= 3; pass++) {
        for (i = 0; i < AXD_WAKE_BSS_COUNT; i++) {
            const struct axd_wake_bss *b = &s->bss[i];
            BLOAD(b->pointer, AXD_WAKE_BSS_POINTER);
            if (b->pointer.value == 0)
                continue;
            BLOAD(b->byte6, AXD_WAKE_BSS6);
            if (pass == 1) {
                if (b->byte6.value == 0)
                    continue;
                BLOAD(b->byte4, AXD_WAKE_BSS4);
                if (b->byte4.value == 0)
                    continue;
            } else {
                if (b->byte6.value != 0)
                    continue;
                BLOAD(b->byte8, AXD_WAKE_BSS8);
                if (b->byte8.value == 0)
                    continue;
                if (pass == 2) {
                    BLOAD(s->primary, AXD_WAKE_PRIMARY);
                    if (b->pointer.value == s->primary.value)
                        continue;
                } else {
                    BLOAD(b->byte10, AXD_WAKE_BSS10);
                    if (b->byte10.value == 0)
                        continue;
                    BLOAD(b->assoc98, AXD_WAKE_BSS98);
                    if (b->assoc98.value == 0)
                        continue;
                    BLOAD(b->state8, AXD_WAKE_BSS_STATE8);
                    if (b->state8.value == 0 || b->state8.value == 0x1cu)
                        continue;
                }
            }
            r->decision = AXD_WAKE_STAY;
            return r->decision;
        }
    }
    r->decision = AXD_WAKE_SLEEP;
    r->reason = AXD_WAKE_NONE;
    r->slot = AXD_WAKE_GLOBAL_SLOT;
    r->pass = 0;
    return r->decision;
}
