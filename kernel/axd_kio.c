// SPDX-License-Identifier: GPL-2.0 OR MIT
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include "axd_kio.h"

static bool axd_kio_window(resource_size_t len, u32 offset, u32 width)
{
	return width && (offset % width) == 0 && offset <= len && width <= len - offset;
}

static int axd_kio_reject(struct axd_kio *kio, int error)
{
	kio->rejected++;
	return error;
}

int axd_kio_d11(void *context, const struct axd_d11_operation *op, uint32_t *value)
{
	struct axd_kio *kio = context;

	if (!kio || !op || !value)
		return -EINVAL;
	if (!kio->hw_enabled)
		return axd_kio_reject(kio, -EPERM);
	switch (op->kind) {
	case AXD_D11_STORE_BARRIER:
		wmb();
		break;
	case AXD_D11_FULL_BARRIER:
		mb();
		break;
	case AXD_D11_DELAY_US:
		if (op->value > AXD_KIO_MAX_DELAY_US)
			return axd_kio_reject(kio, -ERANGE);
		udelay(op->value);
		break;
	case AXD_D11_READ16:
		if (!axd_kio_window(kio->d11_len, op->offset, 2))
			return axd_kio_reject(kio, -EFAULT);
		*value = readw_relaxed(kio->d11 + op->offset);
		break;
	case AXD_D11_WRITE16:
		if (!axd_kio_window(kio->d11_len, op->offset, 2) || op->value > 0xffff)
			return axd_kio_reject(kio, -EFAULT);
		writew_relaxed((u16)op->value, kio->d11 + op->offset);
		break;
	case AXD_D11_READ32:
		if (!axd_kio_window(kio->d11_len, op->offset, 4))
			return axd_kio_reject(kio, -EFAULT);
		*value = readl_relaxed(kio->d11 + op->offset);
		break;
	case AXD_D11_WRITE32:
		if (!axd_kio_window(kio->d11_len, op->offset, 4))
			return axd_kio_reject(kio, -EFAULT);
		writel_relaxed(op->value, kio->d11 + op->offset);
		break;
	default:
		return axd_kio_reject(kio, -EINVAL);
	}
	kio->completed++;
	return 0;
}

int axd_kio_si(void *context, const struct axd_si_core_operation *op, uint32_t *value)
{
	struct axd_kio *kio = context;

	if (!kio || !op || !value)
		return -EINVAL;
	if (!kio->hw_enabled)
		return axd_kio_reject(kio, -EPERM);
	/* The stream names its wrapper; only the owned one may be accessed. */
	if (op->kind == AXD_SICO_READ32 || op->kind == AXD_SICO_WRITE32) {
		if (!kio->wrapper || op->wrapper != kio->wrapper_phys ||
		    !axd_kio_window(kio->wrapper_len, op->offset, 4))
			return axd_kio_reject(kio, -EFAULT);
	}
	switch (op->kind) {
	case AXD_SICO_STORE_BARRIER:
		wmb();
		break;
	case AXD_SICO_FULL_BARRIER:
		mb();
		break;
	case AXD_SICO_READ32:
		*value = readl_relaxed(kio->wrapper + op->offset);
		break;
	case AXD_SICO_WRITE32:
		writel_relaxed(op->value, kio->wrapper + op->offset);
		break;
	default:
		return axd_kio_reject(kio, -EINVAL);
	}
	kio->completed++;
	return 0;
}
