#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "scsc_mx_module.h"
#include IN_SCSC_INC(scsc_mx.h)
#include IN_SCSC_INC(api/bsmhcp.h)

#include "hci_trans.h"
#include "hci_pkt.h"
#include "slsi_bt_log.h"
#include "slsi_bt_controller.h"
#include "slsi_bt_fastpath.h"
#include "slsi_bt_qos.h"

#define SCSC_MIFRAM_FASTPATH_ALIGNMENT    (4)

/* Fastpath support */
static bool disable_fastpath_support = true;
module_param(disable_fastpath_support, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(disable_fastpath_support, "Disable fastpath support");

/* Fastpath write enables */
static unsigned long fastpath_write_enables = BIT(HCI_ACLDATA_PKT) |
			BIT(HCI_ISODATA_PKT) | BIT(HCI_SCODATA_PKT) |
			BIT(HCI_COMMAND_PKT) | BIT(HCI_PROPERTY_PKT);
module_param(fastpath_write_enables, ulong, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(fastpath_write_enables, "Fastpath activated packet types");

static unsigned long fastpath_activated;

/* fastpath shared memory */
struct fpshm {
	unsigned int ref;
	void *ptr;
	int key;
	struct list_head list;
};
static int fpshm_count;

LIST_HEAD(fpshm_list);
static DEFINE_MUTEX(fpshm_list_lock);

#define FPSHM_INVALID_KEY    FASTPATH_INVALID_KEY

#define USE_BUFFER_POOL
#ifdef USE_BUFFER_POOL
/*
 * Buffer pool
 */
#define FPSHM_BUFFER_SIZE               (0x800)
#define FPSHM_BUFFER_GET_ADDR(p, d)     (p->base + (d * FPSHM_BUFFER_SIZE))
struct fpshm_buffer_pool {
	unsigned int base;
	int allocated;
	int cur;
	bool bufs[8];
};

static struct fpshm_buffer_pool fpshm_bufpool = { 0 };
static const int FPSHM_BUFFER_MAX_COUNT =
					sizeof(fpshm_bufpool.bufs)/sizeof(bool);
static DEFINE_MUTEX(fpshm_bufpool_lock);

static bool fpshm_bufpool_enabled = false;
module_param(fpshm_bufpool_enabled, bool, S_IRUGO | S_IWUSR);

static unsigned int bufpool_create(void)
{
	struct fpshm_buffer_pool *p = &fpshm_bufpool;
	void *svc = slsi_bt_controller_get_service();
	int size = FPSHM_BUFFER_MAX_COUNT * FPSHM_BUFFER_SIZE;
	int err;

	if (p->base != 0)
		return 0;
	memset(p, 0, sizeof(fpshm_bufpool));
	err = scsc_mx_service_mifram_alloc(svc, size, &p->base,
					   SCSC_MIFRAM_FASTPATH_ALIGNMENT);
	if (err) {
		BT_ERR("Failed to allocate buffer pool\n");
		p->base = 0;
		return -ENOMEM;
	}
	p->allocated = 0;
	p->cur = 0;
	BT_INFO("fpshm buffer pool is created. base=%llx, count=%d, size=%x\n",
		p->base, FPSHM_BUFFER_MAX_COUNT, size);
	return 0;
}

static void bufpool_destroy(void)
{
	struct fpshm_buffer_pool *p = &fpshm_bufpool;

	if (p->base != 0) {
		void *svc = slsi_bt_controller_get_service();

		if (svc) {
			scsc_mx_service_mifram_free(svc, p->base);
			p->base = 0;
			BT_INFO("destroyed fpshm buffer pool\n");
		}
	}
}

static unsigned int bufpool_allocate(void)
{
	struct fpshm_buffer_pool *p = &fpshm_bufpool;
	int cur = -1;

	mutex_lock(&fpshm_bufpool_lock);
	if (p->base == 0) {
		if (bufpool_create() != 0)
			goto out;
	}

	if (p->allocated >= FPSHM_BUFFER_MAX_COUNT) {
		BT_ERR("buffer pool is full\n");
		goto out;
	}

	cur = p->cur;
	if (p->bufs[cur]) {
		int i;

		for (i = 0; i < FPSHM_BUFFER_MAX_COUNT; i++) {
			if (!p->bufs[cur])
				break;
			cur = (cur + 1) % FPSHM_BUFFER_MAX_COUNT;
		}
	}
	p->bufs[cur] = true;
	p->cur = (cur + 1) % FPSHM_BUFFER_MAX_COUNT;
	p->allocated++;
	BT_DBG("return %d index of buffer. %llx, fpshm_bufpool.allocated=%d\n",
		cur, FPSHM_BUFFER_GET_ADDR(p, cur), p->allocated);
out:
	mutex_unlock(&fpshm_bufpool_lock);
	return cur >= 0 ? FPSHM_BUFFER_GET_ADDR(p, cur) : 0;
}

static int bufpool_free(unsigned int ref)
{
	struct fpshm_buffer_pool *p = &fpshm_bufpool;
	int cur;

	mutex_lock(&fpshm_bufpool_lock);
	if (p->base == 0 || p->allocated == 0 || ref == 0) {
		mutex_unlock(&fpshm_bufpool_lock);
		return -EINVAL;
	}

	cur = (ref - p->base) / FPSHM_BUFFER_SIZE;
	if (cur < 0 || cur >= FPSHM_BUFFER_MAX_COUNT) {
		mutex_unlock(&fpshm_bufpool_lock);
		return -EINVAL;
	}

	if (p->bufs[cur]) {
		p->bufs[cur] = false;
		p->allocated--;
	}
	BT_DBG("free %d index of buffer. %llx, fpshm_bufpool.allocated=%d\n",
		cur, FPSHM_BUFFER_GET_ADDR(p, cur), p->allocated);

	if (!fpshm_bufpool_enabled && p->allocated == 0)
		bufpool_destroy();
	mutex_unlock(&fpshm_bufpool_lock);
	return 0;
}
#endif /* USE_BUFFER_POOL */


/*
 * Returns the available pointer from the offset
 */
static inline void *get_fpshm_ptr(unsigned int ref)
{
	void *svc = slsi_bt_controller_get_service();

	if (svc)
		return scsc_mx_service_mif_addr_to_ptr(svc, ref);
	return NULL;
}

/*
 * Allocates the shared memory region to be used for transmission to send
 */
static struct fpshm *alloc_fpshm(int size)
{
	void *svc = slsi_bt_controller_get_service();
	struct fpshm *p = NULL;;

	if (svc == NULL)
		return NULL;

	p = kzalloc(sizeof(struct fpshm), GFP_KERNEL);
	if (p == NULL)
		return NULL;

#ifdef USE_BUFFER_POOL
	if (fpshm_bufpool_enabled)
		p->ref = bufpool_allocate();
	/* Try dynamic allocation if the buffer pool allocation fails */
#endif
	if (p->ref == 0) {
		int err = scsc_mx_service_mifram_alloc(svc, size, &p->ref,
						SCSC_MIFRAM_FASTPATH_ALIGNMENT);
		if (err)
			p->ref = 0;
	}

	if (p->ref == 0) {
		kfree(p);
		return NULL;
	}

	p->key = FPSHM_INVALID_KEY;
	p->ptr = scsc_mx_service_mif_addr_to_ptr(svc, p->ref);
	INIT_LIST_HEAD(&p->list);

	fpshm_count++;
	if (fpshm_count <= 1)
		BT_DBG("ref=%x, ptr=%p, count=%d\n",
			p->ref, p->ptr, fpshm_count);
	else
		BT_WARNING("ref=%x, ptr=%p, count=%d\n",
			p->ref, p->ptr, fpshm_count);
	return p;
}

static void free_fpshm(struct fpshm *p)
{
	int err = -EINVAL;

	if (p) {
		fpshm_count--;
		if (fpshm_count <= 1)
			BT_DBG("ref=%x, ptr=%p, count=%d\n",
				p->ref, p->ptr, fpshm_count);
		else
			BT_WARNING("ref=%x, ptr=%p, count=%d\n",
				p->ref, p->ptr, fpshm_count);

#ifdef USE_BUFFER_POOL
		if (fpshm_bufpool.base)
			err = bufpool_free(p->ref);
#endif
		if (err) {
			void *svc = slsi_bt_controller_get_service();

			if (svc)
				scsc_mx_service_mifram_free(svc, p->ref);
		}
		kfree(p);
	}
}

static void fpshm_dequeue(struct list_head *list)
{
	struct fpshm *p = list_first_entry(list, struct fpshm, list);

	if (p) {
		list_del(&p->list);
		free_fpshm(p);
	}
}

static struct fpshm *fpshm_find_by_ref(struct list_head *list, unsigned int ref)
{
	struct fpshm *p;

	list_for_each_entry(p, list, list) {
		if (p->ref == ref)
			return p;
	}

	return NULL;
}

static void fpshm_clear(struct list_head *list)
{
	while (!list_empty(list))
		fpshm_dequeue(list);
}

inline void slsi_bt_fastpath_datagram_to_bytes(char *dst,
					const struct fastpath_datagram *src)
{
	dst[0] = src->type;
	memcpy(dst+1, &src->length, sizeof(src->length));
	memcpy(dst+3, &src->location, sizeof(src->location));
}

inline void slsi_bt_fastpath_bytes_to_datagram(struct fastpath_datagram *dst,
					       const char *src)
{
	dst->type = src[0];
	memcpy(&dst->length, src+1, sizeof(dst->length));
	memcpy(&dst->location, src+3, sizeof(dst->location));
}

inline void slsi_bt_fastpath_acknowledgement_all(void)
{
	struct fpshm *p = NULL;
	int cnt = 0;

	if (!fastpath_activated || list_empty(&fpshm_list))
		return;

	mutex_lock(&fpshm_list_lock);
	while (!list_empty(&fpshm_list)) {
		p = list_first_entry(&fpshm_list, struct fpshm, list);
		if (!p || p->key == FPSHM_INVALID_KEY)
			break;
		BT_DBG("free %d\n", p->key);
		fpshm_dequeue(&fpshm_list);
		cnt++;
	}
	mutex_unlock(&fpshm_list_lock);
	BT_DBG("freed count=%d\n", cnt);
}

inline void slsi_bt_fastpath_acknowledgement(int key)
{
	struct fpshm *p = list_first_entry(&fpshm_list, struct fpshm, list);

	if (!fastpath_activated || list_empty(&fpshm_list))
		return;

	BT_DBG("Key=%d wait for %d\n", key, p->key);
	if (p->key == key) {
		mutex_lock(&fpshm_list_lock);
		fpshm_dequeue(&fpshm_list);
		mutex_unlock(&fpshm_list_lock);
	}
}

inline struct sk_buff *slsi_bt_fastpath_enable_req_packet(bool enable)
{
	struct sk_buff *skb = alloc_hci_pkt_skb(FASTPATH_ENABLE_REQ_PKT_SIZE);

	if (skb) {
		SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_HCI);
		skb_put_u8(skb, VSPMP_CHANNEL_FASTPATH);
		skb_put_u8(skb, FASTPATH_ENABLE_REQ_TAG);
		skb_put_u8(skb, FASTPATH_ENABLE_REQ_LEN);
		skb_put_u8(skb, enable);
	}
	return skb;
}

inline struct sk_buff *slsi_bt_fastpath_to_air_activate_rsp_packet(void)
{
	struct sk_buff *skb = alloc_hci_pkt_skb(
					FASTPATH_TO_AIR_ACTIVATE_RSP_PKT_SIZE);

	if (skb) {
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_HCI);
		SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);
		skb_put_u8(skb, VSPMP_CHANNEL_FASTPATH);
		skb_put_u8(skb, FASTPATH_TO_AIR_ACTIVATE_RSP_TAG);
		skb_put_u8(skb, FASTPATH_TO_AIR_ACTIVATE_RSP_LEN);
	}
	return skb;
}

