/* SPDX-License-Identifier: MIT */
#ifndef AXD_CORE_RESET_LOW_H
#define AXD_CORE_RESET_LOW_H
#include "si_core.h"
#include "ai_reset.h"
#include "si_select.h"
#include "core_reset_low_ai.h"
#include "mctrl.h"
struct axd_core_reset_low_context {
    uint8_t hw_present, reset_callback_present, w_present;
    uint16_t revision;
    uint32_t w138, flags, reset_bits, chip_id;
    struct axd_si_context si;
    struct axd_si_state state;
    struct axd_mctrl_context mctrl;
};
enum axd_core_reset_low_phase { AXD_LOW_INVALID=0, AXD_LOW_RUNNING, AXD_LOW_DONE, AXD_LOW_DEPENDENCY, AXD_LOW_FAILED, AXD_LOW_CANCELLED };
enum axd_core_reset_low_error { AXD_LOW_NO_ERROR=0, AXD_LOW_BAD_ARGUMENT, AXD_LOW_IO_ERROR, AXD_LOW_CHILD_ERROR };
enum axd_core_reset_low_kind { AXD_LOW_AI=1, AXD_LOW_SELECT, AXD_LOW_CORE, AXD_LOW_D11 };
struct axd_core_reset_low_operation { enum axd_core_reset_low_kind kind; uint32_t wrapper_mapping; struct axd_low_ai_operation ai; struct axd_si_operation select; struct axd_si_core_operation core; struct axd_d11_operation d11; };
typedef int (*axd_core_reset_low_io_fn)(void *, const struct axd_core_reset_low_operation *, uint32_t *);
struct axd_core_reset_low {
    enum axd_core_reset_low_phase phase; enum axd_core_reset_low_error error;
    struct axd_core_reset_low_context context;
    struct axd_ai_reset d11_ai; struct axd_low_ai ai; struct axd_si_select selector; struct axd_si_core_job core; struct axd_mctrl mctrl;
    uint32_t saved_index, effective_bits, effective_reset_bits;
    unsigned stage, reset_stage, reset_auxiliary, reset_secondary, reset_selector, reset_d11_child;
    uint64_t attempted_ops, completed_ops;
};
int axd_core_reset_low_begin(struct axd_core_reset_low *,const struct axd_core_reset_low_context *);
enum axd_core_reset_low_phase axd_core_reset_low_step(struct axd_core_reset_low *,axd_core_reset_low_io_fn,void *);
enum axd_core_reset_low_phase axd_core_reset_low_cancel(struct axd_core_reset_low *);
#endif
