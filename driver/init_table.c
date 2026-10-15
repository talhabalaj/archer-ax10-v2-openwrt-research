/* SPDX-License-Identifier: MIT */
#include "init_table.h"

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int axd_table_validate(const uint8_t *data, size_t length, uint32_t window_size,
                       struct axd_table_stats *stats)
{
    struct axd_table_stats found = {0, 0, 0};
    size_t pos;

    if (stats != NULL)
        *stats = found;
    if (data == NULL || window_size == 0 || window_size > 0x1000u)
        return AXD_INVALID_ARGUMENT;
    if (length == 0 || length % 8 != 0)
        return AXD_INVALID_TABLE;

    for (pos = 0; pos < length; pos += 8) {
        uint16_t offset = read16(data + pos);
        uint16_t width = read16(data + pos + 2);
        uint32_t value = read32(data + pos + 4);

        if (offset == 0xffffu) {
            if (width != 0 || value != 0 || pos != length - 8)
                return AXD_INVALID_TABLE;
            if (stats != NULL)
                *stats = found;
            return AXD_OK;
        }
        if ((width != 2 && width != 4) || offset % width != 0 ||
            (width == 2 && value > 0xffffu))
            return AXD_INVALID_TABLE;
        if ((uint32_t)offset >= window_size ||
            (uint32_t)width > window_size - (uint32_t)offset)
            return AXD_OUT_OF_RANGE;
        found.writes++;
        if (width == 2)
            found.writes16++;
        else
            found.writes32++;
    }
    return AXD_INVALID_TABLE;
}

int axd_table_replay(const uint8_t *data, size_t length, uint32_t window_size,
                     axd_write_fn write, void *context, size_t *written)
{
    size_t pos;
    int result;

    if (written != NULL)
        *written = 0;
    if (write == NULL)
        return AXD_INVALID_ARGUMENT;
    result = axd_table_validate(data, length, window_size, NULL);
    if (result != AXD_OK)
        return result;
    for (pos = 0; pos < length - 8; pos += 8) {
        if (write(context, read16(data + pos), read16(data + pos + 2),
                  read32(data + pos + 4)) != 0)
            return AXD_WRITE_FAILED;
        if (written != NULL)
            (*written)++;
    }
    return AXD_OK;
}
