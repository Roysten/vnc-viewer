#include "fb_mngr.h"

#include <string.h>

#include "log.h"
#include "macros.h"

void vnc_fb_mngr_init(struct Vnc_fb_mngr *mngr, struct Vnc_drm *drm)
{
	*mngr = (struct Vnc_fb_mngr){ 0 };
	mngr->drm = drm;
}

bool vnc_fb_mngr_register_drawn_rect(struct Vnc_fb_mngr *mngr, struct Vnc_rfb_rect *rect)
{
	if (mngr->rect_backlog_count == ARRAY_COUNT(mngr->rect_backlog)) {
		// vnc_log_error("BUG: Rect backlog overflow");
		return false;
	}

	mngr->rect_backlog[mngr->rect_backlog_count++] = *rect;
	return true;
}

struct Vnc_framebuffer *vnc_fb_mngr_get_framebuffer(struct Vnc_fb_mngr *mngr)
{
	return &mngr->drm->fbs[mngr->current_fb];
}

bool vnc_fb_mngr_flip_buffers(struct Vnc_fb_mngr *mngr)
{
	// FIXME: required for intel???
	bool ok = vnc_drm_flip_buffer(mngr->drm, mngr->current_fb);

	// TODO: double buffering
	/*struct Vnc_framebuffer *front_buffer = vnc_fb_mngr_get_framebuffer(mngr);
		   bool ok = vnc_drm_flip_buffer(mngr->drm, mngr->current_fb);
		   mngr->current_fb = (mngr->current_fb + 1) % ARRAY_COUNT(mngr->drm->fbs);
		   struct Vnc_framebuffer *back_buffer = vnc_fb_mngr_get_framebuffer(mngr);

		   u16 bytes_per_pixel = front_buffer->bpp / 8;
		   for (size_t i = 0; i < mngr->rect_backlog_count; ++i) {
		   struct Vnc_rfb_rect *rect = &mngr->rect_backlog[i];
		   for (u16 y = rect->y; y < rect->y + rect->height; ++y) {
		   size_t offset = ((rect->y * front_buffer->pitch + rect->x) *
		   bytes_per_pixel); size_t count = rect->width * bytes_per_pixel;
		   memcpy(&back_buffer[offset], &front_buffer[offset], count);
		   }
		   }
		   mngr->rect_backlog_count = 0; */
	return ok;
}
