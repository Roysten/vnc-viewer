#pragma once

#include <errno.h>
#include <stdio.h>

#include "fb.h"
#include "types.h"

#define RFB_VERSION_MSG_LEN 12
#define RFB_PACKED __attribute__((__packed__))

#define RFB_TRY_READ_IMPL(vnc_fd, dest, size, flags) \
	do { \
		size_t total_bytes_read = 0; \
		while (total_bytes_read < (size)) { \
			ssize_t bytes_read = recv((vnc_fd), ((u8 *)(dest)) + total_bytes_read, \
						  (size)-total_bytes_read, flags); \
			if (bytes_read < 0) { \
				if (errno == EINTR) { \
					continue; \
				} \
				if (errno == EAGAIN || errno == EWOULDBLOCK) { \
					return VNC_RFB_RESULT_ERROR_IO_RECV_TIMEOUT; \
				} \
				return VNC_RFB_RESULT_ERROR_IO; \
			} \
			total_bytes_read += bytes_read; \
		} \
	} while (0);

#define RFB_TRY_READ(vnc_fd, dest, size) RFB_TRY_READ_IMPL(vnc_fd, dest, size, 0)
#define RFB_TRY_PEEK(vnc_fd, dest, size) RFB_TRY_READ_IMPL(vnc_fd, dest, size, MSG_PEEK)

#define RFB_TRY_WRITE(vnc_fd, buf, size) \
	do { \
		size_t total_bytes_written = 0; \
		while (total_bytes_written < (size)) { \
			ssize_t bytes_written = write((vnc_fd), \
						      ((u8 *)(buf)) + total_bytes_written, \
						      (size)-total_bytes_written); \
			if (bytes_written < 0) { \
				return VNC_RFB_RESULT_ERROR_IO; \
			} \
			total_bytes_written += bytes_written; \
		} \
	} while (0);

#define RFB_TRY_DISCARD(vnc_fd, size) \
	do { \
		size_t to_discard = size; \
		while (to_discard > 0) { \
			char discard_buf[8192]; \
			ssize_t bytes_read = read(vnc_fd, discard_buf, to_discard); \
			if (bytes_read < 0) { \
				if (errno == EINTR) { \
					continue; \
				} \
				if (errno == EAGAIN || errno == EWOULDBLOCK) { \
					return VNC_RFB_RESULT_ERROR_IO_RECV_TIMEOUT; \
				} \
				return VNC_RFB_RESULT_ERROR_IO; \
			} \
			to_discard -= bytes_read; \
		} \
	} while (0);

struct Vnc_fb_mngr;

enum Vnc_rfb_version {
	VNC_RFB_VERSION_33,
	VNC_RFB_VERSION_37,
	VNC_RFB_VERSION_38,
	VNC_RFB_VERSION_UNKNOWN,
};

enum Vnc_rfb_security_type {
	VNC_RFB_SECURITY_TYPE_INVALID = 0,
	VNC_RFB_SECURITY_TYPE_NONE = 1,
	VNC_RFB_SECURITY_TYPE_VNCAUTH = 2,
};

enum Vnc_rfb_encoding {
	VNC_RFB_ENCODING_RAW = 0,
	VNC_RFB_ENCODING_COPY_RECT = 1,
	VNC_RFB_ENCODING_RRE = 2,
	VNC_RFB_ENCODING_HEXTILE = 5,
	VNC_RFB_ENCODING_TRLE = 15,
	VNC_RFB_ENCODING_ZRLE = 16,
	VNC_RFB_ENCODING_DESKTOP_SIZE_PSEUDO = -223,
	VNC_RFB_ENCODING_CURSOR_PSEUDO = -239,
	VNC_RFB_ENCODING_EXTENDED_DESKTOP_SIZE_PSEUDO = -308,
	VNC_RFB_ENCODING_FENCE_PSEUDO = -312,
	VNC_RFB_ENCODING_CONTINUOUS_UPDATES_PSEUDO = -313,
};

enum Vnc_rfb_server_message_type {
	VNC_RFB_SERVER_MESSAGE_TYPE_FRAMEBUFFER_UPDATE = 0,
	VNC_RFB_SERVER_MESSAGE_TYPE_BELL = 2,
	VNC_RFB_SERVER_MESSAGE_TYPE_CUT_TEXT = 3,
	VNC_RFB_SERVER_MESSAGE_TYPE_END_OF_CONTINUOUS_UPDATES = 150,
	VNC_RFB_SERVER_MESSAGE_TYPE_FENCE = 248,
};

enum Vnc_rfb_client_message_type {
	VNC_RFB_CLIENT_MESSAGE_TYPE_SET_ENCODING = 2,
	VNC_RFB_CLIENT_MESSAGE_TYPE_FRAMEBUFFER_UPDATE_REQUEST = 3,
	VNC_RFB_CLIENT_MESSAGE_TYPE_KEY_EVENT = 4,
	VNC_RFB_CLIENT_MESSAGE_TYPE_POINTER_EVENT = 5,
	VNC_RFB_CLIENT_MESSAGE_TYPE_CONTINUOUS_UPDATES = 150,
	VNC_RFB_CLIENT_MESSAGE_TYPE_FENCE = 248,
	VNC_RFB_CLIENT_MESSAGE_TYPE_SET_DESKTOP_SIZE = 251,
};

enum Vnc_rfb_result {
	VNC_RFB_RESULT_SUCCESS = 0,
	VNC_RFB_RESULT_ERROR_IO = -1,
	VNC_RFB_RESULT_ERROR_IO_RECV_TIMEOUT = -2,
	VNC_RFB_RESULT_ERROR_NO_ACCEPTABLE_SECURITY = -4,
	VNC_RFB_RESULT_ERROR_SERVER_SECURITY = -5,
	VNC_RFB_RESULT_ERROR_SERVER_INIT_NAME_TOO_LONG = -6,
};

