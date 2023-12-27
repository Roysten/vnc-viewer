#include "session.h"

#include <assert.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.h"
#include "macros.h"

struct Vnc_session_thread_args {
	struct Vnc_session *session;
};

static void *vnc_session_thread(void *args);
static bool vnc_rfb_pointer_event_eq(struct Vnc_rfb_pointer_event *a,
				     struct Vnc_rfb_pointer_event *b);
static bool set_event(struct Vnc_session *session, enum Vnc_session_event event);
static bool handle_fence(struct Vnc_session *session);
static enum Vnc_rfb_result handle_rect(struct Vnc_rfb_framebuffer_update_action *action,
				       struct Vnc_rfb_rect *rect);
static u8 pointer_toggle_wheel_scroll_button_mask(
	u8 button_mask, enum Vnc_input_state_wheel_scroll_direction scroll_direction);

bool vnc_session_init(struct Vnc_session *session)
{
	*session = (struct Vnc_session){
		.last_sent_pointer_event = {
			.xpos = -1,
			.ypos = -1,
		},
		.event_fd = eventfd(0, EFD_CLOEXEC),
		.event_mutex = PTHREAD_MUTEX_INITIALIZER,
		.fbu_actions = {
			.handle_rect = handle_rect,
		},
	};
	if (session->event_fd == -1) {
		return false;
	}
	return true;
}

bool vnc_session_connect(struct Vnc_session *session, const char *address, u16 port)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		vnc_log_error("Socket create failed");
		return false;
	}
	session->fd = sock;

	struct timeval tv = { 1, 0 };
	int rc = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (rc != 0) {
		vnc_log_error("setsockopt failed");
		goto err;
	}

	rc = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	if (rc != 0) {
		vnc_log_error("setsockopt failed");
		goto err;
	}

	int flag = 1;
	rc = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
	if (rc != 0) {
		vnc_log_error("setsockopt failed");
		goto err;
	}

	in_addr_t encoded_addr = inet_addr(address);
	if (encoded_addr == INADDR_NONE) {
		vnc_log_error("Encode address failed");
		goto err;
	}

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr = { encoded_addr },
	};

	rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (rc == -1) {
		vnc_log_error("connect failed");
		goto err;
	}

	return true;

err:
	close(sock);
	return false;
}

bool vnc_session_initial_handshake(struct Vnc_session *session,
				   enum Vnc_rfb_security_type *security)
{
	enum Vnc_rfb_version version = VNC_RFB_VERSION_UNKNOWN;
	enum Vnc_rfb_result result = vnc_rfb_recv_version(session->fd, &version);
	if (result != VNC_RFB_RESULT_SUCCESS) {
		vnc_log_error("Unable to read server RFB version (%s)",
			      vnc_rfb_result_to_str(result));
		return false;
	}

	if (version != VNC_RFB_VERSION_38) {
		vnc_log_error("This client cannot handle RFB version reported by server (%d)",
			      version);
		return false;
	}

	result = vnc_rfb_send_version(session->fd, version);
	if (result != VNC_RFB_RESULT_SUCCESS) {
		vnc_log_error("Failed writing client RFB version to server (%s)",
			      vnc_rfb_result_to_str(result));
		return false;
	}

	enum Vnc_rfb_security_type acceptable_securities[] = {
		VNC_RFB_SECURITY_TYPE_NONE,
		VNC_RFB_SECURITY_TYPE_VNCAUTH,
	};
	u8 best_security_index;
	result = vnc_rfb_has_desired_security_type(session->fd, acceptable_securities,
						   ARRAY_COUNT(acceptable_securities),
						   &best_security_index);
	if (result != VNC_RFB_RESULT_SUCCESS) {
		vnc_log_error("Handshake failed: no acceptable security type found");
		return false;
	}
	*security = acceptable_securities[best_security_index];
	vnc_log_debug("Chosen security %d", *security);
	return true;
}

