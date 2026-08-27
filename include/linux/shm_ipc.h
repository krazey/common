/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_SHM_IPC_H
#define __LINUX_SHM_IPC_H

#include <linux/types.h>

unsigned long shm_get_phys_base(void);
unsigned int shm_get_phys_size(void);
unsigned int shm_get_cp_size(void);
unsigned int shm_get_ipc_rgn_offset(void);
unsigned int shm_get_ipc_rgn_size(void);
unsigned int shm_get_zmb_size(void);

unsigned long shm_get_vss_base(void);
unsigned int shm_get_vss_size(void);
void __iomem *shm_get_vss_region(void);

unsigned long shm_get_vparam_base(void);
unsigned int shm_get_vparam_size(void);
void __iomem *shm_get_vparam_region(void);

#endif /* __LINUX_SHM_IPC_H */
