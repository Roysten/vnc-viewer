#pragma once

#include <pthread.h>

#include "fb.h"
#include "fb_mngr.h"
#include "input_state.h"
#include "rfb.h"
#include "types.h"

enum Vnc_session_event {
	VNC_SESSION_EVENT_SET_DESKTOP_SIZE = 1,
};

struct Vnc_session {
	int fd;
	int event_fd;
	u32 event_bitmask;
	pthread_mutex_t event_mutex;
	struct Vnc_rfb_server_init server_settings;
	bool server_supports_continuous_updates;
	bool server_supports_fence;
	bool continuous_updates_enabled;
	struct Vnc_rfb_pointer_event last_sent_pointer_event;
	pthread_t thread_id;
	struct Vnc_rfb_framebuffer_update_action fbu_actions;
	struct Vnc_fb_mngr *fb_mngr;
};

bool vnc_session_init(struct Vnc_session *session);
bool vnc_session_connect(struct Vnc_session *session, const char *address, u16 port);
bool vnc_session_initial_handshake(struct Vnc_session *session,
				   enum Vnc_rfb_security_type *security);
bool vnc_session_send_auth(struct Vnc_session *session, const char *passwd,
			   enum Vnc_rfb_security_type security);
int vnc_session_get_fd(struct Vnc_session *session);
bool vnc_session_exchange_connection_params(struct Vnc_session *session, bool shared_connection,
					    u16 screen_width, u16 screen_height);
bool vnc_session_handle_message(struct Vnc_session *session);
bool vnc_session_start_processing_continuous_updates(struct Vnc_session *session,
						     struct Vnc_fb_mngr *fb_mngr);
bool vnc_session_send_pointer_event(struct Vnc_session *session, u16 xpos, u16 ypos,
				    u8 button_mask);
bool vnc_session_send_key_event(struct Vnc_session *session,
				struct Vnc_input_state_key_event *key_event);
int vnc_session_get_event_fd(struct Vnc_session *session);
u32 vnc_session_get_events(struct Vnc_session *session);
bool vnc_session_handle_fence(struct Vnc_session *session);
void vnc_session_get_server_settings(struct Vnc_session *session,
				     struct Vnc_rfb_server_init *server_settings);
void vnc_session_post_process_mouse_input(
	struct Vnc_session *session, u16 xpos, u16 ypos, u8 button_mask, u32 wheel_scrolls,
	enum Vnc_input_state_wheel_scroll_direction scroll_direction);
void vnc_session_post_process_keyboard_input(struct Vnc_session *session,
					     struct Vnc_input_state_key_event *key_events,
					     size_t key_event_count);
