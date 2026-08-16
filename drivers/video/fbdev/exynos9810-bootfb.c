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
#include <linux/fb.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/iosys-map.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#define EXYNOS9810_BOOTFB_FPS		60
#define EXYNOS9810_BOOTFB_MAX_WINDOWS	6
#define EXYNOS9810_BOOTFB_PLANES		3
#define EXYNOS9810_BOOTFB_CHIP_ID	9810

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

struct exynos9810_bootfb {
	struct device *dev;
	struct fb_info *info;
	void __iomem *screen;
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
	ktime_t vsync_period;
	wait_queue_head_t vsync_wait;
	atomic64_t vsync_timestamp;
	atomic64_t vsync_sequence;
	atomic_t present_count;
	bool vsync_enabled;
};

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

static int exynos9810_bootfb_present(struct exynos9810_bootfb *bootfb,
				     unsigned int cmd, unsigned long arg)
{
	struct exynos9810_bootfb_config_data data;
	unsigned int i;
	bool updated = false;
	int ret = 0;

	if (cmd != S3CFB_WIN_CONFIG)
		return -EINVAL;

	memset(&data, 0, sizeof(data));
	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	mutex_lock(&bootfb->lock);
	memset(bootfb->shadow, 0, bootfb->screen_size);

	for (i = 0; i < ARRAY_SIZE(data.config); i++)
		data.config[i].rel_fence = -1;

	for (i = 0; i < EXYNOS9810_BOOTFB_MAX_WINDOWS; i++) {
		struct exynos9810_bootfb_win_config *config = &data.config[i];

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
			ret = exynos9810_bootfb_draw_buffer(bootfb, config);
			if (!ret)
				updated = true;
			else
				dev_warn_ratelimited(bootfb->dev,
						     "window %u skipped: %d\n",
						     i, ret);
			break;
		default:
			dev_warn_ratelimited(bootfb->dev,
					     "unknown window %u state %d\n",
					     i, config->state);
			break;
		}
	}

	if (updated) {
		memcpy_toio(bootfb->screen, bootfb->shadow, bootfb->screen_size);
		/* Publish all pixels before the next hardware scanout. */
		wmb();
	}

	if (atomic_inc_return(&bootfb->present_count) == 1)
		dev_info(bootfb->dev,
			 "E981D: first fbdev window configuration updated=%u\n",
			 updated);

	mutex_unlock(&bootfb->lock);

	data.present_fence = -1;
	if (copy_to_user((void __user *)arg, &data, sizeof(data)))
		return -EFAULT;

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
	return sysfs_emit(buf, "1\n0\n");
}
static DEVICE_ATTR_RO(psr_info);

static struct attribute *exynos9810_bootfb_attrs[] = {
	&dev_attr_vsync.attr,
	&dev_attr_psr_info.attr,
	NULL,
};

static const struct attribute_group exynos9810_bootfb_group = {
	.attrs = exynos9810_bootfb_attrs,
};

static enum hrtimer_restart
exynos9810_bootfb_vsync_timer(struct hrtimer *timer)
{
	struct exynos9810_bootfb *bootfb =
		container_of(timer, struct exynos9810_bootfb, vsync_timer);

	atomic64_set(&bootfb->vsync_timestamp, ktime_get_ns());
	atomic64_inc(&bootfb->vsync_sequence);
	wake_up_interruptible(&bootfb->vsync_wait);
	if (READ_ONCE(bootfb->vsync_enabled))
		sysfs_notify(&bootfb->dev->kobj, NULL, "vsync");

	hrtimer_forward_now(timer, bootfb->vsync_period);
	return HRTIMER_RESTART;
}

static void exynos9810_bootfb_cleanup(void *data)
{
	struct exynos9810_bootfb *bootfb = data;

	hrtimer_cancel(&bootfb->vsync_timer);
	unregister_framebuffer(bootfb->info);
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
	hrtimer_setup(&bootfb->vsync_timer, exynos9810_bootfb_vsync_timer,
		      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_start(&bootfb->vsync_timer, bootfb->vsync_period,
		      HRTIMER_MODE_REL);

	ret = devm_add_action_or_reset(&pdev->dev,
				       exynos9810_bootfb_cleanup, bootfb);
	if (ret)
		return ret;

	dev_info(&pdev->dev,
		 "E981D: preserved %ux%u boot scanout exposed as fb%d\n",
		 bootfb->width, bootfb->height, info->node);
	return 0;

release_shadow:
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
