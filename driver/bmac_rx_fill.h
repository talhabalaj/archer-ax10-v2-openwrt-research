/* SPDX-License-Identifier: MIT */
#ifndef AXD_BMAC_RX_FILL_H
#define AXD_BMAC_RX_FILL_H
#include "dma_rx_fill.h"
#include "dma_rx_status_fill.h"
#define AXD_BMAC_RX_FILL_MAX_FIFOS 72u
struct axd_bmac_rx_fill_context {
    struct axd_wake_u32 hardware,wlc,pub,selected_di;
    uint32_t selected_fifo;
    struct axd_wake_u32 normal_binding,status_binding;
    struct axd_rx_fill_context normal;
    struct axd_rx_status_fill_context status;
    uint8_t public_owned,normal_known,status_known;
};
enum axd_bmac_rx_fill_kind {AXD_BRF_PUBLIC_READ8=1,AXD_BRF_NORMAL,AXD_BRF_STATUS};
struct axd_bmac_rx_fill_operation {
    enum axd_bmac_rx_fill_kind kind;
    uint32_t identity,offset;
    struct axd_rx_fill_operation normal;
    struct axd_rx_status_fill_operation status;
};
typedef int (*axd_bmac_rx_fill_io_fn)(void *,const struct axd_bmac_rx_fill_operation *,uint32_t *);
enum axd_bmac_rx_fill_phase {AXD_BRF_INVALID=0,AXD_BRF_RUNNING,AXD_BRF_TRACE_COMPLETE,
    AXD_BRF_DEPENDENCY,AXD_BRF_FAILED,AXD_BRF_CANCELLED};
enum axd_bmac_rx_fill_error {AXD_BRF_NO_ERROR=0,AXD_BRF_BAD_ARGUMENT,AXD_BRF_BINDING,
    AXD_BRF_BOUNDS,AXD_BRF_CHILD_ERROR,AXD_BRF_IO_ERROR};
enum axd_bmac_rx_fill_dependency {AXD_BRF_NO_DEPENDENCY=0,AXD_BRF_METADATA,
    AXD_BRF_PUBLIC_OWNER,AXD_BRF_NORMAL_CONTEXT,AXD_BRF_STATUS_CONTEXT,
    AXD_BRF_NORMAL_CHILD,AXD_BRF_STATUS_CHILD};
enum axd_bmac_rx_fill_field {AXD_BRF_NO_FIELD=0,AXD_BRF_HARDWARE,AXD_BRF_WLC,
    AXD_BRF_PUBLIC,AXD_BRF_SELECTED_DI,AXD_BRF_NORMAL_BINDING,AXD_BRF_STATUS_BINDING};
struct axd_bmac_rx_fill {
    enum axd_bmac_rx_fill_phase phase;
    enum axd_bmac_rx_fill_error error;
    enum axd_bmac_rx_fill_dependency dependency;
    enum axd_bmac_rx_fill_field last_field;
    struct axd_bmac_rx_fill_context context;
    struct axd_dma_rx_fill normal_job;
    struct axd_dma_rx_status_fill status_job;
    uint32_t fifo,child_return_value;
    uint8_t eligibility_sample;
    unsigned stage,selected_status,child_return_valid,metadata_accesses;
    uint64_t attempted_ops,completed_ops;
};
/* Actual BMAC wrapper: fresh PUBLIC_READ8(pub,+0x1c0) unconditionally precedes
 * FIFO selection. Only fifo3 and sample bit0 choose real status fill; all other
 * cases compose real normal fill. Runtime facts identify hardware->wlc->pub
 * pointer chain and DI[6+fifo]; selected_fifo must match fifo, selected_di must
 * match the reached known child's binding. The array is not invented here.
 * FIFO<72 is explicit host bounds policy, not discovered hardware capacity.
 * Children publish canonical contexts before callbacks and after transitions.
 * At most1 callback/step, alias-safe begin, errors win cancel, completed effects
 * survive and terminals are silent. Child scalar return is exposed but this
 * native void wrapper does not infer successful fill or physical readiness. */
int axd_bmac_rx_fill_begin(struct axd_bmac_rx_fill *,uint32_t fifo,const struct axd_bmac_rx_fill_context *);
enum axd_bmac_rx_fill_phase axd_bmac_rx_fill_step(struct axd_bmac_rx_fill *,axd_bmac_rx_fill_io_fn,void *);
enum axd_bmac_rx_fill_phase axd_bmac_rx_fill_cancel(struct axd_bmac_rx_fill *);
#endif
