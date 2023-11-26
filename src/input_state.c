#include "input_state.h"

#include <math.h>
#include <linux/input-event-codes.h>

#include "log.h"
#include "macros.h"

static bool map_pointer_button(u32 libinput_code, u8 *button);
static void move_pointer(struct Vnc_input_state *input_state, double dx, double dy);

bool vnc_input_state_init(struct Vnc_input_state *input_state)
{
	struct xkb_context *xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (xkb_context == NULL) {
		vnc_log_error("xkb context is NULL");
		return false;
	}

	struct xkb_rule_names names = {
		.rules = NULL,
		.model = "pc105",
		.layout = "us",
		.variant = "",
		.options = "terminate:ctrl_alt_bksp",
	};

	struct xkb_keymap *xkb_keymap =
		xkb_keymap_new_from_names(xkb_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (xkb_keymap == NULL) {
		vnc_log_error("xkb keymap is NULL");
		return false;
	}

	struct xkb_state *xkb_state = xkb_state_new(xkb_keymap);
	if (xkb_state == NULL) {
		vnc_log_error("xkb state is NULL");
		return false;
	}

	*input_state = (struct Vnc_input_state) {
		.callbacks = {
			.pointer_move = vnc_input_state_pointer_move,
			.pointer_button = vnc_input_state_pointer_button,
			.pointer_wheel_scroll = vnc_input_state_pointer_wheel_scroll,
			.keyboard_key = vnc_input_state_keyboard_key,
		},
		.xkb_context = xkb_context,
		.xkb_keymap = xkb_keymap,
		.xkb_state = xkb_state,
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
	input_state->wheel_scroll_direction = v120 > 0 ?
						      VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_UP :
						      VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_DOWN;

	v120 = fabs(v120);
	while (v120 >= 120.0) {
		v120 -= 120.0;
		input_state->wheel_scrolls += 1;
	}
}

void vnc_input_state_keyboard_key(struct Vnc_input_action *action, u32 key, bool pressed,
				  u64 timestamp_usec)
{
	struct Vnc_input_state *input_state =
		container_of(action, struct Vnc_input_state, callbacks);
	key += 8;
	xkb_keysym_t keysym = xkb_state_key_get_one_sym(input_state->xkb_state, key);
	xkb_state_update_key(input_state->xkb_state, key, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
	if (input_state->key_event_count >= ARRAY_COUNT(input_state->key_events)) {
		vnc_log_error("key events full, dropping event");
		return;
	}

	input_state->key_events[input_state->key_event_count].keysym = keysym;
	input_state->key_events[input_state->key_event_count].pressed = pressed;
	input_state->key_event_count += 1;

	input_state->key_repeat.keysym = pressed ? keysym : XKB_KEY_NoSymbol;
	input_state->key_repeat.timestamp_usec = timestamp_usec;
}

void vnc_input_state_pointer_reset_wheel_scrolls(struct Vnc_input_state *input_state)
{
	input_state->wheel_scrolls = 0;
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

struct Vnc_input_state_key_event *
vnc_input_state_pop_keyboard_key_events(struct Vnc_input_state *input_state,
					size_t *key_event_count)
{
	*key_event_count = input_state->key_event_count;
	struct Vnc_input_state_key_event *events = input_state->key_events;
	input_state->key_event_count = 0;
	return events;
}
