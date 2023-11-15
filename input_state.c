#include "input_state.h"

#include <math.h>
#include <linux/input-event-codes.h>

#include "log.h"
#include "macros.h"

#define BTN_SCROLL_UP 4
#define BTN_SCROLL_DOWN 3

static bool map_pointer_button(u32 libinput_code, u8 *button);
static void move_pointer(struct Vnc_input_state *input_state, double dx, double dy);

bool vnc_input_state_init(struct Vnc_input_state *input_state)
{
	*input_state = (struct Vnc_input_state) {
		.callbacks = {
		.pointer_move = vnc_input_state_pointer_move,
			.pointer_button = vnc_input_state_pointer_button,
			.pointer_wheel_scroll = vnc_input_state_pointer_wheel_scroll,
		},
	};
	return true;
}

void vnc_input_state_pointer_move(struct Vnc_input_action *action, double dx, double dy)
{
	struct Vnc_input_state *input_state =
		container_of(action, struct Vnc_input_state, callbacks);
	move_pointer(input_state, dx, dy);
}

void vnc_input_state_pointer_button(struct Vnc_input_action *action, u32 button, bool pressed)
{
	struct Vnc_input_state *input_state =
		container_of(action, struct Vnc_input_state, callbacks);
	u8 vnc_button;
	bool ok = map_pointer_button(button, &vnc_button);
	if (ok) {
		if (pressed) {
			input_state->button_mask |= 1 << vnc_button;
		} else {
			input_state->button_mask &= ~(1 << vnc_button);
		}
	}
	vnc_log_debug("pointer button -- button: %u pressed: %d", button, pressed);
}

void vnc_input_state_pointer_wheel_scroll(struct Vnc_input_action *action, double v120)
{
	struct Vnc_input_state *input_state =
		container_of(action, struct Vnc_input_state, callbacks);
	(void)input_state;
	input_state->wheel_scroll_direction = v120 > 0 ?
						      VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_UP :
						      VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_DOWN;

	v120 = fabs(v120);
	while (v120 >= 120.0) {
		v120 -= 120.0;
		input_state->wheel_scrolls += 1;
	}
}

void vnc_input_state_pointer_reset_wheel_scrolls(struct Vnc_input_state *input_state)
{
	input_state->wheel_scrolls = 0;
}

void vnc_input_state_pointer_toggle_wheel_scroll_button_mask(struct Vnc_input_state *input_state)
{
	u8 button_index = input_state->wheel_scroll_direction ==
					  VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_UP ?
				  BTN_SCROLL_UP :
				  BTN_SCROLL_DOWN;
	if ((input_state->button_mask & (1 << button_index)) > 0) {
		input_state->button_mask &= ~(1 << button_index);
	} else {
		input_state->button_mask |= 1 << button_index;
	}
}

static bool map_pointer_button(u32 libinput_code, u8 *button)
{
	switch (libinput_code) {
	case BTN_LEFT:
		*button = 0;
		break;
	case BTN_MIDDLE:
		*button = 1;
		break;
	case BTN_RIGHT:
		*button = 2;
		break;
	default:
		return false;
	}
	return true;
}

static void move_pointer(struct Vnc_input_state *input_state, double dx, double dy)
{
	input_state->pos.x += dx;
	input_state->pos.y += dy;
	if (input_state->pos.x < 0) {
		input_state->pos.x = 0;
	} else if (input_state->pos.x > input_state->desktop_size.width) {
		input_state->pos.x = input_state->desktop_size.width;
	}

	if (input_state->pos.y < 0) {
		input_state->pos.y = 0;
	} else if (input_state->pos.y > input_state->desktop_size.height) {
		input_state->pos.y = input_state->desktop_size.height;
	}
}

void vnc_input_state_desktop_size_update(struct Vnc_input_state *input_state, double width,
					 double height)
{
	input_state->desktop_size.width = width;
	input_state->desktop_size.height = height;
	move_pointer(input_state, 0, 0);
}
