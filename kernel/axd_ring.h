/* SPDX-License-Identifier: GPL-2.0 OR MIT */
#ifndef AXD_RING_H
#define AXD_RING_H
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include "dma_descriptor.h"

/*
 * Kernel owner of one DMA64 descriptor ring's memory. Coherent memory sized
 * count*16 bytes; the DMA API aligns it to the smallest page order covering
 * that size, so a power-of-two ring never crosses its pointer-mask boundary.
 * The portable axd_dma64_ring holds the software cursors and is validated by
 * axd_dma64_ring_check() before the memory is handed out.
 *
 * Preconditions the caller must hold (not checked here): the DMA engine using
 * the ring is stopped before axd_ring_free(), and every access is serialized
 * with the device lock. No buffer mapping, ownership or engine programming
 * happens here.
 */
struct axd_ring {
	struct device *dev;
	void *cpu;
	dma_addr_t dma;
	size_t bytes;
	struct axd_dma64_ring sw;
};

int axd_ring_dma_mask(struct device *dev);
int axd_ring_alloc(struct axd_ring *ring, struct device *dev, u32 count);
void axd_ring_free(struct axd_ring *ring);
int axd_ring_write(struct axd_ring *ring, u32 index, const struct axd_dma64_descriptor *d);
int axd_ring_read(struct axd_ring *ring, u32 index, struct axd_dma64_descriptor *d);

#endif
