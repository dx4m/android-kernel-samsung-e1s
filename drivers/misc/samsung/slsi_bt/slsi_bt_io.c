/******************************************************************************
 *                                                                            *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * S.LSI Bluetooth Device Driver                                              *
 *                                                                            *
 * This driver manages S.LSI bluetooth service and data transaction           *
 *                                                                            *
 ******************************************************************************/
#include <linux/init.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>

#include "scsc_mx_module.h"
#include IN_SCSC_INC(scsc_wakelock.h)

#include "slsi_bt_io.h"
#include "slsi_bt_err.h"
#include "slsi_bt_controller.h"
#include "hci_pkt.h"
#include "hci_trans.h"
#include "slsi_bt_property.h"
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
#include "hci_lr_wpan.h"
#include "slsi_bt_qos.h"
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_H4)
#include "hci_h4.h"
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_BCSP)
#include "hci_bcsp.h"
#endif

#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
#include "slsi_bt_tty.h"
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
#include "hci_slsi.h"
#endif

#define SLSI_BT_MODDESC "SLSI Maxwell BT Module Driver"
#define SLSI_BT_MODAUTH "Samsung Electronics Co., Ltd"
#define SLSI_BT_MODVERSION "-devel"

#define SLSI_BT_MODULE_NAME "scsc_bt"

static DEFINE_MUTEX(bt_start_mutex);
static DEFINE_MUTEX(bt_file_open_mutex);

/* S.LSI Bluetooth Device Driver Context */
static struct {
	dev_t                          dev;
	struct class                   *class;

	struct cdev                    cdev;
	struct device                  *device;

#define SLSI_BT_PROC_DIR    "driver/"SLSI_BT_MODULE_NAME
	struct proc_dir_entry          *procfs_dir;

	struct scsc_wake_lock          wake_lock;       /* common & write */
	struct scsc_wake_lock          read_wake_lock;

	bool                           open_status;
	atomic_t                       driver_status;   /* open & release */
	atomic_t                       file_io_users;
} bt_drv;

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
static struct {
	dev_t                          dev;
	struct class                   *class;

	struct cdev                    cdev;
	struct device                  *device;

	struct scsc_wake_lock          wake_lock;       /* common & write */
	struct scsc_wake_lock          read_wake_lock;

	bool                           open_status;
	atomic_t                       users;
} lr_wpan_drv;
#endif

/* Data transport Context */
static struct {
	struct hci_trans               *htr;            /* own transporter */
	unsigned int                   enabled_trs;     /* Using transpoters */

	struct sk_buff_head            recv_q;
	struct mutex                   recv_q_lock;

	wait_queue_head_t              read_wait;

	unsigned int                   send_data_count;
	unsigned int                   recv_data_count;
	unsigned int                   read_data_count;

	unsigned int                   recv_pkt_count;
	unsigned int                   read_pkt_count;

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	struct hci_trans               *lr_wpan_htr;
	struct sk_buff_head            lr_wpan_recv_q;
	struct mutex                   lr_wpan_recv_q_lock;

	wait_queue_head_t              lr_wpan_read_wait;
#endif
} bt_trans;

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
static void lr_wpan_hw_error_packet(void)
{
	struct sk_buff *skb;

	if (!lr_wpan_drv.open_status)
		return;

	mutex_lock(&bt_trans.lr_wpan_recv_q_lock);
	skb = skb_peek_tail(&bt_trans.lr_wpan_recv_q);
	if (skb && skb->data_len == 2 &&
	    skb->data[0] == HCI_LR_WPAN_TAG_HW_ERROR) {
		/* There is already HW_ERROR packet in the recv queue */
		mutex_unlock(&bt_trans.lr_wpan_recv_q_lock);
		return;
	}

	skb = alloc_hci_pkt_skb(2);

	if (skb) {
		skb_put_u8(skb, HCI_LR_WPAN_TAG_HW_ERROR); // Tag
		skb_put_u8(skb, 0); // Length

		skb_queue_tail(&bt_trans.lr_wpan_recv_q, skb);

		wake_up(&bt_trans.lr_wpan_read_wait);
	} else
		BT_ERR("failed to allocate skb for hw error\n");

	mutex_unlock(&bt_trans.lr_wpan_recv_q_lock);
}
#endif

static struct sk_buff *hw_error_packet(void)
{
	const char data[] = { 0 };
	struct sk_buff *skb = alloc_hci_pkt_skb(4);

	if (skb) {
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_H4);
		SET_HCI_PKT_HW_ERROR(skb);
		skb_put_u8(skb, HCI_EVENT_PKT);
		skb_put_u8(skb, HCI_EVENT_HARDWARE_ERROR_EVENT);
		skb_put_u8(skb, (sizeof(data))&0xFF);
		skb_put_data(skb, data, sizeof(data));
	} else
		BT_ERR("failed to allocate skb for hw error\n");

	return skb;
}

struct sk_buff *syserr_info_packet(void)
{
	unsigned char buf[10];
	struct sk_buff *skb = alloc_hci_pkt_skb(4+sizeof(buf));
	size_t len = slsi_bt_controller_get_syserror_info(buf, sizeof(buf));

	if (skb) {
		SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_H4);
		SET_HCI_PKT_TYPE(skb, HCI_EVENT_PKT);
		skb_put_u8(skb, HCI_EVENT_PKT);
		skb_put_u8(skb, HCI_EVENT_VENDOR_SPECIFIC_EVENT);
		skb_put_u8(skb, (1+len)&0xFF);
		skb_put_u8(skb, HCI_VSE_SYSTEM_ERROR_INFO_SUB_CODE);
		skb_put_data(skb, buf, len);
	} else
		BT_WARNING("failed to allocate skb for sys error info\n");

	return skb;
}