inline struct sk_buff *slsi_bt_fastpath_datagram_packet(
					struct fastpath_datagram *datagram)
{
	struct sk_buff *skb = alloc_hci_pkt_skb(FASTPATH_DATAGRAM_PKT_SIZE);
	char *p = NULL;

	if (skb) {
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_HCI);
		SET_HCI_PKT_TYPE(skb, HCI_PROPERTY_PKT);

		skb_put_u8(skb, VSPMP_CHANNEL_FASTPATH);

		skb_put_u8(skb, FASTPATH_DATAGRAM_TAG);
		skb_put_u8(skb, FASTPATH_DATAGRAM_LEN);

		p = skb_put(skb, FASTPATH_DATAGRAM_LEN);
		slsi_bt_fastpath_datagram_to_bytes(p, datagram);
	}
	return skb;
}

/*
 * Write a packet to a shared memory for fastpath and fill the datagram
 */
int slsi_bt_fastpath_write(struct sk_buff *skb,
			   struct fastpath_datagram *datagram)
{
	struct fpshm *p = alloc_fpshm(skb->len);
	unsigned char type;

	if (p == NULL) {
		BT_ERR("failed to allocate fastpath shared memory\n");
		return -EINVAL;
	}

	BTTR_TRACE_HEX(BTTR_TAG_FASTPATH_TX, skb);
	memcpy(p->ptr, skb->data, skb->len);
	type = GET_HCI_PKT_TYPE(skb);

