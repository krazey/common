/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SND_SOC_ABOX_ASOC_COMPAT_H
#define __SND_SOC_ABOX_ASOC_COMPAT_H

#include <linux/compat.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>

#include <sound/soc.h>

static inline int abox_component_read(struct snd_soc_component *component,
		unsigned int reg, unsigned int *value)
{
	*value = snd_soc_component_read(component, reg);
	return 0;
}

static inline int abox_pcm_ioctl(struct snd_soc_component *component,
		struct snd_pcm_substream *substream, unsigned int cmd, void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static inline int abox_iommu_map_legacy(struct iommu_domain *domain,
		unsigned long iova, phys_addr_t paddr, size_t size, int prot)
{
	if (!prot)
		prot = IOMMU_READ | IOMMU_WRITE;

	return iommu_map(domain, iova, paddr, size, prot, GFP_KERNEL);
}

static inline void abox_sysmmu_tlb_invalidate(struct iommu_domain *domain,
		unsigned long iova, size_t size)
{
	iommu_flush_iotlb_all(domain);
}

static inline int abox_dma_mmap_writecombine(struct device *dev,
		struct vm_area_struct *vma, void *cpu_addr, dma_addr_t dma_addr,
		size_t size)
{
	return dma_mmap_attrs(dev, vma, cpu_addr, dma_addr, size,
			DMA_ATTR_WRITE_COMBINE);
}

static int abox_dapm_connected_ep(struct snd_soc_dapm_widget *widget,
		struct list_head *list, enum snd_soc_dapm_direction dir)
{
	enum snd_soc_dapm_direction reverse = dir ^ 1;
	struct snd_soc_dapm_path *path;
	int connected = 0;

	if (widget->endpoints[dir] >= 0)
		return widget->endpoints[dir];

	if (list && list_empty(&widget->work_list))
		list_add_tail(&widget->work_list, list);

	if ((widget->is_ep & SND_SOC_DAPM_DIR_TO_EP(dir)) &&
			widget->connected) {
		widget->endpoints[dir] = 1;
		return 1;
	}

	snd_soc_dapm_widget_for_each_path(widget, reverse, path) {
		if (path->is_supply)
			continue;
		if (path->walking)
			return 1;
		if (!path->connect)
			continue;

		path->walking = 1;
		connected += abox_dapm_connected_ep(path->node[dir], list,
				dir);
		path->walking = 0;
	}

	widget->endpoints[dir] = connected;

	return connected;
}

static inline int abox_dapm_connected_input_ep(
		struct snd_soc_dapm_widget *widget, struct list_head *list)
{
	return abox_dapm_connected_ep(widget, list, SND_SOC_DAPM_DIR_IN);
}

static inline int abox_dapm_connected_output_ep(
		struct snd_soc_dapm_widget *widget, struct list_head *list)
{
	return abox_dapm_connected_ep(widget, list, SND_SOC_DAPM_DIR_OUT);
}

#ifndef SND_SOC_DAPM_DEMUX_E
#define SND_SOC_DAPM_DEMUX_E(wname, wreg, wshift, winvert, wcontrols, \
		wevent, wflags) \
((struct snd_soc_dapm_widget) { \
	.id = snd_soc_dapm_demux, .name = wname, \
	SND_SOC_DAPM_INIT_REG_VAL(wreg, wshift, winvert), \
	.kcontrol_news = wcontrols, .num_kcontrols = 1, \
	.event = wevent, .event_flags = wflags })
#endif

#define SND_SOC_DAIFMT_CBM_CFM SND_SOC_DAIFMT_CBP_CFP
#define SND_SOC_DAIFMT_CBS_CFS SND_SOC_DAIFMT_CBC_CFC

#endif
