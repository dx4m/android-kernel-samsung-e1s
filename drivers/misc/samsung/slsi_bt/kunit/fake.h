/******************************************************************************
 *                                                                            *
 * Copyright (c) 2022 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * Fake function helper for bluetooth driver unit test                        *
 *                                                                            *
 ******************************************************************************/
#ifndef __SLSI_BT_UNITTEST_FAKE_H__
#define __SLSI_BT_UNITTEST_FAKE_H__

#include <linux/types.h>

/* Make a fake function and fake result variable */
#define GLOBAL_FAKE(retType, fnName, ...) \
retType fnName##_ret = (retType)0; \
retType fnName(__VA_ARGS__)

/* Make a static void type of fake function */
#define GLOBAL_FAKE_VOID(fnName, ...) \
void fnName(__VA_ARGS__)

/* Make a simple type of fake function */
#define GLOBAL_FAKE_RET(retType, fnName, ...) \
	GLOBAL_FAKE(retType, fnName, __VA_ARGS__) { return fnName##_ret; }

/* Make a static fake function and fake result variable */
#define LOCAL_FAKE(retType, fnName, ...) \
static retType fnName##_ret = (retType)0; \
static retType fnName(__VA_ARGS__)

/* Make a static void type of fake function */
#define LOCAL_FAKE_VOID(fnName, ...) \
static void fnName(__VA_ARGS__)

/* Make a simple static type of fake function */
#define LOCAL_FAKE_RET(retType, fnName, ...) \
	LOCAL_FAKE(retType, fnName, __VA_ARGS__) { return fnName##_ret; }

struct fake_buffer_pool {
	char *start;
	char *offset;
	char *end;
};
void fake_buffer_init(struct fake_buffer_pool *b, char *ref, size_t size);
void *fake_buffer_alloc(struct fake_buffer_pool *b, size_t size);
void fake_buffer_free(void *ref);
bool fake_buffer_all_free_check(struct fake_buffer_pool *b);
#endif /* __SLSI_BT_UNITTEST_FAKE_H__ */