	datagram->type = type;
	datagram->length = skb->len;
	datagram->location = p->ref;

	mutex_lock(&fpshm_list_lock);
	list_add_tail(&p->list, &fpshm_list);
	mutex_unlock(&fpshm_list_lock);
	BT_DBG("add a item to fpshm_list\n");

	return 0;
}

void _slsi_bt_fastpath_update_key(struct fastpath_datagram datagram, int key)
{
	struct fpshm *p = NULL;

	mutex_lock(&fpshm_list_lock);
	p = fpshm_find_by_ref(&fpshm_list, datagram.location);
	if (p)
		p->key = key;
	mutex_unlock(&fpshm_list_lock);
}

void slsi_bt_fastpath_update_key(const char *p, size_t len, int key)
{
	struct fastpath_datagram datagram;

	if (len < 3)
		return;

	if (*p++ != VSPMP_CHANNEL_FASTPATH)
		return;

	if (*p++ != FASTPATH_DATAGRAM_TAG)
		return;

	if (*p++ != FASTPATH_DATAGRAM_LEN)
		return;

	slsi_bt_fastpath_bytes_to_datagram(&datagram, p);
	_slsi_bt_fastpath_update_key(datagram, key);
}

/*
 * Read a packet from the shared memory of the datagram
 */
struct sk_buff *slsi_bt_fastpath_read(struct fastpath_datagram *datagram)
{
	struct sk_buff *skb = NULL;
	void *p = NULL, *qos = NULL;

