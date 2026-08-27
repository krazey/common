/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SND_SOC_ABOX_SMC_H
#define __SND_SOC_ABOX_SMC_H

#include <linux/arm-smccc.h>

#define SMC_CMD_REG			(-101)
#define SMC_REG_CLASS_SFR_W		(0x1UL << 30)
#define SMC_REG_CLASS_SFR_R		(0x3UL << 30)
#define SMC_REG_ID_SFR_W(addr)		\
	(SMC_REG_CLASS_SFR_W | ((addr) >> 2))
#define SMC_REG_ID_SFR_R(addr)		\
	(SMC_REG_CLASS_SFR_R | ((addr) >> 2))

static inline int exynos_smc(unsigned long cmd, unsigned long arg1,
		unsigned long arg2, unsigned long arg3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(cmd, arg1, arg2, arg3, 0, 0, 0, 0, &res);

	return (int)res.a0;
}

#endif /* __SND_SOC_ABOX_SMC_H */
