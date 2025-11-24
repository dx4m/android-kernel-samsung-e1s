// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF camerapool heap exporter for Samsung
 *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd.
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019, 2020 Linaro Ltd.
 *
 * Portions based off of Andrew Davis' SRAM heap:
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/shrinker.h>
#include <linux/spinlock.h>
#include <linux/swap.h>
#include <linux/sched/signal.h>
#include <trace/hooks/mm.h>
#include <linux/rwsem.h>

#include "heap_private.h"
#include "page_pool.h"

#define HIGH_ORDER_GFP  (((GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN \
				| __GFP_NORETRY) & ~__GFP_RECLAIM) \
				| __GFP_COMP)
#define LOW_ORDER_GFP (GFP_HIGHUSER | __GFP_ZERO | __GFP_COMP)
static gfp_t order_flags[] = {HIGH_ORDER_GFP, HIGH_ORDER_GFP, HIGH_ORDER_GFP, LOW_ORDER_GFP};

/*
 * The selection of the orders used for allocation (2MB, 1MB, 64K, 4K) is designed
 * to match with the sizes often found in IOMMUs. Using high order pages instead
 * of order 0 pages can significantly improve the performance of many IOMMUs
 * by reducing TLB pressure and time spent updating page tables.
 */
static const unsigned int camerapool_orders[] = {9, 8, 4, 0};
static const unsigned int camerapool_order_to_index[] = {3, -1, -1, -1, 2, -1, -1, -1, 1, 0};

#define CAMERAPOOL_NUM_ORDERS ARRAY_SIZE(camerapool_orders)

//page_pool functions
/* page types we track in the pool */
enum {
	POOL_LOWPAGE,      /* Clean lowmem pages */
	POOL_HIGHPAGE,     /* Clean highmem pages */
	POOL_TYPE_SIZE,
};

/**
 * struct dmabuf_page_pool - pagepool struct
 * @count[]:		array of number of pages of that type in the pool
 * @items[]:		array of list of pages of the specific type
 * @lock:		lock protecting this struct and especially the count
 *			item list
 * @gfp_mask:		gfp_mask to use from alloc
 * @order:		order of pages in the pool
 * @list:		list node for list of pools
 *
 * Allows you to keep a pool of pre allocated pages to use
 */
struct dmabuf_page_pool {
	int count[POOL_TYPE_SIZE];
	struct list_head items[POOL_TYPE_SIZE];
	spinlock_t lock;
	gfp_t gfp_mask;
	unsigned int order;
	struct list_head list;
};

struct camerapool {
	bool preallocation_flag;
	bool is_pool_allocated;
	unsigned long heap_size;
	atomic_long_t cur_static_heap_size;
	atomic_long_t max_static_heap_size;
	atomic_long_t total_alloc_size;

	struct dmabuf_page_pool *static_pool[CAMERAPOOL_NUM_ORDERS];
	struct dmabuf_page_pool *dynamic_pool[CAMERAPOOL_NUM_ORDERS];

	spinlock_t spn_lock;
	struct rw_semaphore rwsem_lock;

	struct work_struct ws_preallocate;
	struct work_struct ws_free_pages;
};

static struct camerapool g_camerapool;

static inline struct page *camerapool_dmabuf_page_pool_alloc_pages(struct dmabuf_page_pool *pool)
{
	if (fatal_signal_pending(current))
		return NULL;
	return alloc_pages(pool->gfp_mask, pool->order);
}

static inline void camerapool_dmabuf_page_pool_free_pages(struct dmabuf_page_pool *pool,
								struct page *page)
{
	__free_pages(page, pool->order);
}

static struct page *camerapool_dmabuf_page_pool_remove(struct dmabuf_page_pool *pool,
							bool is_static_pool, int index)
{
	struct page *page;

	spin_lock(&pool->lock);
	page = list_first_entry_or_null(&pool->items[index], struct page, lru);
	if (page) {
		pool->count[index]--;
		list_del(&page->lru);
		spin_unlock(&pool->lock);

		if (is_static_pool)
			atomic_long_sub(page_size(page), &g_camerapool.cur_static_heap_size);

		mod_node_page_state(page_pgdat(page), NR_KERNEL_MISC_RECLAIMABLE,
				    -(1 << pool->order));
		goto out;
	}
	spin_unlock(&pool->lock);
out:
	return page;
}

