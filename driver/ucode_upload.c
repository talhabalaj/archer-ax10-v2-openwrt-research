/* SPDX-License-Identifier: MIT */
#include "ucode_upload.h"
#include "ucode_select.h"

static int active(enum axd_upload_phase phase)
{
    return phase >= AXD_UPLOAD_SELECT_BARRIER && phase <= AXD_UPLOAD_DATA_WRITE;
}

static uint32_t word_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int axd_upload_begin(struct axd_ucode_upload *upload,
                      uint16_t core, uint16_t minor, uint32_t boardflags,
                      uint32_t ucode_chunk,
                      const uint8_t *primary, size_t primary_length,
                      const uint8_t *extended, size_t extended_length)
{
    const struct axd_ucode_upload empty = {0};
    struct axd_ucode_selection selected;
    if (upload == NULL)
        return -1;
    *upload = empty;
    upload->phase = AXD_UPLOAD_FAILED;
    upload->error = AXD_UPLOAD_BAD_ARGUMENT;
    if (ucode_chunk != 0 || axd_select_ucode(core, minor, boardflags, &selected) != 0) {
        upload->error = AXD_UPLOAD_UNSUPPORTED_CONTEXT;
        return -1;
    }
    if (primary == NULL || extended == NULL ||
        primary_length != selected.primary_bytes || extended_length != selected.extended_bytes ||
        primary_length % 4 != 0 || extended_length % 4 != 0)
        return -1;
    upload->image[0] = primary;
    upload->image[1] = extended;
    upload->length[0] = primary_length;
    upload->length[1] = extended_length;
    upload->error = AXD_UPLOAD_NO_ERROR;
    upload->phase = AXD_UPLOAD_SELECT_BARRIER;
    return 0;
}

enum axd_upload_phase axd_upload_step(struct axd_ucode_upload *upload,
                                      const struct axd_upload_io *io)
{
    int result;
    uint32_t ignored = 0;
    enum axd_upload_phase current, next;
    if (upload == NULL)
        return AXD_UPLOAD_INVALID;
    if (!active(upload->phase))
        return upload->phase;
    if (io == NULL || io->write32 == NULL || io->read32 == NULL || io->barrier == NULL) {
        upload->error = AXD_UPLOAD_BAD_ARGUMENT;
        upload->phase = AXD_UPLOAD_FAILED;
        return upload->phase;
    }
    current = upload->phase;
    upload->attempted_ops++;
    switch (current) {
    case AXD_UPLOAD_SELECT_BARRIER:
        result = io->barrier(io->context, AXD_BARRIER_STORE);
        next = AXD_UPLOAD_SELECT_WRITE;
        break;
    case AXD_UPLOAD_SELECT_WRITE:
        result = io->write32(io->context, 0x160,
                             upload->bank == 0 ? 0x03000000u : 0x03080000u);
        next = AXD_UPLOAD_SELECT_READBACK;
        break;
    case AXD_UPLOAD_SELECT_READBACK:
        result = io->read32(io->context, 0x160, &ignored);
        next = AXD_UPLOAD_SELECT_FULL_BARRIER;
        break;
    case AXD_UPLOAD_SELECT_FULL_BARRIER:
        result = io->barrier(io->context, AXD_BARRIER_FULL);
        next = AXD_UPLOAD_DATA_BARRIER;
        break;
    case AXD_UPLOAD_DATA_BARRIER:
        result = io->barrier(io->context, AXD_BARRIER_STORE);
        next = AXD_UPLOAD_DATA_WRITE;
        break;
    case AXD_UPLOAD_DATA_WRITE:
        result = io->write32(io->context, 0x164,
                             word_le(upload->image[upload->bank] + upload->word_index * 4));
        next = AXD_UPLOAD_DATA_BARRIER;
        break;
    default:
        return upload->phase;
    }
    if (result != 0) {
        upload->error = AXD_UPLOAD_CALLBACK_ERROR;
        upload->phase = AXD_UPLOAD_FAILED;
        return upload->phase;
    }
    upload->completed_ops++;
    if (current == AXD_UPLOAD_DATA_WRITE) {
        upload->word_index++;
        upload->words[upload->bank]++;
        if (upload->word_index == upload->length[upload->bank] / 4) {
            if (upload->bank == 0) {
                upload->bank = 1;
                upload->word_index = 0;
                next = AXD_UPLOAD_SELECT_BARRIER;
            } else {
                next = AXD_UPLOAD_TRANSFERRED;
            }
        }
    }
    if (upload->phase != AXD_UPLOAD_CANCELLED)
        upload->phase = next;
    return upload->phase;
}

enum axd_upload_phase axd_upload_cancel(struct axd_ucode_upload *upload)
{
    if (upload == NULL)
        return AXD_UPLOAD_INVALID;
    if (active(upload->phase))
        upload->phase = AXD_UPLOAD_CANCELLED;
    return upload->phase;
}
