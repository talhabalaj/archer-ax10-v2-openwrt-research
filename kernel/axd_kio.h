/* SPDX-License-Identifier: GPL-2.0 OR MIT */
#ifndef AXD_KIO_H
#define AXD_KIO_H
#include <linux/types.h>
#include <linux/io.h>
#include "d11_io.h"
#include "si_core.h"

/*
 * Kernel backend for the portable operation streams. Each event maps to one
 * kernel primitive: relaxed MMIO accessors (no implicit barriers), wmb() for
 * the trace's store barrier and mb() for its full barrier, udelay() for
 * delays. On ARMv7 Linux these barriers are DSB ST and DSB SY (plus an outer
 * cache sync only on systems with an outer cache controller).
 *
 * Every access is bounds- and alignment-checked against the owned mapping and
 * fails instead of touching memory outside it. With hw_enabled false every
 * operation fails with -EPERM before any access: probing never implies
 * permission to drive the device.
 */
struct axd_kio {
	struct device *dev;
	void __iomem *d11;
	resource_size_t d11_len;
	void __iomem *wrapper;
	resource_size_t wrapper_len;
	u32 wrapper_phys;	/* identity the SI stream must name */
	bool hw_enabled;
	u64 completed, rejected;
};

/* Upper bound for one delay event; longer requests are rejected. */
#define AXD_KIO_MAX_DELAY_US 20000u

int axd_kio_d11(void *context, const struct axd_d11_operation *op, uint32_t *value);
int axd_kio_si(void *context, const struct axd_si_core_operation *op, uint32_t *value);

#endif