static void camerapool_dmabuf_page_pool_add(struct dmabuf_page_pool *pool,
						struct page *page, bool is_static_pool)
{
	int index;

	if (PageHighMem(page))
		index = POOL_HIGHPAGE;
	else
		index = POOL_LOWPAGE;

	spin_lock(&pool->lock);
	list_add_tail(&page->lru, &pool->items[index]);
	pool->count[index]++;
	spin_unlock(&pool->lock);

	if (is_static_pool)
		atomic_long_add(page_size(page), &g_camerapool.cur_static_heap_size);

	mod_node_page_state(page_pgdat(page), NR_KERNEL_MISC_RECLAIMABLE,
			    1 << pool->order);
}

static struct page *camerapool_dmabuf_page_pool_fetch(struct dmabuf_page_pool *pool,
									bool is_static_pool)
{
	struct page *page = NULL;

	page = camerapool_dmabuf_page_pool_remove(pool, is_static_pool, POOL_HIGHPAGE);
	if (!page)
		page = camerapool_dmabuf_page_pool_remove(pool, is_static_pool, POOL_LOWPAGE);

	return page;
}

struct page *camerapool_dmabuf_page_pool_alloc(struct dmabuf_page_pool *pool, bool is_static_pool)
{
	struct page *page = NULL;

	if (WARN_ON(!pool))
		return NULL;

	page = camerapool_dmabuf_page_pool_fetch(pool, is_static_pool);

	if (!page && !is_static_pool)
		page = camerapool_dmabuf_page_pool_alloc_pages(pool);

	return page;
}

struct dmabuf_page_pool *camerapool_dmabuf_page_pool_create(gfp_t gfp_mask, unsigned int order)
{
	struct dmabuf_page_pool *pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	int i;

	if (!pool)
		return NULL;

	for (i = 0; i < POOL_TYPE_SIZE; i++) {
		pool->count[i] = 0;
		INIT_LIST_HEAD(&pool->items[i]);
	}
	pool->gfp_mask = gfp_mask | __GFP_COMP;
	pool->order = order;
	spin_lock_init(&pool->lock);

	return pool;
}

void camerapool_static_pool_destroy(struct dmabuf_page_pool *pool)
{
	struct page *page;
	int i;

	/* Free any remaining pages in the pool */
	for (i = 0; i < POOL_TYPE_SIZE; i++) {
		while ((page = camerapool_dmabuf_page_pool_remove(pool, true, i)))
			camerapool_dmabuf_page_pool_free_pages(pool, page);
	}

	kfree(pool);
}

//Function to find current static heap usage
unsigned long camerapool_static_pool_size(void)
{
	int i;
	long npages = 0;

	for (i = 0; i < CAMERAPOOL_NUM_ORDERS; i++) {
		if (g_camerapool.static_pool[i]) {
			spin_lock(&g_camerapool.static_pool[i]->lock);
			npages += ((g_camerapool.static_pool[i]->count[POOL_LOWPAGE]
						+ g_camerapool.static_pool[i]->count[POOL_HIGHPAGE])
						<< g_camerapool.static_pool[i]->order);
			spin_unlock(&g_camerapool.static_pool[i]->lock);
		}
	}

	return npages << PAGE_SHIFT;
}

//Function to find current dynamic heap usage
static unsigned long camerapool_dynamic_pool_size(void)
{
	int i;
	long npages = 0;

	for (i = 0; i < CAMERAPOOL_NUM_ORDERS; i++) {
		if (g_camerapool.dynamic_pool[i]) {
			spin_lock(&g_camerapool.dynamic_pool[i]->lock);
			npages += ((g_camerapool.dynamic_pool[i]->count[POOL_LOWPAGE]
					+ g_camerapool.dynamic_pool[i]->count[POOL_HIGHPAGE])
					<< g_camerapool.dynamic_pool[i]->order);
			spin_unlock(&g_camerapool.dynamic_pool[i]->lock);
		}
	}

	return npages << PAGE_SHIFT;
}

