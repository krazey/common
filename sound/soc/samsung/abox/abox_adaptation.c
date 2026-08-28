// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung ABOX adaptation transport
 *
 * Copyright (c) 2016 Samsung Electronics Co. Ltd.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <sound/maxim_dsm.h>
#include <sound/samsung/abox.h>
#include <sound/sec_adaptation.h>

#define ABOX_ADAPTATION_TIMEOUT_MS	130

enum abox_adaptation_operation {
	ABOX_ADAPTATION_IDLE,
	ABOX_ADAPTATION_READ,
	ABOX_ADAPTATION_WRITE,
};

struct abox_adaptation_data {
	struct device *dev;
	struct platform_device *abox;
	/* Serialize each request with its matching firmware response. */
	struct mutex transaction_lock;
	struct completion transaction_done;
	enum abox_adaptation_operation operation;
	struct maxim_dsm *read_dsm;
	unsigned int offset;
	unsigned int size;
};

static struct abox_adaptation_data *abox_adaptation;

static int abox_adaptation_wait(struct abox_adaptation_data *data)
{
	struct completion *done = &data->transaction_done;
	unsigned long expires;
	long timeout;

	expires = msecs_to_jiffies(ABOX_ADAPTATION_TIMEOUT_MS);
	timeout = wait_for_completion_interruptible_timeout(done, expires);
	if (timeout < 0)
		return timeout;
	if (!timeout)
		return -ETIMEDOUT;

	return 0;
}

