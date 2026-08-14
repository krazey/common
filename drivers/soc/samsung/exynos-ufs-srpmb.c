// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Exynos secure UFS RPMB bridge
 *
 * Copyright (C) 2016 Samsung Electronics Co., Ltd.
 * Copyright (C) 2026 Mathias Gluszczynski
 */

#include <linux/arm-smccc.h>
#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/rpmb.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>

#define EXYNOS_SRPMB_SMC_WSM		0x82003811
#define EXYNOS_SRPMB_FRAME_SIZE		512
#define EXYNOS_SRPMB_MAX_FRAMES		64
#define EXYNOS_SRPMB_DATA_SIZE		(EXYNOS_SRPMB_FRAME_SIZE * \
					 EXYNOS_SRPMB_MAX_FRAMES)

/* Commands returned in the firmware request header. */
#define EXYNOS_SRPMB_SECURITY_PROTOCOL_IN	7

enum exynos_srpmb_request_type {
	EXYNOS_SRPMB_GET_WRITE_COUNTER = 1,
	EXYNOS_SRPMB_WRITE_DATA,
	EXYNOS_SRPMB_READ_DATA,
};

enum exynos_srpmb_status {
	EXYNOS_SRPMB_COUNTER_LEN_ERROR = 0x601,
	EXYNOS_SRPMB_COUNTER_IO_ERROR = 0x602,
	EXYNOS_SRPMB_WRITE_LEN_ERROR = 0x604,
	EXYNOS_SRPMB_WRITE_IO_ERROR = 0x605,
	EXYNOS_SRPMB_READ_LEN_ERROR = 0x608,
	EXYNOS_SRPMB_READ_IO_ERROR = 0x609,
	EXYNOS_SRPMB_INVALID_COMMAND = 0x60b,
	EXYNOS_SRPMB_SUSPENDED = 0x60c,
	EXYNOS_SRPMB_IN_PROGRESS = 0xdcdc,
	EXYNOS_SRPMB_PASSED = 0xbaba,
};

/* Shared-memory ABI consumed by the Exynos secure firmware. */
struct exynos_srpmb_request {
	u32 command;
	u32 status;
	u32 type;
	u32 data_len;
	u32 in_len;
	u32 out_len;
	u8 data[];
};

struct exynos_srpmb {
	struct device *dev;
	struct rpmb_dev *rdev;
	struct exynos_srpmb_request *request;
	dma_addr_t request_dma;
	u8 *request_buf;
	u8 *response_buf;
	struct workqueue_struct *workqueue;
	struct work_struct work;
	struct work_struct registration_work;
#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	struct delayed_work diagnostics_work;
#endif
	struct wakeup_source *wakeup_source;
	struct notifier_block pm_notifier;
	s32 smc_ret;
	bool smc_attempted;
#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	atomic_t request_count;
#endif
	int irq;
	irq_hw_number_t hwirq;
};

static DEFINE_MUTEX(exynos_srpmb_interface_lock);
static struct exynos_srpmb *exynos_srpmb_interface_owner;

static void exynos_srpmb_set_status(struct exynos_srpmb *srpmb, u32 status)
{
	/* Publish all response data before secure firmware sees completion. */
	dma_wmb();
	WRITE_ONCE(srpmb->request->status, status);
}

static bool exynos_srpmb_valid_data_len(u32 data_len)
{
	return data_len >= EXYNOS_SRPMB_FRAME_SIZE &&
	       data_len <= EXYNOS_SRPMB_DATA_SIZE &&
	       !(data_len % EXYNOS_SRPMB_FRAME_SIZE);
}

