#pragma once

#include <stdbool.h>

#include "fb.h"
#include "types.h"

struct Vnc_drm {
	int fd;
	struct Vnc_framebuffer fbs[2];
	u32 fb_ids[2];
	u32 crtc_id;
};

bool vnc_drm_init(struct Vnc_drm *drm);
void vnc_drm_deinit(struct Vnc_drm *drm);
bool vnc_drm_flip_buffer(struct Vnc_drm *drm, u32 fb_index);
