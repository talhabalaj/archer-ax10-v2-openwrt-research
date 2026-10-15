/* SPDX-License-Identifier: MIT */
#ifndef AXD_UCODE_SELECT_H
#define AXD_UCODE_SELECT_H
#include <stddef.h>
#include <stdint.h>

struct axd_ucode_selection {
    const char *primary;
    const char *extended;
    size_t primary_bytes;
    size_t extended_bytes;
};

/* Pure selection recovered from the pinned stock module. Supplies names and
 * lengths, never firmware bytes or permission to upload them. Boardflags must
 * come from the correct slice; no default board configuration is assumed.
 * Only core 130 minor 0/1 are admitted by this research component.
 */
int axd_select_ucode(uint16_t core_revision, uint16_t minor_revision,
                     uint32_t slice_boardflags, struct axd_ucode_selection *out);
#endif
