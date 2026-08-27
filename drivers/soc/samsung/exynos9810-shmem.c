// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exynos9810 firmware shared-memory layout
 *
 * Copyright (C) 2026 Mathias Gluszczynski
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/shm_ipc.h>

struct exynos9810_shmem {
	phys_addr_t base;
	size_t size;
	u32 cp_size;
	u32 vss_size;
	u32 ipc_offset;
	u32 ipc_size;
	u32 zmb_offset;
	u32 zmb_size;
	u32 vparam_size;
	void __iomem *vss_region;
	void __iomem *vparam_region;
	bool ready;
};

static struct exynos9810_shmem exynos9810_shmem;

static int exynos9810_shmem_read_u32(struct device *dev, const char *name,
				     u32 *value)
{
	int ret;

	ret = of_property_read_u32(dev->of_node, name, value);
	if (ret)
		dev_err(dev, "missing %s property\n", name);

	return ret;
}

static int exynos9810_shmem_probe(struct platform_device *pdev)
{
	struct exynos9810_shmem *shmem = &exynos9810_shmem;
	struct device *dev = &pdev->dev;
	struct device_node *memory_np;
	struct reserved_mem *rmem;
	phys_addr_t vss_base;
	phys_addr_t vparam_base;
	u64 vparam_offset;
	int ret;

	memory_np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!memory_np)
		return dev_err_probe(dev, -EINVAL,
				"missing reserved memory region\n");

	rmem = of_reserved_mem_lookup(memory_np);
	of_node_put(memory_np);
	if (!rmem)
		return dev_err_probe(dev, -EPROBE_DEFER,
				"reserved memory is not ready\n");

	ret = exynos9810_shmem_read_u32(dev, "samsung,cp-size",
					&shmem->cp_size);
	ret |= exynos9810_shmem_read_u32(dev, "samsung,vss-size",
			&shmem->vss_size);
	ret |= exynos9810_shmem_read_u32(dev, "samsung,ipc-offset",
			&shmem->ipc_offset);
	ret |= exynos9810_shmem_read_u32(dev, "samsung,ipc-size",
			&shmem->ipc_size);
	ret |= exynos9810_shmem_read_u32(dev, "samsung,zmb-offset",
			&shmem->zmb_offset);
	ret |= exynos9810_shmem_read_u32(dev, "samsung,zmb-size",
			&shmem->zmb_size);
	ret |= exynos9810_shmem_read_u32(dev, "samsung,vparam-size",
			&shmem->vparam_size);
	if (ret)
		return -EINVAL;

	if (shmem->ipc_offset != shmem->cp_size + shmem->vss_size) {
		dev_err(dev, "VSS does not end at the IPC boundary\n");
		return -EINVAL;
	}

	if (shmem->zmb_offset != shmem->ipc_offset + shmem->ipc_size) {
		dev_err(dev, "IPC does not end at the ZMB boundary\n");
		return -EINVAL;
	}

	vparam_offset = (u64)shmem->zmb_offset + shmem->zmb_size;
	if (vparam_offset + shmem->vparam_size > rmem->size) {
		dev_err(dev, "shared-memory layout exceeds reserved region\n");
		return -EINVAL;
	}

	shmem->base = rmem->base;
	shmem->size = rmem->size;
	vss_base = shmem->base + shmem->cp_size;
	shmem->vss_region = devm_ioremap_wc(dev, vss_base, shmem->vss_size);
	if (!shmem->vss_region)
		return dev_err_probe(dev, -ENOMEM,
				"failed to map VSS memory\n");

	vparam_base = shmem->base + vparam_offset;
	shmem->vparam_region = devm_ioremap_wc(dev, vparam_base,
					       shmem->vparam_size);
	if (!shmem->vparam_region)
		return dev_err_probe(dev, -ENOMEM,
				"failed to map VSS parameter memory\n");

	shmem->ready = true;
	dev_info(dev,
		 "VSS %pa+%#x, parameter %pa+%#x from %pa+%#zx\n",
		 &vss_base, shmem->vss_size, &vparam_base,
		 shmem->vparam_size, &shmem->base, shmem->size);

	return 0;
}

unsigned long shm_get_phys_base(void)
{
	return exynos9810_shmem.ready ? exynos9810_shmem.base : 0;
}
EXPORT_SYMBOL_GPL(shm_get_phys_base);

unsigned int shm_get_phys_size(void)
{
	return exynos9810_shmem.ready ? exynos9810_shmem.size : 0;
}
EXPORT_SYMBOL_GPL(shm_get_phys_size);

unsigned int shm_get_cp_size(void)
{
	return exynos9810_shmem.ready ? exynos9810_shmem.cp_size : 0;
}
EXPORT_SYMBOL_GPL(shm_get_cp_size);

unsigned int shm_get_ipc_rgn_offset(void)
{
	return exynos9810_shmem.ready ? exynos9810_shmem.ipc_offset : 0;
}
EXPORT_SYMBOL_GPL(shm_get_ipc_rgn_offset);

unsigned int shm_get_ipc_rgn_size(void)
{
	return exynos9810_shmem.ready ? exynos9810_shmem.ipc_size : 0;
}
EXPORT_SYMBOL_GPL(shm_get_ipc_rgn_size);

unsigned int shm_get_zmb_size(void)
{
	return exynos9810_shmem.ready ? exynos9810_shmem.zmb_size : 0;
}
EXPORT_SYMBOL_GPL(shm_get_zmb_size);

unsigned long shm_get_vss_base(void)
{
	if (!exynos9810_shmem.ready)
		return 0;

	return exynos9810_shmem.base + exynos9810_shmem.cp_size;
}
EXPORT_SYMBOL_GPL(shm_get_vss_base);

unsigned int shm_get_vss_size(void)
{
	return exynos9810_shmem.ready ? exynos9810_shmem.vss_size : 0;
}
EXPORT_SYMBOL_GPL(shm_get_vss_size);

void __iomem *shm_get_vss_region(void)
{
	return exynos9810_shmem.ready ? exynos9810_shmem.vss_region : NULL;
}
EXPORT_SYMBOL_GPL(shm_get_vss_region);

unsigned long shm_get_vparam_base(void)
{
	struct exynos9810_shmem *shmem = &exynos9810_shmem;

	if (!shmem->ready)
		return 0;

	return shmem->base + shmem->zmb_offset + shmem->zmb_size;
}
EXPORT_SYMBOL_GPL(shm_get_vparam_base);

unsigned int shm_get_vparam_size(void)
{
	return exynos9810_shmem.ready ? exynos9810_shmem.vparam_size : 0;
}
EXPORT_SYMBOL_GPL(shm_get_vparam_size);

void __iomem *shm_get_vparam_region(void)
{
	return exynos9810_shmem.ready ?
		exynos9810_shmem.vparam_region : NULL;
}
EXPORT_SYMBOL_GPL(shm_get_vparam_region);

static const struct of_device_id exynos9810_shmem_of_match[] = {
	{ .compatible = "samsung,exynos9810-shmem" },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos9810_shmem_of_match);

static struct platform_driver exynos9810_shmem_driver = {
	.probe = exynos9810_shmem_probe,
	.driver = {
		.name = "exynos9810-shmem",
		.of_match_table = exynos9810_shmem_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(exynos9810_shmem_driver);

MODULE_DESCRIPTION("Samsung Exynos9810 firmware shared memory");
MODULE_LICENSE("GPL");
