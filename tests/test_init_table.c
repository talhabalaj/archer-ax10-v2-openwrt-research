/* SPDX-License-Identifier: MIT */
#include "../driver/init_table.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct recorder {
    size_t count;
    size_t fail_at;
    uint16_t offsets[3];
    uint16_t widths[3];
    uint32_t values[3];
};

static int record(void *context, uint16_t offset, uint16_t width, uint32_t value)
{
    struct recorder *r = context;
    if (r->count == r->fail_at)
        return -1;
    assert(r->count < 3);
    r->offsets[r->count] = offset;
    r->widths[r->count] = width;
    r->values[r->count++] = value;
    return 0;
}

int main(void)
{
    const uint8_t table[] = {
        0x60, 0x01, 4, 0, 0x78, 0x56, 0x34, 0x12,
        0x64, 0x01, 4, 0, 0xef, 0xcd, 0xab, 0x90,
        0x64, 0x01, 2, 0, 0x42, 0x00, 0x00, 0x00,
        0xff, 0xff, 0, 0, 0, 0, 0, 0
    };
    uint8_t bad[sizeof(table)];
    struct axd_table_stats stats;
    struct recorder r = {0, 99, {0}, {0}, {0}};
    size_t written = 99;

    assert(axd_table_validate(table, sizeof(table), 0x1000, &stats) == AXD_OK);
    assert(stats.writes == 3 && stats.writes16 == 1 && stats.writes32 == 2);
    assert(axd_table_replay(table, sizeof(table), 0x1000, record, &r, &written) == AXD_OK);
    assert(written == 3 && r.count == 3);
    assert(r.offsets[1] == r.offsets[2] && r.widths[2] == 2);
    assert(r.values[0] == 0x12345678u && r.values[1] == 0x90abcdefu);

    /* A malformed final record must produce zero earlier callbacks. */
    memcpy(bad, table, sizeof(table));
    bad[26] = 4;
    r.count = 0;
    assert(axd_table_replay(bad, sizeof(bad), 0x1000, record, &r, &written) == AXD_INVALID_TABLE);
    assert(written == 0 && r.count == 0);
    assert(axd_table_validate(table, sizeof(table) - 1, 0x1000, NULL) == AXD_INVALID_TABLE);
    assert(axd_table_validate(table, sizeof(table) - 8, 0x1000, NULL) == AXD_INVALID_TABLE);
    assert(axd_table_validate(table, sizeof(table), 0x166, NULL) == AXD_OUT_OF_RANGE);
    assert(axd_table_validate(table + 24, 8, 0x1000, &stats) == AXD_OK && stats.writes == 0);

    memcpy(bad, table, sizeof(table));
    bad[0] = 0x61; /* Misalignment. */
    assert(axd_table_validate(bad, sizeof(bad), 0x1000, NULL) == AXD_INVALID_TABLE);
    memcpy(bad, table, sizeof(table));
    bad[2] = 1; /* Unknown width. */
    assert(axd_table_validate(bad, sizeof(bad), 0x1000, NULL) == AXD_INVALID_TABLE);
    memcpy(bad, table, sizeof(table));
    bad[22] = 1; /* A 16-bit write with unrepresentable value. */
    assert(axd_table_validate(bad, sizeof(bad), 0x1000, NULL) == AXD_INVALID_TABLE);
    assert(axd_table_validate(NULL, 8, 0x1000, NULL) == AXD_INVALID_ARGUMENT);
    assert(axd_table_validate(table, sizeof(table), 0x10001, NULL) == AXD_INVALID_ARGUMENT);

    r.count = 0;
    r.fail_at = 1;
    assert(axd_table_replay(table, sizeof(table), 0x1000, record, &r, &written) == AXD_WRITE_FAILED);
    assert(written == 1 && r.count == 1);
    puts("init table validation, ordered replay, no-write rejection and failure handling passed");
    return 0;
}
