// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim DSM speaker-mode control for Samsung ABOX
 *
 * This implements the platform-C exchange required when MAX98512
 * speakers are enabled. Calibration and diagnostic userspace ABIs from
 * the downstream driver are intentionally kept separate.
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <sound/maxim_dsm.h>
#include <sound/sec_adaptation.h>

#define MAX98512_OSM_MAX	8

static DEFINE_MUTEX(maxdsm_lock);

static struct maxim_dsm maxdsm = {
	.param_size = PARAM_DSM_5_0_MAX,
	.platform_type = PLATFORM_TYPE_C,
	.version = VERSION_5_0_C_S,
};

int maxdsm_init(void)
{
	int ret = 0;

	mutex_lock(&maxdsm_lock);
	if (!maxdsm.param) {
		maxdsm.param = kcalloc(maxdsm.param_size,
				       sizeof(*maxdsm.param), GFP_KERNEL);
		if (!maxdsm.param)
			ret = -ENOMEM;
	}
	mutex_unlock(&maxdsm_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(maxdsm_init);

void maxdsm_deinit(void)
{
	mutex_lock(&maxdsm_lock);
	kfree(maxdsm.param);
	maxdsm.param = NULL;
	maxdsm.spk_state = 0;
	mutex_unlock(&maxdsm_lock);
}
EXPORT_SYMBOL_GPL(maxdsm_deinit);

int maxdsm_update_info(const u32 *platform_info)
{
	int ret = 0;

	if (!platform_info)
		return -EINVAL;
	if (platform_info[PARAM_OFFSET_PLATFORM] != PLATFORM_TYPE_C)
		return -EOPNOTSUPP;
	if (platform_info[PARAM_OFFSET_VERSION] != VERSION_5_0_C &&
	    platform_info[PARAM_OFFSET_VERSION] != VERSION_5_0_C_S)
		return -EINVAL;

	mutex_lock(&maxdsm_lock);
	maxdsm.platform_type = platform_info[PARAM_OFFSET_PLATFORM];
	maxdsm.version = platform_info[PARAM_OFFSET_VERSION];
	if (!maxdsm.param) {
		maxdsm.param = kcalloc(maxdsm.param_size,
				       sizeof(*maxdsm.param), GFP_KERNEL);
		if (!maxdsm.param)
			ret = -ENOMEM;
	}
	mutex_unlock(&maxdsm_lock);

	if (!ret)
		pr_info("maxim-dsm: ABOX platform-C speaker controls ready\n");

	return ret;
}
EXPORT_SYMBOL_GPL(maxdsm_update_info);

void maxdsm_set_spk_state(int state, int osm_mode)
{
	mutex_lock(&maxdsm_lock);
	maxdsm.spk_state = !!state;
	maxdsm.osm_mode = osm_mode;
	mutex_unlock(&maxdsm_lock);
}
EXPORT_SYMBOL_GPL(maxdsm_set_spk_state);

int maxdsm_set_stereo_mode_configuration(unsigned int mode)
{
	int ret;

	if (mode >= MAX98512_OSM_MAX)
		return -EINVAL;

	mutex_lock(&maxdsm_lock);
	if (!maxdsm.param) {
		ret = -ENODEV;
		goto out;
	}
	if (!maxdsm.spk_state) {
		ret = -EPIPE;
		goto out;
	}

	memset(maxdsm.param, 0,
	       maxdsm.param_size * sizeof(*maxdsm.param));
	maxdsm.param_offset = 0;
	ret = maxim_dsm_read(maxdsm.param_offset, PARAM_DSM_5_0_MAX,
			     &maxdsm);
	if (ret)
		goto out;

	maxdsm.param[DSM_API_SETGET_WRITE_FLAG] = PARAM_WRITE_OSM;
	maxdsm.param[DSM_API_SETGET_ENABLE] = mode;
	maxdsm.param_offset = PARAM_DSM_ABOX_SET_OSM;
	ret = maxim_dsm_write(maxdsm.param, maxdsm.param_offset,
			      PARAM_DSM_5_0_MAX);
	maxdsm.param_offset = 0;
	if (!ret)
		maxdsm.osm_mode = mode;

out:
	mutex_unlock(&maxdsm_lock);
	if (!ret)
		pr_info("maxim-dsm: speaker mode %u synchronized with ABOX\n",
			mode);

	return ret;
}
EXPORT_SYMBOL_GPL(maxdsm_set_stereo_mode_configuration);

MODULE_DESCRIPTION("Maxim DSM speaker-mode control for Samsung ABOX");
MODULE_LICENSE("GPL");
