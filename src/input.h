#pragma once

#include <libinput.h>
#include <libudev.h>
#include <stdbool.h>

#include "logind.h"
#include "types.h"

struct Vnc_input_action {
	void (*pointer_move)(struct Vnc_input_action *action, double dx, double dy);
	void (*pointer_button)(struct Vnc_input_action *action, u32 button, bool pressed);
	void (*pointer_wheel_scroll)(struct Vnc_input_action *action, double scroll_value);
	void (*keyboard_key)(struct Vnc_input_action *action, u32 key, bool pressed,
			     u64 timestamp_usec);
};

struct Vnc_input {
	struct Vnc_logind *vnc_logind;
	struct udev *udev;
	struct libinput *libinput;
};

bool vnc_input_init(struct Vnc_input *vnc_input, struct Vnc_logind *vnc_logind);
void vnc_input_deinit(struct Vnc_input *vnc_input);
int vnc_input_get_fd(struct Vnc_input *vnc_input);
void vnc_input_handle_events(struct Vnc_input *vnc_input, struct Vnc_input_action *callbacks);
