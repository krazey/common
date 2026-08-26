/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include "acpm.h"
#include "acpm_ipc.h"
#include "fw_header/framework.h"

static struct acpm_ipc_info *acpm_ipc;
static struct workqueue_struct *debug_logging_wq;
static struct workqueue_struct *update_log_wq;
static struct acpm_debug_info *acpm_debug;
static bool is_acpm_stop_log;
static bool acpm_stop_log_req;
struct acpm_framework *acpm_initdata;
void __iomem *acpm_srambase;
struct regulator_ss_info regulator_ss[REGULATOR_SS_MAX];
char reg_map[0x100] = {0};
bool is_set_regmap;

void acpm_ipc_set_waiting_mode(bool mode)
{
	acpm_ipc->w_mode = mode;
}

void acpm_fw_log_level(unsigned int on)
{
	acpm_debug->debug_log_level = on;
}

void acpm_ramdump(void)
{
#ifdef CONFIG_EXYNOS_SNAPSHOT_ACPM
	if (acpm_debug->dump_size)
		memcpy(acpm_debug->dump_dram_base, acpm_debug->dump_base, acpm_debug->dump_size);
#endif
}

void timestamp_write(void)
{
	unsigned int tmp_index;
	if (spin_trylock(&acpm_debug->lock)) {
		tmp_index = __raw_readl(acpm_debug->time_index);

		tmp_index++;

		if (tmp_index == acpm_debug->num_timestamps)
			tmp_index = 0;

		acpm_debug->timestamps[tmp_index] = sched_clock();

		__raw_writel(tmp_index, acpm_debug->time_index);
		exynos_acpm_timer_clear();

		spin_unlock(&acpm_debug->lock);
	}
}

struct regulator_ss_info *get_regulator_ss(int n)
{
	if (n < REGULATOR_SS_MAX)
		return &regulator_ss[n];
	else
		return NULL;
}

static void set_reg_map(void)
{
	u32 idx;
	int i;

	for (i = 0; i < REGULATOR_SS_MAX; i++) {
		idx = regulator_ss[i].vsel_reg & 0xFF;
		if (idx == 0)
			continue;

		is_set_regmap = true;

		if (reg_map[idx] != 0)
			pr_err("duplicated set_reg_map [%d] reg_map %x\n", i, reg_map[idx]);

		reg_map[idx] = i;
	}
}

static unsigned int get_reg_id(unsigned int addr)
{
	int id;

	if (addr >> 8 != 0x1)
		return NO_SS_RANGE;

	id = reg_map[addr & 0xff];
	if (id != 0)
		return id;

	return NO_SS_RANGE;
}

static unsigned int get_reg_voltage(struct regulator_ss_info reg_info,
				    unsigned int selector)
{
	if (reg_info.name[0] == 'L')
		selector = selector & 0x3F;

	return reg_info.min_uV + (reg_info.uV_step * (selector - reg_info.linear_min_sel));
}

void acpm_log_print(void)
{
	unsigned int front;
	unsigned int rear;
	unsigned int id;
	unsigned int index;
	unsigned int count;
	unsigned char str[9] = {0,};
	unsigned int val;
	unsigned int log_header;
	unsigned long long time;
	unsigned int log_level;
	unsigned int reg_id;

	if (is_acpm_stop_log)
		return ;
	/* ACPM Log data dequeue & print */
	front = __raw_readl(acpm_debug->log_buff_front);
	rear = __raw_readl(acpm_debug->log_buff_rear);

	while (rear != front) {
		log_header = __raw_readl(acpm_debug->log_buff_base + acpm_debug->log_buff_size * rear);

		/* log header information
		 * id: [31:28]
		 * log level : [27]
		 * index: [26:22]
		 * apm systick count: [15:0]
		 */
		id = (log_header & (0xF << LOG_ID_SHIFT)) >> LOG_ID_SHIFT;
		log_level = (log_header & (0x1 << LOG_LEVEL)) >> LOG_LEVEL;
		index = (log_header & (0x1f << LOG_TIME_INDEX)) >> LOG_TIME_INDEX;
		count = log_header & 0xffff;

		/* string length: log_buff_size - header(4) - integer_data(4) */
		memcpy_align_4(str, acpm_debug->log_buff_base + (acpm_debug->log_buff_size * rear) + 4,
				acpm_debug->log_buff_size - 8);

		val = __raw_readl(acpm_debug->log_buff_base + acpm_debug->log_buff_size * rear +
				acpm_debug->log_buff_size - 4);

		time = acpm_debug->timestamps[index];

		/* peritimer period: (1 * 256) / 24.576MHz*/
		time += count * APM_PERITIMER_NS_PERIOD;

		/* addr : [19:8], val : [7:0]*/
		if (id == REGULATOR_INFO_ID) {
			if (is_set_regmap == false)
				set_reg_map();

			if (is_set_regmap == false)
				reg_id = NO_SET_REGMAP;
			else
				reg_id = get_reg_id(val >> 12);

			if (reg_id == NO_SS_RANGE)
				exynos_ss_regulator(time, "outSc", val >> 12, (val >> 4) & 0xFF, (val >> 4) & 0xFF, val & 0xF);
			else if (reg_id == NO_SET_REGMAP)
				exynos_ss_regulator(time, "noMap", val >> 12, (val >> 4) & 0xFF, (val >> 4) & 0xFF, val & 0xF);
			else
				exynos_ss_regulator(time, regulator_ss[reg_id].name, val >> 12,
						get_reg_voltage(regulator_ss[reg_id], (val >> 4) & 0xFF),
						(val >> 4) & 0xFF,
						val & 0xF);
		}
		exynos_ss_acpm(time, str, val);

		if (acpm_debug->debug_log_level == 1 || !log_level)
			pr_info("[ACPM_FW] : %llu id:%u, %s, %x\n", time, id, str, val);

		if (acpm_debug->log_buff_len == (rear + 1))
			rear = 0;
		else
			rear++;

		__raw_writel(rear, acpm_debug->log_buff_rear);
		front = __raw_readl(acpm_debug->log_buff_front);
	}

	if (acpm_stop_log_req) {
		is_acpm_stop_log = true;
		acpm_ramdump();
	}
}

