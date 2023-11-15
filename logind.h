#pragma once

#include <systemd/sd-bus.h>

#include "types.h"

struct Vnc_logind {
	sd_bus *bus;
	const char *session_object_path;
};

bool vnc_logind_init(struct Vnc_logind *session);
bool vnc_logind_take_control(struct Vnc_logind *session);
bool vnc_logind_release_control(struct Vnc_logind *session);
bool vnc_logind_take_device(struct Vnc_logind *session, const char *path, int *fd);
bool vnc_logind_release_device(struct Vnc_logind *session, const char *path);