//To allocate a new page from system.
static struct page *camerapool_prealloc_pages(unsigned long size, int i)
{
	struct page *page = NULL;

	if (!g_camerapool.static_pool[i])
		return NULL;

	if (size >= (PAGE_SIZE << camerapool_orders[i]))
		page = camerapool_dmabuf_page_pool_alloc_pages(g_camerapool.static_pool[i]);
	return page;
}

//To add a page to its corresponding pool
static void camerapool_add_page_to_pool(struct page *page, bool is_static_pool)
{
	struct dmabuf_page_pool *pool;
	int j;

	j = camerapool_order_to_index[compound_order(page)];

	if (is_static_pool)
		pool = g_camerapool.static_pool[j];
	else
		pool = g_camerapool.dynamic_pool[j];

	camerapool_dmabuf_page_pool_add(pool, page, is_static_pool);
}

static void camerapool_free_pages_from_pool(struct dmabuf_page_pool *pool, bool is_static_pool)
{
	struct page *page;
	int i;

	/* Free any remaining pages in the pool */
	for (i = 0; i < POOL_TYPE_SIZE; i++) {
		while ((page = camerapool_dmabuf_page_pool_remove(pool, is_static_pool, i)))
			camerapool_dmabuf_page_pool_free_pages(pool, page);
	}
}

static void camerapool_add_or_free_page_to_pool(
		struct page *page, bool is_static_pool, bool is_pool_exist)
{
	if (is_pool_exist) {
		//If camerapool not freed, add the pages to pool
		camerapool_add_page_to_pool(page, is_static_pool);
	} else {
		//If camerapool is freed, free the pages back to system.
		int j;
		struct dmabuf_page_pool *pool;

		j = camerapool_order_to_index[compound_order(page)];

		if (is_static_pool)
			pool = g_camerapool.static_pool[j];
		else
			pool = g_camerapool.dynamic_pool[j];

		camerapool_dmabuf_page_pool_free_pages(pool, page);
	}
}

//For adding more pages to the static pool till given heap size
static void camerapool_expand(void)
{
	int i;
	long size_remaining;
	struct page *page, *tmp_page;
	struct list_head exception_pages;

	INIT_LIST_HEAD(&exception_pages);

	size_remaining = g_camerapool.heap_size
		- atomic_long_read(&g_camerapool.cur_static_heap_size);

	for (i = 0; i < CAMERAPOOL_NUM_ORDERS; i++) {
		/*Try higher order pages first*/
		while (size_remaining > 0 && size_remaining >= PAGE_SIZE << camerapool_orders[i]) {
			page = camerapool_prealloc_pages(size_remaining, i);
			if (!page)
				break;

			if (!is_dma_heap_exception_page(page)) {
				camerapool_add_page_to_pool(page, true);
				size_remaining -= page_size(page);
			} else {
				list_add_tail(&page->lru, &exception_pages);
			}
		}
		pr_debug("[%s]: [order:%d] [heap_size(MB):%ld] [size_remaining(MB):%ld]",
			__func__, camerapool_orders[i], camerapool_static_pool_size() >> 20,
			size_remaining >> 20);
	}

	list_for_each_entry_safe(page, tmp_page, &exception_pages, lru)
		__free_pages(page, compound_order(page));
}

//For freeing pages from the static pool till given heap size
static void camerapool_shrink(void)
{
	int i;
	struct page *page;
	long size_remaining;

	size_remaining = atomic_long_read(&g_camerapool.cur_static_heap_size)
		- g_camerapool.heap_size;

	for (i = CAMERAPOOL_NUM_ORDERS - 1; i >= 0; i--) {
		/*Try lower order pages first*/
		while (size_remaining > 0 && size_remaining >= PAGE_SIZE << camerapool_orders[i]) {
			page = camerapool_dmabuf_page_pool_fetch(
						g_camerapool.static_pool[i], true);
			if (page) {
				size_remaining -= page_size(page);
				camerapool_dmabuf_page_pool_free_pages(
						g_camerapool.static_pool[i], page);
			} else {
				break;
			}
		}
		pr_debug("[%s]: [order:%d] [heap_size(MB):%ld] [size_remaining(MB):%ld]",
			__func__, camerapool_orders[i], camerapool_static_pool_size() >> 20,
			size_remaining >> 20);
	}
}

