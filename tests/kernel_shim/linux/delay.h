/* SPDX-License-Identifier: MIT */
#ifndef AXD_SHIM_DELAY_H
#define AXD_SHIM_DELAY_H
#include "types.h"
#define udelay(us) axd_shim_log(SHIM_UDELAY,0,(u32)(us))
#endif
