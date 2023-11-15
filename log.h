#pragma once

#include <stdarg.h>
#include <stdio.h>

extern FILE *vnc_log_fptr;

void vnc_log_init(const char *path);
void vnc_log_log(const char *log_level, const char *fmt, va_list args);
void vnc_log_info(const char *fmt, ...);
void vnc_log_debug(const char *fmt, ...);
void vnc_log_error(const char *fmt, ...);