static void slsi_bt_error_packet_ready(void)
{
	struct sk_buff *skb = NULL;

	mutex_lock(&bt_trans.recv_q_lock);
	skb = skb_peek_tail(&bt_trans.recv_q);
	if (skb && TEST_HCI_PKT_HW_ERROR(skb)) {
		/* There is already HW_ERROR packet in the recv queue */
		mutex_unlock(&bt_trans.recv_q_lock);
		return;
	}

	skb = skb_dequeue(&bt_trans.recv_q);
	skb_queue_purge(&bt_trans.recv_q);
	if (skb) {
		/* Save the first packet. It is in reading operation */
		if (GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_ON_READING)
			skb_queue_tail(&bt_trans.recv_q, skb);
		else
			kfree_skb(skb);
	}

	/* send remained logs */
	slsi_bt_fwlog_snoop_queue_tail((void*)&bt_trans.recv_q, false);

	skb = syserr_info_packet();
	if (skb)
		skb_queue_tail(&bt_trans.recv_q, skb);

	skb = hw_error_packet();
	if (skb)
		skb_queue_tail(&bt_trans.recv_q, skb);

	mutex_unlock(&bt_trans.recv_q_lock);
}

static void slsi_bt_error_handle(int reason, bool req_restart)
{
	TR_WARNING("HW Error is occured\n");

	mutex_lock(&bt_start_mutex);
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	if (bt_drv.open_status == false && lr_wpan_drv.open_status == false) {
#else
	if (bt_drv.open_status == false) {
#endif
		mutex_unlock(&bt_start_mutex);
		return;
	}

#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
	/* This closes tty first to avoid receiving unnecessary garbage data
	 * in the abnormal situations. It can be make race condition with
	 * slsi_bt_transport_deinit() called by slsi_bt_release(). */
	BT_DBG("try to close tty\n");
	slsi_bt_tty_close();
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	/* This closes uart first to avoid receiving unnecessary garbage data
	 * in the abnormal situations. It can be make race condition with
	 * slsi_bt_transport_deinit() called by slsi_bt_release(). */
	BT_DBG("stop hci uart to send/recv frame\n");
	hci_slsi_block(bt_trans.htr);
#endif
	mutex_unlock(&bt_start_mutex);

	if (bt_drv.open_status && req_restart) {
		slsi_bt_error_packet_ready();
#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
		wake_up(&bt_trans.read_wait);
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
		if (atomic_read(&bt_drv.file_io_users) == 0){
			struct hci_trans *htr = bt_trans.htr;
			struct sk_buff *skb = skb_dequeue(&bt_trans.recv_q);
			int ret;

			TR_DBG("Move error packets to hci_slsi\n");
			hci_slsi_resume(htr);
			do {
				if (TEST_HCI_PKT_HW_ERROR(skb))
					SET_HCI_PKT_TYPE(skb, HCI_EVENT_PKT);

				if (GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_H4) {
					skb_pull(skb, 1);
					SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_HCI);
				}

				ret = htr->recv_skb(htr, skb);
				skb = skb_dequeue(&bt_trans.recv_q);
			} while (ret >= 0 && skb);

			if (ret)
				BT_ERR("error: %d\n", ret);
		}
#endif
	}
	slsi_bt_controller_stop(SCSC_SERVICE_ID_BT);
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	lr_wpan_hw_error_packet();
	slsi_bt_controller_stop(SCSC_SERVICE_ID_LR_WPAN);
#endif
}

static void slsi_bt_fwlog_to_user(void)
{
	mutex_lock(&bt_trans.recv_q_lock);
	slsi_bt_fwlog_snoop_queue_tail((void*)&bt_trans.recv_q, true);
	mutex_unlock(&bt_trans.recv_q_lock);
}

static int slsi_bt_skb_recv(struct hci_trans *htr, struct sk_buff *skb)
{
	int ret = 0;

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	if (atomic_read(&bt_drv.file_io_users) == 0) {
		TR_DBG("No BT file_io_users, discarding packet\n");
		kfree(skb);
		return ret;
	}
#endif

	if (skb) {
		bt_trans.recv_pkt_count++;
		bt_trans.recv_data_count += skb->len;

		mutex_lock(&bt_trans.recv_q_lock);
		skb_queue_tail(&bt_trans.recv_q, skb);
		ret = skb->len;
		mutex_unlock(&bt_trans.recv_q_lock);
	}
	slsi_bt_fwlog_to_user();

	TR_DBG("%d message(s) is remained\n", skb_queue_len(&bt_trans.recv_q));
	if (skb_queue_len(&bt_trans.recv_q) > 0) {
		if (!wake_lock_active(&bt_drv.read_wake_lock))
			wake_lock(&bt_drv.read_wake_lock);

		wake_up(&bt_trans.read_wait);
	}

	return ret;
}

static unsigned int slsi_bt_poll(struct file *file, poll_table *wait)
{
	TR_DBG("wait\n");
	poll_wait(file, &bt_trans.read_wait, wait);

	if (slsi_bt_in_recovery_progress()) {
		TR_DBG("waiting for reset after recovery\n");
		return POLLOUT;
	}

	if (skb_queue_empty(&bt_trans.recv_q)) {
		TR_DBG("no change\n");
		return POLLOUT;
	}

	TR_DBG("queue changed\n");
	return POLLIN | POLLRDNORM;
}