//For preallocating static pool
static void do_camerapool_preallocate(struct work_struct *work)
{
	if (!g_camerapool.static_pool[0] || !g_camerapool.dynamic_pool[0]) {
		pr_err("[%s]: pool is NULL! static[%s] dynamic[%s]\n", __func__,
			g_camerapool.static_pool[0] ? "VALID" : "NULL",
			g_camerapool.dynamic_pool[0] ? "VALID" : "NULL");
		return;
	}

	down_write(&g_camerapool.rwsem_lock);
	if (g_camerapool.heap_size > atomic_long_read(&g_camerapool.cur_static_heap_size))
		camerapool_expand();
	else
		camerapool_shrink();

	atomic_long_set(&g_camerapool.max_static_heap_size, camerapool_static_pool_size());

	pr_info("[%s]: heap_size(MB) [static: current:%lu, max:%lu] [dynamic: %lu]",
		__func__, atomic_long_read(&g_camerapool.cur_static_heap_size) >> 20,
		atomic_long_read(&g_camerapool.max_static_heap_size) >> 20,
		camerapool_dynamic_pool_size() >> 20);

	g_camerapool.is_pool_allocated = true;
	up_write(&g_camerapool.rwsem_lock);
}

void camerapool_preallocate(unsigned long heap_size)
{
	spin_lock(&g_camerapool.spn_lock);
	if (heap_size != g_camerapool.heap_size) {
		g_camerapool.heap_size = heap_size;
		g_camerapool.preallocation_flag = true;
		spin_unlock(&g_camerapool.spn_lock);
		schedule_work(&g_camerapool.ws_preallocate);
	} else {
		spin_unlock(&g_camerapool.spn_lock);
	}
}
EXPORT_SYMBOL_GPL(camerapool_preallocate);

//For freeing camerapool heap
static void do_camerapool_free_pages(struct work_struct *work)
{
	int i;

	down_write(&g_camerapool.rwsem_lock);

	pr_info("[%s]: heap_size(MB) [static: current:%lu, max:%lu] [dynamic: %lu]",
		__func__, atomic_long_read(&g_camerapool.cur_static_heap_size) >> 20,
		atomic_long_read(&g_camerapool.max_static_heap_size) >> 20,
		camerapool_dynamic_pool_size() >> 20);

	for (i = 0; i < CAMERAPOOL_NUM_ORDERS; i++)
		camerapool_free_pages_from_pool(g_camerapool.static_pool[i], true);

	atomic_long_set(&g_camerapool.max_static_heap_size, camerapool_static_pool_size());

	for (i = 0; i < CAMERAPOOL_NUM_ORDERS; i++)
		camerapool_free_pages_from_pool(g_camerapool.dynamic_pool[i], false);

	g_camerapool.is_pool_allocated = false;
	up_write(&g_camerapool.rwsem_lock);
}

void camerapool_free_pages(void)
{
	spin_lock(&g_camerapool.spn_lock);
	if (g_camerapool.preallocation_flag) {
		g_camerapool.preallocation_flag = false;
		g_camerapool.heap_size = 0;
		spin_unlock(&g_camerapool.spn_lock);
		schedule_work(&g_camerapool.ws_free_pages);
	} else {
		spin_unlock(&g_camerapool.spn_lock);
	}
}
EXPORT_SYMBOL_GPL(camerapool_free_pages);

static struct page *alloc_largest_available(unsigned long size,
			unsigned int max_order, bool is_static_pool)
{
	struct page *page;
	struct dmabuf_page_pool *pool;
	int i;

	for (i = 0; i < CAMERAPOOL_NUM_ORDERS; i++) {
		if (size < (PAGE_SIZE << camerapool_orders[i]))
			continue;
		if (max_order < camerapool_orders[i])
			continue;

		if (is_static_pool)
			pool = g_camerapool.static_pool[i];
		else
			pool = g_camerapool.dynamic_pool[i];

		page = camerapool_dmabuf_page_pool_alloc(pool, is_static_pool);

		if (!page)
			continue;

		return page;
	}
	return NULL;
}

