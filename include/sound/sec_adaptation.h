/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SOUND_SEC_ADAPTATION_H__
#define __SOUND_SEC_ADAPTATION_H__

#include <linux/types.h>

int maxim_dsm_write(const u32 *data, int offset, int size);
int maxim_dsm_read(int offset, int size, void *dsm_data);

#endif /* __SOUND_SEC_ADAPTATION_H__ */
