/* SPDX-License-Identifier: MIT */
#ifndef AXD_PHY_CHANSPEC_H
#define AXD_PHY_CHANSPEC_H
#include "radio_control.h"
#include "si_core.h"
#include "radio20704_pll.h"
/* Registered AC channel initialization, core130.0/1 AC PHY51, chip63178.
 * Canonical pointers (.radio.phy/ac/chanmgr/chanmgr_data, .radio.chip_id)
 * are shared with radio-on; aliases are checked, never assumed. */
struct axd_phy_chanspec_context {
    struct axd_phy_table_context hardware;
    struct axd_radio_control_context radio;
    /* Selected D11 SI mapping for actual core-cflags writes; known must be1. */
    struct axd_si_core_context si;
    uint8_t si_known;
    struct axd_wake_u32 chanspec, common_pointer, radar_pointer, radar_info;
    /* BMAC band: phy pointer (must equal .radio.phy_pointer), u16 phy type
     * at band+1c and BMAC chanspec word1d4 (the scheduler rebinds it from its
     * canonical hardware chanspec; PI396 is published back as bandwidth state). bwinit_slot is the registered
     * chanmgr bwinit label; ac_bwinit_label identifies the actual AC leaf. */
    struct axd_wake_u32 band_phy_pointer, band_phy_type, bmac_chanspec;
    struct axd_wake_u32 bwinit_slot, ac_bwinit_label;
    /* Shared PHY object: u16 phymode at +a and current PI word at +4. */
    struct axd_wake_u32 phymode, shared_current_pi, temp_pointer, temp_state;
    /* SI word at sih+64 (package/variant), selecting the radio20704 tune table
     * with .radio.radio_revision. tune_rows borrows the caller-resolved PRIVATE
     * table (16-byte LE rows: channel, MHz, per-channel bytes); tune_table must
     * name the table the actual selector picks and tune_count its native count.
     * Storage is immutable and outside the job through terminal state. */
    struct axd_wake_u32 sih_package;
    const uint8_t *tune_rows;
    uint32_t tune_count, tune_table;
    uint8_t tune_known;
    /* radio20704 PLL: runtime configuration reached via the AC radio object,
     * its AXD_R20704_PLL_FIELDS register-field descriptors (selector, mask,
     * shift) and the caller-owned u16 value block the computation updates in
     * place. Borrowed; storage outlives the job. pll_known must be1. */
    struct axd_radio20704_pll_config pll_config;
    const uint16_t *pll_selectors, *pll_masks;
    const uint8_t *pll_shifts;
    uint16_t *pll_values;
    uint8_t pll_known;
    /* Software results, all stored in source order. */
    uint8_t chanmgr_data11, ac_flags_be, radar_indicator, chanmgr_byte29;
    uint8_t band_byte9, core_bitmap18, tempsense_byte5;
    uint16_t common_previous_chanspec, bwstate396;
    uint32_t ac_bw_b8;
};
enum axd_phy_chanspec_path { AXD_CS_D11=1, AXD_CS_SI };
struct axd_phy_chanspec_operation {
    enum axd_phy_chanspec_path path;
    struct axd_d11_operation d11;
    struct axd_si_core_operation si;
};
typedef int (*axd_phy_chanspec_io_fn)(void *,const struct axd_phy_chanspec_operation *,uint32_t *);
enum axd_phy_chanspec_phase { AXD_CS_INVALID=0, AXD_CS_RUNNING, AXD_CS_TRACE_COMPLETE,
    AXD_CS_DEPENDENCY, AXD_CS_FAILED, AXD_CS_CANCELLED };
enum axd_phy_chanspec_error { AXD_CS_NO_ERROR=0, AXD_CS_BAD_ARGUMENT, AXD_CS_BINDING,
    AXD_CS_UNSUPPORTED, AXD_CS_BOUNDS, AXD_CS_CHILD_ERROR, AXD_CS_IO_ERROR };
/* Reached real children not yet implemented; each is a named gate. */
enum axd_phy_chanspec_dependency { AXD_CS_NO_DEPENDENCY=0, AXD_CS_METADATA,
    AXD_CS_MAC_CHILD, AXD_CS_RESOURCE_CHILD, AXD_CS_BMAC_PHY_RESET,
    AXD_CS_PHY_INIT_FALLBACK, AXD_CS_CHIP4335_BRANCH,
    AXD_CS_RADIO2069_SETUP, AXD_CS_RADIO20704_SETUP, AXD_CS_MODULE_SETUP_PHY,
    AXD_CS_RADIO20704_2G, AXD_CS_RADIO20704_6G, AXD_CS_PLL_TUNE, AXD_CS_VCOCAL };