int maxim_dsm_read(int offset, int size, void *dsm_data)
{
	struct abox_adaptation_data *data = READ_ONCE(abox_adaptation);
	struct maxim_dsm *dsm = dsm_data;
	ABOX_IPC_MSG msg = { };
	struct IPC_ERAP_MSG *erap = &msg.msg.erap;
	int ret;

	if (!data)
		return -EPROBE_DEFER;
	if (!dsm || !dsm->param || offset < 0 || size <= 0)
		return -EINVAL;

	mutex_lock(&data->transaction_lock);
	reinit_completion(&data->transaction_done);
	data->read_dsm = dsm;
	data->offset = offset >= PARAM_DSM_5_0_MAX ?
			offset % PARAM_DSM_5_0_MAX : offset;
	data->size = size;
	data->operation = ABOX_ADAPTATION_READ;

	msg.ipcid = IPC_ERAP;
	erap->msgtype = REALTIME_EXTRA;
	erap->param.raw.params[0] = 0;
	erap->param.raw.params[1] = offset;
	erap->param.raw.params[2] = size;

	ret = abox_request_ipc(&data->abox->dev, IPC_ERAP, &msg,
			       sizeof(msg), 0, 0);
	if (!ret)
		ret = abox_adaptation_wait(data);
	if (ret)
		dev_err(data->dev, "DSM read offset %d size %d failed: %d\n",
			offset, size, ret);

	data->operation = ABOX_ADAPTATION_IDLE;
	data->read_dsm = NULL;
	mutex_unlock(&data->transaction_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(maxim_dsm_read);

int maxim_dsm_write(const u32 *dsm_data, int offset, int size)
{
	struct abox_adaptation_data *data = READ_ONCE(abox_adaptation);
	ABOX_IPC_MSG msg = { };
	struct IPC_ERAP_MSG *erap = &msg.msg.erap;
	size_t words;
	int ret;

	if (!data)
		return -EPROBE_DEFER;
	if (!dsm_data || offset < 0 || size <= 0)
		return -EINVAL;

	words = min_t(size_t, size, ARRAY_SIZE(erap->param.raw.params) - 3);

	mutex_lock(&data->transaction_lock);
	reinit_completion(&data->transaction_done);
	data->operation = ABOX_ADAPTATION_WRITE;

	msg.ipcid = IPC_ERAP;
	erap->msgtype = REALTIME_EXTRA;
	erap->param.raw.params[0] = 1;
	erap->param.raw.params[1] = offset;
	erap->param.raw.params[2] = size;
	memcpy(&erap->param.raw.params[3], dsm_data,
	       words * sizeof(erap->param.raw.params[0]));

	ret = abox_request_ipc(&data->abox->dev, IPC_ERAP, &msg,
			       sizeof(msg), 0, 0);
	if (!ret)
		ret = abox_adaptation_wait(data);
	if (ret)
		dev_err(data->dev, "DSM write offset %d size %d failed: %d\n",
			offset, size, ret);

	data->operation = ABOX_ADAPTATION_IDLE;
	mutex_unlock(&data->transaction_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(maxim_dsm_write);

static void abox_adaptation_complete_read(struct abox_adaptation_data *data,
					  struct ERAP_RAW_PARAM *raw)
{
	struct maxim_dsm *dsm = READ_ONCE(data->read_dsm);
	unsigned int offset = READ_ONCE(data->offset);
	unsigned int count;

	if (!dsm || !dsm->param)
		return;

	if (offset) {
		if (offset >= dsm->param_size)
			return;
		count = min3(data->size, dsm->param_size - offset,
			     (unsigned int)ARRAY_SIZE(raw->params));
		memcpy(&dsm->param[offset], raw->params,
		       count * sizeof(raw->params[0]));
	} else {
		count = min3(raw->params[0], dsm->param_size,
			     (unsigned int)ARRAY_SIZE(raw->params));
		if (!count)
			return;
		memcpy(dsm->param, raw->params,
		       count * sizeof(raw->params[0]));
	}

	complete(&data->transaction_done);
}

static irqreturn_t abox_adaptation_irq_handler(int irq, void *dev_id,
					       ABOX_IPC_MSG *msg)
{
	struct abox_adaptation_data *data = dev_id;
	struct IPC_ERAP_MSG *erap = &msg->msg.erap;
	enum abox_adaptation_operation operation;

	if (irq != IPC_ERAP || erap->msgtype != REALTIME_EXTRA)
		return IRQ_NONE;

	operation = READ_ONCE(data->operation);
	if (operation == ABOX_ADAPTATION_WRITE &&
	    erap->param.raw.params[0] == PARAM_DSM_5_0_ABOX_WRITE_CB) {
		complete(&data->transaction_done);
	} else if (operation == ABOX_ADAPTATION_READ) {
		abox_adaptation_complete_read(data, &erap->param.raw);
	}

	return IRQ_HANDLED;
}

static int samsung_abox_adaptation_probe(struct platform_device *pdev)
{
	struct abox_adaptation_data *data;
	struct device_node *abox_np;
	int ret;

	if (READ_ONCE(abox_adaptation))
		return -EBUSY;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	abox_np = of_parse_phandle(pdev->dev.of_node, "abox", 0);
	if (!abox_np)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "missing ABOX phandle\n");

	data->abox = of_find_device_by_node(abox_np);
	of_node_put(abox_np);
	if (!data->abox)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "ABOX device is not ready\n");
	if (!platform_get_drvdata(data->abox)) {
		put_device(&data->abox->dev);
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "ABOX driver is not ready\n");
	}

	data->dev = &pdev->dev;
	mutex_init(&data->transaction_lock);
	init_completion(&data->transaction_done);
	platform_set_drvdata(pdev, data);

	ret = abox_register_irq_handler(&data->abox->dev, IPC_ERAP,
					abox_adaptation_irq_handler, data);
	if (ret) {
		put_device(&data->abox->dev);
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register DSM IPC handler\n");
	}

	WRITE_ONCE(abox_adaptation, data);
	dev_info(&pdev->dev, "MAX98512 DSM transport registered\n");

	return 0;
}

static const struct of_device_id samsung_abox_adaptation_match[] = {
	{ .compatible = "samsung,abox-adaptation" },
	{ }
};
MODULE_DEVICE_TABLE(of, samsung_abox_adaptation_match);

static struct platform_driver samsung_abox_adaptation_driver = {
	.probe = samsung_abox_adaptation_probe,
	.driver = {
		.name = "samsung-abox-adaptation",
		.of_match_table = samsung_abox_adaptation_match,
	},
};
module_platform_driver(samsung_abox_adaptation_driver);

MODULE_AUTHOR("SeokYoung Jang <quartz.jang@samsung.com>");
MODULE_DESCRIPTION("Samsung ABOX adaptation transport");
MODULE_LICENSE("GPL");