static ssize_t slsi_bt_read(struct file *file, char __user *buf, size_t nr,
				loff_t *loff)
{
	struct sk_buff *skb;
	size_t len, offset = 0;

	TR_DBG("number of read: %zu\n", nr);

	if (buf == NULL)
		return -EINVAL;

	if (bt_drv.open_status == false)
		return 0;

	if (wait_event_interruptible(bt_trans.read_wait,
				!skb_queue_empty(&bt_trans.recv_q)) != 0)
		return 0;

	while (nr > 0 && (skb = skb_dequeue(&bt_trans.recv_q)) != NULL) {
		mutex_lock(&bt_trans.recv_q_lock);

		len = min(nr, (size_t)skb->len);
		TR_DBG("confirmed length: %zu\n", len);
		if (copy_to_user(buf + offset, skb->data, len) != 0) {
			TR_WARNING("copy_to_user returned error\n");
			skb_queue_head(&bt_trans.recv_q, skb);
			mutex_unlock(&bt_trans.recv_q_lock);
			return -EACCES;
		}

		if (skb->len > len) {
			skb_pull(skb, len);
			SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_ON_READING);
			skb_queue_head(&bt_trans.recv_q, skb);
			TR_DBG("%u byte(s) are left in the packet\n", skb->len);
		} else { /* Complete read one packet */
			bt_trans.read_pkt_count++;
			kfree_skb(skb);
		}
		nr -= len;
		offset += len;

		mutex_unlock(&bt_trans.recv_q_lock);
	}

	if (skb_queue_len(&bt_trans.recv_q) > 0 &&
	    wake_lock_active(&bt_drv.read_wake_lock))
		wake_unlock(&bt_drv.read_wake_lock);

	TR_DBG("%zu bytes read complete. remaining message(s)=%d\n", offset,
		skb_queue_len(&bt_trans.recv_q));
	bt_trans.read_data_count += offset;
	return offset;
}

static ssize_t slsi_bt_write(struct file *file,
			const char __user *data, size_t count, loff_t *offset)
{
	struct hci_trans *htr = hci_trans_get_next(bt_trans.htr);
	ssize_t ret = 0;
	size_t len = count;

	TR_DBG("count of write: %zu\n", count);
	if (slsi_bt_err_status()) {
		if (slsi_bt_in_recovery_progress()) {
			msleep(1000);
			TR_WARNING("service is recovering... try again!\n");
			return -EAGAIN;
		}
		return -EIO;
	}

	if (htr == NULL || htr->send == NULL)
		return -EFAULT;

	wake_lock(&bt_drv.wake_lock);
	while (ret >= 0 && len) {
		ret = htr->send(htr, data, len, HCITRFLAG_UBUF);
		if (ret < 0) {
			TR_ERR("send failed to %s\n", hci_trans_get_name(htr));
			slsi_bt_err(SLSI_BT_ERR_SEND_FAIL);
			break;
		}
		bt_trans.send_data_count += ret;
		len -= ret;
	}
	wake_unlock(&bt_drv.wake_lock);
	return count - len;
}

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
static int slsi_lr_wpan_skb_recv(struct hci_trans *htr, struct sk_buff *skb)
{
	struct slsi_bt_qos *qos;
	int ret = 0;

	if (skb) {
		mutex_lock(&bt_trans.lr_wpan_recv_q_lock);
		skb_queue_tail(&bt_trans.lr_wpan_recv_q, skb);
		ret = skb->len;
		mutex_unlock(&bt_trans.lr_wpan_recv_q_lock);

		qos = slsi_bt_controller_get_lr_wpan_qos();
		slsi_bt_qos_update(qos, ret);
	}

	if (skb_queue_len(&bt_trans.lr_wpan_recv_q) > 0) {
		if (!wake_lock_active(&lr_wpan_drv.read_wake_lock))
			wake_lock(&lr_wpan_drv.read_wake_lock);

		wake_up(&bt_trans.lr_wpan_read_wait);
	}

	return ret;
}

static int slsi_demultiplex_skb_recv(struct hci_trans *htr, struct sk_buff *skb)
{
	if (skb) {
		if (GET_HCI_PKT_TR_TYPE(skb) == HCI_TRANS_LR_WPAN)
			return slsi_lr_wpan_skb_recv(bt_trans.lr_wpan_htr, skb);
		else
			return hci_trans_recv_skb(htr, skb);
	}

	return -EINVAL;
}
#endif

static int slsi_bt_transport_init(unsigned int en_trs)
{
	struct hci_trans *htr = NULL;
	int ret;

	BT_INFO("en_trs: 0x%x\n", en_trs);

	/* Setup the transport layers */
	bt_trans.htr = hci_trans_new("slsi_bt", HCI_TRANS_TYPE_SLSI_BT);
	bt_trans.htr->recv_skb = slsi_bt_skb_recv;

#if IS_ENABLED(CONFIG_SLSI_BT_H4)
	if (en_trs & SLSI_BT_TR_EN_H4) {
		/* Connect to h4(reverse) */
		htr = hci_trans_new("H4 Transport (revert)",
				    HCI_TRANS_TYPE_SLSI_H4);
		hci_trans_add_tail(htr, bt_trans.htr);
		ret = hci_h4_init(htr, true);
		if (ret)
			return ret;
	}
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	{
		htr = hci_trans_new("Demultiplex Transport",
				    HCI_TRANS_TYPE_SLSI_DEMULTIPLEX);
		htr->recv_skb = slsi_demultiplex_skb_recv;
		hci_trans_add_tail(htr, bt_trans.htr);

		/* The htr for LR-WPAN is a special case as that is not stacked
		   on the other htr in this function. */
		init_waitqueue_head(&bt_trans.lr_wpan_read_wait);
		bt_trans.lr_wpan_htr = hci_trans_new("LR-WPAN",
						HCI_TRANS_TYPE_SLSI_LR_WPAN);
		ret = hci_lr_wpan_init(bt_trans.lr_wpan_htr, htr);
		if (ret)
			return ret;
	}
#endif
	if (en_trs & SLSI_BT_TR_EN_PROP) {
		htr = hci_trans_new("driver property handler hook",
				    HCI_TRANS_TYPE_SLSI_PROPERTY);
		hci_trans_add_tail(htr, bt_trans.htr);
		ret = slsi_bt_property_init(htr);
		if (ret)
			return ret;
	}
#if IS_ENABLED(CONFIG_SLSI_BT_BCSP)
	if (en_trs & SLSI_BT_TR_EN_BCSP) {
		/* Connect to bcsp */
		htr = hci_trans_new("BCSP Transport",
				    HCI_TRANS_TYPE_SLSI_BCSP);
		hci_trans_add_tail(htr, bt_trans.htr);
		ret = hci_bcsp_open(htr);
		if (ret)
			return ret;
	}
#endif
	{
		/* Connect to controller for sleep/wakeup */
		htr = hci_trans_new("BT Controller Transport",
				    HCI_TRANS_TYPE_SLSI_CONTROLLER);
		hci_trans_add_tail(htr, bt_trans.htr);
		ret = slsi_bt_controller_transport_configure(htr);
		if (ret)
			return ret;
	}
#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
	if (en_trs & SLSI_BT_TR_EN_TTY) {
		/* Connect to tty */
		ret = slsi_bt_tty_open();
		if (ret)
			return ret;
		htr = hci_trans_new("tty Transport", HCI_TRANS_TYPE_SLSI_TTY);
		hci_trans_add_tail(htr, bt_trans.htr);
		ret = slsi_bt_tty_transport_configure(htr);
		if (ret)
			return ret;
	}
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	if (en_trs & SLSI_BT_TR_EN_HCI_UART) {
		htr = hci_trans_new("hci_slsi", HCI_TRANS_TYPE_SLSI_HCI_SLSI);
		hci_trans_add_tail(htr, bt_trans.htr);
	}
#endif
	bt_trans.enabled_trs = en_trs;
	return 0;
}

