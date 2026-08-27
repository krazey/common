/* sound/soc/samsung/abox/abox_mmap_fd.c
 *
 * ALSA SoC Audio Layer - Samsung Abox mmap_fd driver
 *
 * Copyright (c) 2018 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
/* #define DEBUG */
#include <sound/samsung/abox.h>
#include <sound/sounddev_abox.h>

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/iosys-map.h>
#include <uapi/linux/dma-buf.h>

#include "abox.h"
#include "abox_mmapfd.h"

int abox_mmap_fd(struct abox_platform_data *data,
		struct snd_pcm_mmap_fd *mmap_fd)
{
	struct device *dev = &data->pdev->dev;
	int id = data->id;
	struct abox_ion_buf *buf = &data->ion_buf;
	int ret = 0;

	dev_dbg(dev, "%s id(%d)\n", __func__, id);

	if (buf->fd >= 0) {
		mmap_fd->dir = SNDRV_PCM_STREAM_PLAYBACK;
		mmap_fd->size = buf->size;
		mmap_fd->actual_size = buf->size;
		mmap_fd->fd = buf->fd;
	} else {
		get_dma_buf(buf->dma_buf);
		mmap_fd->fd = dma_buf_fd(buf->dma_buf, O_CLOEXEC);
		if (mmap_fd->fd >= 0) {
			mmap_fd->dir = SNDRV_PCM_STREAM_PLAYBACK;
			mmap_fd->size = buf->size;
			mmap_fd->actual_size = buf->size;
		} else {
			ret = -EFAULT;
			dev_err(dev, "%s dma_buf_fd is failed\n", __func__);
			dma_buf_put(buf->dma_buf);
			goto error_get_fd;
		}

		buf->fd = mmap_fd->fd;
		dev_dbg(dev, "%s fd(%d)\n", __func__, buf->fd);
		data->mmap_fd_state = true;
	}
	dev_info(dev, "%s id(%d) fd(%d)\n", __func__, id, buf->fd);

error_get_fd:
	return ret;
}

int abox_ion_alloc(struct abox_platform_data *data,
		struct abox_ion_buf *buf,
		unsigned long iova,
		size_t size,
		size_t align)
{
	struct device *dev = &data->pdev->dev;
	struct device *dev_abox = &data->abox_data->pdev->dev;
	struct dma_heap *heap;
	int ret;

	if (!buf)
		return -EINVAL;

	size = PAGE_ALIGN(size);
	heap = dma_heap_find("system-uncached");
	if (!heap)
		heap = dma_heap_find("system");
	if (!heap)
		return -ENODEV;

	buf->dma_buf = dma_heap_buffer_alloc(heap, size, O_CLOEXEC, 0);
	dma_heap_put(heap);
	if (IS_ERR(buf->dma_buf)) {
		ret = PTR_ERR(buf->dma_buf);
		buf->dma_buf = NULL;
		goto error_alloc;
	}

	buf->attachment = dma_buf_attach(buf->dma_buf, dev_abox);
	if (IS_ERR(buf->attachment)) {
		ret = PTR_ERR(buf->attachment);
		buf->attachment = NULL;
		goto error_attach;
	}

	buf->direction = DMA_BIDIRECTIONAL;
	buf->sgt = dma_buf_map_attachment(buf->attachment, buf->direction);
	if (IS_ERR(buf->sgt)) {
		ret = PTR_ERR(buf->sgt);
		buf->sgt = NULL;
		goto error_map_dmabuf;
	}

	ret = dma_buf_vmap_unlocked(buf->dma_buf, &buf->map);
	if (ret) {
		buf->kva = NULL;
		goto error_dma_buf_vmap;
	}
	buf->kva = buf->map.vaddr;

	buf->size = size;
	buf->iova = iova;
	buf->fd = -EINVAL;

	ret = abox_iommu_map_sg(dev_abox, buf->iova, buf->sgt->sgl,
			buf->sgt->orig_nents, buf->direction,
			buf->size,
			buf->kva);

	if (ret < 0) {
		dev_err(dev, "Failed to iommu_map: %d\n", ret);
		goto error_iommu_map_sg;
	}
	buf->iommu_mapped = true;

	dev_info(dev, "%s buf(0x%zx, 0x%pad, %p)\n", __func__,
			buf->size, &buf->iova, buf->kva);

	return 0;

error_iommu_map_sg:
	dma_buf_vunmap_unlocked(buf->dma_buf, &buf->map);
error_dma_buf_vmap:
	dma_buf_unmap_attachment(buf->attachment, buf->sgt, buf->direction);
error_map_dmabuf:
	dma_buf_detach(buf->dma_buf, buf->attachment);
error_attach:
	dma_buf_put(buf->dma_buf);
error_alloc:
	dev_err(dev, "%s: buffer allocation failed: %d\n", __func__, ret);
	return ret;
}

int abox_ion_free(struct abox_platform_data *data)
{
	struct device *dev = &data->pdev->dev;
	struct abox_ion_buf *buf = &data->ion_buf;
	int ret = 0;

	if (!buf->dma_buf)
		return 0;

	if (buf->iommu_mapped) {
		ret = abox_iommu_unmap(&data->abox_data->pdev->dev,
				buf->iova, 0, buf->size);
		if (ret < 0)
			dev_err(dev, "Failed to iommu_unmap: %d\n", ret);
		buf->iommu_mapped = false;
	}

	if (buf->kva)
		dma_buf_vunmap_unlocked(buf->dma_buf, &buf->map);
	dma_buf_unmap_attachment(buf->attachment, buf->sgt,
				buf->direction);
	dma_buf_detach(buf->dma_buf, buf->attachment);
	dma_buf_put(buf->dma_buf);
	buf->dma_buf = NULL;
	buf->attachment = NULL;
	buf->sgt = NULL;
	buf->kva = NULL;

	return ret;
}