enum axd_phy_chanspec_field { AXD_CS_NO_FIELD=0, AXD_CS_CHANSPEC, AXD_CS_COMMON,
    AXD_CS_RADAR, AXD_CS_RADAR_INFO, AXD_CS_PHY, AXD_CS_AC, AXD_CS_CHANMGR,
    AXD_CS_CHANMGR_DATA, AXD_CS_CHIP, AXD_CS_BAND_PHY, AXD_CS_BAND_TYPE,
    AXD_CS_BMAC_CHANSPEC, AXD_CS_BWINIT, AXD_CS_AC_BWINIT, AXD_CS_PHYMODE,
    AXD_CS_SHARED_PI, AXD_CS_TEMP, AXD_CS_TEMP_STATE, AXD_CS_SI_MAPPING, AXD_CS_RADIO_ID,
    AXD_CS_RADIO_REVISION, AXD_CS_SIH_PACKAGE, AXD_CS_TUNE_TABLE, AXD_CS_PLL };
/* Actual radio20704 tune tables (rev0: one table; rev3: per band). */
enum axd_phy_chanspec_tune { AXD_CS_TUNE_NONE=0, AXD_CS_TUNE_REV0_63178, AXD_CS_TUNE_REV0_47622A,
    AXD_CS_TUNE_REV0_47622B, AXD_CS_TUNE_REV3_63178_6G, AXD_CS_TUNE_REV3_63178_5G,
    AXD_CS_TUNE_REV3_63178_2G, AXD_CS_TUNE_REV3_47622A_6G, AXD_CS_TUNE_REV3_47622A_5G,
    AXD_CS_TUNE_REV3_47622A_2G, AXD_CS_TUNE_REV3_47622B_6G, AXD_CS_TUNE_REV3_47622B_5G,
    AXD_CS_TUNE_REV3_47622B_2G };
struct axd_phy_chanspec {
    enum axd_phy_chanspec_phase phase; enum axd_phy_chanspec_error error;
    enum axd_phy_chanspec_dependency dependency; enum axd_phy_chanspec_field last_field;
    struct axd_phy_chanspec_context context;
    struct axd_phy_register phy_job;
    struct axd_radio_control radio_job;
    struct axd_psm_control mac_job;
    struct axd_si_core_job si_job;
    struct axd_d11_operation raw;
    uint32_t main_control;
    uint8_t radio20704_argument;
    uint16_t tune_frequency;
    uint32_t tune_selected;
    const uint8_t *tune_row;
    int pll_result;
    unsigned pll_writes;
    uint16_t sync_guard, cca_sample, setup_guard;
    unsigned stage, after_child, after_cca, core_index, part, mac_enabled, cca_valid;
    unsigned modules_completed, sync_completed, cca_completed, corestate_completed;
    unsigned bandwidth_completed, bwstate_stored, software_stores, metadata_accesses, pending_callouts;
    uint64_t attempted_ops, completed_ops;
};
/* Executes, in actual order: prologue stores, optional radar indicator, the
 * radio-set chanspec store, then module0 chanspec_setup: core-to-core sync
 * (argument0) with fresh MAC-control sample and real PSM suspend/enable,
 * guarded resetcca with real PHY-clock force/release cflags, phycorestate
 * (fresh PHY0b core count, bitmap, tempsense) and BMAC bandwidth set/reset
 * with the registered bwinit binding. Module1 chanspec_setup_tpc is its actual
 * rev>46 return. Module2 dispatches on the explicit radio ID: 50e0 runs the
 * real resetcca and the radio20704 5GHz body (tune row, LOGEN reset, band,
 * per-core and tail radio writes), then pll_tune (computed values, the73 field writes and final 9f6 bits),
 * then reaches VCO calibration, a DEPENDENCY;
 * the2.4/6GHz frequency branches are DEPENDENCY gates;
 * 2069 is a DEPENDENCY; other IDs return to module3 setup_phy, a DEPENDENCY. The callback never returns until
 * all fourteen modules exist.
 * Other PHY revisions, the4335 branch, BMAC PHY reset and a null bwinit slot
 * (phy_init fallback) are explicit gates, not successful no-ops. */
int axd_phy_chanspec_begin(struct axd_phy_chanspec *,const struct axd_phy_chanspec_context *);
/* Begin snapshots aliases. One callback per step; child partial context is
 * published before callbacks and after every step. Error wins cancel;
 * successful cancelling effects are retained. Terminal states are silent. */
enum axd_phy_chanspec_phase axd_phy_chanspec_step(struct axd_phy_chanspec *,axd_phy_chanspec_io_fn,void *);
enum axd_phy_chanspec_phase axd_phy_chanspec_cancel(struct axd_phy_chanspec *);
#endif