static struct dma_buf *camerapool_heap_allocate(struct dma_heap *heap, unsigned long len,
					    unsigned long fd_flags, unsigned long heap_flags)
{
	int i = 0, ret = 0;
	int sg_size = 0;
	bool use_static_pool = true;
	bool force_dynamic_pool = false;
	unsigned int max_order = camerapool_orders[0];
	unsigned long size_remaining = 0;
	unsigned long prev_pool_size = 0, cur_pool_size = 0;
	struct scatterlist *sg;
	struct dma_buf *dmabuf;
	struct page *page, *tmp_page;
	struct list_head pages, exception_pages;
	struct samsung_dma_heap *samsung_dma_heap = dma_heap_get_drvdata(heap);
	struct samsung_dma_buffer *buffer;

	dma_heap_event_begin();

	if (!g_camerapool.dynamic_pool[0] || !g_camerapool.static_pool[0]) {
		ret = -ENOMEM;
		goto exit_error;
	}

	if (dma_heap_flags_video_aligned(samsung_dma_heap->flags))
		len = dma_heap_add_video_padding(len);
	size_remaining = len;

	INIT_LIST_HEAD(&pages);
	INIT_LIST_HEAD(&exception_pages);

	while (size_remaining > 0) {
		cur_pool_size = atomic_long_read(&g_camerapool.cur_static_heap_size);
		use_static_pool = true;

		//New pages added to static pool
		//Try alocation from static pool again
		if (cur_pool_size > prev_pool_size) {
			max_order = camerapool_orders[0];
			force_dynamic_pool = false;
		}

		if (cur_pool_size == 0 || force_dynamic_pool)
			use_static_pool = false;

		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by SIGKILL
		 */
		if (fatal_signal_pending(current)) {
			pr_err("[%s]: Fatal signal pending pid #%d", __func__, current->pid);
			ret = -EINTR;
			goto free_buffer;
		}

		page = alloc_largest_available(size_remaining, max_order, use_static_pool);
		if (!page && use_static_pool) {
			//Lowest available page size in static pool > size_remaining,
			//so, try allocattion from dynamic pool
			force_dynamic_pool = true;
			max_order = camerapool_orders[0];
			pr_debug("[%s]: useDynamicPool!, static_heap_size[%lu/%lu] remain[%lu]",
				__func__, prev_pool_size, cur_pool_size, size_remaining);
			prev_pool_size = cur_pool_size;
			continue;
		}

		if (!page) {
			ret = fatal_signal_pending(current) ? -EINTR : -ENOMEM;
			pr_err("[%s]: failed!! len[%lu] static_heap_size: cur[%lu] use[%d] ret: %d",
				__func__, len, cur_pool_size, use_static_pool, ret);
			goto free_buffer;
		}

		if (is_dma_heap_exception_page(page)) {
			list_add_tail(&page->lru, &exception_pages);
		} else {
			list_add_tail(&page->lru, &pages);
			size_remaining -= page_size(page);
			i++;
		}

		max_order = compound_order(page);
		prev_pool_size = cur_pool_size;
	}

	buffer = samsung_dma_buffer_alloc(samsung_dma_heap, len, i);
	if (IS_ERR(buffer)) {
		ret = PTR_ERR(buffer);
		goto free_buffer;
	}

	list_for_each_entry_safe(page, tmp_page, &exception_pages, lru) {
		list_del(&page->lru);
		__free_pages(page, compound_order(page));
	}

	sg = buffer->sg_table.sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_size += page_size(page);
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	heap_cache_flush(buffer);

	atomic_long_add(buffer->len, &g_camerapool.total_alloc_size);

	WARN_ON(buffer->len > sg_size);

	pr_debug("[%s]: [buffer size:%lu] TAllocSize[%lu] CurStaticHeapSize[%lu], sg_size[%d]",
		__func__, buffer->len, atomic_long_read(&g_camerapool.total_alloc_size),
		atomic_long_read(&g_camerapool.cur_static_heap_size), sg_size);

	dmabuf = samsung_export_dmabuf(buffer, fd_flags);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_export;
	}

	dma_heap_event_record(DMA_HEAP_EVENT_ALLOC, dmabuf, begin);

	return dmabuf;

