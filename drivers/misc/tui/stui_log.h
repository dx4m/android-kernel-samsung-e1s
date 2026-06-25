/*
 * Samsung TUI HW Handler driver.
 *
 * Copyright (c) 2015 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SAMSUNG_TUI_LOG_H
#define __LINUX_SAMSUNG_TUI_LOG_H

extern unsigned int tuihw_verbosity;

enum {
	STUI_LOG_LEVEL_ERROR,
	STUI_LOG_LEVEL_INFO,
	STUI_LOG_LEVEL_DEBUG,
	STUI_LOG_LEVEL_TRACE
};

static inline const char *tui_print_prefix(unsigned int lvl)
{
	switch (lvl) {
	case STUI_LOG_LEVEL_ERROR:
		return "ERR";
	case STUI_LOG_LEVEL_INFO:
		return "INF";
	case STUI_LOG_LEVEL_DEBUG:
		return "DBG";
	case STUI_LOG_LEVEL_TRACE:
		return "TRC";
	default:
		return "???";
	}
}

#define tz_print(lvl, param, fmt, ...)			\
	do {							\
		if (lvl <= param)				\
			printk(KERN_ERR "TUIHW %s %s(%d): "fmt,	\
					tui_print_prefix(lvl),	\
					__func__, __LINE__, ##__VA_ARGS__);	\
	} while (0)

#define log_debug(fmt, ...)	tz_print(STUI_LOG_LEVEL_DEBUG, tuihw_verbosity, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)	tz_print(STUI_LOG_LEVEL_INFO, tuihw_verbosity, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...)	tz_print(STUI_LOG_LEVEL_ERROR, tuihw_verbosity, fmt, ##__VA_ARGS__)

struct trace_info {
	const char *file;
	const char *func;
};

void _trace_out_(const struct trace_info *info);
struct trace_info _trace_in_(const char *file, const char *func, const int line);

#define STUI_CALL_TRACE() __attribute__((cleanup(_trace_out_))) struct trace_info __trace_info; \
			   __trace_info = _trace_in_(__FILE__, __func__, __LINE__)

#endif /* __LINUX_SAMSUNG_TUI_LOG_H */