bool vnc_session_send_auth(struct Vnc_session *session, const char *passwd,
			   enum Vnc_rfb_security_type security)
{
	switch (security) {
	case VNC_RFB_SECURITY_TYPE_INVALID:
		vnc_log_error("Invalid security type");
		return false;
	case VNC_RFB_SECURITY_TYPE_NONE:
		vnc_rfb_send_security_type(session->fd, security);
		break;
	case VNC_RFB_SECURITY_TYPE_VNCAUTH:
		if (vnc_rfb_send_security_type(session->fd, security) == VNC_RFB_RESULT_SUCCESS) {
			struct Vnc_rfb_vncauth_challenge challenge = { 0 };
			enum Vnc_rfb_result result =
				vnc_rfb_recv_challenge(session->fd, &challenge);
			if (result != VNC_RFB_RESULT_SUCCESS) {
				vnc_log_error("Unable to get challenge: %s",
					      vnc_rfb_result_to_str(result));
				return false;
			}

			result = vnc_rfb_send_passwd(session->fd, &challenge, passwd);
			if (result != VNC_RFB_RESULT_SUCCESS) {
				vnc_log_error("Unable to send password: %s",
					      vnc_rfb_result_to_str(result));
				return false;
			}

			result = vnc_rfb_recv_security_result(session->fd);
			if (result != VNC_RFB_RESULT_SUCCESS) {
				vnc_log_error("Security negotiation failed: %s",
					      vnc_rfb_result_to_str(result));
				return false;
			}
		}
		break;
	}
	return true;
}

int vnc_session_get_fd(struct Vnc_session *session)
{
	return session->fd;
}

bool vnc_session_exchange_connection_params(struct Vnc_session *session, bool shared_connection,
					    u16 screen_width, u16 screen_height)
{
	enum Vnc_rfb_result result = vnc_rfb_send_client_init(session->fd, shared_connection);
	if (result != VNC_RFB_RESULT_SUCCESS) {
		vnc_log_error("vnc_rfb_send_client_init failed: %s", vnc_rfb_result_to_str(result));
		return false;
	}

	result = vnc_rfb_recv_server_init(session->fd, &session->server_settings);
	if (result != VNC_RFB_RESULT_SUCCESS) {
		vnc_log_error("vnc_rfb_recv_server_init failed: %s", vnc_rfb_result_to_str(result));
		return false;
	}
	vnc_log_debug("Server settings -- w: %u h: %u bpp: %u depth: %u name len: %u name: \"%s\"",
		      session->server_settings.width, session->server_settings.height,
		      session->server_settings.pixel_format.bpp,
		      session->server_settings.pixel_format.depth,
		      session->server_settings.name_len, session->server_settings.name);

	enum Vnc_rfb_encoding encodings[] = {
		VNC_RFB_ENCODING_RAW,
		VNC_RFB_ENCODING_CONTINUOUS_UPDATES_PSEUDO,
		VNC_RFB_ENCODING_FENCE_PSEUDO,
		VNC_RFB_ENCODING_EXTENDED_DESKTOP_SIZE_PSEUDO,
	};
	result = vnc_rfb_send_encodings(session->fd, encodings, ARRAY_COUNT(encodings));
	if (result != VNC_RFB_RESULT_SUCCESS) {
		vnc_log_error("vnc_rfb_send_encodings failed: %s", vnc_rfb_result_to_str(result));
		return false;
	}

	if (session->server_settings.width != screen_width ||
	    session->server_settings.height != screen_height) {
		struct Vnc_rfb_set_desktop_size set_desktop_size = {
			.message_type = VNC_RFB_CLIENT_MESSAGE_TYPE_SET_DESKTOP_SIZE,
			.width = htons(screen_width),
			.height = htons(screen_height),
			.number_of_screens = 1,
			.screens = { {
				.id = htonl(0),
				.xpos = htons(0),
				.ypos = htons(0),
				.width = htons(screen_width),
				.height = htons(screen_height),
				.flags = htonl(0),
			} }
		};
		enum Vnc_rfb_result result =
			vnc_rfb_send_set_desktop_size(session->fd, &set_desktop_size);
		if (result != VNC_RFB_RESULT_SUCCESS) {
			vnc_log_debug("vnc_rfb_send_set_desktop_size failed");
			return false;
		}
	}

	return true;
}