free_export:
	for_each_sgtable_sg(&buffer->sg_table, sg, i) {
		struct page *p = sg_page(sg);

		__free_pages(p, compound_order(p));
	}
	samsung_dma_buffer_free(buffer);
free_buffer:
	list_for_each_entry_safe(page, tmp_page, &exception_pages, lru) {
		list_del(&page->lru);
		__free_pages(page, compound_order(page));
	}

	if (down_read_trylock(&g_camerapool.rwsem_lock)) {
		list_for_each_entry_safe(page, tmp_page, &pages, lru) {
			use_static_pool = true;

			if (atomic_long_read(&g_camerapool.cur_static_heap_size) + page_size(page)
				> atomic_long_read(&g_camerapool.max_static_heap_size)) {
				use_static_pool = false;
			}

			camerapool_add_or_free_page_to_pool(page,
				use_static_pool, g_camerapool.is_pool_allocated);
		}

		up_read(&g_camerapool.rwsem_lock);
	} else {
		//If semaphore read lock failed, free the pages back to system.
		list_for_each_entry_safe(page, tmp_page, &pages, lru) {
			use_static_pool = true;

			if (atomic_long_read(&g_camerapool.cur_static_heap_size) + page_size(page)
				> atomic_long_read(&g_camerapool.max_static_heap_size)) {
				use_static_pool = false;
			}

			camerapool_add_or_free_page_to_pool(page, use_static_pool, false);
		}
	}
exit_error:
	samsung_allocate_error_report(samsung_dma_heap, len, fd_flags, heap_flags);

	return ERR_PTR(ret);
}

static void camerapool_heap_release(struct samsung_dma_buffer *buffer)
{
	struct sg_table *table;
	struct scatterlist *sg;
	int i;
	int sg_size = 0;
	bool use_static_pool = false;
	struct page *page;

	table = &buffer->sg_table;
	if (down_read_trylock(&g_camerapool.rwsem_lock)) {
		for_each_sg(table->sgl, sg, table->nents, i) {
			page = sg_page(sg);
			sg_size += page_size(page);
			use_static_pool = false;

			if ((atomic_long_read(&g_camerapool.total_alloc_size) + page_size(page)
				+ atomic_long_read(&g_camerapool.cur_static_heap_size)
				- buffer->len)
				<= atomic_long_read(&g_camerapool.max_static_heap_size)) {
				use_static_pool = true;
			}

			camerapool_add_or_free_page_to_pool(page,
				use_static_pool, g_camerapool.is_pool_allocated);
		}

		up_read(&g_camerapool.rwsem_lock);
	} else {
		//If semaphore read lock failed, free the pages back to system.
		for_each_sg(table->sgl, sg, table->nents, i) {
			page = sg_page(sg);
			sg_size += page_size(page);
			use_static_pool = false;

			if ((atomic_long_read(&g_camerapool.total_alloc_size) + page_size(page)
				+ atomic_long_read(&g_camerapool.cur_static_heap_size)
				- buffer->len)
				<= atomic_long_read(&g_camerapool.max_static_heap_size)) {
				use_static_pool = true;
			}

			camerapool_add_or_free_page_to_pool(page, use_static_pool, false);
		}
	}

	atomic_long_sub(buffer->len, &g_camerapool.total_alloc_size);

	WARN_ON(buffer->len > sg_size);

	pr_debug("[%s]: [buffer size:%lu] TAllocSize[%lu] CurStaticHeapSize[%lu] sg_size[%d]",
		__func__, buffer->len, atomic_long_read(&g_camerapool.total_alloc_size),
		atomic_long_read(&g_camerapool.cur_static_heap_size), sg_size);

	samsung_dma_buffer_free(buffer);
}

static void camerapool_heap_show_mem(void *data, unsigned int filter, nodemask_t *nodemask)
{
	unsigned long staticpool = 0, dynamicpool = 0;

	staticpool = camerapool_static_pool_size();
	dynamicpool = camerapool_dynamic_pool_size();

	if (staticpool > 0)
		pr_info("camerapool: staticpool: %lu kB\n", staticpool >> 10);

	if (dynamicpool > 0)
		pr_info("camerapool: dynamicpool: %lu kB\n", dynamicpool >> 10);
}

