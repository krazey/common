/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Low-level CPU initialisation
 * Based on arch/arm/kernel/head.S
 *
 * Copyright (C) 1994-2002 Russell King
 * Copyright (C) 2003-2012 ARM Ltd.
 * Authors:	Catalin Marinas <catalin.marinas@arm.com>
 *		Will Deacon <will.deacon@arm.com>
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/pgtable.h>

#include <asm/asm_pointer_auth.h>
#include <asm/assembler.h>
#include <asm/boot.h>
#include <asm/bug.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>
#include <asm/cputype.h>
#include <asm/el2_setup.h>
#include <asm/elf.h>
#include <asm/image.h>
#include <asm/kernel-pgtable.h>
#include <asm/kvm_arm.h>
#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>
#include <asm/page.h>
#include <asm/scs.h>
#include <asm/smp.h>
#include <asm/sysreg.h>
#include <asm/stacktrace/frame.h>
#include <asm/thread_info.h>
#include <asm/virt.h>

#include "efi-header.S"

#if (PAGE_OFFSET & 0x1fffff) != 0
#error PAGE_OFFSET must be at least 2MB aligned
#endif

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	/*
	 * Store a recovery-readable ramoops record without relying on the MMU,
	 * device tree, console, or platform drivers. Skip the write if firmware
	 * entered with address translation enabled because the physical pstore
	 * address may not be present in its active translation tables.
	 */
	.macro	exynos9810_store_boot_marker stage
	movz	x9, #0xfed1, lsl #16
	movz	w10, #0x4244
	movk	w10, #0x4347, lsl #16
	str	w10, [x9]
	str	wzr, [x9, #4]
	mov	w10, #18
	str	w10, [x9, #8]
	movz	w10, #0x3d3d
	movk	w10, #0x3d3d, lsl #16
	str	w10, [x9, #12]
	movz	w10, #0x2e30
	movk	w10, #0x2d30, lsl #16
	str	w10, [x9, #16]
	movz	w10, #0x0a44
	movk	w10, #0x3945, lsl #16
	str	w10, [x9, #20]
	movz	w10, #0x3138
	movk	w10, #0x003a, lsl #16
	str	w10, [x9, #24]
	mov	w10, #\stage
	strb	w10, [x9, #27]
	lsr	w10, w10, #8
	strb	w10, [x9, #28]
	mov	w10, #0x0a
	strb	w10, [x9, #29]
	dc	cvac, x9
	dsb	sy
	.endm

	.macro	exynos9810_boot_marker stage
	mrs	x9, CurrentEL
	cmp	x9, #CurrentEL_EL1
	b.eq	.Lexynos9810_el1\@
	cmp	x9, #CurrentEL_EL2
	b.ne	.Lexynos9810_done\@
	mrs	x9, sctlr_el2
	b	.Lexynos9810_check_mmu\@
.Lexynos9810_el1\@:
	mrs	x9, sctlr_el1
.Lexynos9810_check_mmu\@:
	tst	x9, #SCTLR_ELx_M
	b.ne	.Lexynos9810_done\@
	exynos9810_store_boot_marker \stage
.Lexynos9810_done\@:
	.endm

	.macro	exynos9810_idmap_boot_marker stage
	exynos9810_store_boot_marker \stage
	.endm

	/* Update an initialized record without rewriting its metadata. */
	.macro	exynos9810_update_boot_marker stage, cached
	movz	x9, #0xfed1, lsl #16
	mov	w10, #\stage
	strb	w10, [x9, #27]
	lsr	w10, w10, #8
	strb	w10, [x9, #28]
	.if	\cached
	dc	cvac, x9
	.endif
	dsb	sy
	.endm

	.macro	exynos9810_store_hex value
	mov	x19, #60
.Lexynos9810_hex\@:
	lsr	x10, \value, x19
	and	w10, w10, #0xf
	cmp	w10, #9
	add	w20, w10, #'0'
	add	w21, w10, #('a' - 10)
	csel	w10, w20, w21, ls
	strb	w10, [x11], #1
	subs	x19, x19, #4
	b.ge	.Lexynos9810_hex\@
	.endm

	/* Append a two-character tag and a 64-bit hexadecimal value. */
	.macro	exynos9810_snapshot_entry tag
	mov	w6, #\tag
	strh	w6, [x4], #2
	mov	w6, #'='
	strb	w6, [x4], #1
	mov	x7, #60
.Lexynos9810_snapshot_hex\@:
	lsr	x8, x5, x7
	and	w8, w8, #0xf
	cmp	w8, #9
	add	w6, w8, #'0'
	add	w9, w8, #('a' - 10)
	csel	w6, w6, w9, ls
	strb	w6, [x4], #1
	subs	x7, x7, #4
	b.ge	.Lexynos9810_snapshot_hex\@
	mov	w6, #'\n'
	strb	w6, [x4], #1
	.endm
#else
	.macro	exynos9810_boot_marker stage
	.endm

	.macro	exynos9810_idmap_boot_marker stage
	.endm

	.macro	exynos9810_update_boot_marker stage, cached
	.endm
#endif

/*
 * Kernel startup entry point.
 * ---------------------------
 *
 * The requirements are:
 *   MMU = off, D-cache = off, I-cache = on or off,
 *   x0 = physical address to the FDT blob.
 *
 * Note that the callee-saved registers are used for storing variables
 * that are useful before the MMU is enabled. The allocations are described
 * in the entry routines.
 */
	__HEAD
	/*
	 * DO NOT MODIFY. Image header expected by Linux boot-loaders.
	 */
	efi_signature_nop			// special NOP to identity as PE/COFF executable
	b	primary_entry			// branch to kernel start, magic
	le64sym	_kernel_offset_le		// Image load offset, little-endian
	le64sym	_kernel_size_le			// Effective size of kernel image, little-endian
	le64sym	_kernel_flags_le		// Informative flags, little-endian
	.quad	0				// reserved
	.quad	0				// reserved
	.quad	0				// reserved
	.ascii	ARM64_IMAGE_MAGIC		// Magic number
	.long	.Lpe_header_offset		// Offset to the PE header.

	__EFI_PE_HEADER

	.section ".idmap.text","a"

	/*
	 * The following callee saved general purpose registers are used on the
	 * primary lowlevel boot path:
	 *
	 *  Register   Scope                      Purpose
	 *  x19        primary_entry() .. start_kernel()        whether we entered with the MMU on
	 *  x20        primary_entry() .. __primary_switch()    CPU boot mode
	 *  x21        primary_entry() .. start_kernel()        FDT pointer passed at boot in x0
	 */
SYM_CODE_START(primary_entry)
	/* 01: branch from the Image header reached primary_entry. */
	exynos9810_boot_marker 0x3130
	bl	record_mmu_state
	/* 02: the firmware MMU state was captured. */
	exynos9810_boot_marker 0x3230
	bl	preserve_boot_args
	/* 03: the firmware boot arguments were saved. */
	exynos9810_boot_marker 0x3330

	adrp	x1, early_init_stack
	mov	sp, x1
	mov	x29, xzr

	/*
	 * Firmware may leave stale cache lines covering the bootstrap page
	 * tables across a warm boot. Invalidate and clear the entire allocation
	 * before the position-independent map builder follows any descriptors.
	 */
	adrp	x0, __pi_init_idmap_pg_dir
	adr_l	x1, __pi_init_idmap_pg_end
	adr_l	x2, dcache_inval_poc
	blr	x2
	adrp	x0, __pi_init_idmap_pg_dir
	adr_l	x1, __pi_init_idmap_pg_end
1:	stp	xzr, xzr, [x0], #16
	cmp	x0, x1
	b.lo	1b

	adrp	x0, __pi_init_idmap_pg_dir
	mov	x1, xzr
	bl	__pi_create_init_idmap
	/* 04: the initial identity map was built. */
	exynos9810_boot_marker 0x3430

	/*
	 * If the page tables have been populated with non-cacheable
	 * accesses (MMU disabled), invalidate those tables again to
	 * remove any speculatively loaded cache lines.
	 */
	cbnz	x19, 0f
	dmb     sy
	mov	x1, x0				// end of used region
	adrp    x0, __pi_init_idmap_pg_dir
	adr_l	x2, dcache_inval_poc
	blr	x2
	b	1f

	/*
	 * If we entered with the MMU and caches on, clean the ID mapped part
	 * of the primary boot code to the PoC so we can safely execute it with
	 * the MMU off.
	 */
0:	adrp	x0, __idmap_text_start
	adr_l	x1, __idmap_text_end
	adr_l	x2, dcache_clean_poc
	blr	x2

1:	mov	x0, x19
	bl	init_kernel_el			// w0=cpu_boot_mode
	mov	x20, x0
	/* 05: the exception level was initialized with the MMU disabled. */
	exynos9810_boot_marker 0x3530

	/*
	 * The following calls CPU setup code, see arch/arm64/mm/proc.S for
	 * details.
	 * On return, the CPU will be ready for the MMU to be turned on and
	 * the TCR will have been set.
	 */
	bl	__cpu_setup			// initialise processor
	/* 06: the boot CPU is ready to enable its initial MMU mapping. */
	exynos9810_boot_marker 0x3630
	b	__primary_switch
SYM_CODE_END(primary_entry)

	__INIT
SYM_CODE_START_LOCAL(record_mmu_state)
	mrs	x19, CurrentEL
	cmp	x19, #CurrentEL_EL2
	mrs	x19, sctlr_el1
	b.ne	0f
	mrs	x19, sctlr_el2
0:
CPU_LE( tbnz	x19, #SCTLR_ELx_EE_SHIFT, 1f	)
CPU_BE( tbz	x19, #SCTLR_ELx_EE_SHIFT, 1f	)
	tst	x19, #SCTLR_ELx_C		// Z := (C == 0)
	and	x19, x19, #SCTLR_ELx_M		// isolate M bit
	csel	x19, xzr, x19, eq		// clear x19 if Z
	ret

	/*
	 * Set the correct endianness early so all memory accesses issued
	 * before init_kernel_el() occur in the correct byte order. Note that
	 * this means the MMU must be disabled, or the active ID map will end
	 * up getting interpreted with the wrong byte order.
	 */
1:	eor	x19, x19, #SCTLR_ELx_EE
	bic	x19, x19, #SCTLR_ELx_M
	b.ne	2f
	pre_disable_mmu_workaround
	msr	sctlr_el2, x19
	b	3f
2:	pre_disable_mmu_workaround
	msr	sctlr_el1, x19
3:	isb
	mov	x19, xzr
	ret
SYM_CODE_END(record_mmu_state)

/*
 * Preserve the arguments passed by the bootloader in x0 .. x3
 */
SYM_CODE_START_LOCAL(preserve_boot_args)
	mov	x21, x0				// x21=FDT

	adr_l	x0, boot_args			// record the contents of
	stp	x21, x1, [x0]			// x0 .. x3 at kernel entry
	stp	x2, x3, [x0, #16]

	cbnz	x19, 0f				// skip cache invalidation if MMU is on
	dmb	sy				// needed before dc ivac with
						// MMU off

	add	x1, x0, #0x20			// 4 x 8 bytes
	b	dcache_inval_poc		// tail call
0:	str_l   x19, mmu_enabled_at_boot, x0
	ret
SYM_CODE_END(preserve_boot_args)

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
/*
 * Retain the firmware hand-off and live translation state immediately before
 * the first MMU switch. The fixed-width text format remains readable when a
 * record suffers isolated bit errors and needs no decoder in recovery.
 *
 * x1 = physical address of the first instruction after the MMU switch
 *
 * Preserves x0-x3 and x18-x30. The caller has already installed TTBR0/TTBR1.
 */
SYM_CODE_START_LOCAL(exynos9810_store_boot_snapshot)
	mov	x11, x1
	mov	x12, xzr
	mov	x13, xzr
	mov	x14, xzr
	mov	x15, xzr
	mrs	x9, ttbr0_el1
	ubfx	x10, x9, #12, #36
	lsl	x10, x10, #12
	adrp	x17, __pi_init_idmap_pg_dir
	adr_l	x6, __pi_init_idmap_pg_end

#if IDMAP_ROOT_LEVEL == 0
	cmp	x10, x17
	b.lo	9f
	cmp	x10, x6
	b.hs	9f
	lsr	x9, x11, #39
	and	x9, x9, #0x1ff
	ldr	x12, [x10, x9, lsl #3]
	and	x9, x12, #3
	cmp	x9, #3
	b.ne	9f
	ubfx	x10, x12, #12, #36
	lsl	x10, x10, #12
#endif

#if IDMAP_ROOT_LEVEL <= 1
	cmp	x10, x17
	b.lo	9f
	cmp	x10, x6
	b.hs	9f
	lsr	x9, x11, #30
	and	x9, x9, #0x1ff
	ldr	x13, [x10, x9, lsl #3]
	and	x9, x13, #3
	cmp	x9, #3
	b.ne	9f
	ubfx	x10, x13, #12, #36
	lsl	x10, x10, #12
#endif

#if IDMAP_ROOT_LEVEL <= 2
	cmp	x10, x17
	b.lo	9f
	cmp	x10, x6
	b.hs	9f
	lsr	x9, x11, #21
	and	x9, x9, #0x1ff
	ldr	x14, [x10, x9, lsl #3]
	and	x9, x14, #3
	cmp	x9, #3
	b.ne	9f
	ubfx	x10, x14, #12, #36
	lsl	x10, x10, #12
#endif

	cmp	x10, x17
	b.lo	9f
	cmp	x10, x6
	b.hs	9f
	lsr	x9, x11, #12
	and	x9, x9, #0x1ff
	ldr	x15, [x10, x9, lsl #3]
9:
	movz	x10, #0xfed1, lsl #16
	add	x4, x10, #30

	mov	x5, #1
	exynos9810_snapshot_entry 0x5653	/* SV */
	mov	x5, x11
	exynos9810_snapshot_entry 0x4350	/* PC */
	adrp	x5, _text
	exynos9810_snapshot_entry 0x5854	/* TX */

	adr_l	x16, boot_args
	ldr	x5, [x16]
	exynos9810_snapshot_entry 0x3041	/* A0 */
	ldr	x5, [x16, #8]
	exynos9810_snapshot_entry 0x3141	/* A1 */
	ldr	x5, [x16, #16]
	exynos9810_snapshot_entry 0x3241	/* A2 */
	ldr	x5, [x16, #24]
	exynos9810_snapshot_entry 0x3341	/* A3 */

	mrs	x5, CurrentEL
	exynos9810_snapshot_entry 0x4c45	/* EL */
	mov	x5, x20
	exynos9810_snapshot_entry 0x4d42	/* BM */
	mov	x5, x19
	exynos9810_snapshot_entry 0x534d	/* MS */
	mrs	x5, midr_el1
	exynos9810_snapshot_entry 0x494d	/* MI */
	mrs	x5, mpidr_el1
	exynos9810_snapshot_entry 0x504d	/* MP */
	mrs	x5, ctr_el0
	exynos9810_snapshot_entry 0x5443	/* CT */
	mrs	x5, clidr_el1
	exynos9810_snapshot_entry 0x4c43	/* CL */
	mrs	x5, sctlr_el1
	exynos9810_snapshot_entry 0x4353	/* SC */
	mrs	x5, tcr_el1
	exynos9810_snapshot_entry 0x4354	/* TC */
	mrs	x5, mair_el1
	exynos9810_snapshot_entry 0x414d	/* MA */
	mrs	x5, ttbr0_el1
	exynos9810_snapshot_entry 0x3054	/* T0 */
	mrs	x5, ttbr1_el1
	exynos9810_snapshot_entry 0x3154	/* T1 */
	mrs	x5, vbar_el1
	exynos9810_snapshot_entry 0x4256	/* VB */
	mrs	x5, id_aa64mmfr0_el1
	exynos9810_snapshot_entry 0x4d4d	/* MM */

	mov	x5, #IDMAP_ROOT_LEVEL
	exynos9810_snapshot_entry 0x4c52	/* RL */
	mov	x5, x12
	exynos9810_snapshot_entry 0x304c	/* L0 */
	mov	x5, x13
	exynos9810_snapshot_entry 0x314c	/* L1 */
	mov	x5, x14
	exynos9810_snapshot_entry 0x324c	/* L2 */
	mov	x5, x15
	exynos9810_snapshot_entry 0x334c	/* L3 */

	sub	x5, x4, x10
	sub	w5, w5, #12
	str	w5, [x10, #8]
	mov	x5, x10
1:	dc	cvac, x5
	add	x5, x5, #64
	cmp	x5, x4
	b.lo	1b
	dsb	sy
	ret
SYM_CODE_END(exynos9810_store_boot_snapshot)
#endif

	/*
	 * Initialize CPU registers with task-specific and cpu-specific context.
	 *
	 * Create a final frame record at task_pt_regs(current)->stackframe, so
	 * that the unwinder can identify the final frame record of any task by
	 * its location in the task stack. We reserve the entire pt_regs space
	 * for consistency with user tasks and kthreads.
	 */
	.macro	init_cpu_task_from_stack tsk, stack, tmp1, tmp2
	msr	sp_el0, \tsk

	add	sp, \stack, #THREAD_SIZE
	sub	sp, sp, #PT_REGS_SIZE

	stp	xzr, xzr, [sp, #S_STACKFRAME]
	mov	\tmp1, #FRAME_META_TYPE_FINAL
	str	\tmp1, [sp, #S_STACKFRAME_TYPE]
	add	x29, sp, #S_STACKFRAME

	scs_load_current

	adr_l	\tmp1, __per_cpu_offset
	ldr	w\tmp2, [\tsk, #TSK_TI_CPU]
	ldr	\tmp1, [\tmp1, \tmp2, lsl #3]
	set_this_cpu_offset \tmp1
	.endm

	.macro	init_cpu_task tsk, tmp1, tmp2
	ldr	\tmp1, [\tsk, #TSK_STACK]
	init_cpu_task_from_stack \tsk, \tmp1, \tmp1, \tmp2
	.endm

/*
 * The following fragment of code is executed with the MMU enabled.
 *
 *   x0 = __pa(KERNEL_START)
 */
SYM_FUNC_START_LOCAL(__primary_switched)
	/* 0E: execution reached the high kernel mapping. */
	exynos9810_update_boot_marker 0x4530, 1
	adr_l	x4, init_task
#ifdef CONFIG_EXYNOS9810_UNALIGNED_IMAGE
	/* Firmware placement can disturb the link-time stack alignment. */
	adr_l	x5, exynos9810_init_stack
	mov	x6, #(THREAD_ALIGN - 1)
	add	x5, x5, x6
	bic	x5, x5, x6
	mov	x7, x5
	add	x8, x5, #THREAD_SIZE
1:	stp	xzr, xzr, [x7], #16
	cmp	x7, x8
	b.lo	1b
	str	x5, [x4, #TSK_STACK]
#endif
	init_cpu_task x4, x5, x6

	adr_l	x8, vectors			// load VBAR_EL1 with virtual
	msr	vbar_el1, x8			// vector table address
	isb

	stp	x29, x30, [sp, #-16]!
	mov	x29, sp

	str_l	x21, __fdt_pointer, x5		// Save FDT pointer

	adrp	x4, _text			// Save the offset between
	sub	x4, x4, x0			// the kernel virtual and
	str_l	x4, kimage_voffset, x5		// physical mappings

	mov	x0, x20
	bl	set_cpu_boot_mode_flag

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
	bl	kasan_early_init
#endif
	mov	x0, x20
	bl	finalise_el2			// Prefer VHE if possible
	/* 0F: the kernel is ready to enter start_kernel. */
	exynos9810_update_boot_marker 0x4630, 1
	ldp	x29, x30, [sp], #16
	bl	start_kernel
	ASM_BUG()
SYM_FUNC_END(__primary_switched)

#ifdef CONFIG_EXYNOS9810_EARLY_BOOT_MARKERS
	.macro	exynos9810_mmu_vector marker
	.align	7
	mov	w12, #\marker
	b	exynos9810_mmu_fault
	.endm

	.align	11
SYM_CODE_START_LOCAL(exynos9810_mmu_vectors)
	exynos9810_mmu_vector 0x5330
	exynos9810_mmu_vector 0x4930
	exynos9810_mmu_vector 0x4630
	exynos9810_mmu_vector 0x4530
	exynos9810_mmu_vector 0x5358
	exynos9810_mmu_vector 0x4958
	exynos9810_mmu_vector 0x4658
	exynos9810_mmu_vector 0x4558
	exynos9810_mmu_vector 0x5355
	exynos9810_mmu_vector 0x4955
	exynos9810_mmu_vector 0x4655
	exynos9810_mmu_vector 0x4555
	exynos9810_mmu_vector 0x5343
	exynos9810_mmu_vector 0x4943
	exynos9810_mmu_vector 0x4643
	exynos9810_mmu_vector 0x4543
	.align	11
SYM_CODE_END(exynos9810_mmu_vectors)

SYM_CODE_START_LOCAL(exynos9810_mmu_fault)
	msr	daifset, #0xf
	mrs	x13, esr_el1
	mrs	x14, far_el1
	mrs	x15, elr_el1
	mrs	x16, spsr_el1
	mrs	x17, sctlr_el1

	movz	x9, #0xfed1, lsl #16
	add	x9, x9, #4, lsl #12
	movz	w10, #0x4244
	movk	w10, #0x4347, lsl #16
	str	w10, [x9]
	str	wzr, [x9, #4]
	movz	w10, #0x3945
	movk	w10, #0x3138, lsl #16
	str	w10, [x9, #12]
	mov	w10, #':'
	strb	w10, [x9, #16]
	strb	w12, [x9, #17]
	lsr	w10, w12, #8
	strb	w10, [x9, #18]
	mov	w10, #'\n'
	strb	w10, [x9, #19]
	mov	w10, #8
	str	w10, [x9, #8]
	tst	x17, #SCTLR_ELx_C
	b.eq	1f
	dc	cvac, x9
1:	dsb	sy
	add	x11, x9, #20

	mov	w10, #'E'
	strb	w10, [x11], #1
	mov	w10, #'='
	strb	w10, [x11], #1
	exynos9810_store_hex x13
	mov	w10, #'\n'
	strb	w10, [x11], #1
	mov	w10, #'F'
	strb	w10, [x11], #1
	mov	w10, #'='
	strb	w10, [x11], #1
	exynos9810_store_hex x14
	mov	w10, #'\n'
	strb	w10, [x11], #1
	mov	w10, #'L'
	strb	w10, [x11], #1
	mov	w10, #'='
	strb	w10, [x11], #1
	exynos9810_store_hex x15
	mov	w10, #'\n'
	strb	w10, [x11], #1
	mov	w10, #'P'
	strb	w10, [x11], #1
	mov	w10, #'='
	strb	w10, [x11], #1
	exynos9810_store_hex x16
	mov	w10, #'\n'
	strb	w10, [x11], #1
	mov	w10, #'S'
	strb	w10, [x11], #1
	mov	w10, #'='
	strb	w10, [x11], #1
	exynos9810_store_hex x17
	mov	w10, #'\n'
	strb	w10, [x11]
	mov	w10, #103
	str	w10, [x9, #8]
	tst	x17, #SCTLR_ELx_C
	b.eq	2f
	dc	cvac, x9
	add	x10, x9, #64
	dc	cvac, x10
2:	dsb	sy
3:	wfe
	wfi
	b	3b
SYM_CODE_END(exynos9810_mmu_fault)
#endif

/*
 * end early head section, begin head code that is also used for
 * hotplug and needs to have the same protections as the text region
 */
	.section ".idmap.text","a"

/*
 * Starting from EL2 or EL1, configure the CPU to execute at the highest
 * reachable EL supported by the kernel in a chosen default state. If dropping
 * from EL2 to EL1, configure EL2 before configuring EL1.
 *
 * Since we cannot always rely on ERET synchronizing writes to sysregs (e.g. if
 * SCTLR_ELx.EOS is clear), we place an ISB prior to ERET.
 *
 * Returns either BOOT_CPU_MODE_EL1 or BOOT_CPU_MODE_EL2 in x0 if
 * booted in EL1 or EL2 respectively, with the top 32 bits containing
 * potential context flags. These flags are *not* stored in __boot_cpu_mode.
 *
 * x0: whether we are being called from the primary boot path with the MMU on
 */
SYM_FUNC_START(init_kernel_el)
	mrs	x1, CurrentEL
	cmp	x1, #CurrentEL_EL2
	b.eq	init_el2

SYM_INNER_LABEL(init_el1, SYM_L_LOCAL)
	mov_q	x0, INIT_SCTLR_EL1_MMU_OFF
	pre_disable_mmu_workaround
	msr	sctlr_el1, x0
	isb
	mov_q	x0, INIT_PSTATE_EL1
	msr	spsr_el1, x0
	msr	elr_el1, lr
	mov	w0, #BOOT_CPU_MODE_EL1
	eret

SYM_INNER_LABEL(init_el2, SYM_L_LOCAL)
	msr	elr_el2, lr

	// clean all HYP code to the PoC if we booted at EL2 with the MMU on
	cbz	x0, 0f
	adrp	x0, __hyp_idmap_text_start
	adr_l	x1, __hyp_text_end
	adr_l	x2, dcache_clean_poc
	blr	x2

	mov_q	x0, INIT_SCTLR_EL2_MMU_OFF
	pre_disable_mmu_workaround
	msr	sctlr_el2, x0
	isb
0:

	init_el2_hcr	HCR_HOST_NVHE_FLAGS | HCR_ATA
	init_el2_state

	/* Hypervisor stub */
	adr_l	x0, __hyp_stub_vectors
	msr	vbar_el2, x0
	isb

	mov_q	x1, INIT_SCTLR_EL1_MMU_OFF

	mrs	x0, hcr_el2
	and	x0, x0, #HCR_E2H
	cbz	x0, 2f

	/* Set a sane SCTLR_EL1, the VHE way */
	msr_s	SYS_SCTLR_EL12, x1
	mov	x2, #BOOT_CPU_FLAG_E2H
	b	3f

2:
	msr	sctlr_el1, x1
	mov	x2, xzr
3:
	mov	x0, #INIT_PSTATE_EL1
	msr	spsr_el2, x0

	mov	w0, #BOOT_CPU_MODE_EL2
	orr	x0, x0, x2
	eret
SYM_FUNC_END(init_kernel_el)

	/*
	 * This provides a "holding pen" for platforms to hold all secondary
	 * cores are held until we're ready for them to initialise.
	 */
SYM_FUNC_START(secondary_holding_pen)
	mov	x0, xzr
	bl	init_kernel_el			// w0=cpu_boot_mode
	mrs	x2, mpidr_el1
	mov_q	x1, MPIDR_HWID_BITMASK
	and	x2, x2, x1
	adr_l	x3, secondary_holding_pen_release
pen:	ldr	x4, [x3]
	cmp	x4, x2
	b.eq	secondary_startup
	wfe
	b	pen
SYM_FUNC_END(secondary_holding_pen)

	/*
	 * Secondary entry point that jumps straight into the kernel. Only to
	 * be used where CPUs are brought online dynamically by the kernel.
	 */
SYM_FUNC_START(secondary_entry)
	mov	x0, xzr
	bl	init_kernel_el			// w0=cpu_boot_mode
	b	secondary_startup
SYM_FUNC_END(secondary_entry)

#ifdef CONFIG_EXYNOS9810_MONGOOSE_CPUS
/*
 * Samsung's firmware enters CPUCL1 at EL1 on production Exynos9810
 * devices. Preserve the direct EL1 setup used by the downstream entry,
 * while retaining the generic EL2 initializer for alternate firmware.
 */
SYM_CODE_START(exynos9810_secondary_entry)
	msr	spsel, #1
	mrs	x0, CurrentEL
	cmp	x0, #CurrentEL_EL2
	b.eq	.Lexynos9810_m3_init_el2
	mov_q	x0, INIT_SCTLR_EL1_MMU_OFF
	msr	sctlr_el1, x0
	mov	w0, #BOOT_CPU_MODE_EL1
	isb
	b	.Lexynos9810_m3_el_ready
.Lexynos9810_m3_init_el2:
	mov	x0, xzr
	bl	init_kernel_el
.Lexynos9810_m3_el_ready:
	b	secondary_startup
SYM_CODE_END(exynos9810_secondary_entry)
#endif

SYM_FUNC_START_LOCAL(secondary_startup)
	/*
	 * Common entry point for secondary CPUs.
	 */
	mov	x20, x0				// preserve boot mode

#ifdef CONFIG_ARM64_VA_BITS_52
alternative_if ARM64_HAS_VA52
	bl	__cpu_secondary_check52bitva
alternative_else_nop_endif
#endif

	bl	__cpu_setup			// initialise processor
	adrp	x1, swapper_pg_dir
	adrp	x2, idmap_pg_dir
	bl	__enable_mmu
	ldr	x8, =__secondary_switched
	br	x8
SYM_FUNC_END(secondary_startup)

	.text
SYM_FUNC_START_LOCAL(__secondary_switched)
	mov	x0, x20
	bl	set_cpu_boot_mode_flag

	mov	x0, x20
	bl	finalise_el2

	str_l	xzr, __early_cpu_boot_status, x3
	adr_l	x5, vectors
	msr	vbar_el1, x5
	isb

	adr_l	x0, secondary_data
	ldr	x2, [x0, #CPU_BOOT_TASK]
	cbz	x2, __secondary_too_slow

#ifdef CONFIG_EXYNOS9810_MONGOOSE_CPUS
	ldr	x1, [x0, #CPU_BOOT_STACK]
	init_cpu_task_from_stack x2, x1, x1, x3
#else
	init_cpu_task x2, x1, x3
#endif

#ifdef CONFIG_ARM64_PTR_AUTH
	ptrauth_keys_init_cpu x2, x3, x4, x5
#endif

	bl	secondary_start_kernel
	ASM_BUG()
SYM_FUNC_END(__secondary_switched)

SYM_FUNC_START_LOCAL(__secondary_too_slow)
	wfe
	wfi
	b	__secondary_too_slow
SYM_FUNC_END(__secondary_too_slow)

/*
 * Sets the __boot_cpu_mode flag depending on the CPU boot mode passed
 * in w0. See arch/arm64/include/asm/virt.h for more info.
 */
SYM_FUNC_START_LOCAL(set_cpu_boot_mode_flag)
	adr_l	x1, __boot_cpu_mode
	cmp	w0, #BOOT_CPU_MODE_EL2
	b.ne	1f
	add	x1, x1, #4
1:	str	w0, [x1]			// Save CPU boot mode
	ret
SYM_FUNC_END(set_cpu_boot_mode_flag)

/*
 * The booting CPU updates the failed status @__early_cpu_boot_status,
 * with MMU turned off.
 *
 * update_early_cpu_boot_status tmp, status
 *  - Corrupts tmp1, tmp2
 *  - Writes 'status' to __early_cpu_boot_status and makes sure
 *    it is committed to memory.
 */

	.macro	update_early_cpu_boot_status status, tmp1, tmp2
	mov	\tmp2, #\status
	adr_l	\tmp1, __early_cpu_boot_status
	str	\tmp2, [\tmp1]
	dmb	sy
	dc	ivac, \tmp1			// Invalidate potentially stale cache line
	.endm

/*
 * Enable the MMU.
 *
 *  x0  = SCTLR_EL1 value for turning on the MMU.
 *  x1  = TTBR1_EL1 value
 *  x2  = ID map root table address
 *
 * Returns to the caller via x30/lr. This requires the caller to be covered
 * by the .idmap.text section.
 *
 * Checks if the selected granule size is supported by the CPU.
 * If it isn't, park the CPU
 */
	.section ".idmap.text","a"
SYM_FUNC_START(__enable_mmu)
	mrs	x3, ID_AA64MMFR0_EL1
	ubfx	x3, x3, #ID_AA64MMFR0_EL1_TGRAN_SHIFT, 4
	cmp     x3, #ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MIN
	b.lt    __no_granule_support
	cmp     x3, #ID_AA64MMFR0_EL1_TGRAN_SUPPORTED_MAX
	b.gt    __no_granule_support
	phys_to_ttbr x2, x2
	msr	ttbr0_el1, x2			// load TTBR0
	load_ttbr1 x1, x1, x3

	set_sctlr_el1	x0

	ret
SYM_FUNC_END(__enable_mmu)

#ifdef CONFIG_ARM64_VA_BITS_52
SYM_FUNC_START(__cpu_secondary_check52bitva)
#ifndef CONFIG_ARM64_LPA2
	mrs_s	x0, SYS_ID_AA64MMFR2_EL1
	and	x0, x0, ID_AA64MMFR2_EL1_VARange_MASK
	cbnz	x0, 2f
#else
	mrs	x0, id_aa64mmfr0_el1
	sbfx	x0, x0, #ID_AA64MMFR0_EL1_TGRAN_SHIFT, 4
	cmp	x0, #ID_AA64MMFR0_EL1_TGRAN_LPA2
	b.ge	2f
#endif

	update_early_cpu_boot_status \
		CPU_STUCK_IN_KERNEL | CPU_STUCK_REASON_52_BIT_VA, x0, x1
1:	wfe
	wfi
	b	1b

2:	ret
SYM_FUNC_END(__cpu_secondary_check52bitva)
#endif

SYM_FUNC_START_LOCAL(__no_granule_support)
	/* Indicate that this CPU can't boot and is stuck in the kernel */
	update_early_cpu_boot_status \
		CPU_STUCK_IN_KERNEL | CPU_STUCK_REASON_NO_GRAN, x1, x2
1:
	wfe
	wfi
	b	1b
SYM_FUNC_END(__no_granule_support)

SYM_FUNC_START_LOCAL(__primary_switch)
	/* 07: execution reached the final pre-MMU switch. */
	exynos9810_boot_marker 0x3730
	adrp	x1, reserved_pg_dir
	adrp	x2, __pi_init_idmap_pg_dir
	bl	__enable_mmu

	adrp	x1, early_init_stack
	mov	sp, x1
	mov	x29, xzr
	mov	x0, x20				// pass the full boot status
	mov	x1, x21				// pass the FDT
	bl	__pi_early_map_kernel		// Map and relocate the kernel
	/* 0D: the early virtual kernel mapping was created. */
	exynos9810_update_boot_marker 0x4430, 1

	ldr	x8, =__primary_switched
	adrp	x0, KERNEL_START		// __pa(KERNEL_START)
	br	x8
SYM_FUNC_END(__primary_switch)
