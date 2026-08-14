// SPDX-License-Identifier: GPL-2.0
/*
 * Legacy Android schedtune cgroup compatibility controller.
 *
 * Schedtune policy was superseded by utilization clamping and is no longer
 * part of the mainline scheduler.  Older Android device configurations still
 * use its cgroup v1 hierarchy for task grouping, however.  Retain that ABI
 * without coupling the legacy policy implementation to the current scheduler.
 */

#include <linux/cgroup.h>
#include <linux/err.h>
#include <linux/slab.h>

struct schedtune_cgroup {
	struct cgroup_subsys_state css;
	s64 boost;
	u64 prefer_idle;
	u64 prefer_perf;
	u64 band;
	u64 util_est_en;
	u64 ontime_en;
};

static struct schedtune_cgroup root_schedtune;

static inline struct schedtune_cgroup *
css_schedtune(struct cgroup_subsys_state *css)
{
	return container_of(css, struct schedtune_cgroup, css);
}

static s64 schedtune_boost_read(struct cgroup_subsys_state *css,
				struct cftype *cft)
{
	return css_schedtune(css)->boost;
}

static int schedtune_boost_write(struct cgroup_subsys_state *css,
				 struct cftype *cft, s64 boost)
{
	if (boost < -100 || boost > 100)
		return -EINVAL;

	css_schedtune(css)->boost = boost;
	return 0;
}

static u64 schedtune_prefer_idle_read(struct cgroup_subsys_state *css,
				      struct cftype *cft)
{
	return css_schedtune(css)->prefer_idle;
}

static int schedtune_prefer_idle_write(struct cgroup_subsys_state *css,
				       struct cftype *cft, u64 value)
{
	css_schedtune(css)->prefer_idle = !!value;
	return 0;
}

static u64 schedtune_prefer_perf_read(struct cgroup_subsys_state *css,
				      struct cftype *cft)
{
	return css_schedtune(css)->prefer_perf;
}

static int schedtune_prefer_perf_write(struct cgroup_subsys_state *css,
				       struct cftype *cft, u64 value)
{
	css_schedtune(css)->prefer_perf = value;
	return 0;
}

static u64 schedtune_band_read(struct cgroup_subsys_state *css,
			       struct cftype *cft)
{
	return css_schedtune(css)->band;
}

static int schedtune_band_write(struct cgroup_subsys_state *css,
				struct cftype *cft, u64 value)
{
	css_schedtune(css)->band = value;
	return 0;
}

static u64 schedtune_util_est_read(struct cgroup_subsys_state *css,
				   struct cftype *cft)
{
	return css_schedtune(css)->util_est_en;
}

static int schedtune_util_est_write(struct cgroup_subsys_state *css,
				    struct cftype *cft, u64 value)
{
	css_schedtune(css)->util_est_en = value;
	return 0;
}

static u64 schedtune_ontime_read(struct cgroup_subsys_state *css,
				 struct cftype *cft)
{
	return css_schedtune(css)->ontime_en;
}

static int schedtune_ontime_write(struct cgroup_subsys_state *css,
				  struct cftype *cft, u64 value)
{
	css_schedtune(css)->ontime_en = value;
	return 0;
}

static struct cftype schedtune_files[] = {
	{
		.name = "boost",
		.read_s64 = schedtune_boost_read,
		.write_s64 = schedtune_boost_write,
	},
	{
		.name = "prefer_idle",
		.read_u64 = schedtune_prefer_idle_read,
		.write_u64 = schedtune_prefer_idle_write,
	},
	{
		.name = "prefer_perf",
		.read_u64 = schedtune_prefer_perf_read,
		.write_u64 = schedtune_prefer_perf_write,
	},
	{
		.name = "band",
		.read_u64 = schedtune_band_read,
		.write_u64 = schedtune_band_write,
	},
	{
		.name = "util_est_en",
		.read_u64 = schedtune_util_est_read,
		.write_u64 = schedtune_util_est_write,
	},
	{
		.name = "ontime_en",
		.read_u64 = schedtune_ontime_read,
		.write_u64 = schedtune_ontime_write,
	},
	{}
};

static struct cgroup_subsys_state *
schedtune_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct schedtune_cgroup *st;

	if (!parent_css)
		return &root_schedtune.css;

	st = kzalloc_obj(*st);
	if (!st)
		return ERR_PTR(-ENOMEM);

	return &st->css;
}

static void schedtune_css_free(struct cgroup_subsys_state *css)
{
	kfree(css_schedtune(css));
}

struct cgroup_subsys schedtune_cgrp_subsys = {
	.css_alloc = schedtune_css_alloc,
	.css_free = schedtune_css_free,
	.legacy_cftypes = schedtune_files,
};
