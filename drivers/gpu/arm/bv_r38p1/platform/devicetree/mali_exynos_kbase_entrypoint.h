/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MALI_EXYNOS_KBASE_ENTRYPOINTS_H_
#define _MALI_EXYNOS_KBASE_ENTRYPOINTS_H_

#include <mali_kbase.h>

static inline void mali_exynos_update_job_load(struct kbase_jd_atom *katom,
					       ktime_t *end_timestamp) { }
static inline int mali_exynos_set_pm_state_resume_begin(void) { return 0; }
static inline int mali_exynos_set_pm_state_resume_end(void) { return 0; }
static inline void mali_exynos_coherency_reg_map(void) { }
static inline void mali_exynos_coherency_reg_unmap(void) { }
static inline void mali_exynos_coherency_set_coherency_feature(void) { }
static inline void mali_exynos_llc_set_aruser(void) { }
static inline void mali_exynos_llc_set_awuser(void) { }
static inline void mali_exynos_update_jobslot_util(int slot, bool gpu_active,
						   u32 ns_time) { }
static inline void mali_exynos_set_jobslot_status(int slot, bool active) { }
static inline int mali_exynos_set_count(struct kbase_jd_atom *katom,
					 u32 status, bool stop) { return 0; }
static inline int mali_exynos_ioctl(struct kbase_context *kctx,
				     unsigned int cmd,
				     unsigned long arg) { return 0; }
static inline void mali_exynos_update_firstjob_vsync_time(void) { }
static inline void mali_exynos_update_firstjob_time(void) { }
static inline void mali_exynos_update_lastjob_time(int slot_nr) { }
static inline void mali_exynos_update_jobsubmit_time(void) { }
static inline void mali_exynos_sum_jobs_time(int slot_nr) { }
static inline void mali_exynos_amigo_interframe_hw_update_eof(void) { }
static inline void mali_exynos_amigo_interframe_hw_update(void) { }
static inline int mali_exynos_get_gpu_power_state(void) { return 1; }
static inline void mali_exynos_set_thread_priority(struct kbase_context *kctx) { }
static inline void mali_exynos_set_thread_affinity(void) { }
static inline int mali_exynos_legacy_jm_enter_protected_mode(
						struct kbase_device *kbdev)
{
	return -EOPNOTSUPP;
}
static inline int mali_exynos_legacy_jm_exit_protected_mode(
					       struct kbase_device *kbdev)
{
	return -EOPNOTSUPP;
}
static inline int mali_exynos_legacy_pm_exit_protected_mode(
					       struct kbase_device *kbdev)
{
	return -EOPNOTSUPP;
}
static inline struct protected_mode_ops *mali_exynos_get_protected_ops(void)
{
	return NULL;
}
static inline bool mali_exynos_dmabuf_is_cached(struct dma_buf *dmabuf)
{
	return true;
}
typedef ssize_t (*sysfs_read_func)(struct device *, struct device_attribute *,
				   char *);
static inline void mali_exynos_sysfs_set_gpu_model_callback(
						sysfs_read_func show_gpu_model_fn) { }
static inline void mali_exynos_debug_print_info(struct kbase_device *kbdev) { }

#endif
