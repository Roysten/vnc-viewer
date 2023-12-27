#pragma once

#include <xkbcommon/xkbcommon.h>

#include "input.h"
#include "rfb.h"

enum Vnc_input_state_wheel_scroll_direction {
	VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_UP,
	VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_DOWN,
};

struct Vnc_input_state_key_event {
	u32 keysym;
	bool pressed;
};

struct Vnc_input_state {
	struct {
		double x;
		double y;
	} pos;
	struct {
		double width;
		double height;
	} desktop_size;
	u8 button_mask;
	u32 wheel_scrolls;
	enum Vnc_input_state_wheel_scroll_direction wheel_scroll_direction;
	struct Vnc_input_action callbacks;

	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;

	struct Vnc_input_state_key_event key_events[256];
	size_t key_event_count;

	int key_repeat_tfd;
	struct {
		u32 keycode;
		u64 timestamp_usec;
	} key_repeat;
};

bool vnc_input_state_init(struct Vnc_input_state *input_state);

void vnc_input_state_pointer_move(struct Vnc_input_action *action, double dx, double dy);
void vnc_input_state_pointer_button(struct Vnc_input_action *action, u32 button, bool pressed);
void vnc_input_state_pointer_wheel_scroll(struct Vnc_input_action *action, double scroll_value);
void vnc_input_state_keyboard_key(struct Vnc_input_action *action, u32 key, bool pressed,
				  u64 timestamp_usec);
void vnc_input_state_pointer_reset_wheel_scrolls(struct Vnc_input_state *input_state);
struct Vnc_input_state_key_event *
vnc_input_state_pop_keyboard_key_events(struct Vnc_input_state *input_state,
					size_t *key_event_count);

void vnc_input_state_desktop_size_update(struct Vnc_input_state *input_state, double width,
					 double height);

int vnc_input_state_get_key_repeat_tfd(struct Vnc_input_state *input_state);
void vnc_input_state_reset_key_repeat_tfd(struct Vnc_input_state *input_state);
void vnc_input_state_get_repeat_key_event(struct Vnc_input_state *input_state,
					  struct Vnc_input_state_key_event *key_event);