#if IS_ENABLED(CONFIG_SLSI_BT_H4)
static int slsi_bt_transport_add_h4(void)
{
	int ret;
	struct hci_trans *htr;

	htr = hci_trans_new("H4 Transport (revert)",
			    HCI_TRANS_TYPE_SLSI_H4);
	ret = hci_h4_init(htr, true);
	if (ret) {
		hci_trans_free(htr);
		return ret;
	}

	hci_trans_add(htr, bt_trans.htr);
	return 0;
}

static int slsi_bt_transport_remove_h4(void)
{
	struct hci_trans *htr;
	struct list_head *pos = NULL, *tmp = NULL;

	list_for_each_safe(pos, tmp, &bt_trans.htr->list) {
		htr = list_entry(pos, struct hci_trans, list);
		if (hci_trans_get_type(htr) == HCI_TRANS_TYPE_SLSI_H4) {
			hci_trans_del(htr);
			BT_DBG("free transport: %s\n", hci_trans_get_name(htr));
			hci_trans_free(htr);
			break;
		}
	}

	return 0;
}
#endif

static void slsi_bt_transport_deinit(void)
{
	struct hci_trans *del = NULL;
	struct list_head *pos = NULL, *tmp = NULL;

#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
	/* First, stop receving the lowest layer */
	if (bt_trans.enabled_trs & SLSI_BT_TR_EN_TTY) {
		slsi_bt_tty_close();
		msleep(100);
	}
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	/* First, block receving the lowest layer */
	if (bt_trans.enabled_trs & SLSI_BT_TR_EN_HCI_UART)
		hci_slsi_block(bt_trans.htr);
#endif
	if (bt_trans.htr == NULL)
		return;

	/* Deinitialize all of transport layer */
	list_for_each_safe(pos, tmp, &bt_trans.htr->list) {
		del = list_entry(pos, struct hci_trans, list);
		hci_trans_del(del);
		BT_DBG("free transport: %s\n", hci_trans_get_name(del));
		hci_trans_free(del);
	}

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	hci_trans_del(bt_trans.lr_wpan_htr);
	hci_trans_free(bt_trans.lr_wpan_htr);
#endif
	hci_trans_del(bt_trans.htr);
	hci_trans_free(bt_trans.htr);
	bt_trans.htr = NULL;
	bt_trans.enabled_trs = 0;
}

static int slsi_bt_transport_change(unsigned int en_trs)
{
	if (bt_trans.enabled_trs == en_trs) {
		BT_DBG("Same as current enabled trans: %u\n", en_trs);
		return 0;
	} else if (bt_trans.enabled_trs != 0) {
		BT_DBG("Change transport layers, from %u to %u\n",
						bt_trans.enabled_trs, en_trs);
#if IS_ENABLED(CONFIG_SLSI_BT_H4)
		if (((en_trs | SLSI_BT_TR_EN_H4) == bt_trans.enabled_trs) ||
		    ((bt_trans.enabled_trs | SLSI_BT_TR_EN_H4) == en_trs)) {
			/* H4 is the only difference. Add or remove H4 */
			bt_trans.enabled_trs = en_trs;
			if (en_trs & SLSI_BT_TR_EN_H4)
				return slsi_bt_transport_add_h4();

			return slsi_bt_transport_remove_h4();
		}
#endif
		slsi_bt_transport_deinit();
	}
	return slsi_bt_transport_init(en_trs);
}

bool is_open_release_status(void)
{
	return (atomic_read(&bt_drv.driver_status) > 0);
}

int slsi_bt_open(unsigned int trs, struct hci_trans **top_htr)
{
	int ret = 0;

	BT_INFO("(open_status=%u)\n", bt_drv.open_status ? 1 : 0);
	atomic_inc(&bt_drv.driver_status);

	/* Only 1 user allowed */
	if (bt_drv.open_status) {
		atomic_dec(&bt_drv.driver_status);
		return -EBUSY;
	}

	wake_lock(&bt_drv.wake_lock);
	mutex_lock(&bt_start_mutex);

	mutex_init(&bt_trans.recv_q_lock);
	skb_queue_head_init(&bt_trans.recv_q);
	init_waitqueue_head(&bt_trans.read_wait);

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	if (lr_wpan_drv.open_status) {
		/* Add H4 if needed before BT is started to be ready */
		ret = slsi_bt_transport_change(trs);
		if (ret) {
			BT_ERR("open failed to init transports: %d\n", ret);
			goto out;
		}
	} else
#endif
		slsi_bt_err_reset();

	/* Enable BT Controller */
	ret = slsi_bt_controller_start(SCSC_SERVICE_ID_BT);
	if (ret != 0) {
		BT_ERR("open failed to start controller: %d\n", ret);
		goto out;
	}

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	if (lr_wpan_drv.open_status == false)
#endif
	{
		/* Configure Transporters */
		ret = slsi_bt_transport_change(trs);
		if (ret) {
			BT_ERR("open failed to init transports: %d\n", ret);
			slsi_bt_transport_deinit();
			slsi_bt_controller_stop(SCSC_SERVICE_ID_BT);
			goto out;
		}

		slsi_bt_log_set_transport((void*)bt_trans.htr);
	}

	if (top_htr)
		*top_htr = bt_trans.htr;

	/* Init own context */
	bt_trans.recv_pkt_count = 0;
	bt_trans.send_data_count = 0;
	bt_trans.recv_data_count = 0;
	bt_drv.open_status = true;

	BT_INFO("success\n");
out:
	mutex_unlock(&bt_start_mutex);
	wake_unlock(&bt_drv.wake_lock);
	atomic_dec(&bt_drv.driver_status);
	return ret;
}