static void exynos_srpmb_work(struct work_struct *work)
{
	struct exynos_srpmb *srpmb =
		container_of(work, struct exynos_srpmb, work);
	struct exynos_srpmb_request *request = srpmb->request;
	struct rpmb_frame *response;
	u32 data_len, error_status, response_len, type;
	u32 request_len;
	u16 result;
	int ret;

	/* Pair with the secure firmware write which raised the interrupt. */
	dma_rmb();
	type = READ_ONCE(request->type);
	data_len = READ_ONCE(request->data_len);
#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	if (atomic_inc_return(&srpmb->request_count) == 1)
		dev_info(srpmb->dev,
			 "E981D: secure RPMB first request type=%u len=%u\n",
			 type, data_len);
#endif

	switch (type) {
	case EXYNOS_SRPMB_GET_WRITE_COUNTER:
		if (data_len != EXYNOS_SRPMB_FRAME_SIZE) {
			exynos_srpmb_set_status(srpmb,
						EXYNOS_SRPMB_COUNTER_LEN_ERROR);
			goto out_relax;
		}
		request_len = EXYNOS_SRPMB_FRAME_SIZE;
		response_len = EXYNOS_SRPMB_FRAME_SIZE;
		error_status = EXYNOS_SRPMB_COUNTER_IO_ERROR;
		break;
	case EXYNOS_SRPMB_WRITE_DATA:
		if (!exynos_srpmb_valid_data_len(data_len)) {
			exynos_srpmb_set_status(srpmb,
						EXYNOS_SRPMB_WRITE_LEN_ERROR);
			goto out_relax;
		}
		request_len = data_len;
		response_len = EXYNOS_SRPMB_FRAME_SIZE;
		error_status = EXYNOS_SRPMB_WRITE_IO_ERROR;
		break;
	case EXYNOS_SRPMB_READ_DATA:
		if (!exynos_srpmb_valid_data_len(data_len)) {
			exynos_srpmb_set_status(srpmb,
						EXYNOS_SRPMB_READ_LEN_ERROR);
			goto out_relax;
		}
		request_len = EXYNOS_SRPMB_FRAME_SIZE;
		response_len = data_len;
		error_status = EXYNOS_SRPMB_READ_IO_ERROR;
		break;
	default:
		dev_err(srpmb->dev, "invalid secure RPMB request %u\n", type);
		exynos_srpmb_set_status(srpmb, EXYNOS_SRPMB_INVALID_COMMAND);
		goto out_relax;
	}

	/*
	 * Preserve the request before clearing the firmware buffer. The UFS
	 * RPMB core places its response in that buffer after sending the copy.
	 */
	memcpy(srpmb->request_buf, request->data, request_len);
	memset(request->data, 0, data_len);
	memset(srpmb->response_buf, 0, response_len);
	WRITE_ONCE(request->command, EXYNOS_SRPMB_SECURITY_PROTOCOL_IN);
	WRITE_ONCE(request->in_len, response_len);
	WRITE_ONCE(request->out_len, EXYNOS_SRPMB_FRAME_SIZE);

	ret = rpmb_route_frames(srpmb->rdev, srpmb->request_buf,
				request_len, srpmb->response_buf, response_len);
	if (ret) {
		dev_err(srpmb->dev,
			"secure RPMB request %u failed: %d\n", type, ret);
		exynos_srpmb_set_status(srpmb, error_status);
		goto out_relax;
	}

	response = (struct rpmb_frame *)srpmb->response_buf;
	memcpy(request->data, srpmb->response_buf, response_len);

	if (type == EXYNOS_SRPMB_WRITE_DATA) {
		result = be16_to_cpu(response->result);
		if (result) {
			dev_err(srpmb->dev,
				"secure RPMB write result: %#x\n", result);
			exynos_srpmb_set_status(srpmb, result);
			goto out_relax;
		}
	}

	exynos_srpmb_set_status(srpmb, EXYNOS_SRPMB_PASSED);

out_relax:
	__pm_relax(srpmb->wakeup_source);
}

static irqreturn_t exynos_srpmb_irq(int irq, void *data)
{
	struct exynos_srpmb *srpmb = data;

	__pm_stay_awake(srpmb->wakeup_source);
	exynos_srpmb_set_status(srpmb, EXYNOS_SRPMB_IN_PROGRESS);
	if (unlikely(!queue_work(srpmb->workqueue, &srpmb->work))) {
		dev_err_ratelimited(srpmb->dev,
				    "overlapping secure RPMB request\n");
		exynos_srpmb_set_status(srpmb, EXYNOS_SRPMB_INVALID_COMMAND);
		__pm_relax(srpmb->wakeup_source);
	}

	return IRQ_HANDLED;
}

