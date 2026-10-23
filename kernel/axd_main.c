// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * AX10 v2 (BCM63178, D11 core130, AC PHY51) original WLAN driver: kernel
 * transport layer. Owns the register windows, IRQ number, serialization and
 * lifetime around the portable components in ../driver. It does not yet
 * register a network interface, request the IRQ, allocate DMA rings or start
 * the radio: those need the remaining offline gates (PHY vector, BMAC init,
 * RX/TX paths). Hardware access is off unless hw_access=1 is given, and no
 * code path issues device I/O at probe time.
 */
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include "axd_kio.h"
#include "axd_ring.h"

static bool hw_access;
module_param(hw_access, bool, 0444);
MODULE_PARM_DESC(hw_access, "Permit device register access (default off; required for any job)");

struct axd_dev {
	struct device *dev;
	struct axd_kio kio;
	struct mutex lock;	/* serializes every job and indirect register pair */
	int irq;		/* recorded only: no handler until ack/status exist */
	struct dentry *debugfs;
};

static int axd_state_show(struct seq_file *s, void *unused)
{
	struct axd_dev *ad = s->private;

	mutex_lock(&ad->lock);
	seq_printf(s, "hw_access: %s\n", ad->kio.hw_enabled ? "enabled" : "disabled");
	seq_printf(s, "d11_window: %pa bytes\n", &ad->kio.d11_len);
	seq_printf(s, "wrapper_window: %pa bytes\n", &ad->kio.wrapper_len);
	seq_printf(s, "irq: %d (not requested)\n", ad->irq);
	seq_printf(s, "operations: %llu completed, %llu rejected\n",
		   ad->kio.completed, ad->kio.rejected);
	seq_puts(s, "radio: not started (offline PHY vector incomplete)\n");
	mutex_unlock(&ad->lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(axd_state);

static int axd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct axd_dev *ad;

	ad = devm_kzalloc(dev, sizeof(*ad), GFP_KERNEL);
	if (!ad)
		return -ENOMEM;
	ad->dev = dev;
	ad->kio.dev = dev;
	mutex_init(&ad->lock);

	ad->kio.d11 = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(ad->kio.d11))
		return dev_err_probe(dev, PTR_ERR(ad->kio.d11), "D11 register window\n");
	ad->kio.d11_len = resource_size(res);

	ad->kio.wrapper = devm_platform_get_and_ioremap_resource(pdev, 1, &res);
	if (IS_ERR(ad->kio.wrapper))
		return dev_err_probe(dev, PTR_ERR(ad->kio.wrapper), "core wrapper window\n");
	ad->kio.wrapper_len = resource_size(res);
	if (upper_32_bits(res->start))
		return dev_err_probe(dev, -ERANGE, "wrapper above 4 GiB\n");
	ad->kio.wrapper_phys = lower_32_bits(res->start);

	ad->irq = platform_get_irq_optional(pdev, 0);
	/* Software-only: descriptors carry 32-bit addresses. Rings are allocated
	 * by a future start path, never at probe. */
	if (axd_ring_dma_mask(dev))
		return dev_err_probe(dev, -EIO, "32-bit DMA mask unavailable\n");
	ad->kio.hw_enabled = hw_access;
	platform_set_drvdata(pdev, ad);

	ad->debugfs = debugfs_create_dir(dev_name(dev), NULL);
	debugfs_create_file("state", 0444, ad->debugfs, ad, &axd_state_fops);

	if (hw_access)
		dev_warn(dev, "register access ENABLED by module parameter\n");
	dev_info(dev, "mapped D11 %pa bytes, wrapper %pa bytes; radio not started\n",
		 &ad->kio.d11_len, &ad->kio.wrapper_len);
	return 0;
}

static void axd_remove(struct platform_device *pdev)
{
	struct axd_dev *ad = platform_get_drvdata(pdev);

	/* No IRQ, DMA or work exists yet; mappings are device-managed. */
	debugfs_remove_recursive(ad->debugfs);
}

/* Provisional local binding; not an upstream-reviewed compatible. */
static const struct of_device_id axd_of_match[] = {
	{ .compatible = "axd,bcm63178-d11" },
	{ }
};
MODULE_DEVICE_TABLE(of, axd_of_match);

static struct platform_driver axd_driver = {
	.probe = axd_probe,
	.remove = axd_remove,
	.driver = {
		.name = "axd-bcm63178-d11",
		.of_match_table = axd_of_match,
	},
};
module_platform_driver(axd_driver);

MODULE_DESCRIPTION("Original BCM63178 D11 core130 WLAN driver (offline-built, incomplete)");
MODULE_LICENSE("Dual MIT/GPL");
