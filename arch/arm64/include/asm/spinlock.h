/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#ifdef CONFIG_QUEUED_SPINLOCKS
#include <asm/qspinlock.h>
#else
#include <asm-generic/ticket_spinlock.h>
#endif
#include <asm/qrwlock.h>

/* See include/linux/spinlock.h */
#define smp_mb__after_spinlock()	smp_mb()

/*
 * Changing this will break osq_lock() thanks to the call inside
 * smp_cond_load_relaxed().
 *
 * See:
 * https://lore.kernel.org/lkml/20200110100612.GC2827@hirez.programming.kicks-ass.net
 */
#define vcpu_is_preempted vcpu_is_preempted
static inline bool vcpu_is_preempted(int cpu)
{
	return false;
}

#endif /* __ASM_SPINLOCK_H */
