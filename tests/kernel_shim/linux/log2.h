/* SPDX-License-Identifier: MIT */
#ifndef AXD_SHIM_LOG2_H
#define AXD_SHIM_LOG2_H
#include "types.h"
static inline bool is_power_of_2(unsigned long n){return n && !(n & (n - 1));}
static inline unsigned long roundup_pow_of_two(unsigned long n){unsigned long p = 1; while (p < n) p <<= 1; return p;}
#endif
