#include <time.h>

#include "log.h"

FILE *vnc_log_fptr = NULL;

void vnc_log_init(const char *path)
{
	vnc_log_fptr = fopen(path, "a");

	time_t now;
	time(&now);
	struct tm *tmp = localtime(&now);
	char buf[256] = { '\0' };
	strftime(buf, sizeof(buf), "%c", tmp);
	fprintf(vnc_log_fptr, "\n%s\n", buf);
	fflush(vnc_log_fptr);
}

void vnc_log_log(const char *log_level, const char *fmt, va_list args)
{
	fprintf(vnc_log_fptr, "[%s] ", log_level);
	vfprintf(vnc_log_fptr, fmt, args);
	fprintf(vnc_log_fptr, "\n");
	va_end(args);
	fflush(vnc_log_fptr);
}

void vnc_log_info(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	va_end(args);
	return vnc_log_log("info", fmt, args);
}

void vnc_log_debug(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	va_end(args);
	return vnc_log_log("debug", fmt, args);
}

void vnc_log_error(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	va_end(args);
	return vnc_log_log("error", fmt, args);
}