int slsi_bt_release(void)
{
	BT_INFO("enter\n");
	atomic_inc(&bt_drv.driver_status);

	wake_lock(&bt_drv.wake_lock);
	mutex_lock(&bt_start_mutex);
	BT_DBG("pending recv_q=%d, en_trs=%u\n",
		skb_queue_len(&bt_trans.recv_q), bt_trans.enabled_trs);

	slsi_bt_log_set_transport(NULL);

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	if (lr_wpan_drv.open_status == false)
#endif
		/* step1. stop the data flow to avoid receiving any data */
		slsi_bt_transport_deinit();

	slsi_bt_controller_stop(SCSC_SERVICE_ID_BT);

	/* step2. clear the remain data */
	skb_queue_purge(&bt_trans.recv_q);
	mutex_destroy(&bt_trans.recv_q_lock);
	wake_up(&bt_trans.read_wait);

	bt_drv.open_status = false;
	mutex_unlock(&bt_start_mutex);
	wake_unlock(&bt_drv.wake_lock);
	if (wake_lock_active(&bt_drv.read_wake_lock))
		wake_unlock(&bt_drv.read_wake_lock);
	atomic_dec(&bt_drv.driver_status);
	BT_INFO("done.\n");
	return 0;
}


static long slsi_bt_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case SBTIOCT_CHANGE_TRS:
		/*
		if (bt_trans.send_data_count || bt_trans.recv_data_count) {
			BT_ERR("SBTIOCT_CHANGE_TRS should be set before any "
				"data transmission");
			return -EINVAL;
		}
		*/
		ret = slsi_bt_transport_change((unsigned int)arg);
		if (ret) {
			BT_ERR("failed to change transports: %d\n", ret);
			slsi_bt_transport_deinit();
			slsi_bt_err(SLSI_BT_ERR_TR_INIT_FAIL);
			return ret;
		}
		return ret;
	}

#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
	if (bt_trans.enabled_trs & SLSI_BT_TR_EN_TTY)
		return slsi_bt_tty_ioctl(file, cmd, arg);
#endif
	return ret;
}

static int slsi_bt_h4_file_open(struct inode *inode, struct file *file)
{
	int users, ret = 0;

	if (bt_drv.open_status) {
		BT_INFO("failed. open_status=%u\n", bt_drv.open_status);
		return -EBUSY;
	}
	BT_INFO("\n");

	/*
	 * Only 1 user allowed,
	 * but this driver allows the multi users when it open same time such
	 * as using pipe and redirection operator.
	 * TODO: This is support multi-users by suing the slow opening
	 * operation. It needs to be improved.
	 */
	mutex_lock(&bt_file_open_mutex);
	atomic_inc(&bt_drv.file_io_users);
	users = atomic_read(&bt_drv.file_io_users);
	if (users > 1) {
		BT_INFO("multiple users: %d\n", users);
		mutex_unlock(&bt_file_open_mutex);
		return ret;
	}

#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
	ret = slsi_bt_open(SLSI_BT_TR_ENABLE, NULL);
#elif IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	ret = hci_slsi_open_by_io();
#else
	ret = slsi_bt_open(SLSI_BT_TR_TEST, NULL);
#endif

	if (ret)
		atomic_dec(&bt_drv.file_io_users);
	mutex_unlock(&bt_file_open_mutex);
	return ret;
}

static int slsi_bt_h4_file_release(struct inode *inode, struct file *file)
{
	BT_INFO("\n");

	if (!atomic_dec_and_test(&bt_drv.file_io_users)) {
		BT_INFO("user remained. skip close\n");
		return 0;
	}

#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	return hci_slsi_close_by_io();
#endif
	return slsi_bt_release();
}

static const struct file_operations slsi_bt_fops = {
	.owner            = THIS_MODULE,
	.open             = slsi_bt_h4_file_open,
	.release          = slsi_bt_h4_file_release,
	.poll             = slsi_bt_poll,
	.read             = slsi_bt_read,
	.write            = slsi_bt_write,
	.unlocked_ioctl   = slsi_bt_ioctl,
};

static int slsi_bt_service_proc_show(struct seq_file *m, void *v)
{
	struct hci_trans *tr = bt_trans.htr;
	int i = 0;

	seq_puts(m, "Driver statistics:\n");
	seq_printf(m, "  open status            = %d\n", bt_drv.open_status);
	seq_printf(m, "  file io users          = %d\n",
		atomic_read(&bt_drv.file_io_users));
	for (tr = bt_trans.htr; tr != NULL; tr = hci_trans_get_next(tr), i++)
		seq_printf(m, "  Transporter Stack(%d)   = %s\n", i,
			hci_trans_get_name(tr));

	if (bt_trans.htr->name) {
		seq_printf(m, "\n  %s:\n", hci_trans_get_name(bt_trans.htr));
		seq_printf(m, "    Sent data count        = %u\n",
			bt_trans.send_data_count);
		seq_printf(m, "    Received data count    = %u\n",
			bt_trans.recv_data_count);
		seq_printf(m, "    Read data count        = %u\n",
			bt_trans.recv_data_count);
		seq_printf(m, "    Received packet count  = %u\n",
			bt_trans.recv_pkt_count);
		seq_printf(m, "    Read packet count      = %u\n",
			bt_trans.read_pkt_count);
		seq_printf(m, "    Remeaind packet count  = %u\n",
			skb_queue_len(&bt_trans.recv_q));
	}

	/* Loop transporter's proc_show */
	for (tr = bt_trans.htr; tr != NULL; tr = hci_trans_get_next(tr))
		if (tr->proc_show)
			tr->proc_show(tr, m);

	slsi_bt_err_proc_show(m, v);

	return 0;
}

