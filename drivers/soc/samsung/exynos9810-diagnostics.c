// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bounded boot diagnostics for Samsung Exynos9810 bring-up.
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#define EXYNOS9810_STALL_DUMP_DELAY	(12 * HZ)
#define EXYNOS9810_STALL_DUMP_LIMIT	16
#define EXYNOS9810_STALL_TRACE_LIMIT	8

static struct delayed_work exynos9810_stall_dump_work;

static bool exynos9810_stall_dump_group(struct task_struct *group)
{
	char comm[TASK_COMM_LEN];

	get_task_comm(comm, group);

	return !strcmp(comm, "init") || !strncmp(comm, "apexd", 5);
}

static void exynos9810_stall_dump_task(struct task_struct *task)
{
	unsigned long entries[EXYNOS9810_STALL_TRACE_LIMIT];
	unsigned long switches, wchan;
	char comm[TASK_COMM_LEN];
	unsigned int i, nr;

	get_task_comm(comm, task);
	wchan = get_wchan(task);
	switches = READ_ONCE(task->nvcsw) + READ_ONCE(task->nivcsw);
	nr = stack_trace_save_tsk(task, entries, ARRAY_SIZE(entries), 0);

	pr_info("E981D: task %s/%d group=%d state=%c/%#x cpu=%d sw=%lu\n",
		comm, task_pid_nr(task), task_tgid_nr(task),
		task_state_to_char(task), READ_ONCE(task->__state),
		task_cpu(task), switches);
	pr_info("E981D: wait pid=%d oncpu=%u rq=%u io=%u wchan=%ps frames=%u\n",
		task_pid_nr(task), READ_ONCE(task->on_cpu),
		READ_ONCE(task->on_rq), task->in_iowait, (void *)wchan, nr);

	for (i = 0; i < nr; i++)
		pr_info("E981D: stack pid=%d frame=%u/%u %pS\n",
			task_pid_nr(task), i + 1, nr, (void *)entries[i]);
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

		exynos9810_stall_dump_task(task);
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
