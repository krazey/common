// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exynos9810 boot framebuffer compatibility driver
 *
 * The Samsung bootloader leaves the internal panel and DECON scanout active.
 * Keep using that scanout while the native DECON, DPP and DSIM drivers are
 * being brought up, and provide the legacy fbdev ABI expected by Android's
 * Exynos9810 hardware composer.
 */

#include <linux/dma-buf.h>
#include <linux/dma-fence.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/fb.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iosys-map.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_platform.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#define EXYNOS9810_BOOTFB_FPS		60
#define EXYNOS9810_BOOTFB_MAX_WINDOWS	6
#define EXYNOS9810_BOOTFB_PLANES		3
#define EXYNOS9810_BOOTFB_CHIP_ID	9810

#define EXYNOS9810_BOOTFB_NATIVE_SLOT_COUNT	2U
#define EXYNOS9810_BOOTFB_NATIVE_SLOT0_IOVA	0x24000000ULL
#define EXYNOS9810_BOOTFB_NATIVE_SLOT1_IOVA	0x28000000ULL
#define EXYNOS9810_BOOTFB_NATIVE_WAIT_NS	(100ULL * NSEC_PER_MSEC)
#define EXYNOS9810_BOOTFB_NATIVE_IDLE_WAIT_NS	(50ULL * NSEC_PER_MSEC)

#define EXYNOS9810_DPU_OP_STATUS		(1U << 2)

#define EXYNOS9810_IDMA_IRQ_FRAMEDONE		(1U << 16)
#define EXYNOS9810_IDMA_IRQ_CLEAR_MASK		(0x1fbU << 16)
#define EXYNOS9810_IDMA_IRQ_ENABLE		(1U << 0)
#define EXYNOS9810_IDMA_IRQ_FRAMEDONE_MASK	(1U << 1)

#define EXYNOS9810_DPP_IRQ_FRAMEDONE		(1U << 16)
#define EXYNOS9810_DPP_IRQ_CLEAR_MASK		(0x21U << 16)
#define EXYNOS9810_DPP_IRQ_ENABLE		(1U << 0)
#define EXYNOS9810_DPP_IRQ_FRAMEDONE_MASK	(1U << 1)
#define EXYNOS9810_IDMA_IMG_FORMAT_MASK		(0x1fU << 11)
#define EXYNOS9810_IDMA_IMG_FORMAT_CODE(_v)	(((_v) & 0x1fU) << 11)
#define EXYNOS9810_DPP_RGB_FORMAT_MASK		0x7U
#define EXYNOS9810_DPP_ALPHA_SEL			(1U << 3)
#define EXYNOS9810_BOOTFB_G0_HW_CHANNEL		5U
#define EXYNOS9810_BOOTFB_G0_WINDOW		5U

/*
 * Minimal DECON0 register subset used only to advance the bootloader-owned
 * command-mode pipeline.  Do not grow this into a second native DECON driver.
 */
#define EXYNOS9810_DECON_GLOBAL_CONTROL		0x0000
#define EXYNOS9810_DECON_GLOBAL_CMD_MODE		(1U << 8)
#define EXYNOS9810_DECON_GLOBAL_RUN_STATUS	(1U << 4)
#define EXYNOS9810_DECON_GLOBAL_EN		(1U << 1)
#define EXYNOS9810_DECON_GLOBAL_EN_F		(1U << 0)

#define EXYNOS9810_DECON_SHADOW_UPDATE		0x0060
#define EXYNOS9810_DECON_SHADOW_UPDATE_GLOBAL	(1U << 31)
#define EXYNOS9810_DECON_OUTFIFO_DATA_ORDER	0x0130
#define EXYNOS9810_DECON_PIXEL_ORDER_SHIFT	4
#define EXYNOS9810_DECON_PIXEL_ORDER_MASK	(0x7U << 4)
#define EXYNOS9810_DECON_PIXEL_ORDER_RGB	0U
#define EXYNOS9810_DECON_SHADOW_UPDATE_WIN(_win)	(1U << (_win))

#define EXYNOS9810_DECON_TRIGGER_CONTROL		0x0070
#define EXYNOS9810_DECON_SW_TRIGGER		(1U << 8)
#define EXYNOS9810_DECON_HW_TRIGGER_MASK		(1U << 4)
#define EXYNOS9810_DECON_HW_TRIGGER_EN		(1U << 0)

#define EXYNOS9810_DECON_DATA_PATH0		0x0214
#define EXYNOS9810_DECON_DATA_PATH1		0x0218
#define EXYNOS9810_DECON_DATA_PATH2		0x0230
#define EXYNOS9810_DECON_DSIM_CONNECTION		0x0250
#define EXYNOS9810_DECON_FRAME_COUNT		0x02a0

#define EXYNOS9810_IDMA_G0_ENABLE		0x0000
#define EXYNOS9810_IDMA_G0_IRQ			0x0004
#define EXYNOS9810_IDMA_G0_INPUT_CONTROL		0x0008
#define EXYNOS9810_IDMA_G0_OUTPUT_CONTROL	0x000c
#define EXYNOS9810_IDMA_G0_SOURCE_SIZE		0x0010
#define EXYNOS9810_IDMA_G0_SOURCE_OFFSET		0x0014
#define EXYNOS9810_IDMA_G0_IMAGE_SIZE		0x0018
#define EXYNOS9810_IDMA_G0_BASE_Y		0x0040
#define EXYNOS9810_IDMA_G0_BASE_C		0x0044
#define EXYNOS9810_IDMA_G0_CONFIG_ERROR		0x0870

#define EXYNOS9810_DPP_G0_ENABLE			0x0000
#define EXYNOS9810_DPP_G0_IRQ			0x0004
#define EXYNOS9810_DPP_G0_INPUT_CONTROL		0x0008
#define EXYNOS9810_DPP_G0_CONFIG_ERROR		0x0d08

enum exynos9810_bootfb_window_state {
	EXYNOS9810_WIN_DISABLED = 0,
	EXYNOS9810_WIN_COLOR,
	EXYNOS9810_WIN_BUFFER,
	EXYNOS9810_WIN_UPDATE,
	EXYNOS9810_WIN_CURSOR,
};

enum exynos9810_bootfb_pixel_format {
	EXYNOS9810_FMT_ARGB8888 = 0,
	EXYNOS9810_FMT_ABGR8888,
	EXYNOS9810_FMT_RGBA8888,
	EXYNOS9810_FMT_BGRA8888,
	EXYNOS9810_FMT_XRGB8888,
	EXYNOS9810_FMT_XBGR8888,
	EXYNOS9810_FMT_RGBX8888,
	EXYNOS9810_FMT_BGRX8888,
	EXYNOS9810_FMT_RGBA5551,
	EXYNOS9810_FMT_BGRA5551,
	EXYNOS9810_FMT_ABGR4444,
	EXYNOS9810_FMT_RGBA4444,
	EXYNOS9810_FMT_BGRA4444,
	EXYNOS9810_FMT_RGB565,
};

enum exynos9810_bootfb_blending {
	EXYNOS9810_BLENDING_NONE = 0,
	EXYNOS9810_BLENDING_PREMULT,
	EXYNOS9810_BLENDING_COVERAGE,
};

enum exynos9810_bootfb_rotation {
	EXYNOS9810_ROT_NORMAL = 0,
	EXYNOS9810_ROT_XFLIP,
	EXYNOS9810_ROT_YFLIP,
	EXYNOS9810_ROT_180,
	EXYNOS9810_ROT_90,
	EXYNOS9810_ROT_90_XFLIP,
	EXYNOS9810_ROT_90_YFLIP,
	EXYNOS9810_ROT_270,
};

struct exynos9810_bootfb_win_rect {
	int x;
	int y;
	__u32 w;
	__u32 h;
};

struct exynos9810_bootfb_frame {
	int x;
	int y;
	__u32 w;
	__u32 h;
	__u32 f_w;
	__u32 f_h;
};

struct exynos9810_bootfb_dpp_params {
	__u64 addr[EXYNOS9810_BOOTFB_PLANES];
	int rot;
	int eq_mode;
	int comp_src;
	int hdr_std;
	__u32 min_luminance;
	__u32 max_luminance;
};

struct exynos9810_bootfb_win_config {
	int state;
	union {
		__u32 color;
		struct {
			int fd_idma[EXYNOS9810_BOOTFB_PLANES];
			int acq_fence;
			int rel_fence;
			int plane_alpha;
			int blending;
			int idma_type;
			int format;
			struct exynos9810_bootfb_dpp_params dpp_parm;
			struct exynos9810_bootfb_win_rect block_area;
			struct exynos9810_bootfb_win_rect transparent_area;
			struct exynos9810_bootfb_win_rect opaque_area;
			struct exynos9810_bootfb_frame src;
		};
	};
	struct exynos9810_bootfb_frame dst;
	bool protection;
	bool compression;
};

struct exynos9810_bootfb_config_data {
	int present_fence;
	int fd_odma;
	__u32 fps;
	struct exynos9810_bootfb_win_config
		config[EXYNOS9810_BOOTFB_MAX_WINDOWS + 1];
};

struct exynos9810_bootfb_lcd_res {
	__u32 width;
	__u32 height;
	__u32 dsc_en;
	__u32 dsc_width;
	__u32 dsc_height;
};

struct exynos9810_bootfb_lcd_mres {
	__u32 mres_en;
	__u32 mres_number;
	struct exynos9810_bootfb_lcd_res res_info[5];
};

struct exynos9810_bootfb_disp_info {
	int ver;
	int psr_mode;
	struct exynos9810_bootfb_lcd_mres mres_info;
	__u32 chip_ver;
	__u8 reserved[128];
};

struct exynos9810_bootfb_display_mode {
	__u32 index;
	__u32 width;
	__u32 height;
	__u32 mm_width;
	__u32 mm_height;
	__u32 fps;
	__u32 group;
};

struct exynos9810_bootfb_user_window {
	int x;
	int y;
};

static_assert(sizeof(struct exynos9810_bootfb_win_config) == 200);
static_assert(sizeof(struct exynos9810_bootfb_config_data) == 1416);
static_assert(sizeof(struct exynos9810_bootfb_disp_info) == 248);

#define S3CFB_SET_VSYNC_INT	_IOW('F', 206, __u32)
#define S3CFB_DECON_SELF_REFRESH	_IOW('F', 207, __u32)
#define S3CFB_WIN_CONFIG		_IOW('F', 209, \
				     struct exynos9810_bootfb_config_data)
#define S3CFB_WIN_POSITION	_IOW('F', 222, \
				     struct exynos9810_bootfb_user_window)
#define S3CFB_POWER_MODE		_IOW('F', 223, __u32)
#define EXYNOS_DISP_INFO		_IOW('F', 260, \
				     struct exynos9810_bootfb_disp_info)
#define EXYNOS_GET_DISPLAY_MODE_NUM \
	_IOW('F', 700, __u32)
#define EXYNOS_GET_DISPLAY_MODE \
	_IOW('F', 701, struct exynos9810_bootfb_display_mode)
#define EXYNOS_SET_DISPLAY_MODE \
	_IOW('F', 702, struct exynos9810_bootfb_display_mode)
#define EXYNOS_GET_DISPLAY_CURRENT_MODE \
	_IOW('F', 705, __u32)

struct exynos9810_bootfb_native_slot {
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	dma_addr_t iova;
	size_t mapped;
	bool valid;
};

struct exynos9810_bootfb {
	struct device *dev;
	struct fb_info *info;
	/*
	 * Preserve the bootloader framebuffer at its physical address while
	 * preparing a translated domain for native dma-buf scanout. The display
	 * master changes domains only after the identity mapping is complete.
	 */
	struct iommu_domain *prepared_domain;
	struct platform_device *iommu_supplier;
	dma_addr_t prepared_iova;
	phys_addr_t prepared_phys;
	phys_addr_t prepared_first_phys;
	phys_addr_t prepared_last_phys;
	size_t prepared_size;
	int prepare_iommu_error;
	int attach_iommu_error;
	bool translated_attached;
	bool iommu_supplier_active;

	struct exynos9810_bootfb_native_slot native_slots[EXYNOS9810_BOOTFB_NATIVE_SLOT_COUNT];
	bool native_present_enabled;
	bool native_present_hw_active;
	int native_present_active_slot;
	int native_present_error;
	u32 native_present_saved_input;
	u32 native_present_saved_base_y;
	u32 native_present_saved_base_c;
	atomic_t native_present_count;
	atomic_t native_present_fallback_count;
	atomic64_t native_present_last_ns;
	atomic64_t native_present_max_ns;
	atomic64_t native_present_last_map_ns;
	atomic64_t native_present_last_wait_ns;
	atomic_t native_present_completion_count;
	atomic_t native_present_completion_timeout_count;
	atomic_t native_present_release_fence_count;
	atomic64_t native_present_last_idle_ns;
	atomic64_t native_present_max_idle_ns;
	int idma_g0_irq;
	int dpp_g0_irq;
	bool g0_irqs_available;
	bool g0_irqs_enabled;
	atomic_t idma_g0_irq_count;
	atomic_t dpp_g0_irq_count;
	atomic_t idma_g0_framedone_count;
	atomic_t dpp_g0_framedone_count;
	atomic_t idma_g0_irq_error_count;
	atomic_t dpp_g0_irq_error_count;
	atomic_t native_irq_sample_count;
	atomic_t native_idma_irq_miss_count;
	atomic_t native_dpp_irq_miss_count;
	atomic64_t native_irq_arm_ns;
	atomic64_t idma_g0_last_irq_ns;
	atomic64_t dpp_g0_last_irq_ns;
	atomic64_t idma_g0_last_latency_ns;
	atomic64_t dpp_g0_last_latency_ns;
	atomic64_t idma_g0_max_latency_ns;
	atomic64_t dpp_g0_max_latency_ns;
	wait_queue_head_t native_dpp_irq_wait;
	atomic_t native_dpp_irq_wait_count;
	atomic_t native_dpp_irq_wait_timeout_count;
	atomic_t native_dpp_irq_early_busy_count;
	atomic64_t native_dpp_irq_last_wait_ns;
	atomic64_t native_dpp_irq_max_wait_ns;
	bool native_async_pending;
	int native_async_new_slot;
	int native_async_old_slot;
	int native_async_error;
	int native_async_dpp_before;
	struct dma_fence *native_async_fence;
	struct work_struct native_async_complete_work;
	wait_queue_head_t native_async_wait;
	atomic_t native_async_complete_state;
	u64 native_async_fence_context;
	atomic64_t native_async_fence_seqno;
	atomic_t native_async_submit_count;
	atomic_t native_async_signal_count;
	atomic_t native_async_finalize_count;
	atomic_t native_async_error_count;
	atomic_t native_async_early_busy_count;
	atomic_t native_async_previous_wait_count;
	atomic_t native_async_previous_wait_timeout_count;
	atomic_t native_async_irq_missed_count;
	atomic64_t native_async_submit_start_ns;
	atomic64_t native_async_last_submit_ns;
	atomic64_t native_async_max_submit_ns;
	atomic64_t native_async_last_complete_ns;
	atomic64_t native_async_max_complete_ns;
	atomic_t native_async_irq_signal_count;
	atomic_t native_async_worker_signal_count;
	atomic_t native_async_irq_busy_fallback_count;
	atomic_t native_async_irq_error_signal_count;
	atomic_t native_async_timeout_signal_count;
	atomic64_t native_async_last_irq_complete_ns;
	atomic64_t native_async_max_irq_complete_ns;
	bool native_auto_enabled;
	bool native_auto_active;
	bool native_auto_blocked;
	int native_auto_last_error;
	atomic_t native_auto_attempt_count;
	atomic_t native_auto_entry_count;
	atomic_t native_auto_failure_count;
	atomic_t native_auto_restore_count;
	atomic64_t native_auto_entry_ns;
	atomic64_t native_auto_restore_ns;
	void __iomem *screen;
	void __iomem *decon;
	void __iomem *dpp_g0;
	void __iomem *idma_g0;
	u32 *shadow;
	size_t screen_size;
	u32 width;
	u32 height;
	u32 stride;
	u32 width_mm;
	u32 height_mm;
	/* Serializes window composition into the preserved scanout. */
	struct mutex lock;
	struct hrtimer vsync_timer;
	struct work_struct vsync_notify_work;
	struct work_struct fbdev_refresh_work;
	atomic_t fbdev_open_count;
	atomic_t fbdev_refresh_count;
	atomic_t fbdev_refresh_error_count;
	bool decon_rgb_order_initialized;
	u32 decon_rgb_order_saved;
	atomic_t decon_rgb_order_fix_count;
	bool fbdev_refresh_enabled;
	bool fbdev_hwc_seen;
	bool fbdev_refresh_logged;
	ktime_t vsync_period;
	wait_queue_head_t vsync_wait;
	atomic64_t vsync_timestamp;
	atomic64_t vsync_sequence;
	atomic_t present_count;
	bool vsync_enabled;
	bool decon_state_logged;
	atomic_t decon_kick_count;
	atomic_t decon_kick_failures;

