/* SPDX-License-Identifier: MIT */
#ifndef AXD_SI_SWITCH_H
#define AXD_SI_SWITCH_H
#include "si_select.h"

struct axd_si_switch_context {
    struct axd_si_context si;
    struct axd_si_state state;
    uint32_t bus_core_revision, irq_core_id;
    uint32_t disable_callback, restore_callback, irq_context;
    uint8_t state_known;
};
enum axd_si_switch_command { AXD_SIS_SWITCH=1, AXD_SIS_RESTORE };
enum axd_si_switch_kind { AXD_SIS_SELECT=1, AXD_SIS_IRQ_DISABLE, AXD_SIS_IRQ_RESTORE };
struct axd_si_switch_operation {
    enum axd_si_switch_kind kind;
    struct axd_si_operation select;
    uint32_t callback, callback_context, value;
    const struct axd_si_state *state;
};
struct axd_si_switch_reply {
    uint32_t value;
    uint8_t irq_state_known;
    struct axd_si_state irq_state;
};
typedef int (*axd_si_switch_io_fn)(void *,const struct axd_si_switch_operation *,struct axd_si_switch_reply *);
enum axd_si_switch_phase { AXD_SIS_INVALID=0, AXD_SIS_RUNNING, AXD_SIS_TRACE_COMPLETE, AXD_SIS_DEPENDENCY, AXD_SIS_FAILED, AXD_SIS_CANCELLED };
enum axd_si_switch_error { AXD_SIS_NO_ERROR=0, AXD_SIS_BAD_ARGUMENT, AXD_SIS_BINDING, AXD_SIS_CHILD_ERROR, AXD_SIS_IO_ERROR };
enum axd_si_switch_dependency { AXD_SIS_NO_DEPENDENCY=0, AXD_SIS_CATALOG, AXD_SIS_SELECTION, AXD_SIS_IRQ_STATE };
struct axd_si_switch {
    enum axd_si_switch_phase phase;
    enum axd_si_switch_error error;
    enum axd_si_switch_dependency dependency;
    enum axd_si_switch_command command;
    struct axd_si_switch_context context;
    struct axd_si_select selector;
    uint32_t requested, saved_index, irq_token, return_value;
    unsigned stage, state_valid, saved_stored, token_stored;
    unsigned disable_called, restore_called, selection_completed;
    uint64_t attempted_ops, completed_ops;
};
/* Actual HND si_switch_core/si_restore_core. SWITCH requested is core ID;
 * RESTORE requested is saved index/fast-path ID. saved_index and irq_token
 * initialize the caller's two output slots: stock does not always write them.
 * Fast bus1 windows preserve wrapping u32 addition and the zero CC-map guard.
 * An ordinary SWITCH reloads saved index AFTER a successful IRQ disable.
 * Restore's IRQ guard is independent and uses actual post-selection state.
 * IRQ callbacks must report actual resulting state, including unchanged state.
 * Catalog/config/callback bindings are immutable; state is serialized and
 * copied. state_known preserves unknown post-IRQ state across restarted begins;
 * fast RESTORE may skip that unused state. ARM32 labels never become host
 * function pointers. No kernel backend,
 * invented unwind or retry; error wins over cancellation. Missing metadata is
 * required only on a reached branch. Each step calls at most one OS callback.
 */
int axd_si_switch_begin(struct axd_si_switch *,enum axd_si_switch_command,
                          uint32_t requested,uint32_t saved_index,uint32_t irq_token,
                          const struct axd_si_switch_context *);
enum axd_si_switch_phase axd_si_switch_step(struct axd_si_switch *,axd_si_switch_io_fn,void *);
enum axd_si_switch_phase axd_si_switch_cancel(struct axd_si_switch *);
#endif
