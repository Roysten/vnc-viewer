#pragma once

#include <poll.h>
#include <stdbool.h>

#include "input.h"
#include "session.h"

struct Vnc_event_loop {
	struct pollfd pollfds[4];
};

enum Vnc_event_type {
	VNC_EVENT_TYPE_LIBINPUT = 1,
	VNC_EVENT_TYPE_KEY_REPEAT = 2,
	VNC_EVENT_TYPE_VNC = 4,
	VNC_EVENT_TYPE_EXIT = 8,
};

bool vnc_event_loop_init(struct Vnc_event_loop *event_loop);
bool vnc_event_loop_register_libinput(struct Vnc_event_loop *event_loop, int fd);
bool vnc_event_loop_register_key_repeat(struct Vnc_event_loop *event_loop, int fd);
bool vnc_event_loop_register_vnc(struct Vnc_event_loop *event_loop, int fd);
bool vnc_event_loop_process_events(struct Vnc_event_loop *event_loop, u32 *events);
void vnc_event_loop_exit(struct Vnc_event_loop *event_loop);
