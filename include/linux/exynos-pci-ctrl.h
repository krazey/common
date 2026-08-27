/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_EXYNOS_PCI_CTRL_H
#define __LINUX_EXYNOS_PCI_CTRL_H

/* PCIe L1SS control clients */
#define PCIE_L1SS_CTRL_ARGOS		(1U << 0)
#define PCIE_L1SS_CTRL_BOOT		(1U << 1)
#define PCIE_L1SS_CTRL_CAMERA		(1U << 2)
#define PCIE_L1SS_CTRL_MODEM_IF		(1U << 3)
#define PCIE_L1SS_CTRL_WIFI		(1U << 4)
#define PCIE_L1SS_CTRL_MST		(1U << 5)

int exynos_pcie_l1ss_ctrl(int enable, int id);
int exynos_pcie_l1_exit(int ch_num);

#endif /* __LINUX_EXYNOS_PCI_CTRL_H */
