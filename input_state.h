#pragma once

#include "input.h"
#include "rfb.h"

enum Vnc_input_state_wheel_scroll_direction {
	VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_UP,
	VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_DOWN,
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
};

bool vnc_input_state_init(struct Vnc_input_state *input_state);

void vnc_input_state_pointer_move(struct Vnc_input_action *action, double dx, double dy);
void vnc_input_state_pointer_button(struct Vnc_input_action *action, u32 button, bool pressed);
void vnc_input_state_pointer_wheel_scroll(struct Vnc_input_action *action, double scroll_value);
void vnc_input_state_pointer_reset_wheel_scrolls(struct Vnc_input_state *input_state);
void vnc_input_state_pointer_toggle_wheel_scroll_button_mask(struct Vnc_input_state *input_state);

void vnc_input_state_desktop_size_update(struct Vnc_input_state *input_state, double width,
					 double height);
