/* SPDX-License-Identifier: MIT */
#ifndef AXD_DMA_DESCRIPTOR_H
#define AXD_DMA_DESCRIPTOR_H
#include <stddef.h>
#include <stdint.h>
#define AXD_DMA64_DESCRIPTOR_SIZE 16u
#define AXD_DMA64_EOT UINT32_C(0x10000000)
#define AXD_DMA64_IOC UINT32_C(0x20000000)
#define AXD_DMA64_EOF UINT32_C(0x40000000)
#define AXD_DMA64_SOF UINT32_C(0x80000000)
#define AXD_DMA64_NOT_PCIE UINT32_C(0x00040000)
#define AXD_DMA64_COUNT_MASK UINT32_C(0x00007fff)
#define AXD_DMA64_PARITY UINT32_C(0x00040000)
struct axd_dma64_descriptor { uint32_t control1, control2, address_low, address_high; };
struct axd_dma64_format {
    uint32_t mapped_address_low, data_offset_low, data_offset_high;
    uint32_t control1, byte_count;
    uint8_t parity_enabled;
};
enum axd_dma64_error {
    AXD_DMA64_OK = 0, AXD_DMA64_ARGUMENT, AXD_DMA64_BOUNDS,
    AXD_DMA64_FULL, AXD_DMA64_EMPTY
};
/* Exact current ARM32 dma64_dd_upd value transform, not a DMA mapper. Count
 * truncates to15bits; address adds modulo2^32, with NO carry into high word.
 * Nonzero offsetlow + mapped address top2bits selects address extension.
 * High word is the explicit offsethigh. Optional parity makes total parity
 * even. This does not establish a permitted transfer length/address/window.
 * Input/output may alias: inputs are snapshotted before output stores. */
enum axd_dma64_error axd_dma64_format(struct axd_dma64_descriptor *,
                                      const struct axd_dma64_format *);
/* Raw little-endian four-word codec: preserves all bits, including unknown
 * core-specific/reserved fields. Exactly16bytes required. Overlap is safe.
 * It does not parse a TX header, AQM payload or received frame prefix. */
enum axd_dma64_error axd_dma64_encode(uint8_t *, size_t,
                                      const struct axd_dma64_descriptor *);
enum axd_dma64_error axd_dma64_decode(struct axd_dma64_descriptor *,
                                      const uint8_t *, size_t);
struct axd_dma64_ring {
    uint32_t count, pointer_mask, pointer_base, in, out;
};
enum axd_dma64_range { AXD_DMA64_ALL = 1, AXD_DMA64_TRANSMITTED, AXD_DMA64_TRANSFERRED };
/* Explicit caller-owned immutable metadata and serialized software cursors.
 * Power-of-two count2..32768 follows current u16 index representation; this
 * is NOT a claim that target hardware supports those counts/masks/alignment.
 * Mask must be contiguous low bits including low4; ring cannot cross that
 * mask's boundary. Runtime attach probes/resources remain external. */
enum axd_dma64_error axd_dma64_ring_check(const struct axd_dma64_ring *);
enum axd_dma64_error axd_dma64_ring_counts(const struct axd_dma64_ring *,
                                           uint32_t *used, uint32_t *available);
enum axd_dma64_error axd_dma64_producer(const struct axd_dma64_ring *, uint32_t *pointer);
enum axd_dma64_error axd_dma64_status_index(const struct axd_dma64_ring *,
                                            uint32_t status, uint32_t *index);
/* Pure accounting only: neither function publishes descriptors, supplies a
 * barrier/cache flush, unmaps packets nor authorizes buffer reuse. One slot
 * stays reserved. Reclaim count requires external completion/quiescence proof. */
enum axd_dma64_error axd_dma64_post(struct axd_dma64_ring *, uint32_t count);
enum axd_dma64_error axd_dma64_reclaim(struct axd_dma64_ring *, uint32_t count);
/* Current range arithmetic on explicit samples, without reading hardware or
 * silently using cached completion. ALL selects software out, not completion.
 * TRANSFERRED uses previous(active) when current!=active; others use current.
 * Stricter host policy rejects unaligned/outside-ring samples and a candidate
 * beyond posted descriptors instead of accepting unchecked stock indices.
 * TX status success/IRQ scheduling and descriptor/buffer ownership are external.
 * current_mask and active_mask can differ; supplied ring mask is current_mask.
 * For active status, provide its actual explicit contiguous low-bit mask. */
enum axd_dma64_error axd_dma64_reclaimable(const struct axd_dma64_ring *,
    enum axd_dma64_range, uint32_t current_status, uint32_t active_status,
    uint32_t active_mask, uint32_t *count);
#endif