bool vnc_session_handle_message(struct Vnc_session *session)
{
	u8 message_type;
	enum Vnc_rfb_result result = vnc_rfb_peek_message_type(session->fd, &message_type);
	if (result == VNC_RFB_RESULT_ERROR_IO_RECV_TIMEOUT) {
		return true;
	}
	if (result != VNC_RFB_RESULT_SUCCESS) {
		// vnc_log_error("vnc_rfb_read_message_type failed: %s",
		// vnc_rfb_get_error());
		return false;
	}

	switch ((enum Vnc_rfb_server_message_type)message_type) {
	case VNC_RFB_SERVER_MESSAGE_TYPE_FENCE:
		return handle_fence(session);
	case VNC_RFB_SERVER_MESSAGE_TYPE_END_OF_CONTINUOUS_UPDATES: {
		vnc_log_debug("recvd end of continuous updates");
		session->server_supports_continuous_updates = true;
		session->continuous_updates_enabled = false;

		// Discard message type byte
		RFB_TRY_DISCARD(session->fd, 1);
	} break;
	case VNC_RFB_SERVER_MESSAGE_TYPE_FRAMEBUFFER_UPDATE:
		vnc_rfb_recv_framebuffer_update(session->fd, &session->fbu_actions);
		break;
	case VNC_RFB_SERVER_MESSAGE_TYPE_CUT_TEXT: {
		struct Vnc_rfb_cut_text cut_text;
		enum Vnc_rfb_result result = vnc_rfb_recv_cut_text(session->fd, &cut_text);
		if (result == VNC_RFB_RESULT_SUCCESS) {
			size_t to_read = ntohl(cut_text.length);
			RFB_TRY_DISCARD(session->fd, to_read);
		}
	} break;
	case VNC_RFB_SERVER_MESSAGE_TYPE_BELL:
		// Discard message type byte
		RFB_TRY_DISCARD(session->fd, 1);
		break;
	default:
		vnc_log_error("BUG: unhandled message type %u", message_type);
		assert(false);
	}

	if (!session->continuous_updates_enabled && session->server_supports_fence &&
	    session->server_supports_continuous_updates) {
		struct Vnc_rfb_enable_continuous_updates updates = {
			.message_type = VNC_RFB_CLIENT_MESSAGE_TYPE_CONTINUOUS_UPDATES,
			.enable = true,
			.x = htons(0),
			.y = htons(0),
			.width = htons(session->server_settings.width),
			.height = htons(session->server_settings.height),
		};
		enum Vnc_rfb_result result =
			vnc_rfb_send_enable_continuous_updates(session->fd, &updates);
		if (result != VNC_RFB_RESULT_SUCCESS) {
			vnc_log_error("Enable continuous updates failed: %s",
				      vnc_rfb_result_to_str(result));
		} else {
			session->continuous_updates_enabled = true;
		}
	}
	return true;
}

bool vnc_session_start_processing_continuous_updates(struct Vnc_session *session,
						     struct Vnc_fb_mngr *fb_mngr)
{
	struct Vnc_session_thread_args *thread_args = calloc(1, sizeof(*thread_args));
	thread_args->session = session;
	session->fb_mngr = fb_mngr;
	int rc = pthread_create(&session->thread_id, NULL, &vnc_session_thread, thread_args);
	if (rc != 0) {
		return false;
	}
	(void)pthread_setname_np(session->thread_id, "vnc_session");
	return true;
}

static void *vnc_session_thread(void *args)
{
	struct Vnc_session_thread_args *thread_args = args;
	for (;;) {
		if (!vnc_session_handle_message(thread_args->session)) {
			vnc_log_error("vnc_session_thread encountered an error");
			break;
		}
	}
	vnc_log_debug("thread done");
	pthread_exit(NULL);
}

bool vnc_session_send_pointer_event(struct Vnc_session *session, u16 xpos, u16 ypos, u8 button_mask)
{
	struct Vnc_rfb_pointer_event pointer_event = {
		.message_type = VNC_RFB_CLIENT_MESSAGE_TYPE_POINTER_EVENT,
		.button_mask = button_mask,
		.xpos = htons(xpos),
		.ypos = htons(ypos),
	};
	if (!vnc_rfb_pointer_event_eq(&pointer_event, &session->last_sent_pointer_event)) {
		enum Vnc_rfb_result result =
			vnc_rfb_send_pointer_event(session->fd, &pointer_event);
		if (result == VNC_RFB_RESULT_SUCCESS) {
			session->last_sent_pointer_event = pointer_event;
			return true;
		}
		return false;
	}
	return true;
}