	/*
	 * Fast-blit state.  The general software compositor remains available for
	 * configurations which are not one opaque/premultiplied full-screen layer.
	 */
	u32 *line;
	u32 *source_shadow;
	bool shadow_valid;
	bool source_shadow_valid;
	bool fast_path_logged;
	bool generic_path_logged;
	atomic_t fast_present_count;
	atomic_t generic_present_count;
	atomic_t damage_hint_count;
	atomic_t damage_frame_count;
	atomic_t last_damage_hint;
	atomic_t last_damage_used;
	atomic_t last_damage_y;
	atomic_t last_damage_h;
	atomic_t last_scan_y;
	atomic_t last_scan_rows;
	atomic_t last_source_changed_rows;
	atomic_t last_changed_rows;
	atomic64_t last_blit_bytes;
	atomic64_t last_blit_ns;
	atomic64_t max_blit_ns;
	atomic64_t last_fence_ns;
	atomic64_t last_begin_ns;
	atomic64_t last_map_ns;
	atomic64_t last_scan_ns;
	atomic64_t last_end_ns;
	u32 source_shadow_stride;
	int source_shadow_format;
	int last_fast_format;
};

static void
exynos9810_bootfb_log_decon(struct exynos9810_bootfb *bootfb,
			    const char *stage)
{
	dev_info(bootfb->dev,
		 "E981D: DECON %s global=%#010x shadow=%#010x "
		 "trigger=%#010x path0=%#010x path1=%#010x "
		 "path2=%#010x dsim=%#010x frame=%u\n",
		 stage,
		 readl(bootfb->decon + EXYNOS9810_DECON_GLOBAL_CONTROL),
		 readl(bootfb->decon + EXYNOS9810_DECON_SHADOW_UPDATE),
		 readl(bootfb->decon + EXYNOS9810_DECON_TRIGGER_CONTROL),
		 readl(bootfb->decon + EXYNOS9810_DECON_DATA_PATH0),
		 readl(bootfb->decon + EXYNOS9810_DECON_DATA_PATH1),
		 readl(bootfb->decon + EXYNOS9810_DECON_DATA_PATH2),
		 readl(bootfb->decon + EXYNOS9810_DECON_DSIM_CONNECTION),
		 readl(bootfb->decon + EXYNOS9810_DECON_FRAME_COUNT));
}

static void
exynos9810_bootfb_ensure_decon_rgb_order(struct exynos9810_bootfb *bootfb)
{
	u32 order;
	u32 reg;

	reg = readl(
		bootfb->decon + EXYNOS9810_DECON_OUTFIFO_DATA_ORDER);
	order = (reg & EXYNOS9810_DECON_PIXEL_ORDER_MASK) >>
		EXYNOS9810_DECON_PIXEL_ORDER_SHIFT;

	if (!bootfb->decon_rgb_order_initialized) {
		bootfb->decon_rgb_order_initialized = true;
		bootfb->decon_rgb_order_saved = order;
		dev_info(
			bootfb->dev,
			"E981D: preserved DECON pixel order=%u; driver uses RGB(0)\n",
			order);
	}

	if (order == EXYNOS9810_DECON_PIXEL_ORDER_RGB)
		return;

	reg &= ~EXYNOS9810_DECON_PIXEL_ORDER_MASK;
	reg |= EXYNOS9810_DECON_PIXEL_ORDER_RGB <<
		EXYNOS9810_DECON_PIXEL_ORDER_SHIFT;
	writel(
		reg,
		bootfb->decon + EXYNOS9810_DECON_OUTFIFO_DATA_ORDER);
	wmb();
	atomic_inc(&bootfb->decon_rgb_order_fix_count);
}

static int exynos9810_bootfb_kick_decon(struct exynos9810_bootfb *bootfb)
{
	exynos9810_bootfb_ensure_decon_rgb_order(bootfb);
	u32 frame_before;
	u32 frame_after;
	u32 global;
	u32 trigger;
	u32 trigger_active;
	u32 trigger_masked;
	u32 value;
	bool command_mode;
	bool hardware_trigger;
	int ret;

	global = readl(bootfb->decon + EXYNOS9810_DECON_GLOBAL_CONTROL);
	trigger = readl(bootfb->decon + EXYNOS9810_DECON_TRIGGER_CONTROL);
	frame_before = readl(bootfb->decon + EXYNOS9810_DECON_FRAME_COUNT);
	command_mode = !!(global & EXYNOS9810_DECON_GLOBAL_CMD_MODE);

	hardware_trigger = command_mode &&
		!!(trigger & (EXYNOS9810_DECON_HW_TRIGGER_EN |
			      EXYNOS9810_DECON_HW_TRIGGER_MASK));

	if (!bootfb->decon_state_logged)
		exynos9810_bootfb_log_decon(bootfb, "before-kick");

	trigger_masked = trigger &
		~(EXYNOS9810_DECON_HW_TRIGGER_EN |
		  EXYNOS9810_DECON_HW_TRIGGER_MASK |
		  EXYNOS9810_DECON_SW_TRIGGER);

	if (command_mode) {
		if (hardware_trigger)
			trigger_masked |= EXYNOS9810_DECON_HW_TRIGGER_MASK;
		writel(trigger_masked,
		       bootfb->decon + EXYNOS9810_DECON_TRIGGER_CONTROL);
	}

	global |= EXYNOS9810_DECON_GLOBAL_EN |
		  EXYNOS9810_DECON_GLOBAL_EN_F;
	writel(global, bootfb->decon + EXYNOS9810_DECON_GLOBAL_CONTROL);
	writel(
		EXYNOS9810_DECON_SHADOW_UPDATE_GLOBAL |
		EXYNOS9810_DECON_SHADOW_UPDATE_WIN(
			EXYNOS9810_BOOTFB_G0_WINDOW),
		bootfb->decon + EXYNOS9810_DECON_SHADOW_UPDATE);
	readl(bootfb->decon + EXYNOS9810_DECON_GLOBAL_CONTROL);

	ret = readl_poll_timeout_atomic(
		bootfb->decon + EXYNOS9810_DECON_GLOBAL_CONTROL,
		value, value & EXYNOS9810_DECON_GLOBAL_RUN_STATUS,
		10, 20000);
	if (ret)
		goto restore_trigger;

	if (command_mode) {
		trigger_active = trigger_masked;
		if (hardware_trigger) {
			trigger_active &= ~EXYNOS9810_DECON_HW_TRIGGER_MASK;
			trigger_active |= EXYNOS9810_DECON_HW_TRIGGER_EN;
		} else {
			trigger_active |= EXYNOS9810_DECON_SW_TRIGGER;
		}
		writel(trigger_active,
		       bootfb->decon + EXYNOS9810_DECON_TRIGGER_CONTROL);
		readl(bootfb->decon + EXYNOS9810_DECON_TRIGGER_CONTROL);
	}

	ret = readl_poll_timeout_atomic(
		bootfb->decon + EXYNOS9810_DECON_SHADOW_UPDATE,
		value, !value, 10, 30000);

restore_trigger:
	if (command_mode) {
		writel(trigger_masked,
		       bootfb->decon + EXYNOS9810_DECON_TRIGGER_CONTROL);
		readl(bootfb->decon + EXYNOS9810_DECON_TRIGGER_CONTROL);
	}

	frame_after = readl(bootfb->decon + EXYNOS9810_DECON_FRAME_COUNT);
	if (!bootfb->decon_state_logged) {
		dev_info(bootfb->dev,
			 "E981D: DECON kick mode=%s command=%u ret=%d "
			 "frame=%u->%u\n",
			 hardware_trigger ? "hardware" : "software",
			 command_mode, ret, frame_before, frame_after);
		exynos9810_bootfb_log_decon(
			bootfb, ret ? "after-failed-kick" : "after-kick");
		bootfb->decon_state_logged = true;
	}

	return ret;
}

static int
exynos9810_bootfb_validate_g0_native_runtime(
	struct exynos9810_bootfb *bootfb)
{
	struct iommu_domain *active_domain;
	u32 expected_size;
	u32 idma_input;
	u32 dpp_input;
	u32 path0;
	u32 path1;

	if (!bootfb->idma_g0 || !bootfb->dpp_g0 || !bootfb->decon)
		return -ENODEV;
	if (!bootfb->prepared_domain ||
	    !bootfb->translated_attached ||
	    !bootfb->iommu_supplier ||
	    !bootfb->iommu_supplier_active)
		return -ENODEV;

	active_domain = iommu_get_domain_for_dev(bootfb->dev);
	if (active_domain != bootfb->prepared_domain)
		return -ENODEV;

	path0 = readl(bootfb->decon + EXYNOS9810_DECON_DATA_PATH0);
	path1 = readl(bootfb->decon + EXYNOS9810_DECON_DATA_PATH1);
	if (!((path0 >> (4 * EXYNOS9810_BOOTFB_G0_WINDOW)) & 1U) ||
	    ((path1 >> (4 * EXYNOS9810_BOOTFB_G0_WINDOW)) & 7U) !=
		    EXYNOS9810_BOOTFB_G0_HW_CHANNEL)
		return -EBUSY;

	expected_size = (bootfb->height << 16) | bootfb->width;
	if (readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_SOURCE_SIZE) !=
		    expected_size ||
	    readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_SOURCE_OFFSET) != 0 ||
	    readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_IMAGE_SIZE) !=
		    expected_size)
		return -EINVAL;

	idma_input = readl(
		bootfb->idma_g0 + EXYNOS9810_IDMA_G0_INPUT_CONTROL);
	if ((idma_input & (7U << 8)) || (idma_input & (1U << 7)))
		return -EINVAL;

	dpp_input = readl(
		bootfb->dpp_g0 + EXYNOS9810_DPP_G0_INPUT_CONTROL);
	if ((dpp_input & EXYNOS9810_DPP_RGB_FORMAT_MASK) != 0 ||
	    (dpp_input & EXYNOS9810_DPP_ALPHA_SEL))
		return -EINVAL;

	if (readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_CONFIG_ERROR) ||
	    readl(bootfb->dpp_g0 + EXYNOS9810_DPP_G0_CONFIG_ERROR))
		return -EIO;

	return 0;
}

static int
exynos9810_bootfb_kick_native_g0(struct exynos9810_bootfb *bootfb)
{
	int ret;

	wmb();
	readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_BASE_Y);

	ret = exynos9810_bootfb_kick_decon(bootfb);
	if (ret)
		return ret;

	if (readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_CONFIG_ERROR))
		return -EIO;

	return 0;
}

static u8 exynos9810_bootfb_component(u32 pixel, unsigned int byte)
{
	return (pixel >> (byte * 8)) & 0xff;
}

static u32 exynos9810_bootfb_argb(u8 alpha, u8 red, u8 green, u8 blue)
{
	return ((u32)alpha << 24) | ((u32)red << 16) |
	       ((u32)green << 8) | blue;
}

static int exynos9810_bootfb_read_pixel(const struct iosys_map *map,
					size_t offset, int format, u32 *pixel)
{
	u8 alpha = 0xff;
	u8 red;
	u8 green;
	u8 blue;
	u32 value;
	u16 value16;

	if (format == EXYNOS9810_FMT_RGB565) {
		value16 = iosys_map_rd(map, offset, u16);
		red = ((value16 >> 11) & 0x1f) * 255 / 31;
		green = ((value16 >> 5) & 0x3f) * 255 / 63;
		blue = (value16 & 0x1f) * 255 / 31;
		*pixel = exynos9810_bootfb_argb(alpha, red, green, blue);
		return 0;
	}

	if (format < EXYNOS9810_FMT_ARGB8888 ||
	    format > EXYNOS9810_FMT_BGRX8888)
		return -EOPNOTSUPP;

	value = iosys_map_rd(map, offset, u32);

	switch (format) {
	case EXYNOS9810_FMT_ARGB8888:
		alpha = exynos9810_bootfb_component(value, 0);
		red = exynos9810_bootfb_component(value, 1);
		green = exynos9810_bootfb_component(value, 2);
		blue = exynos9810_bootfb_component(value, 3);
		break;
	case EXYNOS9810_FMT_ABGR8888:
		alpha = exynos9810_bootfb_component(value, 0);
		blue = exynos9810_bootfb_component(value, 1);
		green = exynos9810_bootfb_component(value, 2);
		red = exynos9810_bootfb_component(value, 3);
		break;
	case EXYNOS9810_FMT_RGBA8888:
		red = exynos9810_bootfb_component(value, 0);
		green = exynos9810_bootfb_component(value, 1);
		blue = exynos9810_bootfb_component(value, 2);
		alpha = exynos9810_bootfb_component(value, 3);
		break;
	case EXYNOS9810_FMT_BGRA8888:
		blue = exynos9810_bootfb_component(value, 0);
		green = exynos9810_bootfb_component(value, 1);
		red = exynos9810_bootfb_component(value, 2);
		alpha = exynos9810_bootfb_component(value, 3);
		break;
	case EXYNOS9810_FMT_XRGB8888:
		red = exynos9810_bootfb_component(value, 1);
		green = exynos9810_bootfb_component(value, 2);
		blue = exynos9810_bootfb_component(value, 3);
		break;
	case EXYNOS9810_FMT_XBGR8888:
		blue = exynos9810_bootfb_component(value, 1);
		green = exynos9810_bootfb_component(value, 2);
		red = exynos9810_bootfb_component(value, 3);
		break;
	case EXYNOS9810_FMT_RGBX8888:
		red = exynos9810_bootfb_component(value, 0);
		green = exynos9810_bootfb_component(value, 1);
		blue = exynos9810_bootfb_component(value, 2);
		break;
	case EXYNOS9810_FMT_BGRX8888:
		blue = exynos9810_bootfb_component(value, 0);
		green = exynos9810_bootfb_component(value, 1);
		red = exynos9810_bootfb_component(value, 2);
		break;
	default:
		return -EOPNOTSUPP;
	}

	*pixel = exynos9810_bootfb_argb(alpha, red, green, blue);
	return 0;
}

static u32 exynos9810_bootfb_blend(u32 background, u32 foreground,
				   int mode, unsigned int plane_alpha)
{
	u32 src_a = foreground >> 24;
	u32 dst_a = background >> 24;
	u32 src_r = (foreground >> 16) & 0xff;
	u32 src_g = (foreground >> 8) & 0xff;
	u32 src_b = foreground & 0xff;
	u32 dst_r = (background >> 16) & 0xff;
	u32 dst_g = (background >> 8) & 0xff;
	u32 dst_b = background & 0xff;
	u32 alpha;
	u32 red;
	u32 green;
	u32 blue;

	plane_alpha = min(plane_alpha, 255U);
	if (mode == EXYNOS9810_BLENDING_NONE && plane_alpha == 255)
		return foreground | 0xff000000;

	alpha = src_a * plane_alpha / 255;
	if (mode == EXYNOS9810_BLENDING_PREMULT) {
		src_r = src_r * plane_alpha / 255;
		src_g = src_g * plane_alpha / 255;
		src_b = src_b * plane_alpha / 255;
		red = src_r + dst_r * (255 - alpha) / 255;
		green = src_g + dst_g * (255 - alpha) / 255;
		blue = src_b + dst_b * (255 - alpha) / 255;
	} else {
		red = (src_r * alpha + dst_r * (255 - alpha)) / 255;
		green = (src_g * alpha + dst_g * (255 - alpha)) / 255;
		blue = (src_b * alpha + dst_b * (255 - alpha)) / 255;
	}

	dst_a = alpha + dst_a * (255 - alpha) / 255;
	return exynos9810_bootfb_argb(dst_a, red, green, blue);
}

static int exynos9810_bootfb_wait_fence(int fd)
{
	struct dma_fence *fence;
	signed long ret;

	if (fd < 0)
		return 0;

	fence = sync_file_get_fence(fd);
	if (!fence)
		return -EINVAL;

	ret = dma_fence_wait_timeout(fence, true, msecs_to_jiffies(900));
	dma_fence_put(fence);

	if (ret > 0)
		return 0;
	if (!ret)
		return -ETIMEDOUT;
	return ret;
}

static u32 exynos9810_bootfb_reverse_bytes(u32 value)
{
	return ((value & 0x000000ffU) << 24) |
	       ((value & 0x0000ff00U) << 8) |
	       ((value & 0x00ff0000U) >> 8) |
	       ((value & 0xff000000U) >> 24);
}

static u32 exynos9810_bootfb_swap_red_blue(u32 value)
{
	return (value & 0xff00ff00U) |
	       ((value & 0x000000ffU) << 16) |
	       ((value & 0x00ff0000U) >> 16);
}