void acpm_stop_log(void)
{
	acpm_stop_log_req = true;
}

static void acpm_update_log(struct work_struct *work)
{
	acpm_log_print();
}

static void acpm_debug_logging(struct work_struct *work)
{
	acpm_log_print();
	timestamp_write();

	queue_delayed_work(debug_logging_wq, &acpm_debug->periodic_work,
			msecs_to_jiffies(acpm_debug->period));
}

int acpm_ipc_set_ch_mode(struct device_node *np, bool polling)
{
	int reg;
	int i, len, req_ch_id;
	const __be32 *prop;

	if (!np)
		return -ENODEV;

	prop = of_get_property(np, "acpm-ipc-channel", &len);
	if (!prop)
		return -ENOENT;
	req_ch_id = be32_to_cpup(prop);

	for (i = 0; i < acpm_ipc->num_channels; i++) {
		if (acpm_ipc->channel[i].id == req_ch_id) {

			reg = __raw_readl(acpm_ipc->intr + INTMR1);
			reg &= ~(1 << acpm_ipc->channel[i].id);
			reg |= polling << acpm_ipc->channel[i].id;
			__raw_writel(reg, acpm_ipc->intr + INTMR1);

			acpm_ipc->channel[i].polling = polling;

			return 0;
		}
	}

	return -ENODEV;
}

unsigned int acpm_ipc_request_channel(struct device_node *np, ipc_callback handler,
		unsigned int *id, unsigned int *size)
{
	struct callback_info *cb;
	int i, len, req_ch_id;
	const __be32 *prop;

	if (!np)
		return -ENODEV;

	prop = of_get_property(np, "acpm-ipc-channel", &len);
	if (!prop)
		return -ENOENT;
	req_ch_id = be32_to_cpup(prop);

	for (i = 0; i < acpm_ipc->num_channels; i++) {
		if (acpm_ipc->channel[i].id == req_ch_id) {
			*id = acpm_ipc->channel[i].id;
			*size = acpm_ipc->channel[i].tx_ch.size;

			if (handler) {
				cb = devm_kzalloc(acpm_ipc->dev, sizeof(struct callback_info),
						GFP_KERNEL);
				if (cb == NULL)
					return -ENOMEM;
				cb->ipc_callback = handler;
				cb->client = np;

				spin_lock(&acpm_ipc->channel[i].ch_lock);
				list_add(&cb->list, &acpm_ipc->channel[i].list);
				spin_unlock(&acpm_ipc->channel[i].ch_lock);
			}

			return 0;
		}
	}

	return -ENODEV;
}

unsigned int acpm_ipc_release_channel(struct device_node *np, unsigned int channel_id)
{
	struct acpm_ipc_ch *channel = &acpm_ipc->channel[channel_id];
	struct list_head *cb_list = &channel->list;
	struct callback_info *cb;

	list_for_each_entry(cb, cb_list, list) {
		if (cb->client == np) {
			spin_lock(&channel->ch_lock);
			list_del(&cb->list);
			spin_unlock(&channel->ch_lock);
			devm_kfree(acpm_ipc->dev, cb);
			break;
		}
	}

	return 0;
}