bool vnc_session_send_key_event(struct Vnc_session *session,
				struct Vnc_input_state_key_event *key_event)
{
	struct Vnc_rfb_key_event rfb_key_event = {
		.message_type = VNC_RFB_CLIENT_MESSAGE_TYPE_KEY_EVENT,
		.down = key_event->pressed,
		.key = htonl(key_event->keysym),
	};
	enum Vnc_rfb_result result = vnc_rfb_send_key_event(session->fd, &rfb_key_event);
	return result == VNC_RFB_RESULT_SUCCESS;
}

static bool vnc_rfb_pointer_event_eq(struct Vnc_rfb_pointer_event *a,
				     struct Vnc_rfb_pointer_event *b)
{
	return a->xpos == b->xpos && a->ypos == b->ypos && a->button_mask == b->button_mask;
}

int vnc_session_get_event_fd(struct Vnc_session *session)
{
	return session->event_fd;
}

u32 vnc_session_get_events(struct Vnc_session *session)
{
	pthread_mutex_lock(&session->event_mutex);
	u32 events = session->event_bitmask;
	session->event_bitmask = 0;
	pthread_mutex_unlock(&session->event_mutex);
	return events;
}

static bool set_event(struct Vnc_session *session, enum Vnc_session_event event)
{
	pthread_mutex_lock(&session->event_mutex);
	u64 to_write = (session->event_bitmask |= 1 << event);
	ssize_t bytes_written = write(session->event_fd, &to_write, sizeof(to_write));
	pthread_mutex_unlock(&session->event_mutex);
	return bytes_written == sizeof(to_write);
}

static bool handle_fence(struct Vnc_session *session)
{
	session->server_supports_fence = true;
	struct Vnc_rfb_fence fence;
	enum Vnc_rfb_result result = vnc_rfb_recv_fence(session->fd, &fence);
	if (result != VNC_RFB_RESULT_SUCCESS) {
		vnc_log_error("recv fence failed");
		return false;
	}

	if ((fence.flags & (1u << 31)) > 0) {
		fence.flags = htonl(fence.flags & (~(1u << 31)));
		result = vnc_rfb_send_fence(session->fd, &fence);
		if (result != VNC_RFB_RESULT_SUCCESS) {
			vnc_log_error("send fence failed");
			return false;
		}
	}
	return true;
}

static enum Vnc_rfb_result handle_rect(struct Vnc_rfb_framebuffer_update_action *action,
				       struct Vnc_rfb_rect *rect)
{
	enum Vnc_rfb_result result = VNC_RFB_RESULT_SUCCESS;
	struct Vnc_session *session = container_of(action, struct Vnc_session, fbu_actions);
	// vnc_log_debug("rect -- x: %d y: %d w: %d h: %d enc: %d", rect->x, rect->y, rect->width, rect->height, rect->encoding);
	switch (rect->encoding) {
	case VNC_RFB_ENCODING_RAW: {
		struct Vnc_framebuffer *framebuffer = vnc_fb_mngr_get_framebuffer(session->fb_mngr);
		size_t bottom_right_pixel_index =
			(rect->y + rect->height - 1) * framebuffer->pitch +
			(rect->x + rect->width - 1) *
				(session->server_settings.pixel_format.bpp / 8);
		if (framebuffer->bpp < session->server_settings.pixel_format.bpp ||
		    bottom_right_pixel_index >= framebuffer->size) {
			vnc_log_error("RFB data does not fit in DRM buffer (%u vs %lu)",
				      framebuffer->size, bottom_right_pixel_index);
			exit(1);
		}
		result = vnc_rfb_recv_rect_raw(session->fd, rect, framebuffer->bpp,
					       framebuffer->pitch, framebuffer->buffer);
		vnc_fb_mngr_register_drawn_rect(session->fb_mngr, rect);
		vnc_fb_mngr_flip_buffers(session->fb_mngr);
	} break;
	case VNC_RFB_ENCODING_EXTENDED_DESKTOP_SIZE_PSEUDO: {
		vnc_log_debug(
			"set desktop size response -- reason: %u status code: %u new width: %u new height: %u",
			rect->x, rect->y, rect->width, rect->height);
		u8 number_of_screens;
		result = vnc_rfb_recv_number_of_screens(session->fd, &number_of_screens);
		if (result != VNC_RFB_RESULT_SUCCESS) {
			return result;
		}

		struct Vnc_rfb_screen screens[1];
		if (number_of_screens > ARRAY_COUNT(screens)) {
			vnc_log_error("unsupported number of screens: %u", number_of_screens);
			exit(1);
		}

		result = vnc_rfb_recv_screens(session->fd, screens, number_of_screens);
		if (result != VNC_RFB_RESULT_SUCCESS) {
			return result;
		}

		if (rect->y != 0) {
			vnc_log_error("server denied setting desktop size");
			return result;
		}

		pthread_mutex_lock(&session->event_mutex);
		session->server_settings.width = rect->width;
		session->server_settings.height = rect->height;
		pthread_mutex_unlock(&session->event_mutex);
		if (!set_event(session, VNC_SESSION_EVENT_SET_DESKTOP_SIZE)) {
			vnc_log_error("unable to set desktop size event");
			exit(1);
		}

		struct Vnc_rfb_enable_continuous_updates updates = {
			.message_type = VNC_RFB_CLIENT_MESSAGE_TYPE_CONTINUOUS_UPDATES,
			.enable = true,
			.x = htons(0),
			.y = htons(0),
			.width = htons(session->server_settings.width),
			.height = htons(session->server_settings.height),
		};
		result = vnc_rfb_send_enable_continuous_updates(session->fd, &updates);
		if (result != VNC_RFB_RESULT_SUCCESS) {
			vnc_log_error("Enable continuous updates failed: %s",
				      vnc_rfb_result_to_str(result));
			exit(1);
		}
	} break;
	default:
		vnc_log_error("unsupported encoding %d", rect->encoding);
		exit(1);
	}
	return result;
}

