/* SPDX-License-Identifier: MIT */
#ifndef AXD_SMC_H
#define AXD_SMC_H
#include "phy_table_write.h"
#include "wake_policy.h"
struct axd_smc_image {
    const uint8_t *data;
    size_t length;
    uint32_t source_bytes;
};
struct axd_smc_context {
    struct axd_phy_table_context hardware;
    /* Required pointer graph at each reached real dispatch. Callback bindings
     * name the recovered AC implementation, not an arbitrary success callback.
     * callback_pi is *(AC callback context), and must match phy_pointer. */
    struct axd_wake_u32 band, phy_pointer, smc_info, dispatch;
    struct axd_wake_u32 callback_context, callback_pi;
    struct axd_wake_u32 reset_callback, download_callback;
    uint8_t loaded, ac_downloaded;
};
enum axd_smc_command { AXD_SMC_LOAD=1, AXD_SMC_RESET0, AXD_SMC_RESET1 };
enum axd_smc_phase { AXD_SMC_INVALID=0, AXD_SMC_RUNNING, AXD_SMC_TRACE_COMPLETE, AXD_SMC_DEPENDENCY, AXD_SMC_FAILED, AXD_SMC_CANCELLED };
enum axd_smc_error { AXD_SMC_NO_ERROR=0, AXD_SMC_BAD_ARGUMENT, AXD_SMC_UNSUPPORTED, AXD_SMC_BINDING, AXD_SMC_BOUNDS, AXD_SMC_CHILD_ERROR, AXD_SMC_IO_ERROR };
enum axd_smc_field { AXD_SMC_NO_FIELD=0, AXD_SMC_BAND, AXD_SMC_PHY_POINTER, AXD_SMC_INFO, AXD_SMC_DISPATCH, AXD_SMC_CALLBACK_CONTEXT, AXD_SMC_CALLBACK_PI, AXD_SMC_RESET_CALLBACK, AXD_SMC_DOWNLOAD_CALLBACK, AXD_SMC_IMAGE };
struct axd_smc {
    enum axd_smc_phase phase; enum axd_smc_error error;
    enum axd_smc_command command; enum axd_smc_field last_field;
    struct axd_smc_context context; struct axd_smc_image image;
    struct axd_phy_register phy_job;
    struct axd_phy_table_write table_job;
    uint32_t last_read;
    uint16_t image_rows;
    unsigned stage, after_child, after_reset, binding_index, reset_index, reset_value;
    unsigned reset0_completed, reset1_completed, download_completed, loaded_committed;
    unsigned metadata_accesses, pending_callouts;
    uint64_t attempted_ops, completed_ops;
};
/* Actual core130.0/1 AC PHY51 SMC load/reset. No firmware is embedded: caller
 * owns immutable external bytes and authenticates their source. Pinned target
 * d11smc1 has11420 bytes; stock transfers floor(size/6) low16=1903 rows and
 * ignores its final2bytes. The API retains that arithmetic, without silently
 * padding or publishing the private payload. Unknown reached bindings stop;
 * required nulls fail. LOAD with nonzero full-byte loaded skips payload/reset0
 * and still executes reset1 before storing loaded1. RESET commands expose the
 * actual AC reset leaves separately. Begin snapshots own-context aliases.
 * No backend, hardware/RF operation, resource acquisition or readiness result.
 */
/* Actual pure wlc_smc_hw_supported predicate. */
unsigned axd_smc_hw_supported(uint32_t core_revision);
int axd_smc_begin(struct axd_smc *,enum axd_smc_command,
                   const struct axd_smc_image *,const struct axd_smc_context *);
enum axd_smc_phase axd_smc_step(struct axd_smc *,axd_d11_io_fn,void *);
enum axd_smc_phase axd_smc_cancel(struct axd_smc *);
#endif
