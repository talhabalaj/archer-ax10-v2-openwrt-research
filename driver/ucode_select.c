/* SPDX-License-Identifier: MIT */
#include "ucode_select.h"

int axd_select_ucode(uint16_t core_revision, uint16_t minor_revision,
                     uint32_t slice_boardflags, struct axd_ucode_selection *out)
{
    static const struct axd_ucode_selection pairs[2][2] = {
        {{"d11ucode_mu130", "d11ucodex_mu130", 65272, 37584},
         {"d11ucode_btcxmu130", "d11ucodex_btcxmu130", 68968, 37400}},
        {{"d11ucode_mu130_1", "d11ucodex_mu130_1", 64928, 37584},
         {"d11ucode_btcxmu130_1", "d11ucodex_btcxmu130_1", 68624, 37400}}
    };
    const struct axd_ucode_selection empty = {NULL, NULL, 0, 0};
    if (out == NULL)
        return -1;
    *out = empty;
    if (core_revision != 130 || minor_revision > 1)
        return -1;
    *out = pairs[minor_revision][slice_boardflags & 1u];
    return 0;
}