static bool check_response(struct acpm_ipc_ch *channel, struct ipc_config *cfg)
{
	unsigned int front;
	unsigned int rear;
	struct list_head *cb_list = &channel->list;
	struct callback_info *cb;
	unsigned int data;
	bool ret = true;
	unsigned int i;

	spin_lock(&channel->rx_lock);
	/* IPC command dequeue */
	front = __raw_readl(channel->rx_ch.front);
	rear = __raw_readl(channel->rx_ch.rear);

	i = rear;

	while (i != front) {
		data = __raw_readl(channel->rx_ch.base + channel->rx_ch.size * i);
		data = (data >> ACPM_IPC_PROTOCOL_SEQ_NUM) & 0x3f;

		if (data == ((cfg->cmd[0] >> ACPM_IPC_PROTOCOL_SEQ_NUM) & 0x3f)) {
			memcpy_align_4(cfg->cmd, channel->rx_ch.base + channel->rx_ch.size * i,
					channel->rx_ch.size);
			memcpy_align_4(channel->cmd, channel->rx_ch.base + channel->rx_ch.size * i,
					channel->rx_ch.size);

			/* i: target command, rear: another command
			 * 1. i index command dequeue
			 * 2. rear index command copy to i index position
			 * 3. incresed rear index
			 */
			if (i != rear)
				memcpy_align_4(channel->rx_ch.base + channel->rx_ch.size * i,
						channel->rx_ch.base + channel->rx_ch.size * rear,
						channel->rx_ch.size);

			list_for_each_entry(cb, cb_list, list)
				if (cb && cb->ipc_callback)
					cb->ipc_callback(channel->cmd, channel->rx_ch.size);

			rear++;
			rear = rear % channel->rx_ch.len;

			__raw_writel(rear, channel->rx_ch.rear);
			front = __raw_readl(channel->rx_ch.front);

			if (rear == front) {
				__raw_writel((1 << channel->id), acpm_ipc->intr + INTCR1);
				if (rear != __raw_readl(channel->rx_ch.front)) {
					__raw_writel((1 << channel->id), acpm_ipc->intr + INTGR1);
				}
			}
			ret = false;
			break;
		}
		i++;
		i = i % channel->rx_ch.len;
	}

	spin_unlock(&channel->rx_lock);

	return ret;
}

static void dequeue_policy(struct acpm_ipc_ch *channel)
{
	unsigned int front;
	unsigned int rear;
	struct list_head *cb_list = &channel->list;
	struct callback_info *cb;

	spin_lock(&channel->rx_lock);

	if (channel->type == TYPE_BUFFER) {
		memcpy_align_4(channel->cmd, channel->rx_ch.base, channel->rx_ch.size);
		spin_unlock(&channel->rx_lock);
		list_for_each_entry(cb, cb_list, list)
			if (cb && cb->ipc_callback)
				cb->ipc_callback(channel->cmd, channel->rx_ch.size);

		return;
	}

	/* IPC command dequeue */
	front = __raw_readl(channel->rx_ch.front);
	rear = __raw_readl(channel->rx_ch.rear);

	while (rear != front) {
		memcpy_align_4(channel->cmd, channel->rx_ch.base + channel->rx_ch.size * rear,
				channel->rx_ch.size);

		list_for_each_entry(cb, cb_list, list)
			if (cb && cb->ipc_callback)
				cb->ipc_callback(channel->cmd, channel->rx_ch.size);

		if (channel->rx_ch.len == (rear + 1))
			rear = 0;
		else
			rear++;

		if (!channel->polling)
			complete(&channel->wait);

		__raw_writel(rear, channel->rx_ch.rear);
		front = __raw_readl(channel->rx_ch.front);
	}

	acpm_log_print();
	spin_unlock(&channel->rx_lock);
}

