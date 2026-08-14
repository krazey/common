// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bounded boot diagnostics for Samsung Exynos9810 bring-up.
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#define EXYNOS9810_SERVICE_DUMP_DELAY	(32 * HZ)
#define EXYNOS9810_SERVICE_DUMP_LIMIT	32

enum exynos9810_service_id {
	EXYNOS9810_SERVICE_INIT,
	EXYNOS9810_SERVICE_MANAGER,
	EXYNOS9810_SERVICE_HW_MANAGER,
	EXYNOS9810_SERVICE_VND_MANAGER,
	EXYNOS9810_SERVICE_VOLD,
	EXYNOS9810_SERVICE_APEXD,
	EXYNOS9810_SERVICE_KEYSTORE,
	EXYNOS9810_SERVICE_ZYGOTE,
	EXYNOS9810_SERVICE_SURFACEFLINGER,
	EXYNOS9810_SERVICE_SYSTEM_SERVER,
	EXYNOS9810_SERVICE_ADBD,
	EXYNOS9810_SERVICE_BOOTANIMATION,
	EXYNOS9810_SERVICE_ODREFRESH,
	EXYNOS9810_SERVICE_DEX2OAT,
	EXYNOS9810_SERVICE_LOGD,
	EXYNOS9810_SERVICE_COUNT,
};

static const char * const exynos9810_service_names[] = {
	[EXYNOS9810_SERVICE_INIT] = "init",
	[EXYNOS9810_SERVICE_MANAGER] = "servicemanager",
	[EXYNOS9810_SERVICE_HW_MANAGER] = "hwservicemanage",
	[EXYNOS9810_SERVICE_VND_MANAGER] = "vndservicemanag",
	[EXYNOS9810_SERVICE_VOLD] = "vold",
	[EXYNOS9810_SERVICE_APEXD] = "apexd",
	[EXYNOS9810_SERVICE_KEYSTORE] = "keystore2",
	[EXYNOS9810_SERVICE_ZYGOTE] = "zygote",
	[EXYNOS9810_SERVICE_SURFACEFLINGER] = "surfaceflinger",
	[EXYNOS9810_SERVICE_SYSTEM_SERVER] = "system_server",
	[EXYNOS9810_SERVICE_ADBD] = "adbd",
	[EXYNOS9810_SERVICE_BOOTANIMATION] = "bootanimation",
	[EXYNOS9810_SERVICE_ODREFRESH] = "odrefresh",
	[EXYNOS9810_SERVICE_DEX2OAT] = "dex2oat",
	[EXYNOS9810_SERVICE_LOGD] = "logd",
};

static struct delayed_work exynos9810_service_dump_work;

static int exynos9810_service_id(struct task_struct *task)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(exynos9810_service_names); i++) {
		const char *name = exynos9810_service_names[i];

		if (!strncmp(task->comm, name, strlen(name)))
			return i;
	}

	return -1;
}

static void exynos9810_service_dump(struct work_struct *work)
{
	struct task_struct *task;
	unsigned long found = 0;
	unsigned long wchan;
	char comm[TASK_COMM_LEN];
	unsigned int count = 0;
	int id;

	pr_info("E981D: Android service task dump begin\n");
	rcu_read_lock();
	for_each_process(task) {
		id = exynos9810_service_id(task);
		if (id < 0)
			continue;

		get_task_comm(comm, task);
		found |= BIT(id);
		wchan = get_wchan(task);
		pr_info("E981D: service %s/%d threads=%d state=%c cpu=%d\n",
			comm, task_pid_nr(task), get_nr_threads(task),
			task_state_to_char(task), task_cpu(task));
		pr_info("E981D: service pid=%d io=%u wchan=%ps\n",
			task_pid_nr(task), task->in_iowait, (void *)wchan);
		if (++count == EXYNOS9810_SERVICE_DUMP_LIMIT)
			break;
	}
	rcu_read_unlock();

	pr_info("E981D: core init=%u sm=%u hw=%u vnd=%u vold=%u apex=%u key=%u\n",
		!!(found & BIT(EXYNOS9810_SERVICE_INIT)),
		!!(found & BIT(EXYNOS9810_SERVICE_MANAGER)),
		!!(found & BIT(EXYNOS9810_SERVICE_HW_MANAGER)),
		!!(found & BIT(EXYNOS9810_SERVICE_VND_MANAGER)),
		!!(found & BIT(EXYNOS9810_SERVICE_VOLD)),
		!!(found & BIT(EXYNOS9810_SERVICE_APEXD)),
		!!(found & BIT(EXYNOS9810_SERVICE_KEYSTORE)));
	pr_info("E981D: late zygote=%u sf=%u system=%u adb=%u boot=%u\n",
		!!(found & BIT(EXYNOS9810_SERVICE_ZYGOTE)),
		!!(found & BIT(EXYNOS9810_SERVICE_SURFACEFLINGER)),
		!!(found & BIT(EXYNOS9810_SERVICE_SYSTEM_SERVER)),
		!!(found & BIT(EXYNOS9810_SERVICE_ADBD)),
		!!(found & BIT(EXYNOS9810_SERVICE_BOOTANIMATION)));
	pr_info("E981D: jobs odrefresh=%u dex2oat=%u logd=%u count=%u\n",
		!!(found & BIT(EXYNOS9810_SERVICE_ODREFRESH)),
		!!(found & BIT(EXYNOS9810_SERVICE_DEX2OAT)),
		!!(found & BIT(EXYNOS9810_SERVICE_LOGD)), count);
}

static int __init exynos9810_stall_diagnostics_init(void)
{
	if (!of_machine_is_compatible("samsung,exynos9810"))
		return 0;

	INIT_DELAYED_WORK(&exynos9810_service_dump_work,
			  exynos9810_service_dump);
	schedule_delayed_work(&exynos9810_service_dump_work,
			      EXYNOS9810_SERVICE_DUMP_DELAY);

	return 0;
}
late_initcall(exynos9810_stall_diagnostics_init);