void vnc_session_get_server_settings(struct Vnc_session *session,
				     struct Vnc_rfb_server_init *server_settings)
{
	pthread_mutex_lock(&session->event_mutex);
	*server_settings = session->server_settings;
	pthread_mutex_unlock(&session->event_mutex);
}

void vnc_session_post_process_mouse_input(
	struct Vnc_session *session, u16 xpos, u16 ypos, u8 button_mask, u32 wheel_scrolls,
	enum Vnc_input_state_wheel_scroll_direction scroll_direction)
{
	if (wheel_scrolls == 0) {
		vnc_session_send_pointer_event(session, xpos, ypos, button_mask);
	} else {
		// We need to toggle the scroll up/down button
		// Make sure the scroll buttons are not enabled
		button_mask &= 0x7;
		for (size_t i = 0; i < wheel_scrolls; ++i) {
			button_mask = pointer_toggle_wheel_scroll_button_mask(button_mask,
									      scroll_direction);
			vnc_session_send_pointer_event(session, xpos, ypos, button_mask);
			button_mask = pointer_toggle_wheel_scroll_button_mask(button_mask,
									      scroll_direction);
			vnc_session_send_pointer_event(session, xpos, ypos, button_mask);
		}
	}
}

static u8 pointer_toggle_wheel_scroll_button_mask(
	u8 button_mask, enum Vnc_input_state_wheel_scroll_direction scroll_direction)
{
	static const u32 BTN_SCROLL_UP = 4;
	static const u32 BTN_SCROLL_DOWN = 3;
	u8 button_index = scroll_direction == VNC_INPUT_STATE_WHEEL_SCROLL_DIRECTION_UP ?
				  BTN_SCROLL_UP :
				  BTN_SCROLL_DOWN;
	if ((button_mask & (1 << button_index)) > 0) {
		button_mask &= ~(1 << button_index);
	} else {
		button_mask |= 1 << button_index;
	}
	return button_mask;
}

void vnc_session_post_process_keyboard_input(struct Vnc_session *session,
					     struct Vnc_input_state_key_event *key_events,
					     size_t key_event_count)
{
	for (size_t i = 0; i < key_event_count; ++i) {
		struct Vnc_input_state_key_event *key_event = &key_events[i];
		vnc_session_send_key_event(session, key_event);
	}
}

void vnc_session_handle_key_repeat(struct Vnc_session *session,
				   struct Vnc_input_state_key_event *key_event)
{
	vnc_session_send_key_event(session, key_event);
}