struct Vnc_rfb_vncauth_challenge {
	u8 data[16];
};

struct Vnc_rfb_server_init {
	u16 width;
	u16 height;
	struct Vnc_rfb_pixel_format {
		u8 bpp;
		u8 depth;
		u8 big_endian;
		u8 true_color;
		u16 red_max;
		u16 green_max;
		u16 blue_max;
		u8 red_shift;
		u8 green_shift;
		u8 blue_shift;
		u8 padding[3];
	} RFB_PACKED pixel_format;
	u32 name_len;
	char name[64];
} RFB_PACKED;

struct Vnc_rfb_rect {
	u16 x;
	u16 y;
	u16 width;
	u16 height;
	i32 encoding;
} RFB_PACKED;

struct Vnc_rfb_fence {
	u8 message_type;
	u8 padding[3];
	u32 flags;
	u8 length;
	u8 payload[64];
} RFB_PACKED;

struct Vnc_rfb_enable_continuous_updates {
	u8 message_type;
	u8 enable;
	u16 x;
	u16 y;
	u16 width;
	u16 height;
} RFB_PACKED;

struct Vnc_rfb_key_event {
	u8 message_type;
	u8 down;
	u16 padding;
	u32 key;
} RFB_PACKED;

struct Vnc_rfb_pointer_event {
	u8 message_type;
	u8 button_mask;
	u16 xpos;
	u16 ypos;
} RFB_PACKED;

struct Vnc_rfb_screen {
	u32 id;
	u16 xpos;
	u16 ypos;
	u16 width;
	u16 height;
	u32 flags;
} RFB_PACKED;

struct Vnc_rfb_set_desktop_size {
	u8 message_type;
	u8 padding;
	u16 width;
	u16 height;
	u8 number_of_screens;
	u8 padding2;
	struct Vnc_rfb_screen screens[1];
} RFB_PACKED;

struct Vnc_rfb_cut_text {
	u8 message_type;
	u8 padding[3];
	u32 length;
} RFB_PACKED;

struct Vnc_rfb_framebuffer_update_action {
	enum Vnc_rfb_result (*handle_rect)(struct Vnc_rfb_framebuffer_update_action *action,
					   struct Vnc_rfb_rect *rect);
};

enum Vnc_rfb_result vnc_rfb_recv_version(int vnc_fd, enum Vnc_rfb_version *version);
enum Vnc_rfb_result vnc_rfb_send_version(int vnc_fd, enum Vnc_rfb_version version);

enum Vnc_rfb_result vnc_rfb_has_desired_security_type(int vnc_fd,
						      enum Vnc_rfb_security_type *desired, u8 count,
						      u8 *best_security_type_index);
enum Vnc_rfb_result vnc_rfb_send_security_type(int vnc_fd, enum Vnc_rfb_security_type type);
enum Vnc_rfb_result vnc_rfb_recv_challenge(int vnc_fd, struct Vnc_rfb_vncauth_challenge *challenge);
enum Vnc_rfb_result vnc_rfb_send_passwd(int vnc_fd, struct Vnc_rfb_vncauth_challenge *challenge,
					const char *passwd);
enum Vnc_rfb_result vnc_rfb_recv_security_result(int vnc_id);

enum Vnc_rfb_result vnc_rfb_send_client_init(int vnc_fd, bool shared);
enum Vnc_rfb_result vnc_rfb_recv_server_init(int vnc_fd, struct Vnc_rfb_server_init *server_init);
enum Vnc_rfb_result vnc_rfb_send_encodings(int vnc_fd, enum Vnc_rfb_encoding *encodings,
					   u16 encoding_count);

enum Vnc_rfb_result vnc_rfb_peek_message_type(int vnc_fd, u8 *message_type);

enum Vnc_rfb_result vnc_rfb_recv_fence(int vnc_fd, struct Vnc_rfb_fence *fence);
enum Vnc_rfb_result vnc_rfb_send_fence(int vnc_fd, struct Vnc_rfb_fence *fence);
enum Vnc_rfb_result
vnc_rfb_send_enable_continuous_updates(int vnc_fd,
				       struct Vnc_rfb_enable_continuous_updates *updates);

enum Vnc_rfb_result
vnc_rfb_recv_framebuffer_update(int vnc_fd, struct Vnc_rfb_framebuffer_update_action *action);
enum Vnc_rfb_result vnc_rfb_recv_rect_raw(int vnc_fd, struct Vnc_rfb_rect *rect, u32 bpp, u32 pitch,
					  char *dest);

enum Vnc_rfb_result vnc_rfb_send_pointer_event(int vnc_id,
					       struct Vnc_rfb_pointer_event *pointer_event);
enum Vnc_rfb_result vnc_rfb_send_key_event(int vnc_id, struct Vnc_rfb_key_event *key_event);

enum Vnc_rfb_result
vnc_rfb_send_set_desktop_size(int vnc_id, struct Vnc_rfb_set_desktop_size *set_desktop_size);
enum Vnc_rfb_result vnc_rfb_recv_number_of_screens(int vnc_fd, u8 *number_of_screens);
enum Vnc_rfb_result vnc_rfb_recv_screens(int vnc_fd, struct Vnc_rfb_screen *screen,
					 u8 screen_count);

enum Vnc_rfb_result vnc_rfb_recv_cut_text(int vnc_fd, struct Vnc_rfb_cut_text *cut_text);

const char *vnc_rfb_result_to_str(enum Vnc_rfb_result result);
