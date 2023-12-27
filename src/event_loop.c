#include "event_loop.h"

#include <errno.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "log.h"
#include "macros.h"
#include "types.h"

#define POS_LIBINPUT 0
#define POS_KEY_REPEAT 1
#define POS_VNC 2
#define POS_EXIT_EVENT 3

bool vnc_event_loop_init(struct Vnc_event_loop *event_loop)
{
	*event_loop = (struct Vnc_event_loop){ 0 };
	for (size_t i = 0; i < ARRAY_COUNT(event_loop->pollfds); ++i) {
		event_loop->pollfds[i].fd = -1;
	}
	event_loop->pollfds[POS_EXIT_EVENT].fd = eventfd(0, EFD_CLOEXEC);
	if (event_loop->pollfds[POS_EXIT_EVENT].fd == -1) {
		vnc_log_error("eventfd failed");
		return false;
	}
	event_loop->pollfds[POS_EXIT_EVENT].events = POLLIN;
	return true;
}

bool vnc_event_loop_register_libinput(struct Vnc_event_loop *event_loop, int fd)
{
	struct pollfd *pollfd = &event_loop->pollfds[POS_LIBINPUT];
	pollfd->fd = fd;
	pollfd->events = POLLIN;
	return true;
}

bool vnc_event_loop_register_key_repeat(struct Vnc_event_loop *event_loop, int fd)
{
	struct pollfd *pollfd = &event_loop->pollfds[POS_KEY_REPEAT];
	pollfd->fd = fd;
	pollfd->events = POLLIN;
	return true;
}

bool vnc_event_loop_register_vnc(struct Vnc_event_loop *event_loop, int fd)
{
	struct pollfd *pollfd = &event_loop->pollfds[POS_VNC];
	pollfd->fd = fd;
	pollfd->events = POLLIN;
	return true;
}

bool vnc_event_loop_process_events(struct Vnc_event_loop *event_loop, u32 *events)
{
	int rc;
	do {
		rc = poll(event_loop->pollfds, ARRAY_COUNT(event_loop->pollfds), -1);
	} while (rc == -1 && errno == EINTR);
	if (rc > 0) {
		*events = 0;
		if ((event_loop->pollfds[POS_LIBINPUT].revents & POLLIN) > 0) {
			*events |= VNC_EVENT_TYPE_LIBINPUT;
		}
		if ((event_loop->pollfds[POS_KEY_REPEAT].revents & POLLIN) > 0) {
			*events |= VNC_EVENT_TYPE_KEY_REPEAT;
		}
		if ((event_loop->pollfds[POS_VNC].revents & POLLIN) > 0) {
			*events |= VNC_EVENT_TYPE_VNC;
		}
		if ((event_loop->pollfds[POS_EXIT_EVENT].revents & POLLIN) > 0) {
			*events |= VNC_EVENT_TYPE_EXIT;
		}
		return true;
	}
	return false;
}

void vnc_event_loop_exit(struct Vnc_event_loop *event_loop)
{
	u64 to_add = 1;
	write(event_loop->pollfds[POS_EXIT_EVENT].fd, &to_add, sizeof(to_add));
}
