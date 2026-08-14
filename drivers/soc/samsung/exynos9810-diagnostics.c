// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bounded boot diagnostics for Samsung Exynos9810 bring-up.
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/rcupdate.h>
#include <linux/sched/debug.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#define EXYNOS9810_STALL_DUMP_DELAY	(12 * HZ)
#define EXYNOS9810_STALL_DUMP_LIMIT	16

static struct delayed_work exynos9810_stall_dump_work;

static bool exynos9810_stall_dump_group(struct task_struct *group)
{
	char comm[TASK_COMM_LEN];

	get_task_comm(comm, group);

	return !strcmp(comm, "init") || !strncmp(comm, "apexd", 5);
}

static void exynos9810_stall_dump(struct work_struct *work)
{
	struct task_struct *group, *task;
	unsigned int count = 0;

	pr_info("E981D: userspace stall task dump begin\n");
	rcu_read_lock();
	for_each_process_thread(group, task) {
		if (!exynos9810_stall_dump_group(group))
			continue;

		sched_show_task(task);
		if (++count == EXYNOS9810_STALL_DUMP_LIMIT)
			break;
	}
	rcu_read_unlock();
	pr_info("E981D: userspace stall task dump end count=%u\n", count);
}

static int __init exynos9810_stall_diagnostics_init(void)
{
	if (!of_machine_is_compatible("samsung,exynos9810"))
		return 0;

	INIT_DELAYED_WORK(&exynos9810_stall_dump_work,
			  exynos9810_stall_dump);
	schedule_delayed_work(&exynos9810_stall_dump_work,
			      EXYNOS9810_STALL_DUMP_DELAY);

	return 0;
}
late_initcall(exynos9810_stall_diagnostics_init);
