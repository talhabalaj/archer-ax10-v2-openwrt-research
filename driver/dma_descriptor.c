/* SPDX-License-Identifier: MIT */
#include "dma_descriptor.h"
#include <limits.h>
static unsigned parity(uint32_t x)
{
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return x & 1u;
}
enum axd_dma64_error axd_dma64_format(struct axd_dma64_descriptor *out,
                                      const struct axd_dma64_format *in)
{
    struct axd_dma64_format c;
    struct axd_dma64_descriptor d;
    uint32_t address;
    if (out == NULL || in == NULL) return AXD_DMA64_ARGUMENT;
    c = *in;
    if (c.parity_enabled > 1) return AXD_DMA64_ARGUMENT;
    address = c.mapped_address_low;
    d.control1 = c.control1;
    d.control2 = c.byte_count & AXD_DMA64_COUNT_MASK;
    if (c.data_offset_low != 0 && (address & UINT32_C(0xc0000000)) != 0) {
        d.control2 |= (address >> 30) << 16;
        address &= UINT32_C(0x3fffffff);
    }
    d.address_low = address + c.data_offset_low;
    d.address_high = c.data_offset_high;
    if (c.parity_enabled && parity(d.control1 ^ d.control2 ^ d.address_low ^ d.address_high))
        d.control2 |= AXD_DMA64_PARITY;
    *out = d;
    return AXD_DMA64_OK;
}
static uint32_t load32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void store32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
enum axd_dma64_error axd_dma64_encode(uint8_t *out, size_t size,
                                      const struct axd_dma64_descriptor *in)
{
    struct axd_dma64_descriptor d;
    if (out == NULL || in == NULL) return AXD_DMA64_ARGUMENT;
    if (size != AXD_DMA64_DESCRIPTOR_SIZE) return AXD_DMA64_BOUNDS;
    d = *in;
    store32(out, d.control1); store32(out + 4, d.control2);
    store32(out + 8, d.address_low); store32(out + 12, d.address_high);
    return AXD_DMA64_OK;
}
enum axd_dma64_error axd_dma64_decode(struct axd_dma64_descriptor *out,
                                      const uint8_t *in, size_t size)
{
    struct axd_dma64_descriptor d;
    if (out == NULL || in == NULL) return AXD_DMA64_ARGUMENT;
    if (size != AXD_DMA64_DESCRIPTOR_SIZE) return AXD_DMA64_BOUNDS;
    d.control1 = load32(in); d.control2 = load32(in + 4);
    d.address_low = load32(in + 8); d.address_high = load32(in + 12);
    *out = d;
    return AXD_DMA64_OK;
}
static int mask_valid(uint32_t mask)
{
    return mask >= 15u && (mask & (mask + 1u)) == 0;
}
enum axd_dma64_error axd_dma64_ring_check(const struct axd_dma64_ring *r)
{
    uint64_t end, boundary;
    if (r == NULL) return AXD_DMA64_ARGUMENT;
    if (r->count < 2 || r->count > 32768 || (r->count & (r->count - 1)) != 0 ||
        !mask_valid(r->pointer_mask) || (r->pointer_base & 15u) != 0 ||
        r->in >= r->count || r->out >= r->count) return AXD_DMA64_BOUNDS;
    end = (uint64_t)(r->pointer_base & r->pointer_mask) + (uint64_t)r->count * 16u;
    boundary = (uint64_t)r->pointer_mask + 1u;
    return end <= boundary ? AXD_DMA64_OK : AXD_DMA64_BOUNDS;
}
enum axd_dma64_error axd_dma64_ring_counts(const struct axd_dma64_ring *r,
                                           uint32_t *used, uint32_t *available)
{
    enum axd_dma64_error e = axd_dma64_ring_check(r);
    uint32_t n, free_count;
    if (used == NULL || available == NULL || used == available) return AXD_DMA64_ARGUMENT;
    if (e != AXD_DMA64_OK) return e;
    n = (r->out - r->in) & (r->count - 1u);
    free_count = r->count - n - 1u;
    *used = n; *available = free_count;
    return AXD_DMA64_OK;
}
enum axd_dma64_error axd_dma64_producer(const struct axd_dma64_ring *r, uint32_t *pointer)
{
    enum axd_dma64_error e = axd_dma64_ring_check(r);
    if (pointer == NULL) return AXD_DMA64_ARGUMENT;
    if (e != AXD_DMA64_OK) return e;
    *pointer = r->pointer_base + r->out * 16u;
    return AXD_DMA64_OK;
}
enum axd_dma64_error axd_dma64_status_index(const struct axd_dma64_ring *r,
                                            uint32_t status, uint32_t *index)
{
    uint32_t bytes;
    enum axd_dma64_error e = axd_dma64_ring_check(r);
    if (index == NULL) return AXD_DMA64_ARGUMENT;
    if (e != AXD_DMA64_OK) return e;
    bytes = ((status & r->pointer_mask) - r->pointer_base) & r->pointer_mask;
    if ((bytes & 15u) != 0 || bytes / 16u >= r->count) return AXD_DMA64_BOUNDS;
    *index = bytes / 16u;
    return AXD_DMA64_OK;
}
static enum axd_dma64_error advance(struct axd_dma64_ring *r, uint32_t count, unsigned post)
{
    uint32_t used, available;
    enum axd_dma64_error e = axd_dma64_ring_counts(r, &used, &available);
    if (e != AXD_DMA64_OK) return e;
    if (count > (post ? available : used)) return post ? AXD_DMA64_FULL : AXD_DMA64_EMPTY;
    if (post) r->out = (r->out + count) & (r->count - 1u);
    else r->in = (r->in + count) & (r->count - 1u);
    return AXD_DMA64_OK;
}
enum axd_dma64_error axd_dma64_post(struct axd_dma64_ring *r, uint32_t count)
{ return advance(r, count, 1); }
enum axd_dma64_error axd_dma64_reclaim(struct axd_dma64_ring *r, uint32_t count)
{ return advance(r, count, 0); }
enum axd_dma64_error axd_dma64_reclaimable(const struct axd_dma64_ring *r,
    enum axd_dma64_range range, uint32_t current_status, uint32_t active_status,
    uint32_t active_mask, uint32_t *count)
{
    uint32_t end, active, used, available, n;
    struct axd_dma64_ring a;
    enum axd_dma64_error e;
    if (count == NULL || range < AXD_DMA64_ALL || range > AXD_DMA64_TRANSFERRED)
        return AXD_DMA64_ARGUMENT;
    e = axd_dma64_ring_counts(r, &used, &available);
    if (e != AXD_DMA64_OK) return e;
    if (range == AXD_DMA64_ALL) end = r->out;
    else {
        e = axd_dma64_status_index(r, current_status, &end);
        if (e != AXD_DMA64_OK) return e;
        if (range == AXD_DMA64_TRANSFERRED) {
            a = *r; a.pointer_mask = active_mask;
            e = axd_dma64_status_index(&a, active_status, &active);
            if (e != AXD_DMA64_OK) return e;
            if (end != active) end = (active - 1u) & (r->count - 1u);
        }
    }
    n = (end - r->in) & (r->count - 1u);
    if (n > used) return AXD_DMA64_BOUNDS;
    *count = n;
    return AXD_DMA64_OK;
}
