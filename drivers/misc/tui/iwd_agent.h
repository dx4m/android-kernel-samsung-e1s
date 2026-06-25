/* tui/iwd_agent.h
 *
 * IWD agent module contains all iwd socket calls.
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __IWD_AGENT_H__
#define __IWD_AGENT_H__

#include <linux/types.h>

struct iwd_functions {
	int (*cancel_session)(void);
};

int init_iwd_agent(void);
void uninit_iwd_agent(void);
int __init_iwd_agent(void);
void __uninit_iwd_agent(void);

extern void register_iwd_functions(struct iwd_functions *f);
extern void unregister_iwd_functions(void);
extern unsigned int tzdev_is_up(void);

#endif /* __IWD_AGENT_H__ */