static u32 exynos9810_bootfb_convert_pixel(u32 value, int format)
{
	switch (format) {
	case EXYNOS9810_FMT_ARGB8888:
		return exynos9810_bootfb_reverse_bytes(value);
	case EXYNOS9810_FMT_ABGR8888:
		return (value >> 8) | (value << 24);
	case EXYNOS9810_FMT_RGBA8888:
		return exynos9810_bootfb_swap_red_blue(value);
	case EXYNOS9810_FMT_BGRA8888:
		return value;
	case EXYNOS9810_FMT_XRGB8888:
		return exynos9810_bootfb_reverse_bytes(value) | 0xff000000U;
	case EXYNOS9810_FMT_XBGR8888:
		return ((value >> 8) | (value << 24)) | 0xff000000U;
	case EXYNOS9810_FMT_RGBX8888:
		return exynos9810_bootfb_swap_red_blue(value) | 0xff000000U;
	case EXYNOS9810_FMT_BGRX8888:
		return value | 0xff000000U;
	default:
		return 0;
	}
}

static void
exynos9810_bootfb_convert_row(const struct iosys_map *map, size_t offset,
			      int format, bool force_opaque,
			      u32 *destination, u32 pixels)
{
	const u32 *source = NULL;
	u32 value;
	u32 x;

	if (map->is_iomem) {
		for (x = 0; x < pixels; x++) {
			value = iosys_map_rd(map, offset + x * sizeof(u32), u32);
			value = exynos9810_bootfb_convert_pixel(value, format);
			if (force_opaque)
				value |= 0xff000000U;
			destination[x] = value;
		}
		return;
	}

	source = (const u32 *)((const u8 *)map->vaddr + offset);

	switch (format) {
	case EXYNOS9810_FMT_ARGB8888:
		for (x = 0; x < pixels; x++)
			destination[x] =
				exynos9810_bootfb_reverse_bytes(source[x]);
		break;
	case EXYNOS9810_FMT_ABGR8888:
		for (x = 0; x < pixels; x++)
			destination[x] = (source[x] >> 8) |
					 (source[x] << 24);
		break;
	case EXYNOS9810_FMT_RGBA8888:
		for (x = 0; x < pixels; x++)
			destination[x] =
				exynos9810_bootfb_swap_red_blue(source[x]);
		break;
	case EXYNOS9810_FMT_BGRA8888:
		memcpy(destination, source, pixels * sizeof(u32));
		break;
	case EXYNOS9810_FMT_XRGB8888:
		for (x = 0; x < pixels; x++)
			destination[x] =
				exynos9810_bootfb_reverse_bytes(source[x]) |
				0xff000000U;
		break;
	case EXYNOS9810_FMT_XBGR8888:
		for (x = 0; x < pixels; x++)
			destination[x] = ((source[x] >> 8) |
					  (source[x] << 24)) |
					 0xff000000U;
		break;
	case EXYNOS9810_FMT_RGBX8888:
		for (x = 0; x < pixels; x++)
			destination[x] =
				exynos9810_bootfb_swap_red_blue(source[x]) |
				0xff000000U;
		break;
	case EXYNOS9810_FMT_BGRX8888:
		for (x = 0; x < pixels; x++)
			destination[x] = source[x] | 0xff000000U;
		break;
	default:
		return;
	}

	if (force_opaque) {
		for (x = 0; x < pixels; x++)
			destination[x] |= 0xff000000U;
	}
}

static bool
exynos9810_bootfb_fast_config_supported(
	struct exynos9810_bootfb *bootfb,
	const struct exynos9810_bootfb_win_config *config)
{
	const struct exynos9810_bootfb_frame *src = &config->src;
	const struct exynos9810_bootfb_frame *dst = &config->dst;

	if (config->state != EXYNOS9810_WIN_BUFFER &&
	    config->state != EXYNOS9810_WIN_CURSOR)
		return false;
	if (config->protection || config->compression ||
	    config->dpp_parm.rot != EXYNOS9810_ROT_NORMAL)
		return false;
	if (config->fd_idma[0] < 0)
		return false;
	if (config->format < EXYNOS9810_FMT_ARGB8888 ||
	    config->format > EXYNOS9810_FMT_BGRX8888)
		return false;
	if (config->blending != EXYNOS9810_BLENDING_NONE &&
	    config->blending != EXYNOS9810_BLENDING_PREMULT)
		return false;
	if (config->plane_alpha >= 0 && config->plane_alpha < 255)
		return false;

	if (src->x != 0 || src->y != 0 ||
	    src->w != bootfb->width || src->h != bootfb->height ||
	    src->f_w < bootfb->width || src->f_h < bootfb->height)
		return false;
	if (dst->x != 0 || dst->y != 0 ||
	    dst->w != bootfb->width || dst->h != bootfb->height)
		return false;

	return true;
}

static struct exynos9810_bootfb_win_config *
exynos9810_bootfb_find_fast_config(
	struct exynos9810_bootfb *bootfb,
	struct exynos9810_bootfb_config_data *data)
{
	struct exynos9810_bootfb_win_config *candidate = NULL;
	struct exynos9810_bootfb_win_config *config;
	unsigned int i;

	for (i = 0; i < EXYNOS9810_BOOTFB_MAX_WINDOWS; i++) {
		config = &data->config[i];

		if (config->state == EXYNOS9810_WIN_DISABLED ||
		    config->state == EXYNOS9810_WIN_UPDATE)
			continue;

		if (candidate)
			return NULL;
		if (config->state != EXYNOS9810_WIN_BUFFER &&
		    config->state != EXYNOS9810_WIN_CURSOR)
			return NULL;

		candidate = config;
	}

	if (!candidate ||
	    !exynos9810_bootfb_fast_config_supported(bootfb, candidate))
		return NULL;

	return candidate;
}

static void
exynos9810_bootfb_publish_stage_timings(
	struct exynos9810_bootfb *bootfb,
	u64 fence_ns, u64 begin_ns, u64 map_ns, u64 scan_ns, u64 end_ns)
{
	/* Publish only completed-frame timings as one coherent snapshot. */
	atomic64_set(&bootfb->last_fence_ns, fence_ns);
	atomic64_set(&bootfb->last_begin_ns, begin_ns);
	atomic64_set(&bootfb->last_map_ns, map_ns);
	atomic64_set(&bootfb->last_scan_ns, scan_ns);
	atomic64_set(&bootfb->last_end_ns, end_ns);
}

static bool
exynos9810_bootfb_get_damage_rows(
	struct exynos9810_bootfb *bootfb,
	const struct exynos9810_bootfb_config_data *data,
	u32 *damage_y, u32 *damage_h)
{
	const struct exynos9810_bootfb_win_config *update =
		&data->config[EXYNOS9810_BOOTFB_MAX_WINDOWS];
	const struct exynos9810_bootfb_frame *dst = &update->dst;
	s64 right;
	s64 bottom;

	*damage_y = 0;
	*damage_h = bootfb->height;

	if (update->state != EXYNOS9810_WIN_UPDATE ||
	    !dst->w || !dst->h)
		return false;

	right = (s64)dst->x + dst->w;
	bottom = (s64)dst->y + dst->h;

	/*
	 * psr_info deliberately asks HWC for full-width row granularity.  Reject
	 * anything else rather than trusting a partial-width hint that this
	 * compatibility bridge does not consume.
	 */
	if (dst->x != 0 || dst->y < 0 ||
	    dst->w != bootfb->width || right != bootfb->width ||
	    bottom <= dst->y || bottom > bootfb->height)
		return false;

	*damage_y = dst->y;
	*damage_h = dst->h;
	return true;
}

static int
exynos9810_bootfb_blit_fullscreen(
	struct exynos9810_bootfb *bootfb,
	struct exynos9810_bootfb_win_config *config,
	bool damage_hint, u32 damage_y, u32 damage_h,
	bool *damage_used, u32 *scan_y, u32 *scan_rows,
	u32 *source_changed_rows, u32 *changed_rows, size_t *bytes_written)
{
	struct exynos9810_bootfb_frame *src = &config->src;
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(NULL);
	struct dma_buf *dmabuf;
	const u32 *source_row = NULL;
	const u32 *row_data;
	u32 *source_shadow_row = NULL;
	u32 *shadow_row;
	u64 buffer_size;
	u64 stage_start;
	u64 fence_ns = 0;
	u64 begin_ns = 0;
	u64 map_ns = 0;
	u64 scan_ns = 0;
	u64 end_ns = 0;
	size_t offset;
	size_t row_bytes;
	bool force_opaque;
	bool raw_tracking;
	bool source_history_compatible;
	u32 y_start = 0;
	u32 y_end;
	u32 y;
	int ret;

	*damage_used = false;
	*scan_y = 0;
	*scan_rows = bootfb->height;
	*source_changed_rows = 0;
	*changed_rows = 0;
	*bytes_written = 0;
	y_end = bootfb->height;
	row_bytes = bootfb->width * sizeof(u32);
	force_opaque =
		config->blending == EXYNOS9810_BLENDING_NONE;

	stage_start = ktime_get_ns();
	ret = exynos9810_bootfb_wait_fence(config->acq_fence);
	fence_ns = ktime_get_ns() - stage_start;
	if (ret) {
		exynos9810_bootfb_publish_stage_timings(
			bootfb, fence_ns, begin_ns, map_ns, scan_ns, end_ns);
		return ret;
	}

	dmabuf = dma_buf_get(config->fd_idma[0]);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		exynos9810_bootfb_publish_stage_timings(
			bootfb, fence_ns, begin_ns, map_ns, scan_ns, end_ns);
		return ret;
	}

	if (check_mul_overflow((u64)src->f_w, (u64)src->f_h,
			       &buffer_size) ||
	    check_mul_overflow(buffer_size, (u64)sizeof(u32),
			       &buffer_size) ||
	    buffer_size > dmabuf->size) {
		ret = -EINVAL;
		goto put_dmabuf;
	}

	stage_start = ktime_get_ns();
	ret = dma_buf_begin_cpu_access(dmabuf, DMA_FROM_DEVICE);
	begin_ns = ktime_get_ns() - stage_start;
	if (ret)
		goto put_dmabuf;

	stage_start = ktime_get_ns();
	ret = dma_buf_vmap(dmabuf, &map);
	map_ns = ktime_get_ns() - stage_start;
	if (ret)
		goto end_access;

	raw_tracking = bootfb->source_shadow && !map.is_iomem;
	source_history_compatible =
		raw_tracking && bootfb->source_shadow_valid &&
		bootfb->source_shadow_format == config->format &&
		bootfb->source_shadow_stride == src->f_w;

	if (raw_tracking && !source_history_compatible)
		bootfb->source_shadow_valid = false;

	if (damage_hint && source_history_compatible && damage_h &&
	    damage_y < bootfb->height &&
	    damage_h <= bootfb->height - damage_y) {
		y_start = damage_y;
		y_end = damage_y + damage_h;
	}

	*damage_used = y_start != 0 || y_end != bootfb->height;
	*scan_y = y_start;
	*scan_rows = y_end - y_start;

	stage_start = ktime_get_ns();
	for (y = y_start; y < y_end; y++) {
		offset = ((size_t)(src->y + y) * src->f_w + src->x) *
			 sizeof(u32);
		shadow_row = bootfb->shadow + y * bootfb->width;

		/* Compare raw bytes before any channel conversion. */
		if (raw_tracking) {
			source_row = (const u32 *)
				((const u8 *)map.vaddr + offset);
			source_shadow_row =
				bootfb->source_shadow + y * bootfb->width;

			if (bootfb->source_shadow_valid &&
			    !memcmp(source_shadow_row, source_row, row_bytes))
				continue;

			(*source_changed_rows)++;
			exynos9810_bootfb_convert_row(
				&map, offset, config->format, force_opaque,
				bootfb->line, bootfb->width);
			row_data = bootfb->line;
			memcpy(source_shadow_row, source_row, row_bytes);
		} else {
			(*source_changed_rows)++;

			/* Keep direct BGRA copies and converted I/O mappings. */
			if (!map.is_iomem &&
			    config->format == EXYNOS9810_FMT_BGRA8888 &&
			    !force_opaque) {
				row_data = (const u32 *)
					((const u8 *)map.vaddr + offset);
			} else {
				exynos9810_bootfb_convert_row(
					&map, offset, config->format,
					force_opaque, bootfb->line,
					bootfb->width);
				row_data = bootfb->line;
			}
		}

		/* Ignore source-only changes which produce identical output. */
		if (bootfb->shadow_valid &&
		    !memcmp(shadow_row, row_data, row_bytes))
			continue;

		memcpy(shadow_row, row_data, row_bytes);
		memcpy_toio((u8 __iomem *)bootfb->screen +
			    y * bootfb->stride, row_data, row_bytes);
		(*changed_rows)++;
		*bytes_written += row_bytes;
	}
	scan_ns = ktime_get_ns() - stage_start;

	if (raw_tracking) {
		bootfb->source_shadow_valid = true;
		bootfb->source_shadow_format = config->format;
		bootfb->source_shadow_stride = src->f_w;
	} else {
		bootfb->source_shadow_valid = false;
	}

	if (*changed_rows)
		wmb();
	bootfb->shadow_valid = true;
	ret = 0;

	stage_start = ktime_get_ns();
	dma_buf_vunmap(dmabuf, &map);
	dma_buf_end_cpu_access(dmabuf, DMA_FROM_DEVICE);
	end_ns = ktime_get_ns() - stage_start;
	goto put_dmabuf;

end_access:
	stage_start = ktime_get_ns();
	dma_buf_end_cpu_access(dmabuf, DMA_FROM_DEVICE);
	end_ns = ktime_get_ns() - stage_start;
put_dmabuf:
	dma_buf_put(dmabuf);
	exynos9810_bootfb_publish_stage_timings(
		bootfb, fence_ns, begin_ns, map_ns, scan_ns, end_ns);
	return ret;
}

static int exynos9810_bootfb_draw_buffer(struct exynos9810_bootfb *bootfb,
					 struct exynos9810_bootfb_win_config *config)
{
	struct exynos9810_bootfb_frame *src = &config->src;
	struct exynos9810_bootfb_frame *dst = &config->dst;
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(NULL);
	struct dma_buf *dmabuf;
	u32 bytes_per_pixel;
	u32 x_start;
	u32 y_start;
	u32 x_end;
	u32 y_end;
	u32 x;
	u32 y;
	s64 dst_right;
	s64 dst_bottom;
	u64 buffer_size;
	int ret;

	if (config->compression) {
		dev_warn_ratelimited(bootfb->dev,
				     "AFBC window cannot use boot scanout\n");
		return -EOPNOTSUPP;
	}

	if (config->dpp_parm.rot != EXYNOS9810_ROT_NORMAL) {
		dev_warn_ratelimited(bootfb->dev,
				     "rotated window cannot use boot scanout\n");
		return -EOPNOTSUPP;
	}

	if (!src->w || !src->h || !src->f_w || !src->f_h ||
	    !dst->w || !dst->h || config->fd_idma[0] < 0)
		return -EINVAL;

	dst_right = (s64)dst->x + dst->w;
	dst_bottom = (s64)dst->y + dst->h;
	if (src->x < 0 || src->y < 0 ||
	    src->x >= (int)src->f_w || src->y >= (int)src->f_h ||
	    src->w > src->f_w - src->x || src->h > src->f_h - src->y ||
	    dst_right <= 0 || dst_bottom <= 0 ||
	    dst->x >= (int)bootfb->width ||
	    dst->y >= (int)bootfb->height)
		return -EINVAL;

	if (config->format == EXYNOS9810_FMT_RGB565)
		bytes_per_pixel = 2;
	else if (config->format >= EXYNOS9810_FMT_ARGB8888 &&
		 config->format <= EXYNOS9810_FMT_BGRX8888)
		bytes_per_pixel = 4;
	else
		return -EOPNOTSUPP;

	ret = exynos9810_bootfb_wait_fence(config->acq_fence);
	if (ret)
		return ret;

	dmabuf = dma_buf_get(config->fd_idma[0]);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	if (check_mul_overflow((u64)src->f_w, (u64)src->f_h,
			       &buffer_size) ||
	    check_mul_overflow(buffer_size, (u64)bytes_per_pixel,
			       &buffer_size) ||
	    buffer_size > dmabuf->size) {
		ret = -EINVAL;
		goto put_dmabuf;
	}

	ret = dma_buf_begin_cpu_access(dmabuf, DMA_FROM_DEVICE);
	if (ret)
		goto put_dmabuf;

	ret = dma_buf_vmap(dmabuf, &map);
	if (ret)
		goto end_access;

	x_start = max(dst->x, 0);
	y_start = max(dst->y, 0);
	x_end = min_t(s64, dst_right, bootfb->width);
	y_end = min_t(s64, dst_bottom, bootfb->height);

	for (y = y_start; y < y_end; y++) {
		u32 src_y = src->y +
			div_u64((u64)((s64)y - dst->y) * src->h, dst->h);

		for (x = x_start; x < x_end; x++) {
			u32 src_x = src->x +
				div_u64((u64)((s64)x - dst->x) * src->w,
					dst->w);
			size_t offset = ((size_t)src_y * src->f_w + src_x) *
					bytes_per_pixel;
			u32 foreground;
			u32 *background = &bootfb->shadow[y * bootfb->width + x];

			ret = exynos9810_bootfb_read_pixel(&map, offset,
							   config->format,
							&foreground);
			if (ret)
				goto unmap;

			*background = exynos9810_bootfb_blend(*background,
							      foreground,
							   config->blending,
							   config->plane_alpha);
		}
	}

