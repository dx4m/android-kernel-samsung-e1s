/******************************************************************************
 *                                                                            *
 * Copyright (c) 2022 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * Fake function helper for bluetooth driver unit test                        *
 *                                                                            *
 ******************************************************************************/
#include "linux/printk.h"
#include "fake.h"

struct __fake_buffer_item {
#define __FAKE_ITEM_MAGIC (0xFA837A93)
	unsigned int magic;
	size_t size;
	bool valid;
};

void fake_buffer_init(struct fake_buffer_pool *b, char *ref, size_t size)
{
	char *c;

	b->start = b->offset = ref;
	b->end = b->start + size;
	for (c = b->start; b->start <= c && c < b->end; c++)
		*c = 0;
}

void *fake_buffer_alloc(struct fake_buffer_pool *b, size_t size)
{
	struct __fake_buffer_item *item;

	if (b->offset + sizeof(struct __fake_buffer_item) + size >= b->end) {
		pr_err("%s: remained=%zu, requested=%zu(+%zu)\n", __func__,
			b->end - b->offset,
			size, sizeof(struct __fake_buffer_item));
		return NULL;
	}
	pr_info("%s: remained=%zu, requested=%zu(+%zu)\n", __func__,
		b->end - b->offset,
		size, sizeof(struct __fake_buffer_item));

	item = (struct __fake_buffer_item *)b->offset;
	item->magic = __FAKE_ITEM_MAGIC;
	item->size = size;
	item->valid = true;
	b->offset += sizeof(struct __fake_buffer_item) + size;

	return (void *)(b->offset - size);
}

void fake_buffer_free(void *ref)
{
	struct __fake_buffer_item *item = (struct __fake_buffer_item *)ref;

	if (item->magic == __FAKE_ITEM_MAGIC) {
		item->valid = false;
	}
}

bool fake_buffer_all_free_check(struct fake_buffer_pool *b)
{
	struct __fake_buffer_item *item = (struct __fake_buffer_item *)b->start;

	while (item->magic == __FAKE_ITEM_MAGIC) {
		if (item->valid)
			return false;

		item = (struct __fake_buffer_item *) (((char *)item) +
				sizeof(struct __fake_buffer_item) + item->size);
	}
	return true;
}
