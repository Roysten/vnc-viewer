#include "input.h"

#include "log.h"

static int open_restricted(const char *path, int flags, void *user_data);
static void close_restricted(int fd, void *user_data);

static const struct libinput_interface libinput_interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

bool vnc_input_init(struct Vnc_input *vnc_input, struct Vnc_logind *vnc_logind)
{
	*vnc_input = (struct Vnc_input){ .vnc_logind = vnc_logind };
	vnc_input->udev = udev_new();
	if (vnc_input->udev == NULL) {
		vnc_log_error("Init udev failed");
		return false;
	}

	vnc_input->libinput =
		libinput_udev_create_context(&libinput_interface, vnc_input, vnc_input->udev);
	if (vnc_input->libinput == NULL) {
		vnc_input_deinit(vnc_input);
		vnc_log_error("Init libinput failed");
		return false;
	}

	int rc = libinput_udev_assign_seat(vnc_input->libinput, "seat0");
	if (rc != 0) {
		vnc_input_deinit(vnc_input);
		vnc_log_error("Assign seat failed");
		return false;
	}

	return true;
}

void vnc_input_deinit(struct Vnc_input *vnc_input)
{
	if (vnc_input->libinput != NULL) {
		libinput_unref(vnc_input->libinput);
	}
	if (vnc_input->udev != NULL) {
		udev_unref(vnc_input->udev);
	}
}

static int open_restricted(const char *path, int flags, void *user_data)
{
	struct Vnc_input *vnc_input = user_data;
	int fd = -1;
	if (!vnc_logind_take_device(vnc_input->vnc_logind, path, &fd)) {
		return -1;
	}
	return fd;
}

static void close_restricted(int fd, void *user_data)
{
}

int vnc_input_get_fd(struct Vnc_input *input)
{
	return libinput_get_fd(input->libinput);
}

bool vnc_input_handle_events(struct Vnc_input *vnc_input, struct Vnc_input_action *callbacks)
{
	libinput_dispatch(vnc_input->libinput);
	struct libinput_event *event;
	while ((event = libinput_get_event(vnc_input->libinput)) != NULL) {
		enum libinput_event_type event_type = libinput_event_get_type(event);
		switch (event_type) {
		case LIBINPUT_EVENT_DEVICE_ADDED: {
			struct libinput_device *device = libinput_event_get_device(event);
			const char *dev_name = libinput_device_get_name(device);
			vnc_log_debug("libinput -- device added: %s", dev_name);
			break;
		}
		case LIBINPUT_EVENT_KEYBOARD_KEY: {
			struct libinput_event_keyboard *keyboard_event =
				libinput_event_get_keyboard_event(event);
			u32 button = libinput_event_keyboard_get_key(keyboard_event);
			enum libinput_key_state key_state =
				libinput_event_keyboard_get_key_state(keyboard_event);
			bool pressed = key_state == LIBINPUT_KEY_STATE_PRESSED;
			callbacks->keyboard_key(callbacks, button, pressed);
			break;
		}
		case LIBINPUT_EVENT_POINTER_MOTION: {
			struct libinput_event_pointer *pointer_event =
				libinput_event_get_pointer_event(event);
			double dx = libinput_event_pointer_get_dx(pointer_event);
			double dy = libinput_event_pointer_get_dy(pointer_event);
			callbacks->pointer_move(callbacks, dx, dy);
			break;
		}
		case LIBINPUT_EVENT_POINTER_BUTTON: {
			struct libinput_event_pointer *pointer_event =
				libinput_event_get_pointer_event(event);
			u32 button = libinput_event_pointer_get_button(pointer_event);
			enum libinput_button_state button_state =
				libinput_event_pointer_get_button_state(pointer_event);
			bool pressed = button_state == LIBINPUT_BUTTON_STATE_PRESSED;
			callbacks->pointer_button(callbacks, button, pressed);
			break;
		}
		case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL: {
			struct libinput_event_pointer *pointer_event =
				libinput_event_get_pointer_event(event);
			double scroll_value = libinput_event_pointer_get_scroll_value_v120(
				pointer_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
			callbacks->pointer_wheel_scroll(callbacks, scroll_value);
			break;
		}
		default:
			break;
		}

		libinput_event_destroy(event);
		libinput_dispatch(vnc_input->libinput);
	}
}