	ret = 0;
unmap:
	dma_buf_vunmap(dmabuf, &map);
end_access:
	dma_buf_end_cpu_access(dmabuf, DMA_FROM_DEVICE);
put_dmabuf:
	dma_buf_put(dmabuf);
	return ret;
}

static void exynos9810_bootfb_draw_color(struct exynos9810_bootfb *bootfb,
					 struct exynos9810_bootfb_win_config *config)
{
	struct exynos9810_bootfb_frame *dst = &config->dst;
	u32 x_start;
	u32 y_start;
	u32 x_end;
	u32 y_end;
	u32 x;
	u32 y;
	s64 dst_right;
	s64 dst_bottom;

	if (!dst->w || !dst->h)
		return;
	dst_right = (s64)dst->x + dst->w;
	dst_bottom = (s64)dst->y + dst->h;
	if (dst_right <= 0 || dst_bottom <= 0 ||
	    dst->x >= (int)bootfb->width ||
	    dst->y >= (int)bootfb->height)
		return;

	x_start = max(dst->x, 0);
	y_start = max(dst->y, 0);
	x_end = min_t(s64, dst_right, bootfb->width);
	y_end = min_t(s64, dst_bottom, bootfb->height);

	for (y = y_start; y < y_end; y++) {
		for (x = x_start; x < x_end; x++) {
			u32 *pixel = &bootfb->shadow[y * bootfb->width + x];

			*pixel = exynos9810_bootfb_blend(*pixel, config->color,
							 EXYNOS9810_BLENDING_COVERAGE,
					config->plane_alpha);
		}
	}
}

static struct sync_file *exynos9810_bootfb_create_present_fence(void)
{
	struct dma_fence *fence;
	struct sync_file *sync_file;

	/* Composition into the preserved scanout has already completed. */
	fence = dma_fence_allocate_private_stub(ktime_get());
	if (!fence)
		return NULL;

	sync_file = sync_file_create(fence);
	dma_fence_put(fence);

	return sync_file;
}

struct exynos9810_bootfb_async_fence {
	struct dma_fence base;
	spinlock_t lock;
};

static const char *
exynos9810_bootfb_async_fence_driver_name(struct dma_fence *fence)
{
	return "exynos9810-bootfb";
}

static const char *
exynos9810_bootfb_async_fence_timeline_name(struct dma_fence *fence)
{
	return "dpp-g0-native";
}

static void
exynos9810_bootfb_async_fence_release(struct dma_fence *fence)
{
	struct exynos9810_bootfb_async_fence *async_fence =
		container_of(fence, struct exynos9810_bootfb_async_fence,
			     base);

	kfree_rcu(async_fence, base.rcu);
}

static const struct dma_fence_ops exynos9810_bootfb_async_fence_ops = {
	.get_driver_name = exynos9810_bootfb_async_fence_driver_name,
	.get_timeline_name = exynos9810_bootfb_async_fence_timeline_name,
	.release = exynos9810_bootfb_async_fence_release,
};

static struct dma_fence *
exynos9810_bootfb_create_async_fence(struct exynos9810_bootfb *bootfb)
{
	struct exynos9810_bootfb_async_fence *async_fence;
	u64 seqno;

	async_fence = kzalloc(sizeof(*async_fence), GFP_KERNEL);
	if (!async_fence)
		return NULL;

	spin_lock_init(&async_fence->lock);
	seqno = atomic64_inc_return(&bootfb->native_async_fence_seqno);
	dma_fence_init(
		&async_fence->base,
		&exynos9810_bootfb_async_fence_ops,
		&async_fence->lock,
		bootfb->native_async_fence_context,
		seqno);

	return &async_fence->base;
}

static int
exynos9810_bootfb_publish_present_fences(
	struct exynos9810_bootfb *bootfb,
	struct exynos9810_bootfb_config_data *data,
	unsigned long arg, int release_index,
	struct dma_fence *native_fence)
{
	struct sync_file *sync_file;
	struct file *release_file = NULL;
	int release_fd = -1;
	int present_fd;

	present_fd = get_unused_fd_flags(O_CLOEXEC);
	if (present_fd < 0) {
		dma_fence_put(native_fence);
		return present_fd;
	}

	if (release_index >= 0) {
		if (release_index >= EXYNOS9810_BOOTFB_MAX_WINDOWS) {
			dma_fence_put(native_fence);
			put_unused_fd(present_fd);
			return -EINVAL;
		}

		release_fd = get_unused_fd_flags(O_CLOEXEC);
		if (release_fd < 0) {
			dma_fence_put(native_fence);
			put_unused_fd(present_fd);
			return release_fd;
		}
	}

	if (native_fence)
		sync_file = sync_file_create(native_fence);
	else
		sync_file = exynos9810_bootfb_create_present_fence();
	dma_fence_put(native_fence);
	if (!sync_file) {
		if (release_fd >= 0)
			put_unused_fd(release_fd);
		put_unused_fd(present_fd);
		return -ENOMEM;
	}

	if (release_fd >= 0) {
		release_file = sync_file->file;
		get_file(release_file);
		data->config[release_index].rel_fence = release_fd;
	}
	data->present_fence = present_fd;

	if (copy_to_user((void __user *)arg, data, sizeof(*data))) {
		if (release_file)
			fput(release_file);
		fput(sync_file->file);
		if (release_fd >= 0)
			put_unused_fd(release_fd);
		put_unused_fd(present_fd);
		return -EFAULT;
	}

	if (release_fd >= 0) {
		fd_install(release_fd, release_file);
		atomic_inc(&bootfb->native_present_release_fence_count);
	}
	fd_install(present_fd, sync_file->file);

	return 0;
}

static int
exynos9810_bootfb_disable_native_present(struct exynos9810_bootfb *bootfb);

static int
exynos9810_bootfb_try_native_present(
	struct exynos9810_bootfb *bootfb,
	struct exynos9810_bootfb_win_config *config);

static bool
exynos9810_bootfb_native_config_supported(
	struct exynos9810_bootfb *bootfb,
	const struct exynos9810_bootfb_win_config *config);

static int
exynos9810_bootfb_enable_native_present_auto(
	struct exynos9810_bootfb *bootfb);

static bool
exynos9810_bootfb_native_client_config_supported(
	struct exynos9810_bootfb *bootfb,
	const struct exynos9810_bootfb_win_config *config);

static noinline_for_stack int
exynos9810_bootfb_present(struct exynos9810_bootfb *bootfb,
			  unsigned int cmd, unsigned long arg)
{
	struct exynos9810_bootfb_config_data data;
	struct exynos9810_bootfb_win_config *fast_config;
	struct dma_fence *native_fence = NULL;
	u64 blit_start_ns;
	u64 blit_ns;
	size_t bytes_written = 0;
	unsigned int i;
	u32 damage_y = 0;
	u32 damage_h = 0;
	u32 scan_y = 0;
	u32 scan_rows = 0;
	u32 source_changed_rows = 0;
	u32 changed_rows = 0;
	bool damage_hint = false;
	bool damage_used = false;
	bool auto_was_active = false;
	bool fast_path = false;
	bool updated = false;
	int native_present_ret;
	int native_release_index = -1;
	int ret = 0;
	int kick_ret;

	if (cmd != S3CFB_WIN_CONFIG)
		return -EINVAL;

	memset(&data, 0, sizeof(data));
	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	WRITE_ONCE(bootfb->fbdev_hwc_seen, true);
	WRITE_ONCE(bootfb->fbdev_refresh_enabled, false);

	mutex_lock(&bootfb->lock);

	for (i = 0; i < ARRAY_SIZE(data.config); i++)
		data.config[i].rel_fence = -1;

	blit_start_ns = ktime_get_ns();
	fast_config = exynos9810_bootfb_find_fast_config(bootfb, &data);
	if (fast_config) {

		if (bootfb->native_auto_enabled &&
		    !bootfb->native_auto_blocked &&
		    !bootfb->native_present_enabled &&
		    exynos9810_bootfb_native_client_config_supported(
			    bootfb, fast_config)) {
			atomic_inc(&bootfb->native_auto_attempt_count);
			native_present_ret =
				exynos9810_bootfb_enable_native_present_auto(
					bootfb);
			if (!native_present_ret) {
				atomic_inc(&bootfb->native_auto_entry_count);
				bootfb->native_auto_last_error = 0;
			} else {
				bootfb->native_auto_blocked = true;
				bootfb->native_auto_last_error =
					native_present_ret;
				atomic_inc(&bootfb->native_auto_failure_count);
				dev_warn_ratelimited(
					bootfb->dev,
					"automatic native enable failed: %d; CPU fallback\n",
					native_present_ret);
			}
		}

		if (bootfb->native_present_enabled) {
			auto_was_active = bootfb->native_auto_active;
			native_present_ret =
				exynos9810_bootfb_try_native_present(
					bootfb, fast_config);
			if (!native_present_ret) {
				native_release_index = fast_config - data.config;
				if (bootfb->native_async_fence)
					native_fence = dma_fence_get(
						bootfb->native_async_fence);
				fast_path = true;
				updated = false;
				WRITE_ONCE(
					bootfb->last_fast_format,
					fast_config->format);
				goto native_fast_done;
			}
			if (native_present_ret != -EAGAIN && auto_was_active) {
				bootfb->native_auto_blocked = true;
				bootfb->native_auto_last_error =
					native_present_ret;
				atomic_inc(&bootfb->native_auto_failure_count);
			}
			if (native_present_ret != -EAGAIN)
				dev_warn_ratelimited(
					bootfb->dev,
					"native present failed: %d; CPU fallback\n",
					native_present_ret);
		}
		damage_hint = exynos9810_bootfb_get_damage_rows(
			bootfb, &data, &damage_y, &damage_h);
		if (damage_hint)
			atomic_inc(&bootfb->damage_hint_count);

		ret = exynos9810_bootfb_blit_fullscreen(
			bootfb, fast_config, damage_hint, damage_y, damage_h,
			&damage_used, &scan_y, &scan_rows,
			&source_changed_rows, &changed_rows,
			&bytes_written);
		if (!ret) {
			fast_path = true;
			updated = bytes_written != 0;
			if (damage_used)
				atomic_inc(&bootfb->damage_frame_count);
			WRITE_ONCE(bootfb->last_fast_format,
				   fast_config->format);
			atomic_inc(&bootfb->fast_present_count);

		} else {
			dev_warn_ratelimited(
				bootfb->dev,
				"full-screen fast blit failed: %d; "
				"falling back to general compositor\n",
				ret);
		}
native_fast_done:
		;
	}

	/* A non-fast frame cannot remain on the previous native buffer. */
	if (bootfb->native_present_enabled && !fast_config) {
		atomic_inc(&bootfb->native_present_fallback_count);
		native_present_ret =
			exynos9810_bootfb_disable_native_present(bootfb);
		if (!native_present_ret)
			bootfb->native_present_error = -EAGAIN;
		if (native_present_ret)
			dev_warn_ratelimited(
				bootfb->dev,
				"native restore before CPU fallback failed: %d\n",
				native_present_ret);
	}

	if (!fast_path) {
		damage_used = false;
		scan_y = 0;
		scan_rows = bootfb->height;
		bootfb->source_shadow_valid = false;
		memset(bootfb->shadow, 0, bootfb->screen_size);

		if (!bootfb->generic_path_logged) {
			for (i = 0; i < EXYNOS9810_BOOTFB_MAX_WINDOWS; i++) {
				struct exynos9810_bootfb_win_config *config =
					&data.config[i];

				if (config->state != EXYNOS9810_WIN_BUFFER &&
				    config->state != EXYNOS9810_WIN_CURSOR)
					continue;

				dev_info(
					bootfb->dev,
					"E981D: general compositor fmt=%d "
					"blend=%d alpha=%d compression=%u "
					"src=%d,%d %ux%u/%ux%u "
					"dst=%d,%d %ux%u\n",
					config->format, config->blending,
					config->plane_alpha,
					config->compression,
					config->src.x, config->src.y,
					config->src.w, config->src.h,
					config->src.f_w, config->src.f_h,
					config->dst.x, config->dst.y,
					config->dst.w, config->dst.h);
				bootfb->generic_path_logged = true;
				break;
			}
		}

		for (i = 0; i < EXYNOS9810_BOOTFB_MAX_WINDOWS; i++) {
			struct exynos9810_bootfb_win_config *config =
				&data.config[i];

			switch (config->state) {
			case EXYNOS9810_WIN_DISABLED:
			case EXYNOS9810_WIN_UPDATE:
				break;
			case EXYNOS9810_WIN_COLOR:
				exynos9810_bootfb_draw_color(bootfb, config);
				updated = true;
				break;
			case EXYNOS9810_WIN_BUFFER:
			case EXYNOS9810_WIN_CURSOR:
				ret = exynos9810_bootfb_draw_buffer(
					bootfb, config);
				if (!ret)
					updated = true;
				else
					dev_warn_ratelimited(
						bootfb->dev,
						"window %u skipped: %d\n",
						i, ret);
				break;
			default:
				dev_warn_ratelimited(
					bootfb->dev,
					"unknown window %u state %d\n",
					i, config->state);
				break;
			}
		}

		if (updated) {
			memcpy_toio(bootfb->screen, bootfb->shadow,
				    bootfb->screen_size);
			/* Publish all pixels before the hardware scanout. */
			wmb();
			bootfb->shadow_valid = true;
			changed_rows = bootfb->height;
			bytes_written = bootfb->screen_size;
		}
		atomic_inc(&bootfb->generic_present_count);
	}

	blit_ns = ktime_get_ns() - blit_start_ns;
	atomic_set(&bootfb->last_damage_hint, damage_hint);
	atomic_set(&bootfb->last_damage_used, damage_used);
	atomic_set(&bootfb->last_damage_y, damage_y);
	atomic_set(&bootfb->last_damage_h, damage_h);
	atomic_set(&bootfb->last_scan_y, scan_y);
	atomic_set(&bootfb->last_scan_rows, scan_rows);
	atomic_set(&bootfb->last_source_changed_rows,
		   source_changed_rows);
	atomic_set(&bootfb->last_changed_rows, changed_rows);
	atomic64_set(&bootfb->last_blit_bytes, bytes_written);
	atomic64_set(&bootfb->last_blit_ns, blit_ns);
	if (blit_ns > atomic64_read(&bootfb->max_blit_ns))
		atomic64_set(&bootfb->max_blit_ns, blit_ns);

	if (fast_path && !bootfb->fast_path_logged) {
		dev_info(
			bootfb->dev,
			"E981D: full-screen fast blit fmt=%d blend=%d "
			"alpha=%d source_stride=%u damage_hint=%u "
			"damage_used=%u damage_y=%u damage_h=%u "
			"scan_y=%u scan_rows=%u source_changed_rows=%u "
			"changed_rows=%u bytes=%zu blit_us=%llu\n",
			fast_config->format, fast_config->blending,
			fast_config->plane_alpha, fast_config->src.f_w,
			damage_hint, damage_used, damage_y, damage_h,
			scan_y, scan_rows, source_changed_rows,
			changed_rows, bytes_written,
			(unsigned long long)div_u64(blit_ns, NSEC_PER_USEC));
		bootfb->fast_path_logged = true;
	}

	if (updated &&
	    atomic_read(&bootfb->decon_kick_failures) < 3) {
		kick_ret = exynos9810_bootfb_kick_decon(bootfb);
		if (kick_ret) {
			atomic_inc(&bootfb->decon_kick_failures);
			dev_warn_ratelimited(
				bootfb->dev,
				"DECON frame kick failed: %d\n",
				kick_ret);
		} else {
			atomic_inc(&bootfb->decon_kick_count);
		}
	}

	if (atomic_inc_return(&bootfb->present_count) == 1)
		dev_info(bootfb->dev,
			 "E981D: first fbdev window configuration updated=%u\n",
			 updated);

	mutex_unlock(&bootfb->lock);

	return exynos9810_bootfb_publish_present_fences(
		bootfb, &data, arg, native_release_index,
		native_fence);
}

static int
exynos9810_bootfb_open(struct fb_info *info, int user)
{
	struct exynos9810_bootfb *bootfb = info->par;

	atomic_inc(&bootfb->fbdev_open_count);
	if (!READ_ONCE(bootfb->fbdev_hwc_seen))
		WRITE_ONCE(bootfb->fbdev_refresh_enabled, true);

	return 0;
}

static int
exynos9810_bootfb_release(struct fb_info *info, int user)
{
	struct exynos9810_bootfb *bootfb = info->par;

	if (atomic_dec_and_test(&bootfb->fbdev_open_count) &&
	    !READ_ONCE(bootfb->fbdev_hwc_seen))
		WRITE_ONCE(bootfb->fbdev_refresh_enabled, false);

	return 0;
}

static int exynos9810_bootfb_check_var(struct fb_var_screeninfo *var,
				       struct fb_info *info)
{
	struct exynos9810_bootfb *bootfb = info->par;