static irqreturn_t acpm_ipc_irq_handler(int irq, void *data)
{
	struct acpm_ipc_info *ipc = data;
	unsigned int status;
	int i;

	/* ACPM IPC INTERRUPT STATUS REGISTER */
	status = __raw_readl(acpm_ipc->intr + INTSR1);

	for (i = 0; i < acpm_ipc->num_channels; i++) {
		if (!ipc->channel[i].polling && (status & (0x1 << ipc->channel[i].id))) {
			/* ACPM IPC INTERRUPT PENDING CLEAR */
			__raw_writel(1 << ipc->channel[i].id, ipc->intr + INTCR1);
		}
	}

	ipc->intr_status = status;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t acpm_ipc_irq_handler_thread(int irq, void *data)
{
	struct acpm_ipc_info *ipc = data;
	int i;

	for (i = 0; i < acpm_ipc->num_channels; i++)
		if (!ipc->channel[i].polling && (ipc->intr_status & (1 << i)))
			dequeue_policy(&ipc->channel[i]);

	return IRQ_HANDLED;
}

static void apm_interrupt_gen(unsigned int id)
{
	/* APM NVIC INTERRUPT GENERATE */
	writel((1 << id) << 16, acpm_ipc->intr + INTGR0);
}

static int acpm_ipc_read_channel(unsigned int channel_id,
				 struct ipc_channel *ipc_channel)
{
	void __iomem *channels;
	int i;

	channels = acpm_ipc->sram_base + acpm_ipc->initdata->ipc_channels;
	for (i = 0; i < acpm_ipc->initdata->num_ipc_channels; i++) {
		memcpy_fromio(ipc_channel,
			      channels + i * sizeof(*ipc_channel),
			      sizeof(*ipc_channel));
		if (ipc_channel->id == channel_id)
			return 0;
	}

	return -ENOENT;
}

static int acpm_ipc_find_plugin(unsigned int id, struct plugin *plugin)
{
	void __iomem *plugins;
	int i;

	plugins = acpm_ipc->sram_base + acpm_ipc->initdata->plugins;
	for (i = 0; i < acpm_ipc->initdata->num_plugins; i++) {
		memcpy_fromio(plugin, plugins + i * sizeof(*plugin),
			      sizeof(*plugin));
		if (plugin->id == id)
			return 0;
	}

	return -ENOENT;
}

static void acpm_ipc_plugin_name(const struct plugin *plugin, char *name,
				 size_t size)
{
	if (!plugin->fw_name) {
		strscpy(name, "built-in", size);
		return;
	}

	memcpy_fromio(name, acpm_ipc->sram_base + plugin->fw_name, size - 1);
	name[size - 1] = '\0';
}

static bool acpm_ipc_plugin_is_dvfs(const char *name)
{
	return strnstr(name, "DVFS", 32) || strnstr(name, "dvfs", 32);
}

static int acpm_ipc_find_dvfs_plugin(struct plugin *plugin, char *name,
				     size_t size)
{
	void __iomem *plugins;
	int i;

	plugins = acpm_ipc->sram_base + acpm_ipc->initdata->plugins;
	for (i = 0; i < acpm_ipc->initdata->num_plugins; i++) {
		memcpy_fromio(plugin, plugins + i * sizeof(*plugin),
			      sizeof(*plugin));
		acpm_ipc_plugin_name(plugin, name, size);
		dev_info(acpm_ipc->dev,
			 "plugin[%d] id=%u %s attached=%u stay=%u base=%#x size=%u\n",
			 i, plugin->id, name, plugin->is_attached,
			 plugin->stay_attached, plugin->base_addr, plugin->size);
		if (acpm_ipc_plugin_is_dvfs(name))
			return 0;
	}

	return -ENOENT;
}

static int acpm_ipc_attach_plugin(unsigned int channel_id,
				  unsigned int plugin_id)
{
	struct ipc_config config = { };
	u32 command[4] = { };

	command[0] = BIT(ACPM_IPC_PROTOCOL_DP_A) |
		      plugin_id << ACPM_IPC_PROTOCOL_ID;
	config.cmd = command;
	config.response = true;

	return acpm_ipc_send_data_sync(channel_id, &config);
}

static void acpm_ipc_refresh_channel(const struct ipc_channel *ipc_channel)
{
	struct acpm_ipc_ch *channel = NULL;
	unsigned int reg;
	int i;

	for (i = 0; i < acpm_ipc->num_channels; i++) {
		if (acpm_ipc->channel[i].id == ipc_channel->id) {
			channel = &acpm_ipc->channel[i];
			break;
		}
	}

	if (!channel)
		return;

	if (channel->tx_ch.size != ipc_channel->ch.q_elem_size) {
		dev_warn(acpm_ipc->dev,
			 "channel %u message size changed from %u to %u\n",
			 channel->id, channel->tx_ch.size,
			 ipc_channel->ch.q_elem_size);
		return;
	}

	channel->polling = ipc_channel->ap_poll;
	channel->type = ipc_channel->type;
	channel->rx_ch.len = ipc_channel->ch.q_len;
	channel->tx_ch.len = ipc_channel->ch.q_len;
	channel->rx_ch.rear = acpm_ipc->sram_base + ipc_channel->ch.tx_rear;
	channel->rx_ch.front = acpm_ipc->sram_base + ipc_channel->ch.tx_front;
	channel->rx_ch.base = acpm_ipc->sram_base + ipc_channel->ch.tx_base;
	channel->tx_ch.rear = acpm_ipc->sram_base + ipc_channel->ch.rx_rear;
	channel->tx_ch.front = acpm_ipc->sram_base + ipc_channel->ch.rx_front;
	channel->tx_ch.base = acpm_ipc->sram_base + ipc_channel->ch.rx_base;
	channel->tx_ch.d_buff_size = ipc_channel->ch.rx_indr_buf_size;
	channel->tx_ch.direction = acpm_ipc->sram_base +
				   ipc_channel->ch.rx_indr_buf;

	reg = readl(acpm_ipc->intr + INTMR1);
	reg &= ~BIT(channel->id);
	reg |= channel->polling << channel->id;
	writel(reg, acpm_ipc->intr + INTMR1);
}

static void acpm_ipc_prepare_dvfs(struct device_node *node)
{
	unsigned int control_channel;
	unsigned int dvfs_channel;
	struct ipc_channel ipc_channel;
	struct plugin plugin;
	char name[32];
	int ret;

	ret = of_property_read_u32(node, "samsung,dvfs-channel",
				   &dvfs_channel);
	if (ret)
		return;

	ret = of_property_read_u32(node, "samsung,plugin-channel",
				   &control_channel);
	if (ret) {
		dev_warn(acpm_ipc->dev, "DVFS plugin channel is missing\n");
		return;
	}

	ret = acpm_ipc_read_channel(dvfs_channel, &ipc_channel);
	if (ret) {
		dev_warn(acpm_ipc->dev,
			 "DVFS channel %u is missing\n", dvfs_channel);
		return;
	}

	dev_info(acpm_ipc->dev,
		 "DVFS ch%u owner=%d type=%u poll=%u len=%u size=%u tx=%u/%u rx=%u/%u\n",
		 ipc_channel.id, ipc_channel.owner, ipc_channel.type,
		 ipc_channel.ap_poll, ipc_channel.ch.q_len,
		 ipc_channel.ch.q_elem_size,
		 readl(acpm_ipc->sram_base + ipc_channel.ch.rx_rear),
		 readl(acpm_ipc->sram_base + ipc_channel.ch.rx_front),
		 readl(acpm_ipc->sram_base + ipc_channel.ch.tx_rear),
		 readl(acpm_ipc->sram_base + ipc_channel.ch.tx_front));

	ret = acpm_ipc_find_plugin(ipc_channel.owner, &plugin);
	if (!ret) {
		acpm_ipc_plugin_name(&plugin, name, sizeof(name));
		if (!acpm_ipc_plugin_is_dvfs(name))
			ret = -ENOENT;
	}
	if (ret)
		ret = acpm_ipc_find_dvfs_plugin(&plugin, name, sizeof(name));
	if (ret) {
		dev_warn(acpm_ipc->dev, "named DVFS plugin is missing\n");
		return;
	}
	dev_info(acpm_ipc->dev,
		 "DVFS channel %u plugin %u (%s) attached=%u stay=%u\n",
		 dvfs_channel, plugin.id, name, plugin.is_attached,
		 plugin.stay_attached);

	if (!plugin.stay_attached) {
		dev_warn(acpm_ipc->dev,
			 "DVFS plugin %u is not requested by firmware\n",
			 plugin.id);
		return;
	}
	if (!plugin.is_attached) {
		if (!plugin.base_addr) {
			dev_warn(acpm_ipc->dev,
				 "DVFS plugin %u has no firmware image\n",
				 plugin.id);
			return;
		}

		ret = acpm_ipc_attach_plugin(control_channel, plugin.id);
		if (ret) {
			dev_warn(acpm_ipc->dev,
				 "failed to attach DVFS plugin %u: %d\n",
				 plugin.id, ret);
			return;
		}

		dev_info(acpm_ipc->dev, "DVFS plugin %u attached\n",
			 plugin.id);
	}

	ret = acpm_ipc_read_channel(dvfs_channel, &ipc_channel);
	if (ret)
		return;
	acpm_ipc_refresh_channel(&ipc_channel);
	dev_info(acpm_ipc->dev,
		 "DVFS channel %u ready owner=%d type=%u poll=%u len=%u size=%u\n",
		 ipc_channel.id, ipc_channel.owner, ipc_channel.type,
		 ipc_channel.ap_poll, ipc_channel.ch.q_len,
		 ipc_channel.ch.q_elem_size);
	if (ipc_channel.ch.q_len < 2)
		dev_warn(acpm_ipc->dev,
			 "DVFS channel %u has unusable queue length %u\n",
			 ipc_channel.id, ipc_channel.ch.q_len);
}

static int enqueue_indirection_cmd(struct acpm_ipc_ch *channel,
		struct ipc_config *cfg)
{
	unsigned int front;
	unsigned int rear;
	unsigned int buf;
	bool timeout_flag = 0;

	if (cfg->indirection) {
		front = __raw_readl(channel->tx_ch.front);
		rear = __raw_readl(channel->tx_ch.rear);

		/* another indirection command check */
		while (rear != front) {
			buf = __raw_readl(channel->tx_ch.base + channel->tx_ch.size * rear);

			if (buf & (1 << ACPM_IPC_PROTOCOL_INDIRECTION)) {

				UNTIL_EQUAL(true, rear != __raw_readl(channel->tx_ch.rear),
						timeout_flag);

				if (timeout_flag) {
					acpm_log_print();
					return -ETIMEDOUT;
				} else {
					rear = __raw_readl(channel->tx_ch.rear);
				}

			} else {
				if (channel->tx_ch.len == (rear + 1))
					rear = 0;
				else
					rear++;
			}
		}

		if (cfg->indirection_base)
			memcpy_align_4(channel->tx_ch.direction, cfg->indirection_base,
					cfg->indirection_size);
		else
			return -EINVAL;
	}

	return 0;
}

int acpm_ipc_send_data_sync(unsigned int channel_id, struct ipc_config *cfg)
{
	struct acpm_ipc_ch *channel;
	int ret;

	if (!acpm_ipc || !cfg || channel_id >= acpm_ipc->num_channels)
		return -EINVAL;

	channel = &acpm_ipc->channel[channel_id];
	mutex_lock(&channel->wait_lock);

	if (!channel->polling && cfg->response)
		reinit_completion(&channel->wait);

	ret = acpm_ipc_send_data(channel_id, cfg);
	if (!ret && !channel->polling && cfg->response) {
		ret = wait_for_completion_interruptible_timeout(&channel->wait,
								msecs_to_jiffies(50));
		if (!ret) {
			pr_err("%s: channel %u timeout\n", __func__,
			       channel_id);
			ret = -ETIMEDOUT;
		} else if (ret > 0) {
			ret = 0;
		}
	}

	mutex_unlock(&channel->wait_lock);

	return ret;
}

int acpm_ipc_send_data(unsigned int channel_id, struct ipc_config *cfg)
{
	unsigned int front;
	unsigned int rear;
	unsigned int tmp_index;
	struct acpm_ipc_ch *channel;
	bool timeout_flag = 0;
	int ret;
	u64 timeout, now;
	u32 retry_cnt = 0;
	u32 command[3] = { };

	if (!acpm_ipc || !cfg || !cfg->cmd ||
	    channel_id >= acpm_ipc->num_channels)
		return -EINVAL;

	channel = &acpm_ipc->channel[channel_id];

	spin_lock(&channel->tx_lock);

	front = __raw_readl(channel->tx_ch.front);
	rear = __raw_readl(channel->tx_ch.rear);

	tmp_index = front + 1;

	if (tmp_index >= channel->tx_ch.len)
		tmp_index = 0;

	timeout = sched_clock() + IPC_TIMEOUT;
	while (tmp_index == __raw_readl(channel->tx_ch.rear)) {
		if (sched_clock() > timeout) {
			timeout_flag = true;
			break;
		}
		cpu_relax();
	}

	if (timeout_flag) {
		rear = __raw_readl(channel->tx_ch.rear);
		if (!channel->stall_reported) {
			channel->stall_reported = true;
			memcpy(command, cfg->cmd,
			       min_t(size_t, sizeof(command),
				     channel->tx_ch.size));
			acpm_log_print();
			pr_err("ACPM channel %u stalled: %s tx=%u/%u len=%u cmd=%08x/%08x/%08x task=%s/%d\n",
			       channel->id, channel->polling ? "poll" : "irq",
			       rear, front, channel->tx_ch.len, command[0],
			       command[1], command[2], current->comm, current->pid);
		}
		spin_unlock(&channel->tx_lock);
		return -ETIMEDOUT;
	}
	channel->stall_reported = false;

	if (++channel->seq_num == 64)
		channel->seq_num = 1;

	cfg->cmd[0] |= (channel->seq_num & 0x3f) << ACPM_IPC_PROTOCOL_SEQ_NUM;

	memcpy_align_4(channel->tx_ch.base + channel->tx_ch.size * front, cfg->cmd,
			channel->tx_ch.size);

	cfg->cmd[1] = 0;
	cfg->cmd[2] = 0;
	cfg->cmd[3] = 0;

	ret = enqueue_indirection_cmd(channel, cfg);
	if (ret) {
		pr_err("[ACPM] indirection command fail %d\n", ret);
		spin_unlock(&channel->tx_lock);
		return ret;
	}

	writel(tmp_index, channel->tx_ch.front);

	apm_interrupt_gen(channel->id);
	spin_unlock(&channel->tx_lock);

	if (channel->polling && cfg->response) {
retry:
		timeout = sched_clock() + IPC_TIMEOUT;
		timeout_flag = false;

		while (!(__raw_readl(acpm_ipc->intr + INTSR1) & (1 << channel->id)) ||
				check_response(channel, cfg)) {
			now = sched_clock();
			if (timeout < now) {
				if (retry_cnt++ < 5) {
					pr_err("acpm_ipc timeout retry %d"
						"now = %llu,"
						"timeout = %llu\n",
						retry_cnt, now, timeout);
					goto retry;
				}
				timeout_flag = true;
				break;
			} else {
				if (acpm_ipc->w_mode)
					usleep_range(50, 100);
				else
					cpu_relax();
			}
		}

		if (timeout_flag) {
			if (!check_response(channel, cfg))
				return 0;
			pr_err("%s Timeout error! now = %llu, timeout = %llu\n",
					__func__, now, timeout);
			pr_err("[ACPM] int_status:0x%x, ch_id: 0x%x\n",
					__raw_readl(acpm_ipc->intr + INTSR1),
					1 << channel->id);
			pr_err("[ACPM] queue, rx_rear:%u, rx_front:%u\n",
					__raw_readl(channel->rx_ch.rear),
					__raw_readl(channel->rx_ch.front));
			pr_err("[ACPM] queue, tx_rear:%u, tx_front:%u\n",
					__raw_readl(channel->tx_ch.rear),
					__raw_readl(channel->tx_ch.front));

			acpm_debug->debug_log_level = 1;
			acpm_log_print();
			acpm_debug->debug_log_level = 0;
			acpm_ramdump();

			BUG_ON(timeout_flag);
			return -ETIMEDOUT;
		}

		queue_work(update_log_wq, &acpm_debug->update_log_work);
	}

	return 0;
}

static void log_buffer_init(struct device *dev, struct device_node *node)
{
	const __be32 *prop;
	unsigned int num_timestamps = 0;
	unsigned int len = 0;
	unsigned int dump_base = 0;
	unsigned int dump_size = 0;

	prop = of_get_property(node, "num-timestamps", &len);
	if (prop)
		num_timestamps = be32_to_cpup(prop);

	acpm_debug = devm_kzalloc(dev, sizeof(struct acpm_debug_info), GFP_KERNEL);
	if (IS_ERR(acpm_debug))
		return ;

	acpm_debug->time_index = acpm_ipc->sram_base + acpm_ipc->initdata->ktime_index;
	acpm_debug->num_timestamps = num_timestamps;
	acpm_debug->timestamps = devm_kzalloc(dev,
			sizeof(unsigned long long) * num_timestamps, GFP_KERNEL);
	acpm_debug->log_buff_rear = acpm_ipc->sram_base + acpm_ipc->initdata->log_buf_rear;
	acpm_debug->log_buff_front = acpm_ipc->sram_base + acpm_ipc->initdata->log_buf_front;
	acpm_debug->log_buff_base = acpm_ipc->sram_base + acpm_ipc->initdata->log_data;
	acpm_debug->log_buff_len = acpm_ipc->initdata->log_entry_len;
	acpm_debug->log_buff_size = acpm_ipc->initdata->log_entry_size;

	prop = of_get_property(node, "debug-log-level", &len);
	if (prop)
		acpm_debug->debug_log_level = be32_to_cpup(prop);

	prop = of_get_property(node, "dump-base", &len);
	if (prop)
		dump_base = be32_to_cpup(prop);

	prop = of_get_property(node, "dump-size", &len);
	if (prop)
		dump_size = be32_to_cpup(prop);

	if (dump_base && dump_size) {
		acpm_debug->dump_base = ioremap(dump_base, dump_size);
		acpm_debug->dump_size = dump_size;
	}

	prop = of_get_property(node, "logging-period", &len);
	if (prop)
		acpm_debug->period = be32_to_cpup(prop);

#ifdef CONFIG_EXYNOS_SNAPSHOT_ACPM
	acpm_debug->dump_dram_base = kzalloc(acpm_debug->dump_size, GFP_KERNEL);
	exynos_ss_printk("[ACPM] acpm framework SRAM dump to dram base: 0x%x\n",
			virt_to_phys(acpm_debug->dump_dram_base));
#endif
	pr_info("[ACPM] acpm framework SRAM dump to dram base: 0x%llx\n",
			virt_to_phys(acpm_debug->dump_dram_base));

	spin_lock_init(&acpm_debug->lock);
}

static int channel_init(void)
{
	int i;
	unsigned int mask = 0;
	struct ipc_channel *ipc_ch;

	acpm_ipc->num_channels = acpm_ipc->initdata->ipc_ap_max;

	acpm_ipc->channel = devm_kzalloc(acpm_ipc->dev,
			sizeof(struct acpm_ipc_ch) * acpm_ipc->num_channels, GFP_KERNEL);

	for (i = 0; i < acpm_ipc->num_channels; i++) {
		ipc_ch = (struct ipc_channel *)(acpm_ipc->sram_base + acpm_ipc->initdata->ipc_channels);
		acpm_ipc->channel[i].polling = ipc_ch[i].ap_poll;
		acpm_ipc->channel[i].id = ipc_ch[i].id;
		acpm_ipc->channel[i].type = ipc_ch[i].type;
		mask |= acpm_ipc->channel[i].polling << acpm_ipc->channel[i].id;

		/* Channel's RX buffer info */
		acpm_ipc->channel[i].rx_ch.size = ipc_ch[i].ch.q_elem_size;
		acpm_ipc->channel[i].rx_ch.len = ipc_ch[i].ch.q_len;
		acpm_ipc->channel[i].rx_ch.rear = acpm_ipc->sram_base + ipc_ch[i].ch.tx_rear;
		acpm_ipc->channel[i].rx_ch.front = acpm_ipc->sram_base + ipc_ch[i].ch.tx_front;
		acpm_ipc->channel[i].rx_ch.base = acpm_ipc->sram_base + ipc_ch[i].ch.tx_base;
		/* Channel's TX buffer info */
		acpm_ipc->channel[i].tx_ch.size = ipc_ch[i].ch.q_elem_size;
		acpm_ipc->channel[i].tx_ch.len = ipc_ch[i].ch.q_len;
		acpm_ipc->channel[i].tx_ch.rear = acpm_ipc->sram_base + ipc_ch[i].ch.rx_rear;
		acpm_ipc->channel[i].tx_ch.front = acpm_ipc->sram_base + ipc_ch[i].ch.rx_front;
		acpm_ipc->channel[i].tx_ch.base = acpm_ipc->sram_base + ipc_ch[i].ch.rx_base;
		acpm_ipc->channel[i].tx_ch.d_buff_size = ipc_ch[i].ch.rx_indr_buf_size;
		acpm_ipc->channel[i].tx_ch.direction = acpm_ipc->sram_base + ipc_ch[i].ch.rx_indr_buf;

		acpm_ipc->channel[i].cmd = devm_kzalloc(acpm_ipc->dev,
				acpm_ipc->channel[i].tx_ch.size, GFP_KERNEL);

		init_completion(&acpm_ipc->channel[i].wait);
		mutex_init(&acpm_ipc->channel[i].wait_lock);
		spin_lock_init(&acpm_ipc->channel[i].rx_lock);
		spin_lock_init(&acpm_ipc->channel[i].tx_lock);
		spin_lock_init(&acpm_ipc->channel[i].ch_lock);
		INIT_LIST_HEAD(&acpm_ipc->channel[i].list);
	}

	__raw_writel(mask, acpm_ipc->intr + INTMR1);

	return 0;
}

static int acpm_ipc_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct resource *res;
	int ret = 0, len;
	const __be32 *prop;

	if (!node) {
		dev_err(&pdev->dev, "driver doesnt support"
				"non-dt devices\n");
		return -ENODEV;
	}

	dev_info(&pdev->dev, "acpm_ipc probe\n");

	acpm_ipc = devm_kzalloc(&pdev->dev,
			sizeof(struct acpm_ipc_info), GFP_KERNEL);

	if (IS_ERR(acpm_ipc))
		return PTR_ERR(acpm_ipc);

	acpm_ipc->irq = irq_of_parse_and_map(node, 0);

	ret = devm_request_threaded_irq(&pdev->dev, acpm_ipc->irq, acpm_ipc_irq_handler,
			acpm_ipc_irq_handler_thread,
			IRQF_ONESHOT,
			dev_name(&pdev->dev), acpm_ipc);

	if (ret) {
		dev_err(&pdev->dev, "failed to register acpm_ipc interrupt:%d\n", ret);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	acpm_ipc->intr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(acpm_ipc->intr))
		return PTR_ERR(acpm_ipc->intr);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	acpm_ipc->sram_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(acpm_ipc->sram_base))
		return PTR_ERR(acpm_ipc->sram_base);

	prop = of_get_property(node, "initdata-base", &len);
	if (prop) {
		acpm_ipc->initdata_base = be32_to_cpup(prop);
	} else {
		dev_err(&pdev->dev, "Parsing initdata_base failed.\n");
		return -EINVAL;
	}
	acpm_ipc->initdata = (struct acpm_framework *)(acpm_ipc->sram_base + acpm_ipc->initdata_base);
	acpm_initdata = acpm_ipc->initdata;
	acpm_srambase = acpm_ipc->sram_base;

	acpm_ipc->dev = &pdev->dev;

	log_buffer_init(&pdev->dev, node);

	channel_init();

	update_log_wq = create_freezable_workqueue("acpm_update_log");
	INIT_WORK(&acpm_debug->update_log_work, acpm_update_log);

	if (acpm_debug->period) {
		debug_logging_wq = create_freezable_workqueue("acpm_debug_logging");
		INIT_DELAYED_WORK(&acpm_debug->periodic_work, acpm_debug_logging);

		queue_delayed_work(debug_logging_wq, &acpm_debug->periodic_work,
				msecs_to_jiffies(10000));
	}

	return ret;
}

static void acpm_ipc_remove(struct platform_device *pdev)
{
}

static const struct of_device_id acpm_ipc_match[] = {
	{ .compatible = "samsung,exynos-acpm-ipc" },
	{},
};

static struct platform_driver samsung_acpm_ipc_driver = {
	.probe	= acpm_ipc_probe,
	.remove	= acpm_ipc_remove,
	.driver	= {
		.name = "exynos-acpm-ipc",
		.owner	= THIS_MODULE,
		.of_match_table	= acpm_ipc_match,
	},
};

static int __init exynos_acpm_ipc_init(void)
{
	return platform_driver_register(&samsung_acpm_ipc_driver);
}
arch_initcall(exynos_acpm_ipc_init);

static int __init exynos_acpm_dvfs_init(void)
{
	if (acpm_ipc)
		acpm_ipc_prepare_dvfs(acpm_ipc->dev->of_node);

	return 0;
}
fs_initcall_sync(exynos_acpm_dvfs_init);
