/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SOUND_MAXIM_DSM_H__
#define __SOUND_MAXIM_DSM_H__

#include <linux/types.h>

#define PARAM_WRITE_OSM				0xcba0cba0
#define PARAM_DSM_5_0_MAX			185
#define PARAM_DSM_5_0_ABOX_WRITE_CB		0xdaad
#define PARAM_DSM_ABOX_SET_OSM			500

#define DSM_API_SETGET_ENABLE			1
#define DSM_API_SETGET_WRITE_FLAG		63

enum maxdsm_platform_type {
	PLATFORM_TYPE_A,
	PLATFORM_TYPE_B,
	PLATFORM_TYPE_C,
};

enum maxdsm_version {
	VERSION_5_0_C = 42,
	VERSION_5_0_C_S = 52,
};

enum maxdsm_offset {
	PARAM_OFFSET_PLATFORM,
	PARAM_OFFSET_PORT_ID,
	PARAM_OFFSET_RX_MOD_ID,
	PARAM_OFFSET_TX_MOD_ID,
	PARAM_OFFSET_FILTER_SET,
	PARAM_OFFSET_VERSION,
	PARAM_OFFSET_MAX,
};

struct maxim_dsm {
	u32 *param;
	u32 param_size;
	u32 platform_type;
	u32 version;
	u32 spk_state;
	u32 param_offset;
	u32 osm_mode;
};

int maxdsm_init(void);
void maxdsm_deinit(void);
int maxdsm_update_info(const u32 *platform_info);
void maxdsm_set_spk_state(int state, int osm_mode);
int maxdsm_set_stereo_mode_configuration(unsigned int mode);

#endif /* __SOUND_MAXIM_DSM_H__ */