static int slsi_bt_service_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, slsi_bt_service_proc_show, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops scsc_bt_procfs_fops = {
	.proc_open    = slsi_bt_service_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};
#else
static struct file_operations scsc_bt_procfs_fops = {
	.owner    = THIS_MODULE,
	.open     = slsi_bt_service_proc_open,
	.read     = seq_read,
	.llseek   = seq_lseek,
	.release  = single_release,
};
#endif

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
static int slsi_lr_wpan_open(struct inode *inode, struct file *file)
{
	int lr_wpan_users;
	int ret = 0;

	BT_INFO("(open_status=%u)\n", lr_wpan_drv.open_status ? 1 : 0);

	wake_lock(&lr_wpan_drv.wake_lock);
	mutex_lock(&bt_start_mutex);

	/* Need support for multiple users as that is needed for
	   using pipe and redirection */
	atomic_inc(&lr_wpan_drv.users);
	lr_wpan_users = atomic_read(&lr_wpan_drv.users);
	if (lr_wpan_users > 1) {
		BT_DBG("success as multiple user: %d\n", lr_wpan_users);
		goto out;
	}

	if (lr_wpan_drv.open_status)
		goto out;

	lr_wpan_drv.open_status = true;
	mutex_init(&bt_trans.lr_wpan_recv_q_lock);
	skb_queue_head_init(&bt_trans.lr_wpan_recv_q);

	if (bt_drv.open_status == false)
		slsi_bt_err_reset();

	/* Enable BT Controller */
	ret = slsi_bt_controller_start(SCSC_SERVICE_ID_LR_WPAN);
	if (ret != 0) {
		BT_ERR("open failed to start controller: %d\n", ret);
		goto out;
	}

	if (bt_drv.open_status == false) {
		/* Configure Transporters */
		ret = slsi_bt_transport_change(SLSI_BT_TR_ENABLE);
		if (ret) {
			BT_ERR("open failed to init transports: %d\n", ret);
			slsi_bt_transport_deinit();
			slsi_bt_controller_stop(SCSC_SERVICE_ID_LR_WPAN);
			goto out;
		}

		slsi_bt_log_set_transport((void*)bt_trans.htr);
	}

#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	ret = hci_slsi_open_by_lr_wpan(bt_trans.htr);
	if (ret) {
		BT_ERR("open failed to open lr_wpan: %d\n", ret);
		slsi_bt_transport_deinit();
		slsi_bt_controller_stop(SCSC_SERVICE_ID_LR_WPAN);
		goto out;
	}
#endif

out:
	if (ret) {
		lr_wpan_drv.open_status = false;
		atomic_dec(&lr_wpan_drv.users);
	}
	mutex_unlock(&bt_start_mutex);
	wake_unlock(&lr_wpan_drv.wake_lock);
	return ret;
}

static int slsi_lr_wpan_release(struct inode *inode, struct file *file)
{
	BT_INFO("(open_status=%u) users=%d\n", lr_wpan_drv.open_status ? 1 : 0,
		atomic_read(&lr_wpan_drv.users));

	if (!atomic_dec_and_test(&lr_wpan_drv.users))
		goto out;

	wake_lock(&lr_wpan_drv.wake_lock);
	mutex_lock(&bt_start_mutex);
	BT_DBG("pending recv_q=%d, en_trs=%u\n",
		skb_queue_len(&bt_trans.lr_wpan_recv_q), bt_trans.enabled_trs);

#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	hci_slsi_close_by_lr_wpan();
#endif

	if (bt_drv.open_status == false)
		/* step1. stop the data flow to avoid receiving any data */
		slsi_bt_transport_deinit();

	slsi_bt_controller_stop(SCSC_SERVICE_ID_LR_WPAN);

	/* step2. clear the remain data */
	skb_queue_purge(&bt_trans.lr_wpan_recv_q);
	mutex_destroy(&bt_trans.lr_wpan_recv_q_lock);
	wake_up(&bt_trans.lr_wpan_read_wait);

	lr_wpan_drv.open_status = false;
	mutex_unlock(&bt_start_mutex);
	wake_unlock(&lr_wpan_drv.wake_lock);
	if (wake_lock_active(&lr_wpan_drv.read_wake_lock))
		wake_unlock(&lr_wpan_drv.read_wake_lock);

out:
	BT_INFO("done.\n");

	return 0;
}

static unsigned int slsi_lr_wpan_poll(struct file *file, poll_table *wait)
{
	unsigned int ret = 0;

	if (!lr_wpan_drv.open_status)
		return POLLERR;

	if (slsi_bt_in_recovery_progress())
		return POLLERR;

	poll_wait(file, &bt_trans.lr_wpan_read_wait, wait);

	if (!skb_queue_empty(&bt_trans.lr_wpan_recv_q))
		ret |= POLLIN | POLLRDNORM;

	/* There is nothing at the moment that prevents a write */
	ret |= POLLOUT | POLLWRNORM;

	return ret;
}

static bool slsi_lr_wpan_rx_data_available(void)
{
	if (slsi_bt_err_status() != 0)
		return true;

	return !skb_queue_empty(&bt_trans.lr_wpan_recv_q);
}

static ssize_t slsi_lr_wpan_wait_for_data(void)
{
	ssize_t ret = wait_event_interruptible(bt_trans.lr_wpan_read_wait,
					       slsi_lr_wpan_rx_data_available());

	if (ret < 0)
		return ret;

	return slsi_bt_err_status() == 0 ? 0 : -EIO;
}

