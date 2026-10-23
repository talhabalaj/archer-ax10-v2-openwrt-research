/* SPDX-License-Identifier: MIT */
#ifndef AXD_RADIO_REGTBL_H
#define AXD_RADIO_REGTBL_H
#include "phy_table_write.h"
#include "wake_policy.h"
struct axd_radio_regtbl_span {
    const struct axd_phy_table_write_request *entries;
    size_t count;
    uint8_t known;
};
struct axd_radio_regtbl_context {
    struct axd_phy_table_context hardware;
    struct axd_wake_u32 chanspec;
    struct axd_wake_u32 bmac_pointer,bmac_device_pointer,bmac_word150;
    struct axd_wake_u32 tpc_pointer,tpc_data_pointer,tpc_shared_pointer;
    struct axd_wake_u32 papd_pointer,papd_data_pointer;
    struct axd_wake_u8 tpc_two_range2g,tpc_two_range5g;
    struct axd_wake_u8 flag470,flag471,load_static,papd_byte0,papd_byte4;
    /* Exactly the nine native rev51 write descriptors and eight native zero
     * descriptors, resolved by the caller from the pinned private source.
     * Zero descriptors use count/table_id/width; native clear ignores offset
     * and payload, issuing count individual zero writes at offsets0..count-1.
     * Borrowed descriptors/payloads must remain immutable outside this job. */
    struct axd_radio_regtbl_span tables,zeros;
};
enum axd_radio_regtbl_phase {AXD_RRT_INVALID=0,AXD_RRT_RUNNING,
    AXD_RRT_TRACE_COMPLETE,AXD_RRT_DEPENDENCY,AXD_RRT_FAILED,AXD_RRT_CANCELLED};
enum axd_radio_regtbl_error {AXD_RRT_NO_ERROR=0,AXD_RRT_BAD_ARGUMENT,
    AXD_RRT_UNSUPPORTED,AXD_RRT_BINDING,AXD_RRT_BOUNDS,AXD_RRT_IO_ERROR,AXD_RRT_CHILD_ERROR};
enum axd_radio_regtbl_field {AXD_RRT_NO_FIELD=0,AXD_RRT_CHANSPEC,
    AXD_RRT_TPC,AXD_RRT_TPC_DATA,AXD_RRT_TPC_SHARED,AXD_RRT_TWO_RANGE2G,
    AXD_RRT_TWO_RANGE5G,AXD_RRT_FLAG470,AXD_RRT_FLAG471,AXD_RRT_COREUNIT,
    AXD_RRT_LOAD_STATIC,AXD_RRT_TABLES,AXD_RRT_ZEROS,AXD_RRT_PAPD,
    AXD_RRT_PAPD_DATA,AXD_RRT_PAPD_BYTE0,AXD_RRT_PAPD_BYTE4,
    AXD_RRT_BMAC,AXD_RRT_BMAC_DEVICE};
struct axd_radio_regtbl {
    enum axd_radio_regtbl_phase phase;enum axd_radio_regtbl_error error;
    enum axd_radio_regtbl_field last_field;
    struct axd_radio_regtbl_context context;
    struct axd_phy_table_write writer;
    struct axd_phy_register phy;
    uint8_t data[8];
    uint16_t guard_sample;
    unsigned stage,after_child,index,part,core_index,tssi,coreunit;
    uint32_t zero_index;
    unsigned packed_completed,modify_completed,table_completed,metadata_accesses;
    uint64_t attempted_ops,completed_ops;
};
/* Complete actual wlc_phy_set_regtbl_on_pwron_acphy target PHY51/core130.0/1
 * body. It does not infer radio identity, TSSI/PAPD predicates, initial-load
 * state, private descriptors or cached BMAC state. Runtime facts and private
 * provenance are caller-owned coherent snapshots. Only reached facts are
 * required. Unknown facts stop; required known-null object identities fail.
 * No calibration/static payload, successful vendor callout or device backend.
 */
int axd_radio_regtbl_begin(struct axd_radio_regtbl *,const struct axd_radio_regtbl_context *);
/* Begin snapshots own-context aliases. At most one callback per step. Child
 * partial contexts publish before callbacks and after every step; successful
 * cancelling child observations/effects/within-step commits survive. Error
 * wins cancellation. Later stages and child returns do not run. Silent
 * terminals; no rollback, retry, guard cleanup or readiness inference. */
enum axd_radio_regtbl_phase axd_radio_regtbl_step(struct axd_radio_regtbl *,axd_d11_io_fn,void *);
enum axd_radio_regtbl_phase axd_radio_regtbl_cancel(struct axd_radio_regtbl *);
#endif
