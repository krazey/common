#
# arch/arm64/boot/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2012, ARM Ltd.
# Author: Will Deacon <will.deacon@arm.com>
#
# Based on the ia64 boot/Makefile.
#

OBJCOPYFLAGS_Image :=-O binary -R .note -R .note.gnu.build-id -R .comment -S

targets := Image Image.bz2 Image.gz Image.lz4 Image.lzma Image.lzo \
	Image.zst Image.xz image.fit

targets += dt.img Image.samsung

# Only explicitly selected Samsung boards belong in the DTBH container.
# Do not sweep this directory: ARCH_EXYNOS also builds unrelated SoCs.
SAMSUNG_DTB_NAMES ?= exynos/exynos9810-starlte.dtb
SAMSUNG_DTB_OBJS := $(addprefix $(obj)/dts/,$(SAMSUNG_DTB_NAMES))
SAMSUNG_PYTHON3 ?= python3
SAMSUNG_BOOT_PAGESIZE ?= 2048
samsung_bootimg := $(srctree)/scripts/samsung_bootimg.py

quiet_cmd_samsung_dtbh = DTBH    $@
      cmd_samsung_dtbh = $(SAMSUNG_PYTHON3) $(samsung_bootimg) dtbh \
		--pagesize $(SAMSUNG_BOOT_PAGESIZE) --output $@ \
		$(SAMSUNG_DTB_OBJS)

quiet_cmd_samsung_bundle = SAMSUNG $@
      cmd_samsung_bundle = $(SAMSUNG_PYTHON3) $(samsung_bootimg) bundle \
		--pagesize $(SAMSUNG_BOOT_PAGESIZE) --kernel $(obj)/Image \
		--dt $(obj)/dt.img --output $@

$(obj)/dt.img: $(SAMSUNG_DTB_OBJS) $(samsung_bootimg) FORCE
	$(call if_changed,samsung_dtbh)

$(obj)/Image.samsung: $(obj)/Image $(obj)/dt.img $(samsung_bootimg) FORCE
	$(call if_changed,samsung_bundle)

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/Image.bz2: $(obj)/Image FORCE
	$(call if_changed,bzip2)

$(obj)/Image.gz: $(obj)/Image FORCE
	$(call if_changed,gzip)

$(obj)/Image.lz4: $(obj)/Image FORCE
	$(call if_changed,lz4)

$(obj)/Image.lzma: $(obj)/Image FORCE
	$(call if_changed,lzma)

$(obj)/Image.lzo: $(obj)/Image FORCE
	$(call if_changed,lzo)

$(obj)/Image.zst: $(obj)/Image FORCE
	$(call if_changed,zstd)

$(obj)/Image.xz: $(obj)/Image FORCE
	$(call if_changed,xzkern)

$(obj)/image.fit: $(obj)/Image $(obj)/dts/dtbs-list FORCE
	$(call if_changed,fit)

EFI_ZBOOT_PAYLOAD	:= Image
EFI_ZBOOT_BFD_TARGET	:= elf64-littleaarch64
EFI_ZBOOT_MACH_TYPE	:= ARM64
EFI_ZBOOT_FORWARD_CFI	:= $(CONFIG_ARM64_BTI_KERNEL)

EFI_ZBOOT_OBJCOPY_FLAGS	= --add-symbol zboot_code_size=0x$$( \
				$(NM) vmlinux|grep _kernel_codesize|cut -d' ' -f1)

include $(srctree)/drivers/firmware/efi/libstub/Makefile.zboot