	if (var->xres != bootfb->width || var->yres != bootfb->height ||
	    var->bits_per_pixel != 32)
		return -EINVAL;

	var->xres_virtual = bootfb->width;
	var->yres_virtual = bootfb->height;
	var->red = (struct fb_bitfield) { 16, 8, 0 };
	var->green = (struct fb_bitfield) { 8, 8, 0 };
	var->blue = (struct fb_bitfield) { 0, 8, 0 };
	var->transp = (struct fb_bitfield) { 24, 8, 0 };
	return 0;
}

static int exynos9810_bootfb_blank(int blank, struct fb_info *info)
{
	struct exynos9810_bootfb *bootfb = info->par;

	if (blank == FB_BLANK_UNBLANK &&
	    !READ_ONCE(bootfb->fbdev_hwc_seen))
		WRITE_ONCE(bootfb->fbdev_refresh_enabled, true);
	else if (blank == FB_BLANK_POWERDOWN)
		WRITE_ONCE(bootfb->fbdev_refresh_enabled, false);

	return 0;
}

static int exynos9810_bootfb_wait_vsync(struct exynos9810_bootfb *bootfb)
{
	s64 sequence = atomic64_read(&bootfb->vsync_sequence);
	long ret;

	ret = wait_event_interruptible_timeout(bootfb->vsync_wait,
					       atomic64_read(&bootfb->vsync_sequence) != sequence,
				msecs_to_jiffies(100));
	if (ret > 0)
		return 0;
	if (!ret)
		return -ETIMEDOUT;
	return ret;
}

static int exynos9810_bootfb_ioctl(struct fb_info *info, unsigned int cmd,
				   unsigned long arg)
{
	struct exynos9810_bootfb *bootfb = info->par;
	struct exynos9810_bootfb_display_mode mode = {
		.width = bootfb->width,
		.height = bootfb->height,
		.mm_width = bootfb->width_mm,
		.mm_height = bootfb->height_mm,
		.fps = EXYNOS9810_BOOTFB_FPS,
	};
	struct exynos9810_bootfb_disp_info disp_info = { };
	void __user *argp = (void __user *)arg;
	u32 value;

	if (cmd == FBIO_WAITFORVSYNC)
		return exynos9810_bootfb_wait_vsync(bootfb);

	switch (cmd) {
	case S3CFB_SET_VSYNC_INT:
		if (get_user(value, (u32 __user *)argp))
			return -EFAULT;
		WRITE_ONCE(bootfb->vsync_enabled, !!value);
		return 0;
	case S3CFB_DECON_SELF_REFRESH:
	case S3CFB_WIN_POSITION:
	case S3CFB_POWER_MODE:
		return 0;
	case S3CFB_WIN_CONFIG:
		return exynos9810_bootfb_present(bootfb, cmd, arg);
	case EXYNOS_DISP_INFO:
		if (get_user(disp_info.ver, (int __user *)argp))
			return -EFAULT;
		disp_info.psr_mode = 1;
		disp_info.chip_ver = EXYNOS9810_BOOTFB_CHIP_ID;
		if (copy_to_user(argp, &disp_info, sizeof(disp_info)))
			return -EFAULT;
		return 0;
	case EXYNOS_GET_DISPLAY_MODE_NUM:
		return put_user(1U, (u32 __user *)argp);
	case EXYNOS_GET_DISPLAY_MODE:
		if (copy_from_user(&mode.index, argp, sizeof(mode.index)))
			return -EFAULT;
		if (mode.index)
			return -EINVAL;
		if (copy_to_user(argp, &mode, sizeof(mode)))
			return -EFAULT;
		return 0;
	case EXYNOS_SET_DISPLAY_MODE:
		return 0;
	case EXYNOS_GET_DISPLAY_CURRENT_MODE:
		return put_user(0U, (u32 __user *)argp);
	default:
		return -ENOTTY;
	}
}

static const struct fb_ops exynos9810_bootfb_ops = {
	.fb_open = exynos9810_bootfb_open,
	.fb_release = exynos9810_bootfb_release,
	.owner = THIS_MODULE,
	FB_DEFAULT_IOMEM_OPS,
	.fb_check_var = exynos9810_bootfb_check_var,
	.fb_blank = exynos9810_bootfb_blank,
	.fb_ioctl = exynos9810_bootfb_ioctl,
};

static ssize_t vsync_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct exynos9810_bootfb *bootfb = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld %u\n",
			  atomic64_read(&bootfb->vsync_timestamp),
			  EXYNOS9810_BOOTFB_FPS);
}
static DEVICE_ATTR_RO(vsync);

static ssize_t psr_info_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct exynos9810_bootfb *bootfb = dev_get_drvdata(dev);

	/*
	 * Compatibility row-damage metadata, not physical panel DSC data.
	 * One full-width, one-row block makes SLSI HWC publish its existing
	 * damage rectangle while the preserved DECON still performs a full
	 * hardware frame transfer.
	 */
	return sysfs_emit(buf, "1\n1\n%u\n%u\n%u\n1\n0\n",
			  bootfb->width, bootfb->height, bootfb->width);
}
static DEVICE_ATTR_RO(psr_info);

static int
exynos9810_bootfb_prepare_iommu_domain(struct exynos9810_bootfb *bootfb)
{
	struct iommu_domain *current_domain;
	struct iommu_domain *domain;
	phys_addr_t phys;
	phys_addr_t first_phys;
	phys_addr_t last_phys;
	dma_addr_t iova;
	size_t map_size;
	int ret;

	current_domain = iommu_get_domain_for_dev(bootfb->dev);
	if (!current_domain)
		return -ENODEV;

	/*
	 * The boot scanout must still use the identity domain here. Refuse an
	 * unexpected translated domain so ownership of the handoff is explicit.
	 */
	if (current_domain->type != IOMMU_DOMAIN_IDENTITY)
		return -EBUSY;

	phys = bootfb->info->fix.smem_start;
	iova = phys;
	map_size = PAGE_ALIGN(bootfb->screen_size);
	if (!map_size)
		return -EINVAL;

	domain = iommu_paging_domain_alloc(bootfb->dev);
	if (IS_ERR(domain))
		return PTR_ERR(domain);

	ret = iommu_map(domain, iova, phys, map_size,
			IOMMU_READ | IOMMU_WRITE, GFP_KERNEL);
	if (ret) {
		iommu_domain_free(domain);
		return ret;
	}

	first_phys = iommu_iova_to_phys(domain, iova);
	last_phys = iommu_iova_to_phys(
		domain, iova + bootfb->screen_size - 1);
	if (first_phys != phys ||
	    last_phys != phys + bootfb->screen_size - 1) {
		iommu_unmap(domain, iova, map_size);
		iommu_domain_free(domain);
		return -EIO;
	}

	bootfb->prepared_domain = domain;
	bootfb->prepared_iova = iova;
	bootfb->prepared_phys = phys;
	bootfb->prepared_size = map_size;
	bootfb->prepared_first_phys = first_phys;
	bootfb->prepared_last_phys = last_phys;

	return 0;
}

static int
exynos9810_bootfb_attach_prepared_iommu(struct exynos9810_bootfb *bootfb)
{
	struct iommu_domain *active_domain;
	struct device_node *iommu_np;
	struct platform_device *supplier;
	int ret;

	if (!bootfb->prepared_domain || bootfb->prepare_iommu_error)
		return -EINVAL;

	active_domain = iommu_get_domain_for_dev(bootfb->dev);
	if (!active_domain ||
	    active_domain->type != IOMMU_DOMAIN_IDENTITY)
		return -EBUSY;

	iommu_np = of_parse_phandle(bootfb->dev->of_node, "iommus", 0);
	if (!iommu_np)
		return -ENODEV;

	supplier = of_find_device_by_node(iommu_np);
	of_node_put(iommu_np);
	if (!supplier)
		return -EPROBE_DEFER;

	/*
	 * exynos_iommu_attach_device() only enables the controller
	 * immediately when the SysMMU provider is runtime-active. Hold a
	 * provider PM reference across the translated lifetime so the page
	 * table cannot become inactive while G0 is fetching from it.
	 */
	ret = pm_runtime_resume_and_get(&supplier->dev);
	if (ret < 0) {
		put_device(&supplier->dev);
		return ret;
	}

	ret = iommu_attach_device(bootfb->prepared_domain, bootfb->dev);
	if (ret)
		goto put_supplier;

	active_domain = iommu_get_domain_for_dev(bootfb->dev);
	if (active_domain != bootfb->prepared_domain) {
		iommu_detach_device(bootfb->prepared_domain, bootfb->dev);
		ret = -EIO;
		goto put_supplier;
	}

	bootfb->iommu_supplier = supplier;
	bootfb->iommu_supplier_active = true;
	bootfb->translated_attached = true;
	return 0;

put_supplier:
	pm_runtime_put_sync(&supplier->dev);
	put_device(&supplier->dev);
	return ret;
}

static bool
exynos9810_bootfb_native_config_supported(
	struct exynos9810_bootfb *bootfb,
	const struct exynos9810_bootfb_win_config *config)
{
	const struct exynos9810_bootfb_frame *src = &config->src;
	const struct exynos9810_bootfb_frame *dst = &config->dst;

	return config->state == EXYNOS9810_WIN_BUFFER &&
	       !config->protection && !config->compression &&
	       config->dpp_parm.rot == EXYNOS9810_ROT_NORMAL &&
	       config->fd_idma[0] >= 0 &&
	       config->format == EXYNOS9810_FMT_RGBX8888 &&
	       src->x == 0 && src->y == 0 &&
	       src->w == bootfb->width && src->h == bootfb->height &&
	       src->f_w == bootfb->width && src->f_h == bootfb->height &&
	       dst->x == 0 && dst->y == 0 &&
	       dst->w == bootfb->width && dst->h == bootfb->height;
}

static bool
exynos9810_bootfb_native_client_config_supported(
	struct exynos9810_bootfb *bootfb,
	const struct exynos9810_bootfb_win_config *config)
{
	struct exynos9810_bootfb_win_config translated;

	/*
	 * SurfaceFlinger's forced full-screen client target is RGBA8888,
	 * whereas the native path was validated with RGBX8888.  Reuse every
	 * current native geometry/protection/compression check by translating
	 * only the format field in a local copy.
	 */
	if (config->format != EXYNOS9810_FMT_RGBX8888 &&
	    config->format != EXYNOS9810_FMT_RGBA8888)
		return false;

	if (config->format == EXYNOS9810_FMT_RGBX8888)
		return exynos9810_bootfb_native_config_supported(bootfb, config);

	translated = *config;
	translated.format = EXYNOS9810_FMT_RGBX8888;

	return exynos9810_bootfb_native_config_supported(
		bootfb, &translated);
}

static void
exynos9810_bootfb_release_native_slot(
	struct exynos9810_bootfb *bootfb,
	struct exynos9810_bootfb_native_slot *slot)
{
	if (!slot->valid)
		return;

	if (bootfb->prepared_domain && slot->mapped) {
		size_t unmapped;

		unmapped = iommu_unmap(
			bootfb->prepared_domain, slot->iova, slot->mapped);
		if (unmapped != slot->mapped)
			dev_warn(
				bootfb->dev,
				"native present unmap short: %#zx/%#zx at %#llx\n",
				unmapped, slot->mapped,
				(unsigned long long)slot->iova);
	}

	if (slot->attachment && slot->sgt)
		dma_buf_unmap_attachment(
			slot->attachment, slot->sgt, DMA_TO_DEVICE);
	if (slot->dmabuf && slot->attachment)
		dma_buf_detach(slot->dmabuf, slot->attachment);
	if (slot->dmabuf)
		dma_buf_put(slot->dmabuf);

	memset(slot, 0, sizeof(*slot));
}

static int
exynos9810_bootfb_map_native_slot(
	struct exynos9810_bootfb *bootfb,
	struct exynos9810_bootfb_win_config *config,
	struct exynos9810_bootfb_native_slot *slot,
	dma_addr_t iova)
{
	struct dma_buf_attachment *attachment;
	struct dma_buf *dmabuf;
	struct sg_table *sgt;
	ssize_t mapped;
	int ret;

	if (slot->valid)
		return -EBUSY;
	if (!bootfb->prepared_domain || !bootfb->iommu_supplier ||
	    !bootfb->iommu_supplier_active)
		return -ENODEV;

	ret = exynos9810_bootfb_wait_fence(config->acq_fence);
	if (ret)
		return ret;

	dmabuf = dma_buf_get(config->fd_idma[0]);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);
	if (dmabuf->size < bootfb->screen_size) {
		ret = -EINVAL;
		goto put_dmabuf;
	}

	/*
	 * Use the SysMMU provider as the attachment device, then map the
	 * physical scatterlist into the display domain at a fixed IOVA.
	 * Exporter DMA addresses belong to the attachment domain and cannot
	 * be reused after the display master changes domains.
	 */
	attachment = dma_buf_attach(dmabuf, &bootfb->iommu_supplier->dev);
	if (IS_ERR(attachment)) {
		ret = PTR_ERR(attachment);
		goto put_dmabuf;
	}

	sgt = dma_buf_map_attachment(attachment, DMA_TO_DEVICE);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto detach;
	}

	mapped = iommu_map_sg(
		bootfb->prepared_domain, iova,
		sgt->sgl, sgt->orig_nents, IOMMU_READ, GFP_KERNEL);
	if (mapped < 0) {
		ret = mapped;
		goto unmap_attachment;
	}
	if ((size_t)mapped < bootfb->screen_size) {
		if (mapped)
			iommu_unmap(bootfb->prepared_domain, iova, mapped);
		ret = -ENOSPC;
		goto unmap_attachment;
	}
	if (!iommu_iova_to_phys(bootfb->prepared_domain, iova) ||
	    !iommu_iova_to_phys(
		    bootfb->prepared_domain,
		    iova + bootfb->screen_size - 1)) {
		iommu_unmap(bootfb->prepared_domain, iova, mapped);
		ret = -EIO;
		goto unmap_attachment;
	}

	slot->dmabuf = dmabuf;
	slot->attachment = attachment;
	slot->sgt = sgt;
	slot->iova = iova;
	slot->mapped = mapped;
	slot->valid = true;
	return 0;

unmap_attachment:
	dma_buf_unmap_attachment(attachment, sgt, DMA_TO_DEVICE);
detach:
	dma_buf_detach(dmabuf, attachment);
put_dmabuf:
	dma_buf_put(dmabuf);
	return ret;
}

static int
exynos9810_bootfb_wait_native_frame(
	struct exynos9810_bootfb *bootfb, u32 frame_before, u64 *wait_ns)
{
	u64 start = ktime_get_ns();
	u64 now;

	do {
		if (readl(bootfb->decon + EXYNOS9810_DECON_FRAME_COUNT) !=
		    frame_before) {
			*wait_ns = ktime_get_ns() - start;
			return 0;
		}
		usleep_range(250, 500);
		now = ktime_get_ns();
	} while (now - start < EXYNOS9810_BOOTFB_NATIVE_WAIT_NS);

	*wait_ns = ktime_get_ns() - start;
	return -ETIMEDOUT;
}

static int
exynos9810_bootfb_wait_native_idle(
	struct exynos9810_bootfb *bootfb, u64 *idle_ns)
{
	u64 start;
	u64 now;
	u32 idma;
	u32 dpp;

	start = ktime_get_ns();
	do {
		idma = readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_ENABLE);
		dpp = readl(bootfb->dpp_g0 + EXYNOS9810_DPP_G0_ENABLE);
		if (!(idma & EXYNOS9810_DPU_OP_STATUS) &&
		    !(dpp & EXYNOS9810_DPU_OP_STATUS)) {
			*idle_ns = ktime_get_ns() - start;
			atomic_inc(&bootfb->native_present_completion_count);
			if (*idle_ns >
			    atomic64_read(&bootfb->native_present_max_idle_ns))
				atomic64_set(
					&bootfb->native_present_max_idle_ns,
					*idle_ns);
			return 0;
		}

		usleep_range(100, 250);
		now = ktime_get_ns();
	} while (now - start < EXYNOS9810_BOOTFB_NATIVE_IDLE_WAIT_NS);

	*idle_ns = ktime_get_ns() - start;
	atomic_inc(&bootfb->native_present_completion_timeout_count);
	return -ETIMEDOUT;
}

static void
exynos9810_bootfb_finish_async_fence(
	struct exynos9810_bootfb *bootfb, int error)
{
	struct dma_fence *fence;

	fence = READ_ONCE(bootfb->native_async_fence);
	WRITE_ONCE(bootfb->native_async_error, error);

	if (fence) {
		if (error)
			dma_fence_set_error(fence, error);
		dma_fence_signal(fence);
		atomic_inc(&bootfb->native_async_signal_count);
	}

	if (error)
		atomic_inc(&bootfb->native_async_error_count);

	smp_wmb();
	atomic_set(&bootfb->native_async_complete_state, 2);
	wake_up_all(&bootfb->native_async_wait);
}