static int exynos_srpmb_pm_notify(struct notifier_block *notifier,
				  unsigned long action, void *unused)
{
	struct exynos_srpmb *srpmb =
		container_of(notifier, struct exynos_srpmb, pm_notifier);

	switch (action) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
	case PM_RESTORE_PREPARE:
		flush_workqueue(srpmb->workqueue);
		exynos_srpmb_set_status(srpmb, EXYNOS_SRPMB_SUSPENDED);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
	case PM_POST_RESTORE:
		exynos_srpmb_set_status(srpmb, 0);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int exynos_srpmb_add_rdev(struct device *dev)
{
	struct rpmb_dev *rdev = to_rpmb_dev(dev);
	struct exynos_srpmb *srpmb;

	if (rdev->descr.type != RPMB_TYPE_UFS)
		return 0;

	mutex_lock(&exynos_srpmb_interface_lock);
	srpmb = exynos_srpmb_interface_owner;
	if (!srpmb || srpmb->rdev)
		goto out_unlock;

	srpmb->rdev = rpmb_dev_get(rdev);
	dev_info(srpmb->dev,
		 "E981D: UFS RPMB endpoint id=%d capacity=%u write=%u\n",
		 rdev->id, rdev->descr.capacity,
		 rdev->descr.reliable_wr_count);
	schedule_work(&srpmb->registration_work);

out_unlock:
	mutex_unlock(&exynos_srpmb_interface_lock);
	return 0;
}

static struct class_interface exynos_srpmb_interface = {
	.add_dev = exynos_srpmb_add_rdev,
};

static void exynos_srpmb_unregister_interface(void *data)
{
	struct exynos_srpmb *srpmb = data;

	mutex_lock(&exynos_srpmb_interface_lock);
	if (exynos_srpmb_interface_owner == srpmb)
		exynos_srpmb_interface_owner = NULL;
	mutex_unlock(&exynos_srpmb_interface_lock);

	rpmb_interface_unregister(&exynos_srpmb_interface);
	cancel_work_sync(&srpmb->registration_work);
	rpmb_dev_put(srpmb->rdev);
	srpmb->rdev = NULL;
}

static void exynos_srpmb_destroy_workqueue(void *data)
{
	destroy_workqueue(data);
}

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
static void exynos_srpmb_diagnostics_work(struct work_struct *work)
{
	struct exynos_srpmb *srpmb =
		container_of(to_delayed_work(work), struct exynos_srpmb,
			     diagnostics_work);

	dma_rmb();
	dev_info(srpmb->dev,
		 "E981D: srpmb dma=%pad ep=%u smc=%u/%#x req=%d st=%#x ty=%u\n",
		 &srpmb->request_dma, !!READ_ONCE(srpmb->rdev),
		 READ_ONCE(srpmb->smc_attempted),
		 READ_ONCE(srpmb->smc_ret),
		 atomic_read(&srpmb->request_count),
		 READ_ONCE(srpmb->request->status),
		 READ_ONCE(srpmb->request->type));
}

static void exynos_srpmb_cancel_diagnostics(void *data)
{
	struct exynos_srpmb *srpmb = data;

	cancel_delayed_work_sync(&srpmb->diagnostics_work);
}
#endif

static void exynos_srpmb_unregister_wakeup(void *data)
{
	wakeup_source_unregister(data);
}

static void exynos_srpmb_unregister_pm(void *data)
{
	struct exynos_srpmb *srpmb = data;

	unregister_pm_notifier(&srpmb->pm_notifier);
}

static void exynos_srpmb_registration_work(struct work_struct *work)
{
	struct exynos_srpmb *srpmb =
		container_of(work, struct exynos_srpmb, registration_work);
	struct arm_smccc_res res;
	s32 smc_ret;

	dma_wmb();
	WRITE_ONCE(srpmb->smc_attempted, true);
	arm_smccc_smc(EXYNOS_SRPMB_SMC_WSM, srpmb->request_dma,
		      srpmb->hwirq, 0, 0, 0, 0, 0, &res);
	smc_ret = (s32)res.a0;
	WRITE_ONCE(srpmb->smc_ret, smc_ret);
	if (smc_ret) {
		dev_err(srpmb->dev,
			"E981D: secure RPMB registration failed: %#x\n",
			smc_ret);
		return;
	}

	dev_info(srpmb->dev,
		 "E981D: secure RPMB buffer=%pad hwirq=%lu\n",
		 &srpmb->request_dma, srpmb->hwirq);
}

static int exynos_srpmb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct irq_data *irq_data;
	struct exynos_srpmb *srpmb;
	irq_hw_number_t hwirq;
	size_t request_size;
	int ret;

	srpmb = devm_kzalloc(dev, sizeof(*srpmb), GFP_KERNEL);
	if (!srpmb)
		return -ENOMEM;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(dev, ret, "cannot set 32-bit DMA mask\n");

	request_size = sizeof(*srpmb->request) + EXYNOS_SRPMB_DATA_SIZE;
	srpmb->request = dmam_alloc_coherent(dev, request_size,
					     &srpmb->request_dma, GFP_KERNEL);
	if (!srpmb->request)
		return -ENOMEM;
	if (upper_32_bits(srpmb->request_dma))
		return dev_err_probe(dev, -ERANGE,
				     "secure buffer is above 4 GiB\n");

	srpmb->request_buf = devm_kmalloc(dev, EXYNOS_SRPMB_DATA_SIZE,
					  GFP_KERNEL);
	if (!srpmb->request_buf)
		return -ENOMEM;
	srpmb->response_buf = devm_kmalloc(dev, EXYNOS_SRPMB_DATA_SIZE,
					   GFP_KERNEL);
	if (!srpmb->response_buf)
		return -ENOMEM;

	srpmb->irq = platform_get_irq(pdev, 0);
	if (srpmb->irq < 0)
		return srpmb->irq;

	irq_data = irq_get_irq_data(srpmb->irq);
	if (!irq_data)
		return dev_err_probe(dev, -EINVAL,
				     "cannot resolve secure RPMB interrupt\n");
	hwirq = irqd_to_hwirq(irq_data);

	srpmb->dev = dev;
	srpmb->hwirq = hwirq;
	INIT_WORK(&srpmb->work, exynos_srpmb_work);
	INIT_WORK(&srpmb->registration_work,
		  exynos_srpmb_registration_work);
#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	INIT_DELAYED_WORK(&srpmb->diagnostics_work,
			  exynos_srpmb_diagnostics_work);