static ssize_t slsi_lr_wpan_read(struct file *file, char __user *buf, size_t nr,
				 loff_t *loff)
{
	struct sk_buff *skb;
	size_t len, offset = 0;

	if (!lr_wpan_drv.open_status)
		return -EIO;

	if (buf == NULL)
		return -EINVAL;

	if (!slsi_lr_wpan_rx_data_available()) {
		ssize_t ret;

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		TR_DBG("Waiting for data...\n");
		ret = slsi_lr_wpan_wait_for_data();

		if (ret && skb_queue_empty(&bt_trans.lr_wpan_recv_q))
			return ret;
	}

	if (slsi_bt_err_status() && skb_queue_empty(&bt_trans.lr_wpan_recv_q))
		return -EIO;

	while (nr > 0 && (skb = skb_dequeue(&bt_trans.lr_wpan_recv_q)) != NULL) {
		mutex_lock(&bt_trans.lr_wpan_recv_q_lock);

		len = min(nr, (size_t)skb->len);
		TR_DBG("confirmed length: %zu\n", len);
		if (copy_to_user(buf + offset, skb->data, len) != 0) {
			TR_DBG("copy_to_user returned error\n");
			skb_queue_head(&bt_trans.lr_wpan_recv_q, skb);
			mutex_unlock(&bt_trans.lr_wpan_recv_q_lock);
			return -EACCES;
		}

		if (skb->len > len) {
			skb_pull(skb, len);
			SET_HCI_PKT_TR_TYPE(skb, HCI_TRANS_ON_READING);
			skb_queue_head(&bt_trans.lr_wpan_recv_q, skb);
			TR_DBG("%u byte(s) are left in the packet\n", skb->len);
		} else /* Complete read one packet */
			kfree_skb(skb);

		nr -= len;
		offset += len;

		mutex_unlock(&bt_trans.lr_wpan_recv_q_lock);
	}

	if (skb_queue_len(&bt_trans.lr_wpan_recv_q) == 0 &&
		wake_lock_active(&lr_wpan_drv.read_wake_lock)) {
		wake_unlock(&lr_wpan_drv.read_wake_lock);
	}

	TR_DBG("%zu bytes read complete. remaining message(s)=%d\n", offset,
	       skb_queue_len(&bt_trans.lr_wpan_recv_q));
	return offset;
}

static ssize_t slsi_lr_wpan_write(struct file *file, const char __user *data,
				  size_t count, loff_t *offset)
{
	struct hci_trans *htr = bt_trans.lr_wpan_htr;
	ssize_t ret = 0;
	size_t len = count;

	if (!lr_wpan_drv.open_status)
		return -EIO;

	if (slsi_bt_err_status())
		return -EIO;

	if (htr == NULL || htr->send == NULL)
		return -EFAULT;

	wake_lock(&lr_wpan_drv.wake_lock);
	while (ret >= 0 && len) {
		ret = htr->send(htr, data, len, 0);
		if (ret < 0) {
			TR_DBG("send failed to %s\n", hci_trans_get_name(htr));
			slsi_bt_err(SLSI_BT_ERR_SEND_FAIL);
			break;
		}
		len -= ret;
	}
	wake_unlock(&lr_wpan_drv.wake_lock);

	return count - len;
}

static const struct file_operations slsi_lr_wpan_fops = {
	.owner            = THIS_MODULE,
	.open             = slsi_lr_wpan_open,
	.release          = slsi_lr_wpan_release,
	.poll             = slsi_lr_wpan_poll,
	.read             = slsi_lr_wpan_read,
	.write            = slsi_lr_wpan_write
};

