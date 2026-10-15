/* SPDX-License-Identifier: MIT */
/* Compile-only integration check: all public driver interfaces share one TU. */
#include "../driver/ai_reset.h"
#include "../driver/bf_clean.h"
#include "../driver/chipcommon_clock.h"
#include "../driver/clock_control.h"
#include "../driver/core_reset.h"
#include "../driver/core_reset_low.h"
#include "../driver/core_reset_low_ai.h"
#include "../driver/mctrl.h"
#include "../driver/si_core.h"
#include "../driver/d11_io.h"
#include "../driver/dma_reset.h"
#include "../driver/effective_init.h"
#include "../driver/idle_tssi.h"
#include "../driver/init_table.h"
#include "../driver/phy_clock.h"
#include "../driver/phy_guard.h"
#include "../driver/phy_register.h"
#include "../driver/phy_reset.h"
#include "../driver/phy_table.h"
#include "../driver/phy_tssi.h"
#include "../driver/psm_control.h"
#include "../driver/psm_start.h"
#include "../driver/si_clock.h"
#include "../driver/si_select.h"
#include "../driver/status_poll.h"
#include "../driver/ucode_select.h"
#include "../driver/ucode_upload.h"
#include "../driver/wake_policy.h"
#include "../driver/wl_clock.h"

/* Namespace migration preserves the existing AI transport/state ABI values. */
_Static_assert(AXD_AI_STORE_BARRIER == 1 && AXD_AI_FULL_BARRIER == 2 &&
               AXD_AI_WRITE32 == 3 && AXD_AI_READ32 == 4 && AXD_AI_DELAY_US == 5,
               "AI event numbers changed");
_Static_assert(AXD_AI_INVALID == 0 && AXD_AI_RUNNING == 1 &&
               AXD_AI_TRACE_COMPLETE == 2 && AXD_AI_FAILED == 3 && AXD_AI_CANCELLED == 4,
               "AI phase numbers changed");
_Static_assert(AXD_AI_NO_ERROR == 0 && AXD_AI_BAD_ARGUMENT == 1 &&
               AXD_AI_UNSUPPORTED == 2 && AXD_AI_IO_ERROR == 3,
               "AI error numbers changed");
_Static_assert(AXD_RESET_DEPENDENCY == 3 && AXD_RESET_FAILED == 4,
               "PHY reset retains its distinct phase namespace");