#endif
	srpmb->workqueue = alloc_ordered_workqueue("exynos-srpmb",
						   WQ_MEM_RECLAIM | WQ_HIGHPRI);
	if (!srpmb->workqueue)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, exynos_srpmb_destroy_workqueue,
				       srpmb->workqueue);
	if (ret)
		return ret;

	srpmb->wakeup_source = wakeup_source_register(dev, "exynos-srpmb");
	if (!srpmb->wakeup_source)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, exynos_srpmb_unregister_wakeup,
				       srpmb->wakeup_source);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, srpmb->irq, exynos_srpmb_irq,
			       IRQF_TRIGGER_RISING, dev_name(dev), srpmb);
	if (ret)
		return dev_err_probe(dev, ret,
				     "cannot request secure RPMB interrupt\n");

	srpmb->pm_notifier.notifier_call = exynos_srpmb_pm_notify;
	ret = register_pm_notifier(&srpmb->pm_notifier);
	if (ret)
		return dev_err_probe(dev, ret,
				     "cannot register power notifier\n");

	ret = devm_add_action_or_reset(dev, exynos_srpmb_unregister_pm,
				       srpmb);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, srpmb);

	mutex_lock(&exynos_srpmb_interface_lock);
	if (exynos_srpmb_interface_owner) {
		mutex_unlock(&exynos_srpmb_interface_lock);
		return dev_err_probe(dev, -EBUSY,
				     "secure RPMB bridge already registered\n");
	}
	exynos_srpmb_interface_owner = srpmb;
	mutex_unlock(&exynos_srpmb_interface_lock);

	ret = rpmb_interface_register(&exynos_srpmb_interface);
	if (ret) {
		mutex_lock(&exynos_srpmb_interface_lock);
		if (exynos_srpmb_interface_owner == srpmb)
			exynos_srpmb_interface_owner = NULL;
		mutex_unlock(&exynos_srpmb_interface_lock);
		return dev_err_probe(dev, ret,
				     "cannot watch for UFS RPMB device\n");
	}

	ret = devm_add_action_or_reset(dev,
				       exynos_srpmb_unregister_interface,
				       srpmb);
	if (ret)
		return ret;

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	ret = devm_add_action_or_reset(dev, exynos_srpmb_cancel_diagnostics,
				       srpmb);
	if (ret)
		return ret;
	schedule_delayed_work(&srpmb->diagnostics_work, 10 * HZ);
#endif

	return 0;
}

static const struct of_device_id exynos_srpmb_of_match[] = {
	{ .compatible = "samsung,ufs-srpmb" },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_srpmb_of_match);

static struct platform_driver exynos_srpmb_driver = {
	.probe = exynos_srpmb_probe,
	.driver = {
		.name = "exynos-ufs-srpmb",
		.of_match_table = exynos_srpmb_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(exynos_srpmb_driver);

MODULE_AUTHOR("Mathias Gluszczynski <admin@krazey.de>");
MODULE_DESCRIPTION("Samsung Exynos secure UFS RPMB bridge");
MODULE_LICENSE("GPL");
