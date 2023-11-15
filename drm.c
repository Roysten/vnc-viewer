#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "drm.h"
#include "log.h"
#include "macros.h"

static bool create_and_map_dumb_buffer(int drm_fd, struct Vnc_framebuffer *fb, u32 *drm_fb_id);

bool vnc_drm_init(struct Vnc_drm *drm)
{
	*drm = (struct Vnc_drm){ 0 };
	drm->fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
	// drm->fd = drmOpen(NULL, NULL);
	if (drm->fd == -1) {
		vnc_log_error("drmOpen failed");
		return false;
	}

	drmModeResPtr resources = drmModeGetResources(drm->fd);
	if (resources == NULL) {
		vnc_log_error("drmModeGetResources failed");
		goto err;
	}

	u32 connector_id = 0;
	drmModeConnectorPtr connector = NULL;
	for (int i = 0; i < resources->count_connectors; ++i) {
		connector_id = resources->connectors[i];
		connector = drmModeGetConnector(drm->fd, connector_id);
		if (connector == NULL) {
			vnc_log_error("drmModeGetConnector failed");
			goto err;
		}
		if (connector->count_modes != 0) {
			break;
		}
	}

	if (connector == NULL) {
		vnc_log_error("DRM: no suitable connector found");
		goto err;
	}
	// According to man drm-kms the first mode is the default and highest
	// resolution
	drmModeModeInfo mode = connector->modes[0];
	vnc_log_debug("#%s %d %d %d %d %d %d %d %d %d", mode.name, mode.hdisplay, mode.hsync_start,
		      mode.hsync_end, mode.htotal, mode.vdisplay, mode.vsync_start, mode.vsync_end,
		      mode.vtotal, mode.clock);

	if (!drmIsMaster(drm->fd)) {
		if (drmSetMaster(drm->fd) == -1) {
			vnc_log_error("drmSetMaster failed");
			goto err;
		}
	}

	for (size_t i = 0; i < ARRAY_COUNT(drm->fbs); ++i) {
		struct Vnc_framebuffer *fb = &drm->fbs[i];
		fb->width = mode.hdisplay;
		fb->height = mode.vdisplay;
		bool rc = create_and_map_dumb_buffer(drm->fd, fb, &drm->fb_ids[i]);
		if (!rc) {
			vnc_log_error("Create dumb buffer #1 failed");
			goto err;
		}
	}

	drm->crtc_id = resources->crtcs[0];
	drmModeSetCrtc(drm->fd, drm->crtc_id, drm->fb_ids[0], 0, 0, &connector_id, 1, &mode);
	drmModeFreeConnector(connector);
	drmModeFreeResources(resources);
	return true;

err:
	drmClose(drm->fd);
	return false;
}

static bool create_and_map_dumb_buffer(int drm_fd, struct Vnc_framebuffer *fb, u32 *drm_fb_id)
{
	fb->bpp = 32;
	struct drm_mode_create_dumb create_dumb_request = {
		.height = fb->height,
		.width = fb->width,
		.bpp = fb->bpp,
		.flags = 0,
	};

	int rc = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb_request);
	if (rc < 0) {
		vnc_log_error("Create dumb failed");
		return false;
	}
	fb->pitch = create_dumb_request.pitch;
	fb->size = create_dumb_request.size;

	rc = drmModeAddFB(drm_fd, fb->width, fb->height, 24, create_dumb_request.bpp, fb->pitch,
			  create_dumb_request.handle, drm_fb_id);

	struct drm_mode_map_dumb map_request = {
		.handle = create_dumb_request.handle,
	};
	rc = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_request);
	if (rc < 0) {
		return false;
	}

	char *map = mmap(0, create_dumb_request.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd,
			 map_request.offset);
	if (map == MAP_FAILED) {
		return false;
	}
	fb->buffer = map;
	memset(fb->buffer, 255, fb->size);
	return true;
}

void vnc_drm_deinit(struct Vnc_drm *drm)
{
	vnc_log_debug("DRM deinit");
	drmDropMaster(drm->fd);
	drmClose(drm->fd);
	*drm = (struct Vnc_drm){ 0 };
	drm->fd = -1;
}

bool vnc_drm_flip_buffer(struct Vnc_drm *drm, u32 fb_index)
{
	int rc = drmModePageFlip(drm->fd, drm->crtc_id, drm->fb_ids[fb_index], 0, NULL);
	return rc == 0;
}