static bool
exynos9810_bootfb_try_async_irq_complete(
	struct exynos9810_bootfb *bootfb, u32 dpp_pending)
{
	u64 complete_ns;
	u64 start_ns;
	u32 dpp_enable;
	u32 idma_enable;
	int error = 0;

	if (!READ_ONCE(bootfb->native_async_pending))
		return false;
	smp_rmb();

	if (atomic_read(&bootfb->dpp_g0_framedone_count) ==
	    READ_ONCE(bootfb->native_async_dpp_before))
		return false;

	/*
	 * A worker/fallback may already own completion.  In that case the IRQ
	 * has nothing more to schedule.
	 */
	if (atomic_read(&bootfb->native_async_complete_state) != 0)
		return true;

	idma_enable = readl(
		bootfb->idma_g0 + EXYNOS9810_IDMA_G0_ENABLE);
	dpp_enable = readl(
		bootfb->dpp_g0 + EXYNOS9810_DPP_G0_ENABLE);
	if ((idma_enable & EXYNOS9810_DPU_OP_STATUS) ||
	    (dpp_enable & EXYNOS9810_DPU_OP_STATUS)) {
		atomic_inc(&bootfb->native_async_irq_busy_fallback_count);
		return false;
	}

	if (atomic_cmpxchg(
		    &bootfb->native_async_complete_state, 0, 1) != 0)
		return true;

	/*
	 * DPP IRQ status is W1C and its config-error state can be consumed by
	 * IRQ clearing, so preserve a simultaneous non-framedone cause here.
	 * The IDMA/DPP config-state reads remain the same additional checks
	 * used by the worker path.
	 */
	if (dpp_pending & ~EXYNOS9810_DPP_IRQ_FRAMEDONE)
		error = -EIO;
	if (!error &&
	    readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_CONFIG_ERROR))
		error = -EIO;
	if (!error &&
	    readl(bootfb->dpp_g0 + EXYNOS9810_DPP_G0_CONFIG_ERROR))
		error = -EIO;

	start_ns = atomic64_read(&bootfb->native_async_submit_start_ns);
	if (start_ns && ktime_get_ns() >= start_ns) {
		complete_ns = ktime_get_ns() - start_ns;
		atomic64_set(
			&bootfb->native_async_last_complete_ns,
			complete_ns);
		if (complete_ns >
		    atomic64_read(&bootfb->native_async_max_complete_ns))
			atomic64_set(
				&bootfb->native_async_max_complete_ns,
				complete_ns);

		atomic64_set(
			&bootfb->native_async_last_irq_complete_ns,
			complete_ns);
		if (complete_ns >
		    atomic64_read(
			    &bootfb->native_async_max_irq_complete_ns))
			atomic64_set(
				&bootfb->native_async_max_irq_complete_ns,
				complete_ns);
	}

	atomic_inc(&bootfb->native_async_irq_signal_count);
	if (error)
		atomic_inc(&bootfb->native_async_irq_error_signal_count);

	exynos9810_bootfb_finish_async_fence(bootfb, error);
	return true;
}

static void
exynos9810_bootfb_async_complete_work(struct work_struct *work)
{
	struct exynos9810_bootfb *bootfb =
		container_of(work, struct exynos9810_bootfb,
			     native_async_complete_work);
	u64 complete_ns;
	u64 idle_ns = 0;
	u64 start_ns;
	u32 dpp_enable;
	u32 idma_enable;
	int error = 0;

	if (!READ_ONCE(bootfb->native_async_pending))
		return;
	smp_rmb();

	if (atomic_read(&bootfb->dpp_g0_framedone_count) ==
	    READ_ONCE(bootfb->native_async_dpp_before)) {
		atomic_inc(&bootfb->native_async_irq_missed_count);
		return;
	}

	if (atomic_cmpxchg(
		    &bootfb->native_async_complete_state, 0, 1) != 0)
		return;

	start_ns = atomic64_read(&bootfb->native_async_submit_start_ns);

	idma_enable = readl(
		bootfb->idma_g0 + EXYNOS9810_IDMA_G0_ENABLE);
	dpp_enable = readl(
		bootfb->dpp_g0 + EXYNOS9810_DPP_G0_ENABLE);
	if ((idma_enable & EXYNOS9810_DPU_OP_STATUS) ||
	    (dpp_enable & EXYNOS9810_DPU_OP_STATUS))
		atomic_inc(&bootfb->native_async_early_busy_count);

	/*
	 * DPP framedone may arrive while OP_STATUS remains transiently busy.
	 * Use a bounded idle wait as the completion criterion before signaling
	 * the hardware composer fence.
	 */
	error = exynos9810_bootfb_wait_native_idle(bootfb, &idle_ns);
	atomic64_set(&bootfb->native_present_last_idle_ns, idle_ns);
	if (!error &&
	    readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_CONFIG_ERROR))
		error = -EIO;
	if (!error &&
	    readl(bootfb->dpp_g0 + EXYNOS9810_DPP_G0_CONFIG_ERROR))
		error = -EIO;

	if (start_ns && ktime_get_ns() >= start_ns) {
		complete_ns = ktime_get_ns() - start_ns;
		atomic64_set(
			&bootfb->native_async_last_complete_ns,
			complete_ns);
		if (complete_ns >
		    atomic64_read(&bootfb->native_async_max_complete_ns))
			atomic64_set(
				&bootfb->native_async_max_complete_ns,
				complete_ns);
	}

	atomic_inc(&bootfb->native_async_worker_signal_count);
	exynos9810_bootfb_finish_async_fence(bootfb, error);
}

static int
exynos9810_bootfb_wait_native_dpp_irq(
	struct exynos9810_bootfb *bootfb, int dpp_before, u64 *irq_wait_ns)
{
	unsigned long timeout;
	u64 start;
	long ret;

	start = ktime_get_ns();
	timeout = msecs_to_jiffies(50);
	if (!timeout)
		timeout = 1;

	ret = wait_event_timeout(
		bootfb->native_dpp_irq_wait,
		atomic_read(&bootfb->dpp_g0_framedone_count) != dpp_before,
		timeout);

	*irq_wait_ns = ktime_get_ns() - start;
	atomic64_set(&bootfb->native_dpp_irq_last_wait_ns, *irq_wait_ns);
	if (*irq_wait_ns >
	    atomic64_read(&bootfb->native_dpp_irq_max_wait_ns))
		atomic64_set(
			&bootfb->native_dpp_irq_max_wait_ns,
			*irq_wait_ns);

	if (!ret) {
		atomic_inc(&bootfb->native_dpp_irq_wait_timeout_count);
		return -ETIMEDOUT;
	}

	atomic_inc(&bootfb->native_dpp_irq_wait_count);
	return 0;
}

static void
exynos9810_bootfb_clear_g0_irq_pending(struct exynos9810_bootfb *bootfb)
{
	u32 status;

	if (bootfb->idma_g0) {
		status = readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_IRQ);
		if (status & EXYNOS9810_IDMA_IRQ_CLEAR_MASK)
			writel(status,
			       bootfb->idma_g0 + EXYNOS9810_IDMA_G0_IRQ);
	}

	if (bootfb->dpp_g0) {
		status = readl(bootfb->dpp_g0 + EXYNOS9810_DPP_G0_IRQ);
		if (status & EXYNOS9810_DPP_IRQ_CLEAR_MASK)
			writel(status,
			       bootfb->dpp_g0 + EXYNOS9810_DPP_G0_IRQ);
	}
}

static void
exynos9810_bootfb_record_irq_latency(
	atomic64_t *last_ns, atomic64_t *last_latency_ns,
	atomic64_t *max_latency_ns, atomic64_t *arm_ns)
{
	u64 arm;
	u64 latency;
	u64 now;

	now = ktime_get_ns();
	atomic64_set(last_ns, now);

	arm = atomic64_read(arm_ns);
	if (!arm || now < arm)
		return;

	latency = now - arm;
	atomic64_set(last_latency_ns, latency);
	if (latency > atomic64_read(max_latency_ns))
		atomic64_set(max_latency_ns, latency);
}

static irqreturn_t
exynos9810_bootfb_idma_g0_irq_handler(int irq, void *data)
{
	struct exynos9810_bootfb *bootfb = data;
	u32 pending;
	u32 status;

	status = readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_IRQ);
	pending = status & EXYNOS9810_IDMA_IRQ_CLEAR_MASK;
	if (!pending)
		return IRQ_NONE;

	/*
	 * Pending bits are W1C. Re-writing the observed register preserves
	 * the low enable/mask bits while clearing every pending cause we saw.
	 */
	writel(status, bootfb->idma_g0 + EXYNOS9810_IDMA_G0_IRQ);
	atomic_inc(&bootfb->idma_g0_irq_count);

	if (pending & EXYNOS9810_IDMA_IRQ_FRAMEDONE) {
		atomic_inc(&bootfb->idma_g0_framedone_count);
		exynos9810_bootfb_record_irq_latency(
			&bootfb->idma_g0_last_irq_ns,
			&bootfb->idma_g0_last_latency_ns,
			&bootfb->idma_g0_max_latency_ns,
			&bootfb->native_irq_arm_ns);
	}

	if (pending & ~EXYNOS9810_IDMA_IRQ_FRAMEDONE)
		atomic_inc(&bootfb->idma_g0_irq_error_count);

	return IRQ_HANDLED;
}

static irqreturn_t
exynos9810_bootfb_dpp_g0_irq_handler(int irq, void *data)
{
	struct exynos9810_bootfb *bootfb = data;
	u32 pending;
	u32 status;

	status = readl(bootfb->dpp_g0 + EXYNOS9810_DPP_G0_IRQ);
	pending = status & EXYNOS9810_DPP_IRQ_CLEAR_MASK;
	if (!pending)
		return IRQ_NONE;

	writel(status, bootfb->dpp_g0 + EXYNOS9810_DPP_G0_IRQ);
	atomic_inc(&bootfb->dpp_g0_irq_count);

	if (pending & EXYNOS9810_DPP_IRQ_FRAMEDONE) {
		atomic_inc(&bootfb->dpp_g0_framedone_count);
		exynos9810_bootfb_record_irq_latency(
			&bootfb->dpp_g0_last_irq_ns,
			&bootfb->dpp_g0_last_latency_ns,
			&bootfb->dpp_g0_max_latency_ns,
			&bootfb->native_irq_arm_ns);
		wake_up(&bootfb->native_dpp_irq_wait);
		if (READ_ONCE(bootfb->native_async_pending) &&
		    !exynos9810_bootfb_try_async_irq_complete(
			    bootfb, pending))
			schedule_work(
				&bootfb->native_async_complete_work);
	}

	if (pending & ~EXYNOS9810_DPP_IRQ_FRAMEDONE)
		atomic_inc(&bootfb->dpp_g0_irq_error_count);

	return IRQ_HANDLED;
}

static int
exynos9810_bootfb_setup_g0_irqs(
	struct exynos9810_bootfb *bootfb, struct platform_device *pdev)
{
	int ret;

	bootfb->idma_g0_irq = platform_get_irq_byname(pdev, "idma-g0");
	if (bootfb->idma_g0_irq < 0)
		return bootfb->idma_g0_irq;

	bootfb->dpp_g0_irq = platform_get_irq_byname(pdev, "dpp-g0");
	if (bootfb->dpp_g0_irq < 0)
		return bootfb->dpp_g0_irq;

	atomic_set(&bootfb->idma_g0_irq_count, 0);
	atomic_set(&bootfb->dpp_g0_irq_count, 0);
	atomic_set(&bootfb->idma_g0_framedone_count, 0);
	atomic_set(&bootfb->dpp_g0_framedone_count, 0);
	atomic_set(&bootfb->idma_g0_irq_error_count, 0);
	atomic_set(&bootfb->dpp_g0_irq_error_count, 0);
	atomic_set(&bootfb->native_irq_sample_count, 0);
	atomic_set(&bootfb->native_idma_irq_miss_count, 0);
	atomic_set(&bootfb->native_dpp_irq_miss_count, 0);
	init_waitqueue_head(&bootfb->native_dpp_irq_wait);
	atomic_set(&bootfb->native_dpp_irq_wait_count, 0);
	atomic_set(&bootfb->native_dpp_irq_wait_timeout_count, 0);
	atomic_set(&bootfb->native_dpp_irq_early_busy_count, 0);
	atomic64_set(&bootfb->native_dpp_irq_last_wait_ns, 0);
	atomic64_set(&bootfb->native_dpp_irq_max_wait_ns, 0);
	atomic64_set(&bootfb->native_irq_arm_ns, 0);
	atomic64_set(&bootfb->idma_g0_last_irq_ns, 0);
	atomic64_set(&bootfb->dpp_g0_last_irq_ns, 0);
	atomic64_set(&bootfb->idma_g0_last_latency_ns, 0);
	atomic64_set(&bootfb->dpp_g0_last_latency_ns, 0);
	atomic64_set(&bootfb->idma_g0_max_latency_ns, 0);
	atomic64_set(&bootfb->dpp_g0_max_latency_ns, 0);

	/* Remove the bootloader's stale framedone status before requesting. */
	exynos9810_bootfb_clear_g0_irq_pending(bootfb);

	ret = devm_request_irq(
		bootfb->dev, bootfb->idma_g0_irq,
		exynos9810_bootfb_idma_g0_irq_handler, IRQF_NO_AUTOEN,
		"exynos9810-bootfb-idma-g0", bootfb);
	if (ret)
		return ret;

	ret = devm_request_irq(
		bootfb->dev, bootfb->dpp_g0_irq,
		exynos9810_bootfb_dpp_g0_irq_handler, IRQF_NO_AUTOEN,
		"exynos9810-bootfb-dpp-g0", bootfb);
	if (ret)
		return ret;

	bootfb->g0_irqs_available = true;
	return 0;
}

static int
exynos9810_bootfb_enable_g0_irqs(struct exynos9810_bootfb *bootfb)
{
	u32 dpp_irq;
	u32 idma_irq;

	if (bootfb->g0_irqs_enabled)
		return 0;
	if (!bootfb->g0_irqs_available)
		return -ENODEV;

	/*
	 * Preserve the bootloader interrupt policy: both blocks must have IRQs
	 * enabled and framedone unmasked. Refuse takeover if their state differs.
	 */
	idma_irq = readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_IRQ);
	dpp_irq = readl(bootfb->dpp_g0 + EXYNOS9810_DPP_G0_IRQ);

	if (!(idma_irq & EXYNOS9810_IDMA_IRQ_ENABLE) ||
	    (idma_irq & EXYNOS9810_IDMA_IRQ_FRAMEDONE_MASK) ||
	    !(dpp_irq & EXYNOS9810_DPP_IRQ_ENABLE) ||
	    (dpp_irq & EXYNOS9810_DPP_IRQ_FRAMEDONE_MASK))
		return -EOPNOTSUPP;

	exynos9810_bootfb_clear_g0_irq_pending(bootfb);
	atomic64_set(&bootfb->native_irq_arm_ns, 0);

	enable_irq(bootfb->idma_g0_irq);
	enable_irq(bootfb->dpp_g0_irq);
	bootfb->g0_irqs_enabled = true;

	return 0;
}

static void
exynos9810_bootfb_disable_g0_irqs(struct exynos9810_bootfb *bootfb)
{
	if (!bootfb->g0_irqs_enabled)
		return;

	disable_irq(bootfb->dpp_g0_irq);
	disable_irq(bootfb->idma_g0_irq);
	bootfb->g0_irqs_enabled = false;
	atomic64_set(&bootfb->native_irq_arm_ns, 0);
	exynos9810_bootfb_clear_g0_irq_pending(bootfb);
}

static void
exynos9810_bootfb_write_native_source(
	struct exynos9810_bootfb *bootfb, u32 base_y, u32 base_c, u32 input)
{
	writel(base_c, bootfb->idma_g0 + EXYNOS9810_IDMA_G0_BASE_C);
	writel(base_y, bootfb->idma_g0 + EXYNOS9810_IDMA_G0_BASE_Y);
	writel(input, bootfb->idma_g0 + EXYNOS9810_IDMA_G0_INPUT_CONTROL);
}

