/* SPDX-License-Identifier: MIT */
#ifndef AXD_HND_MATH_FP_H
#define AXD_HND_MATH_FP_H
#include <stdint.h>
/* Original reimplementation of the HND fixed-point helpers used by radio PLL
 * tuning, matching the pinned hnd.ko behavior including ARM register-shift
 * edge cases (counts of32 or more give zero) and byte-truncated Q formats.
 * Pure functions: no I/O, allocation or global state. */
uint8_t axd_hnd_fp_head_room_32(uint32_t value);
uint8_t axd_hnd_fp_head_room_64(uint64_t value);
/* math_uint64_divide: divisor <=1 leaves *quotient unchanged (actual early
 * return); otherwise stores the low32 bits of the actual iterative quotient.
 * Not exact division: its carry (0xffffffff%d+1)%d drops one unit when the sum
 * equals d (2^32/2 gives 0x7fffffff). Reproduced deliberately: PLL values depend on it. */
void axd_hnd_uint64_divide(uint32_t *quotient, uint32_t high, uint32_t low, uint32_t divisor);
uint64_t axd_hnd_fp_mult_64(uint64_t a, uint64_t b, uint8_t qa, uint8_t qb, uint8_t qout);
/* Returns the result Q format; *quotient as math_uint64_divide. */
uint8_t axd_hnd_fp_div_64(uint64_t numerator, uint32_t denominator, uint8_t qn, uint8_t qd, uint32_t *quotient);
uint32_t axd_hnd_fp_round_32(uint32_t value, uint32_t shift);
uint32_t axd_hnd_fp_floor_32(uint32_t value, uint32_t shift);
uint32_t axd_hnd_fp_round_64(uint64_t value, uint32_t shift);
uint64_t axd_hnd_fp_ceil_64(uint64_t value, uint32_t shift);
#endif
