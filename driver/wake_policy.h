/* SPDX-License-Identifier: MIT */
#ifndef AXD_WAKE_POLICY_H
#define AXD_WAKE_POLICY_H
#include <stdint.h>

#define AXD_WAKE_BSS_COUNT 16u
#define AXD_WAKE_TRACE_CAPACITY 256u
#define AXD_WAKE_GLOBAL_SLOT 255u
struct axd_wake_u8 { uint8_t known, value; };
struct axd_wake_u32 { uint32_t value; uint8_t known; };
enum axd_wake_decision {
    AXD_WAKE_INVALID = 0, AXD_WAKE_STAY, AXD_WAKE_SLEEP, AXD_WAKE_DEPENDENCY
};
/* Neutral source-object offsets, not feature names or MMIO registers. */
enum axd_wake_field {
    AXD_WAKE_NONE = 0,
    AXD_WAKE_W19C, AXD_WAKE_W19B, AXD_WAKE_WB4, AXD_WAKE_B4_C,
    AXD_WAKE_W2FC, AXD_WAKE_2FC_4, AXD_WAKE_2FC_4_C,
    AXD_WAKE_PUBLIC, AXD_WAKE_PUBLIC58, AXD_WAKE_W530, AXD_WAKE_RRM14,
    AXD_WAKE_RRM_C, AXD_WAKE_W22C, AXD_WAKE_ASSOC4, AXD_WAKE_ASSOC_WORD,
    AXD_WAKE_W199, AXD_WAKE_W19A, AXD_WAKE_W198, AXD_WAKE_W300,
    AXD_WAKE_W336, AXD_WAKE_W568, AXD_WAKE_W188, AXD_WAKE_W18,
    AXD_WAKE_OVERRIDE, AXD_WAKE_W1A0, AXD_WAKE_W1E0,
    AXD_WAKE_BSS_POINTER, AXD_WAKE_BSS6, AXD_WAKE_BSS4,
    AXD_WAKE_BSS8, AXD_WAKE_PRIMARY, AXD_WAKE_BSS10,
    AXD_WAKE_BSS98, AXD_WAKE_BSS_STATE8
};
struct axd_wake_bss {
    struct axd_wake_u32 pointer, assoc98, state8;
    struct axd_wake_u8 byte4, byte6, byte8, byte10;
};
struct axd_wake_inputs {
    struct axd_wake_u8 w19c, w19b, b4_c, public58;
    struct axd_wake_u8 w199, w19a, w198, w300, w336;
    struct axd_wake_u32 wb4, w2fc, object2fc_4, object2fc_4_c;
    struct axd_wake_u32 public_pointer, w530, rrm14, rrm_c;
    struct axd_wake_u32 w22c, assoc4, assoc_word;
    struct axd_wake_u32 w568, w188, w18, w1a0, w1e0, primary;
    struct axd_wake_bss bss[AXD_WAKE_BSS_COUNT];
};
struct axd_wake_access {
    enum axd_wake_field field;
    uint8_t slot, pass;
};
struct axd_wake_result {
    enum axd_wake_decision decision;
    enum axd_wake_field reason;
    uint8_t slot, pass;
    unsigned accesses;
    struct axd_wake_access trace[AXD_WAKE_TRACE_CAPACITY];
};
/* Pure, bounded software predicate for the recovered core130 policy. Values
 * are acquired externally and remain stable throughout this call; pointers are
 * ARM32 identities, never dereferenced. Repeated identities must describe the
 * same object. All reached fields require known==1; known==0 is DEPENDENCY,
 * other known values INVALID. A known null unguarded pointer is INVALID, not
 * an absent feature. Guarded null pointers skip their actual branch.
 * current_override is the CURRENT PSM reason word at evaluation, after any
 * reason clear; it is not captured with the rest of the snapshot.
 * Result identifies the first positive/missing/invalid field and BSS pass/slot.
 * Trace includes attempted missing/invalid accesses in exact source order.
 * SLEEP has reason NONE. Input/result storage must not overlap. No callbacks,
 * public-up gate, physical I/O, state mutation, synchronization or readiness.
 */
enum axd_wake_decision axd_wake_policy_evaluate(
    const struct axd_wake_inputs *inputs, struct axd_wake_u32 current_override,
    struct axd_wake_result *result);
#endif