static int
exynos9810_bootfb_latch_native_source(
	struct exynos9810_bootfb *bootfb,
	u32 base_y, u32 base_c, u32 input,
	u64 *wait_ns, u64 *idle_ns)
{
	u64 irq_wait_ns = 0;
	u32 dpp_enable;
	u32 idma_enable;
	u32 frame_before;
	int dpp_irq_before = 0;
	int idma_irq_before = 0;
	int irq_ret = 0;
	int ret;

	if (bootfb->g0_irqs_enabled) {
		idma_irq_before = atomic_read(
			&bootfb->idma_g0_framedone_count);
		dpp_irq_before = atomic_read(
			&bootfb->dpp_g0_framedone_count);
		atomic64_set(&bootfb->native_irq_arm_ns, ktime_get_ns());
	}

	frame_before = readl(bootfb->decon + EXYNOS9810_DECON_FRAME_COUNT);
	exynos9810_bootfb_write_native_source(
		bootfb, base_y, base_c, input);

	ret = exynos9810_bootfb_kick_native_g0(bootfb);
	if (ret)
		return ret;
	atomic_inc(&bootfb->decon_kick_count);

	ret = exynos9810_bootfb_wait_native_frame(
		bootfb, frame_before, wait_ns);
	if (ret)
		return ret;

	if (bootfb->g0_irqs_enabled) {
		irq_ret = exynos9810_bootfb_wait_native_dpp_irq(
			bootfb, dpp_irq_before, &irq_wait_ns);
		if (!irq_ret) {
			idma_enable = readl(
				bootfb->idma_g0 + EXYNOS9810_IDMA_G0_ENABLE);
			dpp_enable = readl(
				bootfb->dpp_g0 + EXYNOS9810_DPP_G0_ENABLE);
			if ((idma_enable & EXYNOS9810_DPU_OP_STATUS) ||
			    (dpp_enable & EXYNOS9810_DPU_OP_STATUS))
				atomic_inc(
					&bootfb->native_dpp_irq_early_busy_count);
		}
	}

	/*
	 * Confirm both blocks are idle after framedone. This normally completes
	 * immediately and also covers an early or missing interrupt.
	 */
	ret = exynos9810_bootfb_wait_native_idle(bootfb, idle_ns);
	atomic64_set(&bootfb->native_present_last_idle_ns, *idle_ns);
	if (ret)
		return ret;

	if (bootfb->g0_irqs_enabled) {
		synchronize_irq(bootfb->idma_g0_irq);
		synchronize_irq(bootfb->dpp_g0_irq);
		atomic_inc(&bootfb->native_irq_sample_count);
		if (atomic_read(&bootfb->idma_g0_framedone_count) ==
		    idma_irq_before)
			atomic_inc(&bootfb->native_idma_irq_miss_count);
		if (atomic_read(&bootfb->dpp_g0_framedone_count) ==
		    dpp_irq_before)
			atomic_inc(&bootfb->native_dpp_irq_miss_count);
	}

	if (readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_CONFIG_ERROR))
		return -EIO;
	return 0;
}

static int
exynos9810_bootfb_enable_native_present_auto(
	struct exynos9810_bootfb *bootfb)
{
	int ret;

	if (bootfb->native_present_enabled)
		return 0;

	ret = exynos9810_bootfb_validate_g0_native_runtime(bootfb);
	if (ret)
		return ret;

	ret = exynos9810_bootfb_enable_g0_irqs(bootfb);
	if (ret)
		return ret;

	bootfb->native_present_saved_input = readl(
		bootfb->idma_g0 + EXYNOS9810_IDMA_G0_INPUT_CONTROL);
	bootfb->native_present_saved_base_y = readl(
		bootfb->idma_g0 + EXYNOS9810_IDMA_G0_BASE_Y);
	bootfb->native_present_saved_base_c = readl(
		bootfb->idma_g0 + EXYNOS9810_IDMA_G0_BASE_C);
	bootfb->native_present_active_slot = -1;
	bootfb->native_present_hw_active = false;
	bootfb->native_present_error = 0;

	bootfb->native_present_enabled = true;
	bootfb->native_auto_active = true;
	atomic64_set(&bootfb->native_auto_entry_ns, ktime_get_ns());
	dev_info(
		bootfb->dev,
		"E981D: automatic async-native scanout entered\n");

	return 0;
}

static int
exynos9810_bootfb_wait_async_completion(
	struct exynos9810_bootfb *bootfb)
{
	unsigned long timeout;
	u64 idle_ns = 0;
	long waited;
	int error;

	if (!READ_ONCE(bootfb->native_async_pending))
		return 0;

	if (atomic_read(&bootfb->native_async_complete_state) != 2) {
		atomic_inc(&bootfb->native_async_previous_wait_count);
		timeout = msecs_to_jiffies(75);
		if (!timeout)
			timeout = 1;

		waited = wait_event_timeout(
			bootfb->native_async_wait,
			atomic_read(
				&bootfb->native_async_complete_state) == 2,
			timeout);
		if (!waited) {
			atomic_inc(
				&bootfb->native_async_previous_wait_timeout_count);

			/*
			 * Lost/delayed DPP IRQ fallback: if no worker claimed
			 * completion, use the proven synchronous idle criterion
			 * and signal the already-exported fence from here.
			 */
			if (atomic_cmpxchg(
				    &bootfb->native_async_complete_state,
				    0, 1) == 0) {
				error = exynos9810_bootfb_wait_native_idle(
					bootfb, &idle_ns);
				atomic64_set(
					&bootfb->native_present_last_idle_ns,
					idle_ns);
				if (!error &&
				    readl(
					    bootfb->idma_g0 +
					    EXYNOS9810_IDMA_G0_CONFIG_ERROR))
					error = -EIO;
				if (!error &&
				    readl(
					    bootfb->dpp_g0 +
					    EXYNOS9810_DPP_G0_CONFIG_ERROR))
					error = -EIO;
				atomic_inc(&bootfb->native_async_timeout_signal_count);
				exynos9810_bootfb_finish_async_fence(
					bootfb, error);
			}

			timeout = msecs_to_jiffies(60);
			if (!timeout)
				timeout = 1;
			waited = wait_event_timeout(
				bootfb->native_async_wait,
				atomic_read(
					&bootfb->native_async_complete_state) ==
					2,
				timeout);
			if (!waited)
				return -ETIMEDOUT;
		}
	}

	return 0;
}

static int
exynos9810_bootfb_finalize_async_present(
	struct exynos9810_bootfb *bootfb, bool wait)
{
	struct exynos9810_bootfb_native_slot *old_slot = NULL;
	struct dma_fence *fence;
	int new_index;
	int old_index;
	int error;
	int ret;

	if (!READ_ONCE(bootfb->native_async_pending))
		return 0;

	if (wait) {
		ret = exynos9810_bootfb_wait_async_completion(bootfb);
		if (ret)
			return ret;
	} else if (atomic_read(
			   &bootfb->native_async_complete_state) != 2) {
		return -EAGAIN;
	}

	smp_rmb();
	error = READ_ONCE(bootfb->native_async_error);
	if (error)
		return error;

	new_index = READ_ONCE(bootfb->native_async_new_slot);
	old_index = READ_ONCE(bootfb->native_async_old_slot);
	if (new_index < 0 ||
	    new_index >= (int)EXYNOS9810_BOOTFB_NATIVE_SLOT_COUNT)
		return -EIO;
	if (old_index < -1 ||
	    old_index >= (int)EXYNOS9810_BOOTFB_NATIVE_SLOT_COUNT)
		return -EIO;

	if (old_index >= 0)
		old_slot = &bootfb->native_slots[old_index];

	bootfb->native_present_active_slot = new_index;
	bootfb->native_present_hw_active = true;
	if (old_slot)
		exynos9810_bootfb_release_native_slot(bootfb, old_slot);

	fence = bootfb->native_async_fence;
	bootfb->native_async_fence = NULL;
	bootfb->native_async_new_slot = -1;
	bootfb->native_async_old_slot = -1;
	bootfb->native_async_dpp_before = 0;
	bootfb->native_async_error = 0;
	WRITE_ONCE(bootfb->native_async_pending, false);
	atomic_set(&bootfb->native_async_complete_state, 0);
	dma_fence_put(fence);

	atomic_inc(&bootfb->native_async_finalize_count);
	return 0;
}

static int
exynos9810_bootfb_try_native_present_async(
	struct exynos9810_bootfb *bootfb,
	struct exynos9810_bootfb_win_config *config)
{
	struct exynos9810_bootfb_native_slot *new_slot;
	struct dma_fence *fence = NULL;
	dma_addr_t iova;
	u64 frame_wait_ns = 0;
	u64 map_start_ns;
	u64 map_ns;
	u64 start_ns;
	u64 submit_ns;
	u32 frame_before;
	u32 input;
	int dpp_before;
	int new_index;
	int old_index;
	int restore_ret;
	int ret;

	ret = exynos9810_bootfb_finalize_async_present(bootfb, true);
	if (ret)
		goto fallback_without_new;

	start_ns = ktime_get_ns();
	old_index = bootfb->native_present_active_slot;
	new_index = old_index == 0 ? 1 : 0;
	if (old_index < 0)
		new_index = 0;

	new_slot = &bootfb->native_slots[new_index];
	if (new_slot->valid) {
		ret = -EBUSY;
		goto fallback_without_new;
	}

	iova = new_index ? EXYNOS9810_BOOTFB_NATIVE_SLOT1_IOVA :
		EXYNOS9810_BOOTFB_NATIVE_SLOT0_IOVA;

	map_start_ns = ktime_get_ns();
	ret = exynos9810_bootfb_map_native_slot(
		bootfb, config, new_slot, iova);
	map_ns = ktime_get_ns() - map_start_ns;
	if (ret)
		goto fallback_without_new;

	fence = exynos9810_bootfb_create_async_fence(bootfb);
	if (!fence) {
		ret = -ENOMEM;
		goto fallback;
	}

	/*
	 * Record the already-handled DPP sequence before programming.  If DPP
	 * completes unusually early, the post-arm recheck below schedules the
	 * completion worker even if the hardirq ran before async_pending=true.
	 */
	dpp_before = atomic_read(&bootfb->dpp_g0_framedone_count);
	atomic64_set(&bootfb->native_irq_arm_ns, ktime_get_ns());

	input = readl(
		bootfb->idma_g0 + EXYNOS9810_IDMA_G0_INPUT_CONTROL);
	input &= ~EXYNOS9810_IDMA_IMG_FORMAT_MASK;
	if (config->format == EXYNOS9810_FMT_RGBA8888)
		input |= (u32)EXYNOS9810_FMT_ABGR8888 << 11;
	else
		input |= EXYNOS9810_IDMA_IMG_FORMAT_CODE(
		EXYNOS9810_FMT_XBGR8888);

	frame_before = readl(
		bootfb->decon + EXYNOS9810_DECON_FRAME_COUNT);
	exynos9810_bootfb_write_native_source(
		bootfb, (u32)new_slot->iova, 0, input);
	/*
	 * Once source registers are written, a failed kick must restore the
	 * preserved source before any newly mapped slot can be released.
	 */
	bootfb->native_present_hw_active = true;

	ret = exynos9810_bootfb_kick_native_g0(bootfb);
	if (ret)
		goto fallback;
	atomic_inc(&bootfb->decon_kick_count);

	ret = exynos9810_bootfb_wait_native_frame(
		bootfb, frame_before, &frame_wait_ns);
	if (ret)
		goto fallback;
	if (readl(bootfb->idma_g0 + EXYNOS9810_IDMA_G0_CONFIG_ERROR)) {
		ret = -EIO;
		goto fallback;
	}

	bootfb->native_async_fence = fence;
	fence = NULL;
	bootfb->native_async_new_slot = new_index;
	bootfb->native_async_old_slot = old_index;
	bootfb->native_async_dpp_before = dpp_before;
	bootfb->native_async_error = 0;
	atomic_set(&bootfb->native_async_complete_state, 0);
	atomic64_set(&bootfb->native_async_submit_start_ns, start_ns);
	smp_wmb();
	WRITE_ONCE(bootfb->native_async_pending, true);

	/*
	 * Cover the narrow race where DPP framedone happened after dpp_before
	 * was sampled but before async_pending became visible to the handler.
	 */
	smp_mb();
	if (atomic_read(&bootfb->dpp_g0_framedone_count) != dpp_before)
		schedule_work(&bootfb->native_async_complete_work);

	bootfb->native_present_error = 0;
	bootfb->shadow_valid = false;
	bootfb->source_shadow_valid = false;

	submit_ns = ktime_get_ns() - start_ns;
	atomic_inc(&bootfb->native_present_count);
	atomic_inc(&bootfb->native_async_submit_count);
	atomic64_set(&bootfb->native_present_last_ns, submit_ns);
	atomic64_set(&bootfb->native_present_last_map_ns, map_ns);
	atomic64_set(&bootfb->native_present_last_wait_ns, frame_wait_ns);
	atomic64_set(&bootfb->native_async_last_submit_ns, submit_ns);
	if (submit_ns > atomic64_read(&bootfb->native_present_max_ns))
		atomic64_set(&bootfb->native_present_max_ns, submit_ns);
	if (submit_ns > atomic64_read(&bootfb->native_async_max_submit_ns))
		atomic64_set(&bootfb->native_async_max_submit_ns, submit_ns);

	return 0;

fallback:
	dma_fence_put(fence);
	atomic_inc(&bootfb->native_present_fallback_count);
	bootfb->native_present_error = ret;
	restore_ret = exynos9810_bootfb_disable_native_present(bootfb);
	if (restore_ret)
		return restore_ret;
	bootfb->native_present_error = ret;
	return ret;

fallback_without_new:
	atomic_inc(&bootfb->native_present_fallback_count);
	bootfb->native_present_error = ret;
	restore_ret = exynos9810_bootfb_disable_native_present(bootfb);
	if (restore_ret)
		return restore_ret;
	bootfb->native_present_error = ret;
	return ret;
}

static int
exynos9810_bootfb_disable_native_present(struct exynos9810_bootfb *bootfb)
{
	u64 wait_ns = 0;
	u64 idle_ns = 0;
	bool auto_active;
	int i;
	int ret = 0;

	auto_active = bootfb->native_auto_active;

	ret = exynos9810_bootfb_finalize_async_present(bootfb, true);
	if (ret) {
		bootfb->native_present_error = ret;
		return ret;
	}

	if (!bootfb->native_present_enabled &&
	    bootfb->native_present_active_slot < 0)
		return 0;

	if (bootfb->native_present_hw_active ||
	    bootfb->native_present_active_slot >= 0) {
		ret = exynos9810_bootfb_latch_native_source(
			bootfb,
			bootfb->native_present_saved_base_y,
			bootfb->native_present_saved_base_c,
			bootfb->native_present_saved_input,
			&wait_ns, &idle_ns);
		if (ret) {
			bootfb->native_present_error = ret;
			/* Keep every mapping pinned while hardware state is uncertain. */
			return ret;
		}
	}

	for (i = 0; i < EXYNOS9810_BOOTFB_NATIVE_SLOT_COUNT; i++)
		exynos9810_bootfb_release_native_slot(
			bootfb, &bootfb->native_slots[i]);

	bootfb->native_present_enabled = false;
	bootfb->native_present_hw_active = false;
	bootfb->native_present_active_slot = -1;
	bootfb->native_present_error = 0;
	bootfb->native_auto_active = false;
	if (auto_active) {
		atomic_inc(&bootfb->native_auto_restore_count);
		atomic64_set(
			&bootfb->native_auto_restore_ns, ktime_get_ns());
		dev_info(
			bootfb->dev,
			"E981D: automatic native scanout disabled\n");
	}
	bootfb->shadow_valid = false;
	bootfb->source_shadow_valid = false;
	exynos9810_bootfb_disable_g0_irqs(bootfb);
	return 0;
}

static int
exynos9810_bootfb_try_native_present(
	struct exynos9810_bootfb *bootfb,
	struct exynos9810_bootfb_win_config *config)
{
	int ret;

	if (!bootfb->native_present_enabled)
		return -EAGAIN;

	if (!exynos9810_bootfb_native_client_config_supported(bootfb, config)) {
		atomic_inc(&bootfb->native_present_fallback_count);
		ret = exynos9810_bootfb_disable_native_present(bootfb);
		return ret ? ret : -EAGAIN;
	}

	return exynos9810_bootfb_try_native_present_async(bootfb, config);
}

static struct attribute *exynos9810_bootfb_attrs[] = {
	&dev_attr_vsync.attr,
	&dev_attr_psr_info.attr,
	NULL,
};

static const struct attribute_group exynos9810_bootfb_group = {
	.attrs = exynos9810_bootfb_attrs,
};

static void
exynos9810_bootfb_fbdev_refresh_work(struct work_struct *work)
{
	struct exynos9810_bootfb *bootfb =
		container_of(work, struct exynos9810_bootfb,
			     fbdev_refresh_work);
	int ret;

	if (!READ_ONCE(bootfb->fbdev_refresh_enabled) ||
	    READ_ONCE(bootfb->fbdev_hwc_seen))
		return;

	mutex_lock(&bootfb->lock);

	if (!READ_ONCE(bootfb->fbdev_refresh_enabled) ||
	    READ_ONCE(bootfb->fbdev_hwc_seen) ||
	    bootfb->native_present_enabled)
		goto out;

	/*
	 * Plain fbdev users mmap the preserved front buffer and write pixels
	 * directly.  The panel is command-mode, so periodically kick the same
	 * preserved DECON/window path that the CPU HWC bridge already uses.
	 */
	wmb();
	ret = exynos9810_bootfb_kick_decon(bootfb);
	if (ret) {
		atomic_inc(&bootfb->fbdev_refresh_error_count);
		goto out;
	}

	atomic_inc(&bootfb->fbdev_refresh_count);
	if (!bootfb->fbdev_refresh_logged) {
		bootfb->fbdev_refresh_logged = true;
		dev_info(
			bootfb->dev,
			"E981D: plain fbdev command-mode refresh active\n");
	}

out:
	mutex_unlock(&bootfb->lock);
}