static void show_camerapool_meminfo(void *data, struct seq_file *m)
{
	unsigned long staticpool = 0, dynamicpool = 0;

	staticpool = camerapool_static_pool_size();
	dynamicpool = camerapool_dynamic_pool_size();

	if (staticpool > 0)
		show_val_meminfo(m, "CamPoolStatic", staticpool >> 10);

	if (dynamicpool > 0)
		show_val_meminfo(m, "CamPoolDynamic", dynamicpool >> 10);
}

static long camerapool_heap_get_pool_size(struct dma_heap *heap)
{
	const char *name = dma_heap_get_name(heap);
	long staticpool = 0, dynamicpool = 0;

	if (strcmp(name, "camerapool"))
		return 0;

	staticpool = camerapool_static_pool_size();
	dynamicpool = camerapool_dynamic_pool_size();

	return staticpool + dynamicpool;
}

static const struct dma_heap_ops camerapool_heap_ops = {
	.allocate = camerapool_heap_allocate,
	.get_pool_size = camerapool_heap_get_pool_size,
};

static int camerapool_heap_probe(struct platform_device *pdev)
{
	int ret = samsung_heap_add(&pdev->dev, NULL, camerapool_heap_release, &camerapool_heap_ops);

	if (ret)
		goto out;

	register_trace_android_vh_show_mem(camerapool_heap_show_mem, NULL);
	register_trace_android_vh_meminfo_proc_show(show_camerapool_meminfo, NULL);

	return ret;
out:
	return -ENOMEM;
}

static const struct of_device_id camerapool_heap_of_match[] = {
	{ .compatible = "samsung,dma-heap-camerapool", },
	{ },
};
MODULE_DEVICE_TABLE(of, camerapool_heap_of_match);

static struct platform_driver camerapool_heap_driver = {
	.driver		= {
		.name	= "samsung,dma-heap-camerapool",
		.of_match_table = camerapool_heap_of_match,
	},
	.probe		= camerapool_heap_probe,
};

int __init camerapool_dma_heap_init(void)
{
	int i, j;
	struct dmabuf_page_pool *pool;

	g_camerapool.preallocation_flag = false;
	g_camerapool.is_pool_allocated = false;
	g_camerapool.heap_size = 0;
	atomic_long_set(&g_camerapool.cur_static_heap_size, 0);
	atomic_long_set(&g_camerapool.max_static_heap_size, 0);
	atomic_long_set(&g_camerapool.total_alloc_size, 0);
	spin_lock_init(&g_camerapool.spn_lock);
	init_rwsem(&g_camerapool.rwsem_lock);

	for (i = 0; i < CAMERAPOOL_NUM_ORDERS; i++) {
		g_camerapool.static_pool[i] =
			camerapool_dmabuf_page_pool_create(order_flags[i], camerapool_orders[i]);

		g_camerapool.dynamic_pool[i] =
			dmabuf_page_pool_create(order_flags[i], camerapool_orders[i]);

		if (!g_camerapool.static_pool[i] || !g_camerapool.dynamic_pool[i]) {
			pr_err("[%s]: pool creation failed! static[%s] dynamic[%s]\n", __func__,
				g_camerapool.static_pool[i] ? "VALID" : "NULL",
				g_camerapool.dynamic_pool[i] ? "VALID" : "NULL");

			goto error_pool_create;
		}
	}

	INIT_WORK(&g_camerapool.ws_preallocate, do_camerapool_preallocate);
	INIT_WORK(&g_camerapool.ws_free_pages, do_camerapool_free_pages);

	return platform_driver_register(&camerapool_heap_driver);

error_pool_create:
	for (j = 0; j <= i; j++) {
		pool = g_camerapool.static_pool[j];
		if (pool)
			camerapool_static_pool_destroy(pool);

		pool = g_camerapool.dynamic_pool[j];
		if (pool)
			dmabuf_page_pool_destroy(pool);

		g_camerapool.static_pool[j] = NULL;
		g_camerapool.dynamic_pool[j] = NULL;
	}

	return -ENOMEM;
}

void camerapool_dma_heap_exit(void)
{
	platform_driver_unregister(&camerapool_heap_driver);
}
