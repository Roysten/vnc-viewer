#include "logind.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "log.h"

bool vnc_logind_init(struct Vnc_logind *session)
{
	int rc = sd_bus_open_system(&session->bus);
	if (rc < 0) {
		vnc_log_error("Could not open system bus");
		return false;
	}

	u32 mypid = getpid();
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	rc = sd_bus_call_method(session->bus, "org.freedesktop.login1", "/org/freedesktop/login1",
				"org.freedesktop.login1.Manager", "GetSessionByPID", &error, &reply,
				"u", mypid);

	if (rc < 0) {
		vnc_log_error("Could not get session object path: %s", error.message);
		sd_bus_error_free(&error);
		return false;
	}

	rc = sd_bus_message_read(reply, "o", &session->session_object_path);
	if (rc < 0) {
		vnc_log_error("Could not get session object path: %s", error.message);
		sd_bus_error_free(&error);
		sd_bus_message_unref(reply);
		return false;
	}

	session->session_object_path = strdup(session->session_object_path);
	vnc_log_info("Session object path: %s", session->session_object_path);
	sd_bus_message_unref(reply);
	return true;
}

bool vnc_logind_take_control(struct Vnc_logind *session)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int rc = sd_bus_call_method(session->bus, "org.freedesktop.login1",
				    session->session_object_path, "org.freedesktop.login1.Session",
				    "TakeControl", &error, &reply, "b", false);
	if (rc < 0) {
		vnc_log_error("Could not take control: %s", error.message);
		sd_bus_error_free(&error);
		return false;
	}
	sd_bus_message_unref(reply);
	return true;
}

bool vnc_logind_release_control(struct Vnc_logind *session)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int rc = sd_bus_call_method(session->bus, "org.freedesktop.login1",
				    session->session_object_path, "org.freedesktop.login1.Session",
				    "ReleaseControl", &error, &reply, "");
	if (rc < 0) {
		vnc_log_error("Could not release control: %s", error.message);
		sd_bus_error_free(&error);
		return false;
	}
	sd_bus_message_unref(reply);
	return false;
}

bool vnc_logind_take_device(struct Vnc_logind *session, const char *path, int *fd)
{
	struct stat st;
	if (stat(path, &st) < 0) {
		vnc_log_error("Could not stat path '%s'", path);
		return false;
	}

	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int rc = sd_bus_call_method(session->bus, "org.freedesktop.login1",
				    session->session_object_path, "org.freedesktop.login1.Session",
				    "TakeDevice", &error, &reply, "uu", major(st.st_rdev),
				    minor(st.st_rdev));
	if (rc < 0) {
		vnc_log_error("Take device failed for path %s: %s", path, error.message);
		sd_bus_error_free(&error);
		return false;
	}

	int tmpfd = -1;
	int paused = 0;
	rc = sd_bus_message_read(reply, "hb", &tmpfd, &paused);
	if (rc < 0) {
		vnc_log_error("Unable to read TakeDevice response");
		sd_bus_error_free(&error);
		sd_bus_message_unref(reply);
		return false;
	}
	// Clone file descriptor (see man (3) sd_bus_message_read)
	tmpfd = fcntl(tmpfd, F_DUPFD_CLOEXEC, 0);
	if (tmpfd < 0) {
		vnc_log_error("Could not duplicate fd");
		sd_bus_error_free(&error);
		sd_bus_message_unref(reply);
		return false;
	}

	sd_bus_message_unref(reply);
	*fd = tmpfd;
	return true;
}

bool vnc_logind_release_device(struct Vnc_logind *session, const char *path)
{
	return false;
}
