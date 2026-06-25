/* tui/stui_log.c
 *
 * Samsung TUI HW Handler driver.
 *
 * Copyright (c) 2015 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include "stui_log.h"

#define STUI_DEBUG_PARAM_DECLARE(param, value)				\
	unsigned int param##_verbosity = value;				\
	module_param(param##_verbosity, uint, 0644);				\
	MODULE_PARM_DESC(param_##verbosity, "0: error, 1: info, 3: trace")

#ifdef DEBUG_LOG_ALLOWED
#define STUI_LOG_LEVEL STUI_LOG_LEVEL_TRACE
#else
#define STUI_LOG_LEVEL STUI_LOG_LEVEL_INFO
#endif

STUI_DEBUG_PARAM_DECLARE(tuihw, STUI_LOG_LEVEL);

#define trace_print(lvl, param, fmt, ...)			\
	do { if (lvl <= param) pr_err("TUIHW %s: "fmt, tui_print_prefix(lvl), ##__VA_ARGS__); } while (0)

#define PRINT_LOG(x) trace_print(STUI_LOG_LEVEL_TRACE, tuihw_verbosity, "%s", x)
#define STUI_LOG_LIMIT 512

__attribute__ ((visibility("default"))) void _trace_out_(const struct trace_info *info)
{
	char string[STUI_LOG_LIMIT] = {};
	char *data = string;
	const char *name = strrchr(info->file, '/');

	int ret = snprintf(data, STUI_LOG_LIMIT, "%s (%s): out\n", info->func, name + 1);

	if (ret >= 0 && ret < STUI_LOG_LIMIT)
		PRINT_LOG(data);
}

__attribute__ ((visibility("default"))) struct trace_info _trace_in_(const char *file,
									const char *func,
									const int line)
{
	struct trace_info info = { file, func };
	char string[STUI_LOG_LIMIT] = {};
	char *data = string;
	const char *name = strrchr(file, '/');

	int ret = snprintf(data, STUI_LOG_LIMIT, "%s (%s:%d): in\n", func, name + 1, line);

	if (ret >= 0 && ret < STUI_LOG_LIMIT)
		PRINT_LOG(data);

	return info;
}
