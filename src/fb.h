#pragma once

#include "types.h"

struct Vnc_framebuffer {
	u32 width;
	u32 height;
	u32 pitch; // Stride (line length)
	u32 size; // Total size in bytes
	u32 bpp;
	char *buffer;
};