	if (datagram == NULL) {
		BT_ERR("Invalid argument. datagram is NULL\n");
		return NULL;
	}

	p = get_fpshm_ptr(datagram->location);
	if (p == NULL) {
		BT_ERR("failed to transfer ref to ptr\n");
		return NULL;
	}

	skb = alloc_hci_pkt_skb(datagram->length);
	if (skb == NULL) {
		BT_ERR("failed to allocate hci_pkt_skb\n");
		return NULL;
	}

	SET_HCI_PKT_TYPE(skb, datagram->type);
	skb_put_data(skb, p, datagram->length);

	qos = slsi_bt_controller_get_qos();
	if (qos)
		slsi_bt_qos_update((struct slsi_bt_qos *)qos, datagram->length);

	BTTR_TRACE_HEX(BTTR_TAG_FASTPATH_RX, skb);

	return skb;
}

bool slsi_bt_fastpath_supported(void)
{
	return !disable_fastpath_support;
}

void slsi_bt_fastpath_activate(bool active)
{
	if (active) {
		fastpath_activated = fastpath_write_enables;
		BT_INFO("activated fastpath: %lx\n", fastpath_activated);
	} else {
#ifdef USE_BUFFER_POOL
		mutex_lock(&fpshm_bufpool_lock);
		bufpool_destroy();
		mutex_unlock(&fpshm_bufpool_lock);
#endif
		mutex_lock(&fpshm_list_lock);
		fpshm_clear(&fpshm_list);
		mutex_unlock(&fpshm_list_lock);

		fastpath_activated = 0;
		BT_INFO("deactivated fastpath\n");
	}
}

bool slsi_bt_fastpath_available(struct sk_buff *skb)
{
	unsigned char type;

	if (skb == NULL || disable_fastpath_support)
		return false;

	/* Don't send by fastpath if it is too small */
	if (skb->len <= (FASTPATH_DATAGRAM_PKT_SIZE-1))
		return false;

	type = GET_HCI_PKT_TYPE(skb);
	if (type == HCI_UNKNOWN_PKT || type > 5)
		return false;

	return test_bit(type, &fastpath_activated);
}

int slsi_bt_fastpath_proc_show(struct seq_file *m)
{
#define IS_ACTIVE(e) test_bit(e, &fastpath_activated) ? "enabled" : "disabled"

	seq_printf(m, "    fastpath support: %d\n", !disable_fastpath_support);
	if (disable_fastpath_support)
		return 0;

#ifdef USE_BUFFER_POOL
	seq_printf(m, "    bufpool enabled: %d\n", fpshm_bufpool_enabled);
	if (fpshm_bufpool.base)
		seq_printf(m, "      - base reference: %x\n",
			fpshm_bufpool.base);

	if (fpshm_bufpool_enabled || fpshm_bufpool.allocated)
		seq_printf(m, "      - allocated/max: %d/%d\n",
			fpshm_bufpool.allocated, FPSHM_BUFFER_MAX_COUNT);
#endif
	seq_printf(m, "    fastpath write enabled: %lx\n", fastpath_activated);
	seq_printf(m, "      - HCI_COMMAND_PKT(0x%lx): %s\n",
			BIT(HCI_COMMAND_PKT), IS_ACTIVE(HCI_COMMAND_PKT));
	seq_printf(m, "      - HCI_ACLDATA_PKT(0x%lx): %s\n",
			BIT(HCI_ACLDATA_PKT), IS_ACTIVE(HCI_ACLDATA_PKT));
	seq_printf(m, "      - HCI_SCODATA_PKT(0x%lx): %s\n",
			BIT(HCI_ISODATA_PKT), IS_ACTIVE(HCI_ISODATA_PKT));
	seq_printf(m, "      - HCI_ISODATA_PKT(0x%lx): %s\n",
			BIT(HCI_SCODATA_PKT), IS_ACTIVE(HCI_SCODATA_PKT));
	seq_printf(m, "      - HCI_VENDOR_PKT(0x%lx): %s\n",
			BIT(HCI_PROPERTY_PKT), IS_ACTIVE(HCI_PROPERTY_PKT));
	return 0;
}
