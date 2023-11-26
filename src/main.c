#include "d3des.h"
#include "drm.h"
#include "event_loop.h"
#include "fb_mngr.h"
#include "input.h"
#include "input_state.h"
#include "log.h"
#include "logind.h"
#include "macros.h"
#include "rfb.h"
#include "session.h"
#include "util.h"

#include <arpa/inet.h>

static struct Vnc_event_loop event_loop;

static void sigterm_handler(int signo)
{
	(void)signo;
	vnc_event_loop_exit(&event_loop);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	vnc_log_init("/tmp/vnc-client.log");

	bool ok = vnc_event_loop_init(&event_loop);
	if (!ok) {
		vnc_log_error("Unable to initialize event loop");
		return 1;
	}

	if (signal(SIGINT, sigterm_handler) == SIG_ERR) {
		vnc_log_error("Unable to set SIGINT handler");
	}

	if (signal(SIGTERM, sigterm_handler) == SIG_ERR) {
		vnc_log_error("Unable to set SIGTERM handler");
	}

	struct Vnc_session vnc_session;
	ok = vnc_session_init(&vnc_session);
	if (!ok) {
		vnc_log_error("vnc_session_init failed");
		return 1;
	}
	ok = vnc_session_connect(&vnc_session, "127.0.0.1", 5901);
	if (!ok) {
		vnc_log_error("vnc_session_connect failed");
		return 1;
	}

	enum Vnc_rfb_security_type security;
	ok = vnc_session_initial_handshake(&vnc_session, &security);
	if (!ok) {
		vnc_log_error("vnc_session_initial_handshake failed");
		return 1;
	}
	// TODO: Do this through custom DRM form
	printf("Password:\n");
	char password_buf[128] = { 0 };
	int rc = read_password(password_buf, ARRAY_COUNT(password_buf));
	if (rc != 0) {
		vnc_log_error("Password input failed");
		return 1;
	}

	struct Vnc_logind logind_session;
	ok = vnc_logind_init(&logind_session);
	if (!ok) {
		vnc_log_error("logind_session_init failure");
		return 1;
	}

	ok = vnc_logind_take_control(&logind_session);
	if (!ok) {
		vnc_log_error("logind_session_take_control failure");
		return 1;
	}

	struct Vnc_drm drm;
	ok = vnc_drm_init(&drm);
	if (!ok) {
		return 1;
	}

	struct Vnc_input vnc_input;
	ok = vnc_input_init(&vnc_input, &logind_session);
	if (!ok) {
		vnc_log_error("vnc_input_init failure");
		return 1;
	}

	ok = vnc_session_send_auth(&vnc_session, password_buf, security);
	memset(password_buf, 0, ARRAY_COUNT(password_buf));
	if (!ok) {
		vnc_log_error("Unable to send auth");
		return 1;
	}

	bool shared_connection = true;
	ok = vnc_session_exchange_connection_params(&vnc_session, shared_connection,
						    drm.fbs[0].width, drm.fbs[0].height);
	if (!ok) {
		vnc_log_error("Unable to exchange connection parameters");
		return 1;
	}

	struct Vnc_fb_mngr fb_mngr;
	vnc_fb_mngr_init(&fb_mngr, &drm);

	vnc_session_start_processing_continuous_updates(&vnc_session, &fb_mngr);

	struct Vnc_input_state input_state;
	vnc_input_state_init(&input_state);
	struct Vnc_rfb_server_init server_settings;
	vnc_session_get_server_settings(&vnc_session, &server_settings);
	vnc_input_state_desktop_size_update(&input_state, server_settings.width,
					    server_settings.height);

	vnc_event_loop_register_vnc(&event_loop, vnc_session_get_event_fd(&vnc_session));
	vnc_event_loop_register_libinput(&event_loop, vnc_input_get_fd(&vnc_input));

	u32 events;
	while ((ok = vnc_event_loop_process_events(&event_loop, &events))) {
		if ((events & VNC_EVENT_TYPE_VNC) > 0) {
			vnc_log_debug("Got vnc event");
			u64 eventfd_data;
			read(vnc_session_get_event_fd(&vnc_session), &eventfd_data,
			     sizeof(eventfd_data));
			struct Vnc_rfb_server_init server_settings;
			vnc_session_get_server_settings(&vnc_session, &server_settings);
			vnc_input_state_desktop_size_update(&input_state, server_settings.width,
							    server_settings.height);
		}
		if ((events & VNC_EVENT_TYPE_LIBINPUT) > 0) {
			vnc_input_handle_events(&vnc_input, &input_state.callbacks);

			vnc_session_post_process_mouse_input(&vnc_session, input_state.pos.x,
							     input_state.pos.y,
							     input_state.button_mask,
							     input_state.wheel_scrolls,
							     input_state.wheel_scroll_direction);
			vnc_input_state_pointer_reset_wheel_scrolls(&input_state);
		}

		size_t key_event_count;
		struct Vnc_input_state_key_event *key_events =
			vnc_input_state_pop_keyboard_key_events(&input_state, &key_event_count);
		vnc_session_post_process_keyboard_input(&vnc_session, key_events, key_event_count);

		if ((events & VNC_EVENT_TYPE_EXIT) > 0) {
			vnc_log_debug("Exit requested");
			break;
		}
	}

	vnc_drm_deinit(&drm);
	return 0;
}