static void
exynos9810_bootfb_vsync_notify_work(struct work_struct *work)
{
	struct exynos9810_bootfb *bootfb =
		container_of(work, struct exynos9810_bootfb,
			     vsync_notify_work);

	if (READ_ONCE(bootfb->vsync_enabled))
		sysfs_notify(&bootfb->dev->kobj, NULL, "vsync");
}

static enum hrtimer_restart
exynos9810_bootfb_vsync_timer(struct hrtimer *timer)
{
	struct exynos9810_bootfb *bootfb =
		container_of(timer, struct exynos9810_bootfb, vsync_timer);

	atomic64_set(&bootfb->vsync_timestamp, ktime_get_ns());
	atomic64_inc(&bootfb->vsync_sequence);
	wake_up_interruptible(&bootfb->vsync_wait);
	if (READ_ONCE(bootfb->vsync_enabled))
		schedule_work(&bootfb->vsync_notify_work);
	if (READ_ONCE(bootfb->fbdev_refresh_enabled) &&
	    !READ_ONCE(bootfb->fbdev_hwc_seen))
		schedule_work(&bootfb->fbdev_refresh_work);

	hrtimer_forward_now(timer, bootfb->vsync_period);
	return HRTIMER_RESTART;
}

static void exynos9810_bootfb_cleanup(void *data)
{
	struct exynos9810_bootfb *bootfb = data;

	hrtimer_cancel(&bootfb->vsync_timer);
	cancel_work_sync(&bootfb->fbdev_refresh_work);
	cancel_work_sync(&bootfb->vsync_notify_work);

	if (bootfb->native_present_enabled ||
	    bootfb->native_present_active_slot >= 0) {
		int native_present_ret;

		native_present_ret =
			exynos9810_bootfb_disable_native_present(bootfb);
		if (native_present_ret) {
			dev_err(
				bootfb->dev,
				"synchronous native present cleanup failed: %d; "
				"keeping display mappings pinned\n",
				native_present_ret);
			return;
		}
	}

	cancel_work_sync(&bootfb->native_async_complete_work);

	if (bootfb->translated_attached && bootfb->prepared_domain) {
		/*
		 * Detach the translated domain before releasing its provider and
		 * page table. The IOMMU core restores the default domain.
		 */
		iommu_detach_device(bootfb->prepared_domain, bootfb->dev);
		bootfb->translated_attached = false;
	}

	if (bootfb->iommu_supplier_active && bootfb->iommu_supplier) {
		pm_runtime_put_sync(&bootfb->iommu_supplier->dev);
		bootfb->iommu_supplier_active = false;
	}
	if (bootfb->iommu_supplier) {
		put_device(&bootfb->iommu_supplier->dev);
		bootfb->iommu_supplier = NULL;
	}

	if (bootfb->prepared_domain) {
		size_t unmapped;

		unmapped = iommu_unmap(
			bootfb->prepared_domain, bootfb->prepared_iova,
			bootfb->prepared_size);
		if (unmapped != bootfb->prepared_size)
			dev_warn(
				bootfb->dev,
				"prepared display IOMMU unmap short: %#zx/%#zx\n",
				unmapped, bootfb->prepared_size);
		iommu_domain_free(bootfb->prepared_domain);
		bootfb->prepared_domain = NULL;
	}

	unregister_framebuffer(bootfb->info);
	vfree(bootfb->source_shadow);
	vfree(bootfb->shadow);
	framebuffer_release(bootfb->info);
}

static int exynos9810_bootfb_probe(struct platform_device *pdev)
{
	struct exynos9810_bootfb *bootfb;
	struct fb_info *info;
	struct resource framebuffer;
	u64 required_size;
	int ret;

	info = framebuffer_alloc(sizeof(*bootfb), &pdev->dev);
	if (!info)
		return -ENOMEM;

	bootfb = info->par;
	bootfb->dev = &pdev->dev;
	bootfb->info = info;
	mutex_init(&bootfb->lock);
	init_waitqueue_head(&bootfb->vsync_wait);
	atomic64_set(&bootfb->vsync_timestamp, ktime_get_ns());
	atomic64_set(&bootfb->vsync_sequence, 0);
	atomic_set(&bootfb->present_count, 0);
	atomic_set(&bootfb->fbdev_open_count, 0);
	atomic_set(&bootfb->fbdev_refresh_count, 0);
	atomic_set(&bootfb->fbdev_refresh_error_count, 0);
	bootfb->decon_rgb_order_initialized = false;
	bootfb->decon_rgb_order_saved = 0;
	atomic_set(&bootfb->decon_rgb_order_fix_count, 0);
	bootfb->fbdev_refresh_enabled = false;
	bootfb->fbdev_hwc_seen = false;
	bootfb->fbdev_refresh_logged = false;

	bootfb->native_present_active_slot = -1;
	atomic_set(&bootfb->native_present_count, 0);
	atomic_set(&bootfb->native_present_fallback_count, 0);
	atomic64_set(&bootfb->native_present_last_ns, 0);
	atomic64_set(&bootfb->native_present_max_ns, 0);
	atomic64_set(&bootfb->native_present_last_map_ns, 0);
	atomic64_set(&bootfb->native_present_last_wait_ns, 0);
	bootfb->native_async_new_slot = -1;
	bootfb->native_async_old_slot = -1;
	bootfb->native_async_fence_context = dma_fence_context_alloc(1);
	atomic64_set(&bootfb->native_async_fence_seqno, 0);
	init_waitqueue_head(&bootfb->native_async_wait);
	atomic_set(&bootfb->native_async_complete_state, 0);
	atomic_set(&bootfb->native_async_submit_count, 0);
	atomic_set(&bootfb->native_async_signal_count, 0);
	atomic_set(&bootfb->native_async_finalize_count, 0);
	atomic_set(&bootfb->native_async_error_count, 0);
	atomic_set(&bootfb->native_async_early_busy_count, 0);
	atomic_set(&bootfb->native_async_previous_wait_count, 0);
	atomic_set(
		&bootfb->native_async_previous_wait_timeout_count, 0);
	atomic_set(&bootfb->native_async_irq_missed_count, 0);
	atomic64_set(&bootfb->native_async_submit_start_ns, 0);
	atomic64_set(&bootfb->native_async_last_submit_ns, 0);
	atomic64_set(&bootfb->native_async_max_submit_ns, 0);
	atomic64_set(&bootfb->native_async_last_complete_ns, 0);
	atomic64_set(&bootfb->native_async_max_complete_ns, 0);
	atomic_set(&bootfb->native_async_irq_signal_count, 0);
	atomic_set(&bootfb->native_async_worker_signal_count, 0);
	atomic_set(
		&bootfb->native_async_irq_busy_fallback_count, 0);
	atomic_set(
		&bootfb->native_async_irq_error_signal_count, 0);
	atomic_set(&bootfb->native_async_timeout_signal_count, 0);
	atomic64_set(&bootfb->native_async_last_irq_complete_ns, 0);
	atomic64_set(&bootfb->native_async_max_irq_complete_ns, 0);
	bootfb->native_auto_enabled = true;
	bootfb->native_auto_active = false;
	bootfb->native_auto_blocked = false;
	bootfb->native_auto_last_error = 0;
	atomic_set(&bootfb->native_auto_attempt_count, 0);
	atomic_set(&bootfb->native_auto_entry_count, 0);
	atomic_set(&bootfb->native_auto_failure_count, 0);
	atomic_set(&bootfb->native_auto_restore_count, 0);
	atomic64_set(&bootfb->native_auto_entry_ns, 0);
	atomic64_set(&bootfb->native_auto_restore_ns, 0);
	atomic_set(&bootfb->native_present_completion_count, 0);
	atomic_set(&bootfb->native_present_completion_timeout_count, 0);
	atomic_set(&bootfb->native_present_release_fence_count, 0);
	atomic64_set(&bootfb->native_present_last_idle_ns, 0);
	atomic64_set(&bootfb->native_present_max_idle_ns, 0);
	atomic_set(&bootfb->decon_kick_count, 0);
	atomic_set(&bootfb->decon_kick_failures, 0);
	atomic_set(&bootfb->fast_present_count, 0);
	atomic_set(&bootfb->generic_present_count, 0);
	atomic_set(&bootfb->damage_hint_count, 0);
	atomic_set(&bootfb->damage_frame_count, 0);
	atomic_set(&bootfb->last_damage_hint, 0);
	atomic_set(&bootfb->last_damage_used, 0);
	atomic_set(&bootfb->last_damage_y, 0);
	atomic_set(&bootfb->last_damage_h, 0);
	atomic_set(&bootfb->last_scan_y, 0);
	atomic_set(&bootfb->last_scan_rows, 0);
	atomic_set(&bootfb->last_source_changed_rows, 0);
	atomic_set(&bootfb->last_changed_rows, 0);
	atomic64_set(&bootfb->last_blit_bytes, 0);
	atomic64_set(&bootfb->last_blit_ns, 0);
	atomic64_set(&bootfb->max_blit_ns, 0);
	atomic64_set(&bootfb->last_fence_ns, 0);
	atomic64_set(&bootfb->last_begin_ns, 0);
	atomic64_set(&bootfb->last_map_ns, 0);
	atomic64_set(&bootfb->last_scan_ns, 0);
	atomic64_set(&bootfb->last_end_ns, 0);
	bootfb->source_shadow_format = -1;
	bootfb->last_fast_format = -1;

	ret = of_property_read_u32(pdev->dev.of_node, "width", &bootfb->width);
	if (ret)
		goto release_info;
	ret = of_property_read_u32(pdev->dev.of_node, "height", &bootfb->height);
	if (ret)
		goto release_info;
	ret = of_property_read_u32(pdev->dev.of_node, "stride", &bootfb->stride);
	if (ret)
		goto release_info;
	of_property_read_u32(pdev->dev.of_node, "width-mm", &bootfb->width_mm);
	of_property_read_u32(pdev->dev.of_node, "height-mm", &bootfb->height_mm);

	required_size = (u64)bootfb->stride * bootfb->height;
	if (!bootfb->width || !bootfb->height ||
	    bootfb->stride != bootfb->width * sizeof(u32) ||
	    required_size > SIZE_MAX) {
		ret = -EINVAL;
		goto release_info;
	}
	bootfb->screen_size = required_size;

	bootfb->decon = devm_platform_ioremap_resource_byname(pdev, "decon");
	if (IS_ERR(bootfb->decon)) {
		ret = PTR_ERR(bootfb->decon);
		goto release_info;
	}

	bootfb->dpp_g0 = devm_platform_ioremap_resource_byname(pdev, "dpp-g0");
	if (IS_ERR(bootfb->dpp_g0)) {
		ret = PTR_ERR(bootfb->dpp_g0);
		goto release_info;
	}

	bootfb->idma_g0 = devm_platform_ioremap_resource_byname(pdev, "idma-g0");
	if (IS_ERR(bootfb->idma_g0)) {
		ret = PTR_ERR(bootfb->idma_g0);
		goto release_info;
	}

	ret = exynos9810_bootfb_setup_g0_irqs(bootfb, pdev);
	if (ret)
		goto release_info;

	ret = of_reserved_mem_region_to_resource(pdev->dev.of_node, 0,
						 &framebuffer);
	if (ret)
		goto release_info;
	if (resource_size(&framebuffer) < bootfb->screen_size) {
		ret = -EINVAL;
		goto release_info;
	}

	bootfb->screen = devm_ioremap_wc(&pdev->dev, framebuffer.start,
					 bootfb->screen_size);
	if (!bootfb->screen) {
		ret = -ENOMEM;
		goto release_info;
	}

	bootfb->shadow = vzalloc(bootfb->screen_size);
	if (!bootfb->shadow) {
		ret = -ENOMEM;
		goto release_info;
	}

	bootfb->line = devm_kmalloc(&pdev->dev, bootfb->stride, GFP_KERNEL);
	if (!bootfb->line) {
		ret = -ENOMEM;
		goto release_shadow;
	}

	/* Optional 17 MiB raw history; allocation failure is non-fatal. */
	bootfb->source_shadow = vzalloc(bootfb->screen_size);
	if (!bootfb->source_shadow)
		dev_warn(&pdev->dev,
			 "raw source history unavailable; using convert-all fast path\n");

	strscpy(info->fix.id, "exynos9810", sizeof(info->fix.id));
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.accel = FB_ACCEL_NONE;
	info->fix.smem_start = framebuffer.start;
	info->fix.smem_len = bootfb->screen_size;
	info->fix.line_length = bootfb->stride;
	info->screen_base = bootfb->screen;
	info->screen_size = bootfb->screen_size;
	info->fbops = &exynos9810_bootfb_ops;
	info->pseudo_palette = devm_kcalloc(&pdev->dev, 16, sizeof(u32),
					    GFP_KERNEL);
	if (!info->pseudo_palette) {
		ret = -ENOMEM;
		goto release_shadow;
	}

	info->var.xres = bootfb->width;
	info->var.yres = bootfb->height;
	info->var.xres_virtual = bootfb->width;
	info->var.yres_virtual = bootfb->height;
	info->var.bits_per_pixel = 32;
	info->var.width = bootfb->width_mm;
	info->var.height = bootfb->height_mm;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.vmode = FB_VMODE_NONINTERLACED;
	info->var.red = (struct fb_bitfield) { 16, 8, 0 };
	info->var.green = (struct fb_bitfield) { 8, 8, 0 };
	info->var.blue = (struct fb_bitfield) { 0, 8, 0 };
	info->var.transp = (struct fb_bitfield) { 24, 8, 0 };
	info->var.reserved[0] = bootfb->width;
	info->var.reserved[1] = bootfb->height;

	platform_set_drvdata(pdev, bootfb);
	ret = devm_device_add_group(&pdev->dev, &exynos9810_bootfb_group);
	if (ret)
		goto release_shadow;

	ret = register_framebuffer(info);
	if (ret)
		goto release_shadow;

	bootfb->vsync_period =
		ns_to_ktime(DIV_ROUND_CLOSEST_ULL(NSEC_PER_SEC,
						  EXYNOS9810_BOOTFB_FPS));
	INIT_WORK(&bootfb->vsync_notify_work,
		  exynos9810_bootfb_vsync_notify_work);
	INIT_WORK(&bootfb->fbdev_refresh_work,
		  exynos9810_bootfb_fbdev_refresh_work);
	INIT_WORK(&bootfb->native_async_complete_work,
		  exynos9810_bootfb_async_complete_work);
	hrtimer_setup(&bootfb->vsync_timer, exynos9810_bootfb_vsync_timer,
		      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_start(&bootfb->vsync_timer, bootfb->vsync_period,
		      HRTIMER_MODE_REL);

	ret = devm_add_action_or_reset(&pdev->dev,
				       exynos9810_bootfb_cleanup, bootfb);
	if (ret)
		return ret;

	bootfb->prepare_iommu_error =
		exynos9810_bootfb_prepare_iommu_domain(bootfb);
	if (bootfb->prepare_iommu_error) {
		dev_warn(
			&pdev->dev,
			"translated display domain preparation failed: %d\n",
			bootfb->prepare_iommu_error);
	} else {
		dev_info(
			&pdev->dev,
			"E981D: prepared display IOMMU map %#llx -> %#llx size=%#zx; "
			"master remains identity\n",
			(unsigned long long)bootfb->prepared_iova,
			(unsigned long long)bootfb->prepared_phys,
			bootfb->prepared_size);

		bootfb->attach_iommu_error =
			exynos9810_bootfb_attach_prepared_iommu(bootfb);
		if (bootfb->attach_iommu_error)
			dev_warn(
				&pdev->dev,
				"translated display IOMMU attach failed: %d; "
				"identity scanout retained\n",
				bootfb->attach_iommu_error);
		else
			dev_info(
				&pdev->dev,
				"E981D: display master switched to prepared "
				"translated domain at IOVA %#llx\n",
				(unsigned long long)bootfb->prepared_iova);
	}

	dev_info(&pdev->dev,
		 "E981D: preserved %ux%u boot scanout exposed as fb%d\n",
		 bootfb->width, bootfb->height, info->node);
	return 0;

release_shadow:
	vfree(bootfb->source_shadow);
	vfree(bootfb->shadow);
release_info:
	framebuffer_release(info);
	return dev_err_probe(&pdev->dev, ret,
			     "failed to register preserved boot scanout\n");
}

static const struct of_device_id exynos9810_bootfb_of_match[] = {
	{ .compatible = "samsung,exynos9810-bootfb" },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos9810_bootfb_of_match);

static struct platform_driver exynos9810_bootfb_driver = {
	.probe = exynos9810_bootfb_probe,
	.driver = {
		.name = "exynos9810-bootfb",
		.of_match_table = exynos9810_bootfb_of_match,
	},
};
module_platform_driver(exynos9810_bootfb_driver);

MODULE_DESCRIPTION("Exynos9810 preserved boot framebuffer bridge");
MODULE_LICENSE("GPL");