static int slsi_bt_module_init_lr_wpan(void)
{
	int ret;

	memset(&lr_wpan_drv, 0, sizeof(lr_wpan_drv));

	/* register character device */
	ret = alloc_chrdev_region(&lr_wpan_drv.dev, 0, SLSI_BT_MINORS,
				  SLSI_LR_WPAN_DEV_NAME);
	if (ret) {
		BT_ERR("error alloc_chrdev_region %d\n", ret);
		return ret;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	lr_wpan_drv.class = class_create(SLSI_LR_WPAN_DEV_NAME);
#else
	lr_wpan_drv.class = class_create(THIS_MODULE, SLSI_LR_WPAN_DEV_NAME);
#endif
	if (IS_ERR(lr_wpan_drv.class)) {
		ret = PTR_ERR(lr_wpan_drv.class);
		goto error_class;
	}

	cdev_init(&lr_wpan_drv.cdev, &slsi_lr_wpan_fops);
	ret = cdev_add(&lr_wpan_drv.cdev, MKDEV(MAJOR(lr_wpan_drv.dev),
		       MINOR(SLSI_BT_MINOR_CTRL)), 1);
	if (ret) {
		BT_ERR("cdev_add failed for (%s)\n", SLSI_LR_WPAN_DEV_NAME);
		goto error_cdev_add;
	}

	lr_wpan_drv.device = device_create(lr_wpan_drv.class, NULL,
					   lr_wpan_drv.cdev.dev,
					   NULL, SLSI_LR_WPAN_DEV_NAME);
	if (IS_ERR(lr_wpan_drv.device)) {
		cdev_del(&lr_wpan_drv.cdev);
		ret = PTR_ERR(lr_wpan_drv.device);
		goto error_device_create;
	}

	wake_lock_init(NULL, &lr_wpan_drv.wake_lock.ws, "lr_wpan_wake_lock");
	wake_lock_init(NULL, &lr_wpan_drv.read_wake_lock.ws,
		       "lr_wpan_read_wake_lock");

	BT_INFO("success. cdev=%p class=%p\n", lr_wpan_drv.device,
		lr_wpan_drv.class);
	return 0;

error_device_create:
	cdev_del(&lr_wpan_drv.cdev);
	lr_wpan_drv.cdev.dev = 0;

error_cdev_add:
	class_destroy(lr_wpan_drv.class);

error_class:
	unregister_chrdev_region(lr_wpan_drv.dev, SLSI_BT_MINORS);

	BT_ERR("error create lr_wpan device\n");
	return ret;
}

static void slsi_bt_module_exit_lr_wpan(void)
{
	wake_lock_destroy(&lr_wpan_drv.wake_lock);
	wake_lock_destroy(&lr_wpan_drv.read_wake_lock);

	device_destroy(lr_wpan_drv.class, lr_wpan_drv.cdev.dev);
	cdev_del(&lr_wpan_drv.cdev);
	class_destroy(lr_wpan_drv.class);
	unregister_chrdev_region(lr_wpan_drv.dev, SLSI_BT_MINORS);
}
#endif // CONFIG_SLSI_BT_LR_WPAN

static int __init slsi_bt_module_init(void)
{
	int ret;

	BT_INFO("%s %s (C) %s\n", SLSI_BT_MODDESC, SLSI_BT_MODVERSION,
		SLSI_BT_MODAUTH);

#if defined(CONFIG_SEC_FACTORY_INTERPOSER)
	BT_ERR("No wlbt device\n");
	return 0;
#endif

	memset(&bt_drv, 0, sizeof(bt_drv));
	atomic_set(&bt_drv.file_io_users, 0);

	/* register character device */
	ret = alloc_chrdev_region(&bt_drv.dev, 0, SLSI_BT_MINORS,
				  SLSI_BT_DEV_NAME);
	if (ret) {
		BT_ERR("error alloc_chrdev_region %d\n", ret);
		return ret;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	bt_drv.class = class_create(SLSI_BT_DEV_NAME);
#else
	bt_drv.class = class_create(THIS_MODULE, SLSI_BT_DEV_NAME);
#endif
	if (IS_ERR(bt_drv.class)) {
		ret = PTR_ERR(bt_drv.class);
		goto error;
	}

	cdev_init(&bt_drv.cdev, &slsi_bt_fops);
	ret = cdev_add(&bt_drv.cdev, MKDEV(MAJOR(bt_drv.dev),
			MINOR(SLSI_BT_MINOR_CTRL)), 1);
	if (ret) {
		BT_ERR("cdev_add failed for (%s)\n", SLSI_BT_DEV_NAME);
		bt_drv.cdev.dev = 0;
		goto error;
	}

	bt_drv.device = device_create(bt_drv.class, NULL, bt_drv.cdev.dev,
				      NULL, SLSI_BT_DEV_NAME);
	if (bt_drv.device == NULL) {
		cdev_del(&bt_drv.cdev);
		ret = -EFAULT;
		goto error;
	}

	/* register proc */
	bt_drv.procfs_dir = proc_mkdir(SLSI_BT_PROC_DIR, NULL);
	if (NULL != bt_drv.procfs_dir) {
		proc_create_data("stats", S_IRUSR | S_IRGRP, bt_drv.procfs_dir,
				 &scsc_bt_procfs_fops, NULL);
	}

	wake_lock_init(NULL, &bt_drv.wake_lock.ws, "bt_drv_wake_lock");
	wake_lock_init(NULL, &bt_drv.read_wake_lock.ws, "bt_read_wake_lock");

	/* bt controller init */
	ret = slsi_bt_controller_init();
	if (ret) {
		BT_ERR("error bt_ctrl-init %d\n", ret);
		goto error;
	}

#if IS_ENABLED(CONFIG_SLSI_BT_BCSP)
	hci_bcsp_init();
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
	/* tty init */
	ret = slsi_bt_tty_init();
	if (ret) {
		BT_ERR("error slsi_bt_tty_init %d\n", ret);
		slsi_bt_controller_exit();
		goto error;
	}
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
       /* hci uart init */
	ret = hci_slsi_init();
	if (ret) {
		BT_ERR("error hci_slsi_init %d\n", ret);
		slsi_bt_controller_exit();
		goto error;
	}
#endif
	slsi_bt_err_init(slsi_bt_error_handle);

	BT_INFO("success. cdev=%p class=%p\n", bt_drv.device, bt_drv.class);

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	if (slsi_bt_module_init_lr_wpan() != 0)
		BT_ERR("LR_WPAN failed");
#endif

	return 0;

error:
	BT_ERR("error class_create bt device\n");
	unregister_chrdev_region(bt_drv.dev, SLSI_BT_MINORS);
	bt_drv.dev = 0u;

	return ret;
}

static void __exit slsi_bt_module_exit(void)
{
	BT_INFO("enter\n");

#if defined(CONFIG_SEC_FACTORY_INTERPOSER)
	BT_ERR("No wlbt device\n");
	return;
#endif

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	slsi_bt_module_exit_lr_wpan();
#endif

	slsi_bt_err_deinit();
	slsi_bt_log_set_transport(NULL);
#if IS_ENABLED(CONFIG_SLSI_BT_USE_UART_INTERFACE)
	slsi_bt_tty_exit();
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_USE_HCI_INTERFACE)
	hci_slsi_deinit();
#endif
#if IS_ENABLED(CONFIG_SLSI_BT_BCSP)
	hci_bcsp_deinit();
#endif
	slsi_bt_controller_exit();
	wake_lock_destroy(&bt_drv.wake_lock);
	wake_lock_destroy(&bt_drv.read_wake_lock);

	remove_proc_entry("stats", bt_drv.procfs_dir);
	remove_proc_entry(SLSI_BT_PROC_DIR, NULL);

	if (bt_drv.device) {
		device_destroy(bt_drv.class, bt_drv.cdev.dev);
		bt_drv.device = NULL;
	}

	cdev_del(&bt_drv.cdev);
	class_destroy(bt_drv.class);
	unregister_chrdev_region(bt_drv.dev, SLSI_BT_MINORS);
	BT_INFO("exit, module unloaded\n");
}

module_init(slsi_bt_module_init);
module_exit(slsi_bt_module_exit);

MODULE_DESCRIPTION(SLSI_BT_MODDESC);
MODULE_AUTHOR(SLSI_BT_MODAUTH);
MODULE_LICENSE("GPL");
MODULE_VERSION(SLSI_BT_MODVERSION);
