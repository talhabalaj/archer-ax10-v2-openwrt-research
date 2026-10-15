/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_CHANNEL_INIT_H
#define AXD_PHY_CHANNEL_INIT_H
#include "phy_table_write.h"
#include "object_memory.h"

struct axd_phy_radar_threshold {struct axd_wake_u32 first,second;};
struct axd_phy_channel_context {
    struct axd_phy_table_context hardware;
    struct axd_object_context object;
    struct axd_wake_u32 chanspec, radar_state_pointer;
    /* Index0..3: channels<71, BW1000/1800/2000/other. Index4..7:
     * channels>=71 in that order. These are runtime state, not defaults. */
    struct axd_phy_radar_threshold thresholds[8];
    struct axd_wake_u32 state176,state1b0,radar_shm_offset,hwobss_shm_offset;
    struct axd_wake_u32 current_band,mhf_cache_pointer,mhf0,mhf0_shm_offset;
    struct axd_wake_u8 mhf_clock_up,mhf_extended;
    uint16_t state170,state172,pi47c;
};
enum axd_phy_channel_command {AXD_CHANNEL_RADAR_INIT=1,AXD_CHANNEL_HWOBSS};
enum axd_phy_channel_phase {AXD_CHANNEL_INVALID=0,AXD_CHANNEL_RUNNING,
    AXD_CHANNEL_TRACE_COMPLETE,AXD_CHANNEL_DEPENDENCY,AXD_CHANNEL_FAILED,AXD_CHANNEL_CANCELLED};
enum axd_phy_channel_error {AXD_CHANNEL_NO_ERROR=0,AXD_CHANNEL_BAD_ARGUMENT,
    AXD_CHANNEL_UNSUPPORTED,AXD_CHANNEL_BINDING,AXD_CHANNEL_BOUNDS,
    AXD_CHANNEL_CHILD_ERROR,AXD_CHANNEL_IO_ERROR};
enum axd_phy_channel_field {AXD_CHANNEL_NO_FIELD=0,AXD_CHANNEL_CHANSPEC,
    AXD_CHANNEL_RADAR_STATE,AXD_CHANNEL_THRESHOLD_FIRST,AXD_CHANNEL_THRESHOLD_SECOND,
    AXD_CHANNEL_STATE176,AXD_CHANNEL_STATE1B0,AXD_CHANNEL_RADAR_SHM,
    AXD_CHANNEL_HWOBSS_SHM,AXD_CHANNEL_CURRENT_BAND,AXD_CHANNEL_MHF_CACHE,
    AXD_CHANNEL_MHF0,AXD_CHANNEL_MHF_CLOCK,AXD_CHANNEL_MHF_OFFSET,
    AXD_CHANNEL_MHF_EXTENDED,AXD_CHANNEL_OBJECT_OWNER};
struct axd_phy_channel_init {
    enum axd_phy_channel_phase phase;enum axd_phy_channel_error error;
    enum axd_phy_channel_command command;enum axd_phy_channel_field last_field;
    struct axd_phy_channel_context context;
    struct axd_phy_table_write packed_job;
    struct axd_phy_register phy_job;
    struct axd_object_memory object_job;
    uint16_t read_sample,mhf_before;
    unsigned enabled,stage,after_child,index,threshold_index,read_valid;
    unsigned packed_completed,modify_completed,shm_completed,software_stores;
    unsigned mhf_completed,metadata_accesses;
    uint64_t attempted_ops,completed_ops;
};
/* Actual AC PHY51 radar-init and HWOBSS bodies, core130.0/1. RADAR_INIT
 * includes current threshold copies, packed PHY writes, real SHM16 writer and
 * its literal MHF(index0,mask2000,value=enabled?2000:0,band0) tail. It does not
 * implement other hostflag indexes/bands or radar detection/calibration.
 * HWOBSS includes actual packed/RMW writes, fresh PHY13a read and SHM16.
 * enabled is the actual byte/int predicate; any supplied byte nonzero is true.
 * Unknown reached bindings stop; required nulls fail. Runtime thresholds,
 * descriptors, band/cache and powered resources are caller-owned/coherent.
 * Private/NVRAM defaults, arbitrary callback success and device access are
 * never supplied. object identity must agree with the canonical hardware.
 */
int axd_phy_channel_init_begin(struct axd_phy_channel_init *,enum axd_phy_channel_command,
                                unsigned enabled,const struct axd_phy_channel_context *);
/* Begin snapshots aliases. One synchronous callback maximum per step. Child
 * partial context is published before callbacks and after every step; error
 * wins cancellation. Later software stores and child returns remain separate.
 * A successful cancelling callback retains its actual child observation/effect,
 * but cannot execute later SHM/MHF work. No terminal unwind/cleanup/retry. */
enum axd_phy_channel_phase axd_phy_channel_init_step(struct axd_phy_channel_init *,axd_d11_io_fn,void *);
enum axd_phy_channel_phase axd_phy_channel_init_cancel(struct axd_phy_channel_init *);
#endif
