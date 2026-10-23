/* SPDX-License-Identifier: MIT */
#include "bf_clean.h"

enum stage {
    ENTRY, MAC_HW, RUN_MAC, BITMAP, SLOT, CUBBY, PEER,
    INV_TWT, INV_WT21, INV_PUBLIC, INV_CORE,
    SET_TWT, SET_WT21, SET_PUBLIC, SET_EXT, SET_CORE,
    PRIMARY_BOUND, SECONDARY_INITIAL, SECONDARY_RELOAD, NEXT_SECONDARY,
    OUTER_PUBLIC, OUTER_EXT, FINISH
};
struct relay { struct axd_bf_clean *job; axd_d11_io_fn io; void *context; };
static void fail(struct axd_bf_clean *j, enum axd_bf_error error)
{
    j->phase = AXD_BF_FAILED;
    j->error = error;
}
static int perform(void *opaque, const struct axd_d11_operation *op, uint32_t *value)
{
    struct relay *r = opaque;
    if (r->io == NULL) { fail(r->job, AXD_BF_BAD_ARGUMENT); return -1; }
    r->job->attempted_ops++;
    if (r->io(r->context, op, value) != 0) {
        fail(r->job, AXD_BF_IO_ERROR);
        return -1;
    }
    r->job->completed_ops++;
    return 0;
}
static int observe(struct axd_bf_clean *j, enum axd_bf_field field,
                   unsigned known, uint32_t value, unsigned indexed)
{
    const struct axd_bf_access a = {
        field, (enum axd_bf_list_id)(indexed ? j->list : AXD_BF_NO_LIST),
        indexed ? j->index : AXD_BF_GLOBAL_INDEX, known == 1 ? value : 0,
        (uint8_t)known
    };
    j->last_access = a;
    j->metadata_accesses++;
    if (known != 1) {
        if (known == 0) j->phase = AXD_BF_DEPENDENCY;
        else fail(j, AXD_BF_BAD_ARGUMENT);
        return 0;
    }
    return 1;
}
static int pointer(struct axd_bf_clean *j, enum axd_bf_field field,
                   struct axd_wake_u32 value, uint32_t expected)
{
    if (!observe(j, field, value.known, value.value, 0)) return 0;
    if (value.value == 0 || (expected != 0 && value.value != expected)) {
        fail(j, AXD_BF_BINDING);
        return 0;
    }
    return 1;
}
static void mac(struct axd_bf_clean *j, enum axd_psm_command command, unsigned resume)
{
    j->next_mac = (unsigned)command;
    j->after_child = resume;
    j->stage = MAC_HW;
}
static void next_entry(struct axd_bf_clean *j)
{
    j->stage = j->list == AXD_BF_PRIMARY ? PRIMARY_BOUND : NEXT_SECONDARY;
}
static void after_leaves(struct axd_bf_clean *j)
{
    j->stage = j->list == AXD_BF_PRIMARY ? PRIMARY_BOUND : SECONDARY_RELOAD;
}
static const struct axd_bf_list *list(const struct axd_bf_clean *j)
{
    return j->list == AXD_BF_PRIMARY ? &j->context.metadata->primary : &j->context.metadata->secondary;
}
static int bounded_access(struct axd_bf_clean *j, enum axd_bf_field field,
                          const void *storage, size_t length, uint32_t index)
{
    if (storage == NULL) {
        (void)observe(j, field, 0, 0, 1);
        return 0;
    }
    if ((size_t)index >= length || j->index >= AXD_BF_MAX_SLOTS) {
        (void)observe(j, field, 1, 0, 1);
        fail(j, AXD_BF_BOUNDS);
        return 0;
    }
    return 1;
}
int axd_bf_clean_begin(struct axd_bf_clean *j, const struct axd_bf_clean_context *context)
{
    static const struct axd_bf_clean empty={0};
    struct axd_bf_clean_context snapshot;
    if (j == NULL) return -1;
    if (context != NULL) { snapshot = *context; context = &snapshot; }
    *j = empty;
    fail(j, AXD_BF_BAD_ARGUMENT);
    if (context == NULL || context->txbf_identity == 0 || context->wlc_identity == 0 ||
        context->hardware_identity == 0) return -1;
    j->context = *context;
    if (axd_psm_control_begin_policy(&j->mac_job, AXD_PSM_WAIT_WAKE,
                                     &context->mac, &context->policy) != 0) {
        fail(j, AXD_BF_MAC_ERROR);
        return -1;
    }
    j->stage = ENTRY;
    j->list = AXD_BF_PRIMARY;
    j->phase = AXD_BF_RUNNING;
    j->error = AXD_BF_NO_ERROR;
    return 0;
}
enum axd_bf_phase axd_bf_clean_step(struct axd_bf_clean *j, axd_d11_io_fn io, void *context)
{
    const struct axd_bf_metadata *m;
    const struct axd_bf_list *l;
    struct relay relay = {j, io, context};
    if (j == NULL) return AXD_BF_INVALID;
    if (j->phase != AXD_BF_RUNNING) return j->phase;
    m = j->context.metadata;
    if (m == NULL) {
        (void)observe(j, AXD_BF_METADATA, 0, 0, 0);
        return j->phase;
    }
    switch (j->stage) {
    case ENTRY:
        if (pointer(j, AXD_BF_T_WT, m->txbf_wlc, j->context.wlc_identity))
            mac(j, AXD_PSM_SUSPEND_MAIN, BITMAP);
        break;
    case MAC_HW:
        if (!pointer(j, AXD_BF_WT_HW, m->wt_hardware, j->context.hardware_identity)) break;
        if (axd_psm_control_begin_policy(&j->mac_job, (enum axd_psm_command)j->next_mac,
                                         &j->context.mac, &j->context.policy) != 0)
            fail(j, AXD_BF_MAC_ERROR);
        else j->stage = RUN_MAC;
        break;
    case RUN_MAC: {
        enum axd_psm_phase phase = axd_psm_control_step(&j->mac_job, perform, &relay);
        j->context.mac = j->mac_job.context;
        j->pending_callouts |= j->mac_job.pending_callouts;
        if (j->phase != AXD_BF_RUNNING) break;
        if (phase == AXD_PSM_DONE) j->stage = j->after_child;
        else if (phase == AXD_PSM_DEPENDENCY) j->phase = AXD_BF_DEPENDENCY;
        else if (phase == AXD_PSM_CANCELLED) j->phase = AXD_BF_CANCELLED;
        else if (phase != AXD_PSM_RUNNING) fail(j, AXD_BF_MAC_ERROR);
        break;
    }
    case BITMAP:
        l = list(j);
        if (!bounded_access(j, AXD_BF_BITMAP, l->bitmap, l->bitmap_length, j->index >> 3)) break;
        if (!observe(j, AXD_BF_BITMAP, l->bitmap[j->index >> 3].known,
                      l->bitmap[j->index >> 3].value, 1)) break;
        if ((l->bitmap[j->index >> 3].value & (1u << (j->index & 7u))) != 0) j->stage = SLOT;
        else next_entry(j);
        break;
    case SLOT:
        l = list(j);
        if (!bounded_access(j, AXD_BF_SLOT, l->slots, l->slot_length, j->index)) break;
        if (!observe(j, AXD_BF_SLOT, l->slots[j->index].pointer.known,
                      l->slots[j->index].pointer.value, 1)) break;
        j->slot_pointer = l->slots[j->index].pointer.value;
        if (j->slot_pointer != 0) j->stage = CUBBY;
        else next_entry(j);
        break;
    case CUBBY:
        if (observe(j, AXD_BF_CUBBY_OFFSET, m->cubby_offset.known, m->cubby_offset.value, 0)) j->stage = PEER;
        break;
    case PEER:
        l = list(j);
        if (!observe(j, AXD_BF_PEER, l->slots[j->index].peer.known,
                      l->slots[j->index].peer.value, 1)) break;
        j->peer_pointer = l->slots[j->index].peer.value;
        if (j->peer_pointer != 0) j->stage = INV_TWT;
        else next_entry(j);
        break;
    case INV_TWT:
        j->invalidation_calls++;
        j->invalidate_result_valid = 0;
        if (pointer(j, AXD_BF_T_WT, m->txbf_wlc, j->context.wlc_identity)) j->stage = INV_WT21;
        break;
    case INV_WT21:
        if (!observe(j, AXD_BF_WT21, m->wt21.known, m->wt21.value, 0)) break;
        j->leaf_byte = m->wt21.value;
        if (m->wt21.value == 0) {
            j->invalidate_result = 0;
            j->invalidate_result_valid = 1;
        }
        j->stage = m->wt21.value == 0 ? SET_TWT : INV_PUBLIC;
        break;
    case INV_PUBLIC:
        if (pointer(j, AXD_BF_WT_PUBLIC, m->wt_public, 0)) j->stage = INV_CORE;
        break;
    case INV_CORE:
    case SET_CORE:
        if (!observe(j, AXD_BF_PUBLIC_CORE, m->public_core.known, m->public_core.value, 0)) break;
        if (m->public_core.value != 130) fail(j, AXD_BF_UNSUPPORTED);
        else if (j->stage == INV_CORE) {
            j->invalidate_result = j->leaf_byte;
            j->invalidate_result_valid = 1;
            j->stage = SET_TWT;
        }
        else after_leaves(j);
        break;
    case SET_TWT:
        j->enable_calls++;
        j->enable_value = j->invalidate_result;
        if (pointer(j, AXD_BF_T_WT, m->txbf_wlc, j->context.wlc_identity)) j->stage = SET_WT21;
        break;
    case SET_WT21:
        if (!observe(j, AXD_BF_WT21, m->wt21.known, m->wt21.value, 0)) break;
        if (m->wt21.value == 0) after_leaves(j);
        else j->stage = SET_PUBLIC;
        break;
    case SET_PUBLIC:
        if (pointer(j, AXD_BF_WT_PUBLIC, m->wt_public, 0)) j->stage = SET_EXT;
        break;
    case SET_EXT:
        if (!observe(j, AXD_BF_PUBLIC_EXT, m->public_extended.known, m->public_extended.value, 0)) break;
        if (m->public_extended.value == 0) after_leaves(j);
        else j->stage = SET_CORE;
        break;
    case PRIMARY_BOUND:
        if (!observe(j, AXD_BF_PRIMARY_LAST, m->primary_last.known, m->primary_last.value, 1)) break;
        if (m->primary_last.value >= AXD_BF_MAX_SLOTS) { fail(j, AXD_BF_BOUNDS); break; }
        j->primary_iterations++;
        j->index++;
        if (j->index <= m->primary_last.value) j->stage = BITMAP;
        else mac(j, AXD_PSM_ENABLE_MAIN, OUTER_PUBLIC);
        break;
    case OUTER_PUBLIC:
        if (pointer(j, AXD_BF_WT_PUBLIC, m->wt_public, 0)) j->stage = OUTER_EXT;
        break;
    case OUTER_EXT:
        if (!observe(j, AXD_BF_PUBLIC_EXT, m->public_extended.known, m->public_extended.value, 0)) break;
        if (m->public_extended.value == 0) j->stage = FINISH;
        else mac(j, AXD_PSM_SUSPEND_EXT, SECONDARY_INITIAL);
        break;
    case SECONDARY_INITIAL:
        j->list = AXD_BF_SECONDARY;
        j->index = 0;
        if (!observe(j, AXD_BF_SECONDARY_COUNT, m->secondary_count.known, m->secondary_count.value, 1)) break;
        j->secondary_limit = m->secondary_count.value;
        if (j->secondary_limit == 0) mac(j, AXD_PSM_ENABLE_EXT, FINISH);
        else j->stage = BITMAP;
        break;
    case SECONDARY_RELOAD:
        if (!observe(j, AXD_BF_SECONDARY_COUNT, m->secondary_count.known, m->secondary_count.value, 1)) break;
        j->secondary_limit = m->secondary_count.value;
        j->stage = NEXT_SECONDARY;
        break;
    case NEXT_SECONDARY:
        j->secondary_iterations++;
        j->index++;
        if (j->index < j->secondary_limit) j->stage = BITMAP;
        else mac(j, AXD_PSM_ENABLE_EXT, FINISH);
        break;
    case FINISH:
        j->phase = AXD_BF_TRACE_COMPLETE;
        break;
    default: fail(j, AXD_BF_BAD_ARGUMENT); break;
    }
    return j->phase;
}
enum axd_bf_phase axd_bf_clean_cancel(struct axd_bf_clean *j)
{
    if (j == NULL) return AXD_BF_INVALID;
    if (j->phase == AXD_BF_RUNNING) {
        j->phase = AXD_BF_CANCELLED;
        if (j->stage == RUN_MAC) axd_psm_control_cancel(&j->mac_job);
    }
    return j->phase;
}
