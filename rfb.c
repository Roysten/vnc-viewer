#include "rfb.h"

#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "d3des.h"
#include "fb_mngr.h"
#include "log.h"
#include "macros.h"

#define RFB_TRY_READ_IMPL(vnc_fd, dest, size, flags) \
	do { \
		size_t total_bytes_read = 0; \
		while (total_bytes_read < (size)) { \
			ssize_t bytes_read = recv((vnc_fd), ((u8 *)(dest)) + total_bytes_read, \
						  (size)-total_bytes_read, flags); \
			if (bytes_read <= 0) { \
				if (errno == EINTR) { \
					continue; \
				} \
				if (errno == EAGAIN || errno == EWOULDBLOCK) { \
					return VNC_RFB_RESULT_ERROR_IO_RECV_TIMEOUT; \
				} \
				return VNC_RFB_RESULT_ERROR_IO_SHORT_READ; \
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
			if (bytes_written <= 0) { \
				return VNC_RFB_RESULT_ERROR_IO_SHORT_WRITE; \
			} \
			total_bytes_written += bytes_written; \
		} \
	} while (0);

enum Vnc_rfb_result vnc_rfb_recv_version(int vnc_fd, enum Vnc_rfb_version *version)
{
	char buf[12] = { '\0' };
	RFB_TRY_READ(vnc_fd, buf, sizeof(buf));
	*version = VNC_RFB_VERSION_UNKNOWN;
	if (strncmp(buf, "RFB 003.003\n", RFB_VERSION_MSG_LEN) == 0) {
		*version = VNC_RFB_VERSION_33;
	} else if (strncmp(buf, "RFB 003.007\n", RFB_VERSION_MSG_LEN) == 0) {
		*version = VNC_RFB_VERSION_37;
	} else if (strncmp(buf, "RFB 003.008\n", RFB_VERSION_MSG_LEN) == 0) {
		*version = VNC_RFB_VERSION_38;
	}

	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_send_version(int vnc_fd, enum Vnc_rfb_version version)
{
	const char *to_write = NULL;
	switch (version) {
	case VNC_RFB_VERSION_33:
		to_write = "RFB 003.003\n";
		break;
	case VNC_RFB_VERSION_37:
		to_write = "RFB 003.007\n";
		break;
	default:
		to_write = "RFB 003.008\n";
	}
	RFB_TRY_WRITE(vnc_fd, to_write, 12);
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_has_desired_security_type(int vnc_fd,
						      enum Vnc_rfb_security_type *desired, u8 count,
						      u8 *best_security_type_index)
{
	u8 security_type_count;
	RFB_TRY_READ(vnc_fd, &security_type_count, sizeof(security_type_count));

	int tmp = -1;
	for (u8 i = 0; i < security_type_count; ++i) {
		u8 security_type;
		RFB_TRY_READ(vnc_fd, &security_type, sizeof(security_type));

		for (u8 j = 0; j < count; ++j) {
			if (security_type == desired[j] && (j < tmp || tmp == -1)) {
				tmp = j;
				break;
			}
		}
	}

	if (tmp == -1) {
		return VNC_RFB_RESULT_ERROR_NO_ACCEPTABLE_SECURITY;
	}
	*best_security_type_index = tmp;
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_send_security_type(int vnc_fd, enum Vnc_rfb_security_type type)
{
	u8 type_value = type;
	RFB_TRY_WRITE(vnc_fd, &type_value, sizeof(type_value));
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_recv_challenge(int vnc_fd, struct Vnc_rfb_vncauth_challenge *challenge)
{
	*challenge = (struct Vnc_rfb_vncauth_challenge){ 0 };
	RFB_TRY_READ(vnc_fd, challenge->data, ARRAY_COUNT(challenge->data));
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_send_passwd(int vnc_fd, struct Vnc_rfb_vncauth_challenge *challenge,
					const char *passwd)
{
	char password_buf[9] = { '\0' };
	snprintf(password_buf, ARRAY_COUNT(password_buf), "%s", passwd);
	password_buf[8] = '\0';
	deskey((unsigned char *)password_buf, EN0);

	for (size_t i = 0; i < ARRAY_COUNT(challenge->data); i += 8) {
		des(&challenge->data[i], &challenge->data[i]);
	}

	RFB_TRY_WRITE(vnc_fd, challenge->data, ARRAY_COUNT(challenge->data));
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_recv_security_result(int vnc_fd)
{
	u32 status = 0;
	RFB_TRY_READ(vnc_fd, &status, sizeof(status));
	status = ntohl(status);
	if (status == 1) {
		u32 err_msg_len = 0;
		RFB_TRY_READ(vnc_fd, &err_msg_len, sizeof(err_msg_len));
		err_msg_len = ntohl(err_msg_len);

		char err_msg_buf[256] = { '\0' };
		if (err_msg_len > ARRAY_COUNT(err_msg_buf)) {
			err_msg_len = ARRAY_COUNT(err_msg_buf) - 1;
		}

		RFB_TRY_READ(vnc_fd, err_msg_buf, err_msg_len);
		vnc_log_error("security error reported: %s", err_msg_buf);
		return VNC_RFB_RESULT_ERROR_SERVER_SECURITY;
	}
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_send_client_init(int vnc_fd, bool shared)
{
	u8 shared_value = shared ? 1 : 0;
	RFB_TRY_WRITE(vnc_fd, &shared_value, sizeof(shared_value));
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_recv_server_init(int vnc_fd, struct Vnc_rfb_server_init *server_init)
{
	*server_init = (struct Vnc_rfb_server_init){ 0 };
	RFB_TRY_READ(vnc_fd, server_init, offsetof(struct Vnc_rfb_server_init, name));
	server_init->width = ntohs(server_init->width);
	server_init->height = ntohs(server_init->height);
	server_init->pixel_format.red_max = ntohs(server_init->pixel_format.red_max);
	server_init->pixel_format.green_max = ntohs(server_init->pixel_format.green_max);
	server_init->pixel_format.blue_max = ntohs(server_init->pixel_format.blue_max);
	server_init->name_len = ntohl(server_init->name_len);
	if (server_init->name_len > ARRAY_COUNT(server_init->name)) {
		return VNC_RFB_RESULT_ERROR_SERVER_INIT_NAME_TOO_LONG;
	}
	RFB_TRY_READ(vnc_fd, server_init->name, server_init->name_len);
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_send_encodings(int vnc_fd, enum Vnc_rfb_encoding *encodings,
					   u16 encoding_count)
{
	char buf[256];
	struct {
		u8 message_type;
		u8 padding;
		u16 number_of_encodings;
	} RFB_PACKED to_write = {
		.message_type = (u8)VNC_RFB_MESSAGE_TYPE_SET_ENCODING,
		.number_of_encodings = htons(encoding_count),
	};
	memcpy(buf, &to_write, sizeof(to_write));
	size_t offset = sizeof(to_write);

	for (u16 i = 0; i < encoding_count && offset < ARRAY_COUNT(buf) - sizeof(u32); ++i) {
		u32 encoding = htonl((i32)encodings[i]);
		memcpy(buf + offset, &encoding, sizeof(encoding));
		offset += sizeof(encoding);
	}
	RFB_TRY_WRITE(vnc_fd, buf, offset);
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_peek_message_type(int vnc_fd, u8 *message_type)
{
	RFB_TRY_PEEK(vnc_fd, message_type, sizeof(*message_type));
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_recv_fence(int vnc_fd, struct Vnc_rfb_fence *fence)
{
	RFB_TRY_READ(vnc_fd, fence, sizeof(*fence) - sizeof(fence->payload));
	fence->flags = ntohl(fence->flags);
	RFB_TRY_READ(vnc_fd, fence->payload, fence->length);

	/*bool response_required = (server_fence.flags & (1u << 31)) > 0;
		   if (response_required) {
		   u32 response_flags = server_fence.flags & ~(1u << 31);
		   struct {
		   u8 message_type;
		   u8 padding[3];
		   u32 flags;
		   u8 length;
		   } RFB_PACKED client_fence = {
		   .message_type = (u8)Vnc_rfb_message_type_server_fence,
		   .flags = htonl(response_flags),
		   .length = server_fence.length,
		   };
		   RFB_TRY_WRITE(&client_fence, sizeof(client_fence), vnc_rfb_stream);
		   RFB_TRY_WRITE(payload_buf, server_fence.length, vnc_rfb_stream);
		   fflush(vnc_rfb_stream);
		   vnc_log_debug("written client fence %d %u", sizeof(client_fence),
		   response_flags);
		   } */

	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_send_fence(int vnc_fd, struct Vnc_rfb_fence *fence)
{
	RFB_TRY_WRITE(vnc_fd, fence, offsetof(struct Vnc_rfb_fence, payload) + fence->length);
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result
vnc_rfb_send_enable_continuous_updates(int vnc_fd,
				       struct Vnc_rfb_enable_continuous_updates *updates)
{
	RFB_TRY_WRITE(vnc_fd, updates, sizeof(*updates));
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result
vnc_rfb_recv_framebuffer_update(int vnc_fd, struct Vnc_rfb_framebuffer_update_action *action)
{
	struct {
		u8 message_type;
		u8 padding;
		u16 number_of_rectangles;
	} RFB_PACKED hdr;
	RFB_TRY_READ(vnc_fd, &hdr, sizeof(hdr));
	hdr.number_of_rectangles = ntohs(hdr.number_of_rectangles);

	for (u16 i = 0; i < hdr.number_of_rectangles; ++i) {
		struct Vnc_rfb_rect rect;
		RFB_TRY_READ(vnc_fd, &rect, sizeof(rect));

		rect.x = ntohs(rect.x);
		rect.y = ntohs(rect.y);
		rect.width = ntohs(rect.width);
		rect.height = ntohs(rect.height);
		rect.encoding = ntohl(rect.encoding);

		enum Vnc_rfb_result result = action->handle_rect(action, &rect);
		if (result != VNC_RFB_RESULT_SUCCESS) {
			return result;
		}
	}

	return VNC_RFB_RESULT_SUCCESS;
}

const char *vnc_rfb_result_to_str(enum Vnc_rfb_result result)
{
	switch (result) {
	case VNC_RFB_RESULT_SUCCESS:
		return "success";
	case VNC_RFB_RESULT_ERROR_IO_SHORT_READ:
		return "short read";
	case VNC_RFB_RESULT_ERROR_IO_SHORT_WRITE:
		return "short write";
	case VNC_RFB_RESULT_ERROR_NO_ACCEPTABLE_SECURITY:
		return "no acceptable security";
	case VNC_RFB_RESULT_ERROR_SERVER_SECURITY:
		return "server security";
	case VNC_RFB_RESULT_ERROR_SERVER_INIT_NAME_TOO_LONG:
		return "ServerInit name too long";
	default:
		return "unknown";
	}
}

enum Vnc_rfb_result vnc_rfb_send_pointer_event(int vnc_id,
					       struct Vnc_rfb_pointer_event *pointer_event)
{
	RFB_TRY_WRITE(vnc_id, pointer_event, sizeof(*pointer_event));
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_send_set_desktop_size(int vnc_id,
						  struct Vnc_rfb_set_desktop_size *set_desktop_size)
{
	assert(set_desktop_size->number_of_screens == 1);
	RFB_TRY_WRITE(vnc_id, set_desktop_size,
		      offsetof(struct Vnc_rfb_set_desktop_size, screens) +
			      set_desktop_size->number_of_screens *
				      sizeof(*set_desktop_size->screens));
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_recv_number_of_screens(int vnc_fd, u8 *number_of_screens)
{
	u8 buf[4];
	RFB_TRY_READ(vnc_fd, buf, sizeof(buf));
	*number_of_screens = buf[0];
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_recv_screens(int vnc_fd, struct Vnc_rfb_screen *screens,
					 u8 number_of_screens)
{
	RFB_TRY_READ(vnc_fd, screens, sizeof(*screens) * number_of_screens);
	return VNC_RFB_RESULT_SUCCESS;
}

enum Vnc_rfb_result vnc_rfb_recv_rect_raw(int vnc_fd, struct Vnc_rfb_rect *rect, u32 bpp, u32 pitch,
					  char *dest)
{
	for (u16 y = rect->y; y < rect->y + rect->height; ++y) {
		RFB_TRY_READ(vnc_fd, &dest[pitch * y + rect->x * (bpp / 8)],
			     rect->width * (bpp / 8));
	}
	return VNC_RFB_RESULT_SUCCESS;
}
