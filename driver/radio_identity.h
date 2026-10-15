/* SPDX-License-Identifier: MIT */
#ifndef AXD_RADIO_IDENTITY_H
#define AXD_RADIO_IDENTITY_H
#include "d11_io.h"
#include "wake_policy.h"
struct axd_radio_identity_context {
    uint16_t core, minor;
    struct axd_wake_u32 phy_pointer, public_pointer;
    /* Registered AC query thunk: callback_context's first word is callback_pi.
     * Caller binds the recovered thunk, not an arbitrary successful callback. */
    struct axd_wake_u32 callback_context, callback_pi;
    uint16_t radio_id;
    uint8_t radio_revision, radio_version;
};
enum axd_radio_identity_command {
    AXD_RID_QUERY_DIRECT=1, AXD_RID_QUERY_REGISTERED, AXD_RID_QUERY_PARSE_AC,
    AXD_RID_PARSE_AC, AXD_RID_PARSE_COMMON
};
enum axd_radio_identity_phase { AXD_RID_INVALID=0, AXD_RID_RUNNING, AXD_RID_TRACE_COMPLETE, AXD_RID_DEPENDENCY, AXD_RID_FAILED, AXD_RID_CANCELLED };
enum axd_radio_identity_error { AXD_RID_NO_ERROR=0, AXD_RID_BAD_ARGUMENT, AXD_RID_UNSUPPORTED, AXD_RID_BINDING, AXD_RID_IO_ERROR };
enum axd_radio_identity_field { AXD_RID_NO_FIELD=0, AXD_RID_PHY, AXD_RID_PUBLIC, AXD_RID_CALLBACK_CONTEXT, AXD_RID_CALLBACK_PI };
struct axd_radio_identity {
    enum axd_radio_identity_phase phase; enum axd_radio_identity_error error;
    enum axd_radio_identity_command command; enum axd_radio_identity_field last_field;
    struct axd_radio_identity_context context;
    uint32_t input_code, result_code;
    uint16_t high_sample, low_sample;
    unsigned stage, samples_observed, barriers_completed, result_valid, decoded_stores;
    unsigned metadata_accesses;
    uint64_t attempted_ops, completed_ops;
};
/* Actual core130.0/1 descriptor-based AC radio identity query. It writes radio
 * address0 then1 to3d8, with separate R16(3da)/SY samples; the FIRST sample is
 * high16. No SI/PHY-register selection, wake or radio-on operation is invented.
 * QUERY_PARSE_AC composes actual direct query then AC parser; registered entry
 * first validates the actual AC context->PI binding. AC parse stores low16,
 * bits16..23,0 to the id/revision/version fields. The distinct common parser
 * stores bits12..27,bits28..31,bits0..3. No observed identity/model is assumed.
 * input_code is used only by PARSE commands. Begin snapshots own-context aliases.
 * Reached unknown pointers are dependencies, required nulls fail. Caller owns
 * actual powered D11/radio resources, descriptors and synchronization. No MMIO
 * backend, hardware/RF action, automatic cleanup or readiness claim is provided.
 */
int axd_radio_identity_begin(struct axd_radio_identity *,enum axd_radio_identity_command,
                              uint32_t input_code,const struct axd_radio_identity_context *);
/* One callback maximum per step. Successful read/barrier samples survive
 * callback cancellation; error wins simultaneous cancel. Later combine/parse
 * software stores are separate steps and do not execute after cancellation.
 * Parse id/revision/version stores are individually observable, in source order.
 * Terminals silent. Callback may cancel, never reenter or mutate the job. */
enum axd_radio_identity_phase axd_radio_identity_step(struct axd_radio_identity *,axd_d11_io_fn,void *);
enum axd_radio_identity_phase axd_radio_identity_cancel(struct axd_radio_identity *);
#endif
