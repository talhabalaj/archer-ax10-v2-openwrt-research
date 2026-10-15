/* SPDX-License-Identifier: MIT */
#ifndef AXD_BF_CLEAN_H
#define AXD_BF_CLEAN_H
#include "psm_control.h"

#define AXD_BF_MAX_SLOTS 256u
#define AXD_BF_GLOBAL_INDEX UINT32_MAX
struct axd_bf_u16 { uint16_t value; uint8_t known; };
struct axd_bf_slot { struct axd_wake_u32 pointer, peer; };
struct axd_bf_list {
    const struct axd_wake_u8 *bitmap;
    size_t bitmap_length;
    const struct axd_bf_slot *slots;
    size_t slot_length;
};
/* Caller bindings for software objects. Peer is the word obtained from the
 * slot pointer plus cubby_offset, not a live pointer dereference by this code.
 */
struct axd_bf_metadata {
    struct axd_wake_u32 txbf_wlc, wt_hardware, wt_public, cubby_offset, primary_last;
    struct axd_wake_u8 wt21, public_extended, secondary_count;
    struct axd_bf_u16 public_core;
    struct axd_bf_list primary, secondary;
};
struct axd_bf_clean_context {
    struct axd_psm_context mac;
    struct axd_psm_policy policy;
    uint32_t txbf_identity, wlc_identity, hardware_identity;
    const struct axd_bf_metadata *metadata;
};
enum axd_bf_phase {
    AXD_BF_INVALID = 0, AXD_BF_RUNNING, AXD_BF_TRACE_COMPLETE,
    AXD_BF_DEPENDENCY, AXD_BF_FAILED, AXD_BF_CANCELLED
};
enum axd_bf_error {
    AXD_BF_NO_ERROR = 0, AXD_BF_BAD_ARGUMENT, AXD_BF_BINDING,
    AXD_BF_BOUNDS, AXD_BF_UNSUPPORTED, AXD_BF_MAC_ERROR, AXD_BF_IO_ERROR
};
enum axd_bf_list_id { AXD_BF_NO_LIST = 0, AXD_BF_PRIMARY, AXD_BF_SECONDARY };
enum axd_bf_field {
    AXD_BF_NO_FIELD = 0, AXD_BF_METADATA, AXD_BF_T_WT, AXD_BF_WT_HW,
    AXD_BF_WT_PUBLIC, AXD_BF_WT21, AXD_BF_PUBLIC_EXT, AXD_BF_PUBLIC_CORE,
    AXD_BF_BITMAP, AXD_BF_SLOT, AXD_BF_CUBBY_OFFSET, AXD_BF_PEER,
    AXD_BF_PRIMARY_LAST, AXD_BF_SECONDARY_COUNT
};
struct axd_bf_access {
    enum axd_bf_field field;
    enum axd_bf_list_id list;
    uint32_t index, value;
    uint8_t known;
};
struct axd_bf_clean {
    enum axd_bf_phase phase;
    enum axd_bf_error error;
    struct axd_bf_clean_context context;
    struct axd_psm_control mac_job;
    struct axd_bf_access last_access;
    uint64_t metadata_accesses, attempted_ops, completed_ops;
    uint32_t index, slot_pointer, peer_pointer;
    unsigned stage, after_child, list, next_mac;
    unsigned primary_iterations, secondary_iterations, secondary_limit;
    unsigned invalidation_calls, enable_calls, invalidate_result, invalidate_result_valid;
    unsigned leaf_byte, enable_value;
    unsigned pending_callouts;
};
/* Cleaner BODY only, after the reset caller's flags/public/txbf12 gate. Caller
 * must bind its selected TXBF pointer to context.txbf_identity. T->WT must equal
 * wlc_identity; WT->HW must equal hardware_identity for the supplied MAC snapshot.
 * Identities are ARM32 labels, not dereferenced pointers. Metadata, arrays and
 * policy inputs are immutable caller storage outside all jobs until terminal.
 * Core130/minor0..1 only; reached public core must match130. No SHM substitute:
 * actual leaf WT21/public predicates return before SHM for this core.
 * Inclusive primary bound is reloaded after EACH visited index; secondary u8
 * count initially then only after valid peer's two leaf calls. Strict cap256
 * rejects primary bound>=256 and reached array bounds rather than wrapping.
 * begin copies context safely from &job.context. It does not acquire metadata.
 */
int axd_bf_clean_begin(struct axd_bf_clean *job, const struct axd_bf_clean_context *context);
/* At most ONE metadata access OR user I/O callback per step. Last access plus
 * metadata_accesses exposes ordered provenance, including unknown attempted
 * fields. known0 => DEPENDENCY; other non1 => FAILED. Child context/diagnostics
 * always propagate. Callback may cancel, never reenter/mutate; error wins over
 * cancel, successful effects survive, terminals silent. No unwind/rollback.
 */
enum axd_bf_phase axd_bf_clean_step(struct axd_bf_clean *job, axd_d11_io_fn io, void *context);
enum axd_bf_phase axd_bf_clean_cancel(struct axd_bf_clean *job);
#endif
