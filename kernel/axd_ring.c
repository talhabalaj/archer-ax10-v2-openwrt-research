// SPDX-License-Identifier: GPL-2.0 OR MIT
#include <linux/errno.h>
#include <linux/log2.h>
#include <linux/string.h>
#include "axd_ring.h"

/* Descriptors carry a 32-bit low address with no carry into the high word
 * (see axd_dma64_format), so restrict the device to 32-bit DMA addresses. */
int axd_ring_dma_mask(struct device *dev)
{
	return dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
}

int axd_ring_alloc(struct axd_ring *ring, struct device *dev, u32 count)
{
	memset(ring, 0, sizeof(*ring));
	if (!dev || count < 2 || count > 32768 || !is_power_of_2(count))
		return -EINVAL;
	ring->dev = dev;
	ring->bytes = (size_t)count * AXD_DMA64_DESCRIPTOR_SIZE;
	ring->cpu = dma_alloc_coherent(dev, ring->bytes, &ring->dma, GFP_KERNEL);
	if (!ring->cpu)
		return -ENOMEM;
	ring->sw.count = count;
	ring->sw.pointer_mask = (u32)roundup_pow_of_two(ring->bytes) - 1;
	ring->sw.pointer_base = lower_32_bits(ring->dma);
	if (upper_32_bits(ring->dma) || axd_dma64_ring_check(&ring->sw) != AXD_DMA64_OK) {
		axd_ring_free(ring);
		return -ERANGE;
	}
	return 0;
}

void axd_ring_free(struct axd_ring *ring)
{
	if (ring->cpu)
		dma_free_coherent(ring->dev, ring->bytes, ring->cpu, ring->dma);
	memset(ring, 0, sizeof(*ring));
}

/* Store one descriptor; dma_wmb() orders it before any later publication
 * (producer pointer write) the caller performs. */
int axd_ring_write(struct axd_ring *ring, u32 index, const struct axd_dma64_descriptor *d)
{
	if (!ring->cpu || index >= ring->sw.count || !d)
		return -EINVAL;
	if (axd_dma64_encode((u8 *)ring->cpu + (size_t)index * AXD_DMA64_DESCRIPTOR_SIZE,
			     AXD_DMA64_DESCRIPTOR_SIZE, d) != AXD_DMA64_OK)
		return -EINVAL;
	dma_wmb();
	return 0;
}

/* Read one descriptor after the caller observed device completion;
 * dma_rmb() orders the read after that observation. */
int axd_ring_read(struct axd_ring *ring, u32 index, struct axd_dma64_descriptor *d)
{
	if (!ring->cpu || index >= ring->sw.count || !d)
		return -EINVAL;
	dma_rmb();
	if (axd_dma64_decode(d, (const u8 *)ring->cpu + (size_t)index * AXD_DMA64_DESCRIPTOR_SIZE,
			     AXD_DMA64_DESCRIPTOR_SIZE) != AXD_DMA64_OK)
		return -EINVAL;
	return 0;
}
