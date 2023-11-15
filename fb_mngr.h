#pragma once

#include <limits.h>

#include "drm.h"
#include "fb.h"
#include "rfb.h"
#include "types.h"

struct Vnc_fb_mngr {
	struct Vnc_drm *drm;
	u32 current_fb;
	struct Vnc_rfb_rect rect_backlog[USHRT_MAX];
	u16 rect_backlog_count;
};

void vnc_fb_mngr_init(struct Vnc_fb_mngr *mngr, struct Vnc_drm *drm);
bool vnc_fb_mngr_register_drawn_rect(struct Vnc_fb_mngr *mngr, struct Vnc_rfb_rect *rect);
struct Vnc_framebuffer *vnc_fb_mngr_get_framebuffer(struct Vnc_fb_mngr *mngr);
bool vnc_fb_mngr_flip_buffers(struct Vnc_fb_mngr *mngr);
