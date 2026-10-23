/* SPDX-License-Identifier: MIT */
#ifndef AXD_RADIO20704_PLL_H
#define AXD_RADIO20704_PLL_H
#include <stdint.h>
/* PLL value computation of wlc_phy_radio20704_pll_tune (radio 0x50e0 path).
 * The configuration is the runtime struct reached from the AC radio object:
 * reference word, reference shift byte and mode byte (0, 1 or 2). The value
 * block is caller-owned u16 state: only the offsets the actual code stores are
 * updated; all other entries keep their prior values, and all
 * AXD_R20704_PLL_FIELDS entries are later written to the radio. */
#define AXD_R20704_PLL_FIELDS 73u
struct axd_radio20704_pll_config { uint32_t reference; uint8_t shift, mode; };
/* Returns 0 after updating values, 1 for an unsupported mode (the actual code
 * returns before any store or radio write), -1 for a bad argument. */
int axd_radio20704_pll_compute(uint16_t frequency, const struct axd_radio20704_pll_config *config,
                               uint16_t values[AXD_R20704_PLL_FIELDS]);
#endif
