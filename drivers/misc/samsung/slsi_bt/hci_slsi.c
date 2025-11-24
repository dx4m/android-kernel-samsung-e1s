#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/of_device.h>
#include <linux/serdev.h>
#include <linux/mutex.h>

#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/acpi.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <net/bluetooth/hci_core.h>

#include "hci_pkt.h"
#include "slsi_bt_io.h"
#include "slsi_bt_controller.h"
#include "slsi_bt_log.h"

#include "hci_slsi.h"

static DEFINE_MUTEX(rx_lock);

struct hci_slsidev {
	struct serdev_device *serdev;
	struct hci_dev       *hdev;

	struct sk_buff_head  txq;
	struct sk_buff       *tx_skb;

	struct hci_trans     *top_htr;
	struct hci_trans     *htr;

	unsigned long        flags;

	struct work_struct   write_work;

} *hci_slsidev;

/* SLSI_DATA flag bits */
#define HCI_SLSIDEV_READY                 1
#define HCI_SLSIDEV_TX_WAKEUP             2
#define HCI_SLSIDEV_SENDING               3
#define HCI_SLSIDEV_STOP_RECV_FRAME       4
#define HCI_SLSIDEV_STOP_SEND_FRAME       5
#define HCI_SLSIDEV_FILE_IO_CHANNEL       6    /* SLSI_HUP is opened by io */
#define HCI_SLSIDEV_READY_LR_WPAN         7

static bool is_any_hci_slsidev_ready(struct hci_slsidev *hsdev)
{
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	return test_bit(HCI_SLSIDEV_READY, &hsdev->flags) ||
	       test_bit(HCI_SLSIDEV_READY_LR_WPAN, &hsdev->flags);
#else
	return test_bit(HCI_SLSIDEV_READY, &hsdev->flags);
#endif
}

static struct sk_buff *hci_slsi_dequeue(struct hci_slsidev *hsdev)
{
	struct sk_buff *skb;

	if (!hsdev || !is_any_hci_slsidev_ready(hsdev))
		return NULL;

	if (hsdev->tx_skb) {
		skb = hsdev->tx_skb;
		hsdev->tx_skb = NULL;
		TR_DBG("tx_skb\n");
		return skb;
	}

	skb = skb_dequeue(&hsdev->txq);
	if (!skb_queue_empty(&hsdev->txq))
		set_bit(HCI_SLSIDEV_TX_WAKEUP, &hsdev->flags);
	return skb;
}

static int _hci_slsi_write(struct hci_slsidev *hsdev, struct sk_buff *skb)
{
	struct hci_dev *hdev = hsdev->hdev;
	int len;

	len = serdev_device_write_buf(hsdev->serdev, skb->data, skb->len);
	hdev->stat.byte_tx += len;

	skb_pull(skb, len);
	if (skb->len) {
		hsdev->tx_skb = skb;
		return skb->len;
	}

	/* tx complete */
	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	}
	kfree_skb(skb);
	return 0;
}

static void hci_slsi_write_work(struct work_struct *work)
{
	struct hci_slsidev *hsdev = container_of(work , struct hci_slsidev,
						 write_work);
	struct sk_buff *skb;

	TR_DBG("\n");
	do {
		clear_bit(HCI_SLSIDEV_TX_WAKEUP, &hsdev->flags);
		while ((skb = hci_slsi_dequeue(hsdev)))
			_hci_slsi_write(hsdev, skb);
		clear_bit(HCI_SLSIDEV_SENDING, &hsdev->flags);
	} while (test_bit(HCI_SLSIDEV_TX_WAKEUP, &hsdev->flags));
}

static void hci_slsi_write_wakeup(struct serdev_device *serdev)
{
	struct hci_slsidev *hsdev = serdev_device_get_drvdata(serdev);

	TR_DBG("\n");
	if (!hsdev || serdev != hsdev->serdev)
		return;

	if (hsdev->tx_skb == NULL && skb_queue_empty(&hsdev->txq))
		return;

	set_bit(HCI_SLSIDEV_TX_WAKEUP, &hsdev->flags);
	if (test_and_set_bit(HCI_SLSIDEV_SENDING, &hsdev->flags))
		return;

	if (is_any_hci_slsidev_ready(hsdev))
		schedule_work(&hsdev->write_work);
}

/*
 * hci_slsi_send_skb sends tx data to the controller by serial interface. The
 * packet is passed from upper layer of htr. The upper layer may be H5.
 */
static int hci_slsi_send_skb(struct hci_trans *htr, struct sk_buff *skb)
{
	struct hci_slsidev *hsdev;
	int len;

	if (!htr || !skb)
		return -EINVAL;

	hsdev = htr->tdata;
	len = skb->len;
	TR_DBG("len: %d\n", len);

	skb_queue_tail(&hsdev->txq, skb);
	hci_slsi_write_wakeup(hsdev->serdev);

	return len;
}

/*
 * hci_slsi_recv_skb receives the final data frame through slsi_bt transports.
 * It sends the received frame to the HCI layer.
 */
int hci_slsi_recv_skb(struct hci_trans *htr, struct sk_buff *skb)
{
	struct hci_slsidev *hsdev = htr->tdata;
	struct sk_buff *nskb = NULL;

	TR_DBG("\n");

	if (!skb || !test_bit(HCI_SLSIDEV_READY, &hsdev->flags))
		return -EINVAL;

	if (test_bit(HCI_SLSIDEV_STOP_RECV_FRAME, &hsdev->flags)) {
		kfree_skb(skb);
		return 0;
	}

	nskb = bt_skb_alloc(skb->len, GFP_ATOMIC);
	if (!nskb) {
		kfree_skb(skb);
		return -ENOMEM;
	}

	TR_DBG("packet type: %u\n", GET_HCI_PKT_TYPE(skb));
	hci_skb_pkt_type(nskb) = GET_HCI_PKT_TYPE(skb);
	hci_skb_expect(nskb) = skb->len;

	skb_put_data(nskb, skb->data, skb->len);
	kfree_skb(skb);

	/* Send to HCI layer */
	return hci_recv_frame(hsdev->hdev, nskb);
}

void hci_slsi_resume(struct hci_trans *htr)
{
	struct hci_slsidev *hsdev = htr->tdata;

	if (hsdev) {
		clear_bit(HCI_SLSIDEV_STOP_RECV_FRAME, &hsdev->flags);
		clear_bit(HCI_SLSIDEV_STOP_SEND_FRAME, &hsdev->flags);
	}
}

void hci_slsi_block(struct hci_trans *htr)
{
	struct hci_slsidev *hsdev = htr->tdata;

	if (hsdev) {
		set_bit(HCI_SLSIDEV_STOP_RECV_FRAME, &hsdev->flags);
		set_bit(HCI_SLSIDEV_STOP_SEND_FRAME, &hsdev->flags);
	}
}

static void hci_slsi_set_htrs(struct hci_slsidev *hsdev, struct hci_trans *htr)
{
	if (!htr)
		return;

	hsdev->top_htr = htr;
	while (hci_trans_get_next(htr))
		htr = hci_trans_get_next(htr);

	BT_INFO("%s\n", hci_trans_get_name(htr));

	/* The bottom of hci_trans */
	htr->tdata = hsdev;
	htr->send_skb = hci_slsi_send_skb;
	hsdev->htr = htr;
}

/********************************************
 * slsi_bt interface
 ********************************************/

static int _slsi_bt_setup(struct hci_slsidev *hsdev, unsigned int enable_trs)
{
	struct hci_trans *htr;
	int err;

	if (hsdev == NULL) {
		err = -EINVAL;
		goto out;
	}


	err = slsi_bt_open(enable_trs, &htr);
	if (err) {
		BT_ERR("slsi_bt_open failed.\n");
		goto out;
	}
	hci_slsi_set_htrs(hsdev, htr);

	clear_bit(HCI_SLSIDEV_STOP_RECV_FRAME, &hsdev->flags);
	clear_bit(HCI_SLSIDEV_STOP_SEND_FRAME, &hsdev->flags);
	set_bit(HCI_SLSIDEV_READY, &hsdev->flags);

out:
	return err;
}

static int hci_slsi_setup_diag(struct hci_dev *hdev, bool enable)
{
	BT_DBG("handle first open\n");

	set_bit(HCI_QUIRK_NON_PERSISTENT_SETUP, &hdev->quirks);
	return 0;
}

static int slsi_bt_setup(struct hci_slsidev *hsdev)
{
	struct hci_dev *hdev = hsdev->hdev;
	unsigned int enable_trs;
	int ret;

	BT_INFO("\n");
	if (test_bit(HCI_SLSIDEV_FILE_IO_CHANNEL, &hsdev->flags)) {
		BT_ERR("slsi_bt is busy\n");
		return -EBUSY;
	}

	/*
	 * This file is for using the Linux Bluetooth subsystem for AIDL. The
	 * net/bluetooth performs some operations after registering a hci
	 * device.  However, we don't want these operations that are out of our
	 * control and the first driver open after booting to be performed at
	 * this stage because it cannot to download converted HCF file at the
	 * openning time. Therefore, we add this flag to ignore the first open
	 * (during Bluetooth device registeration) after device botting.
	 */
	if (!test_bit(HCI_QUIRK_NON_PERSISTENT_DIAG, &hdev->quirks)) {
		BT_DBG("set the diagnostic function.\n");

		set_bit(HCI_QUIRK_NON_PERSISTENT_DIAG, &hdev->quirks);
		hci_dev_set_flag(hdev, HCI_VENDOR_DIAG);
		hdev->set_diag = hci_slsi_setup_diag;

		BT_DBG("skip this setup.\n");
		return -EAGAIN;
	}

	if (test_bit(HCI_SLSIDEV_READY, &hsdev->flags))
		return 0;

	enable_trs = SLSI_BT_TR_ENABLE;
	ret = _slsi_bt_setup(hsdev, enable_trs);
	if (ret)
		return ret;

	/* Switch the recv_skb of top transport to hci_slsi_recv_skb */
	hsdev->top_htr->tdata = hsdev;
	hsdev->top_htr->recv_skb = hci_slsi_recv_skb;

	return 0;
}

static int slsi_bt_clear(struct hci_slsidev *hsdev)
{
	if (!hsdev || !test_bit(HCI_SLSIDEV_READY, &hsdev->flags))
		return 0;

	mutex_lock(&rx_lock);
	clear_bit(HCI_SLSIDEV_READY, &hsdev->flags);
	if (!is_any_hci_slsidev_ready(hsdev)) {
		hsdev->top_htr = NULL;
		hsdev->htr = NULL;
		skb_queue_purge(&hsdev->txq);
	}
	mutex_unlock(&rx_lock);
	return slsi_bt_release();
}

static int slsi_bt_shutdown(struct hci_slsidev *hsdev)
{
	int ret = 0;

	BT_INFO("\n");

	if (!test_bit(HCI_SLSIDEV_READY, &hsdev->flags))
		return 0;

	mutex_lock(&rx_lock);
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	if (!test_bit(HCI_SLSIDEV_READY_LR_WPAN, &hsdev->flags))
#endif
		set_bit(HCI_SLSIDEV_STOP_RECV_FRAME, &hsdev->flags);
	hsdev->hdev->hw_error = NULL;
	hsdev->hdev->cmd_timeout = NULL;
	mutex_unlock(&rx_lock);
	ret = slsi_bt_clear(hsdev);

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
static size_t slsi_bt_receive_buf(struct serdev_device *serdev, const u8 *data,
				  size_t count)
#else
static int slsi_bt_receive_buf(struct serdev_device *serdev, const u8 *data,
				size_t count)
#endif
{
	struct hci_slsidev *hsdev = serdev_device_get_drvdata(serdev);
	struct hci_trans *upper = NULL;
	size_t offset = 0;

	if (!hsdev || serdev != hsdev->serdev) {
		WARN_ON(1);
		return 0;
	}

	if (!is_any_hci_slsidev_ready(hsdev))
		return 0;

	mutex_lock(&rx_lock);
	if (test_bit(HCI_SLSIDEV_STOP_RECV_FRAME, &hsdev->flags))
		goto out;

	TR_DBG("count: %d bytes\n", count);
	upper = hci_trans_get_prev(hsdev->htr);
	if (upper != NULL && upper->recv != NULL) {
		while (count > 0) {
			/* The return value may be 0 if the input is c0 (1 byte)
			 * it indicating that the data has already been copied.
			 */
			int ret = upper->recv(upper, data + offset, count, 0);
			if (ret <= 0)
				break;

			offset += ret;
			count -= ret;
		}
		hsdev->hdev->stat.byte_rx += count;
	} else
		TR_WARNING("It does not have valid upper layer\n");

out:
	mutex_unlock(&rx_lock);
	return offset;
}

int hci_slsi_open_by_io(void)
{
	struct hci_slsidev *hsdev = hci_slsidev;
	struct hci_dev *hdev = hsdev->hdev;
	unsigned int enable_trs;
	int ret = 0;

	BT_INFO("\n");
	if (hdev == NULL)
		return -EIO;

	ret = hdev->open(hdev);
	if (ret) {
		BT_ERR("hdev open failed: %d\n", ret);
		return -EIO;
	}

	if (test_bit(HCI_SLSIDEV_READY, &hsdev->flags)) {
		BT_ERR("slsi_bt is busy\n");
		return -EBUSY;
	}

	enable_trs = SLSI_BT_TR_EN_H4 | SLSI_BT_TR_ENABLE;

	set_bit(HCI_SLSIDEV_FILE_IO_CHANNEL, &hsdev->flags);
	return _slsi_bt_setup(hsdev, enable_trs);
}

static int hci_slsi_flush(struct hci_dev *hdev);
int hci_slsi_close_by_io(void)
{
	struct hci_slsidev *hsdev = hci_slsidev;
	struct hci_dev *hdev = hsdev->hdev;

	BT_INFO("\n");
	hci_slsi_flush(hdev);
	hdev->close(hdev);
	clear_bit(HCI_SLSIDEV_FILE_IO_CHANNEL, &hsdev->flags);
	return slsi_bt_clear(hsdev);
}

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
int hci_slsi_open_by_lr_wpan(struct hci_trans *htr)
{
	int ret;
	struct hci_slsidev *hsdev = hci_slsidev;

	BT_INFO("\n");
	if (hsdev->hdev == NULL)
		return -EIO;

	set_bit(HCI_SLSIDEV_READY_LR_WPAN, &hsdev->flags);

	if (test_bit(HCI_SLSIDEV_READY, &hsdev->flags) ||
	    test_bit(HCI_SLSIDEV_FILE_IO_CHANNEL, &hsdev->flags))
		return 0;

	hci_slsi_set_htrs(hsdev, htr);
	hsdev->hdev->flush = hci_slsi_flush;

	ret = serdev_device_open(hsdev->serdev);
	if (!ret) {
		serdev_device_set_baudrate(hsdev->serdev, 4800000);
		serdev_device_set_flow_control(hsdev->serdev, true);
	}

	clear_bit(HCI_SLSIDEV_STOP_RECV_FRAME, &hsdev->flags);
	clear_bit(HCI_SLSIDEV_STOP_SEND_FRAME, &hsdev->flags);
	return ret;
}

int hci_slsi_close_by_lr_wpan(void)
{
	struct hci_slsidev *hsdev = hci_slsidev;

	BT_INFO("\n");
	clear_bit(HCI_SLSIDEV_READY_LR_WPAN, &hsdev->flags);

	if (test_bit(HCI_SLSIDEV_READY, &hsdev->flags) ||
	    test_bit(HCI_SLSIDEV_FILE_IO_CHANNEL, &hsdev->flags))
		return 0;

	serdev_device_close(hsdev->serdev);

	mutex_lock(&rx_lock);
	hsdev->top_htr = NULL;
	hsdev->htr = NULL;
	skb_queue_purge(&hsdev->txq);
	hsdev->hdev->flush = NULL;
	mutex_unlock(&rx_lock);

	return 0;
}
#endif

/********************************************
 * HCI layer interface
 ********************************************/

static int hci_slsi_flush(struct hci_dev *hdev)
{
	struct hci_slsidev *hsdev = hci_get_drvdata(hdev);

	TR_DBG("\n");
	serdev_device_write_flush(hsdev->serdev);

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	if (!test_bit(HCI_SLSIDEV_READY_LR_WPAN, &hsdev->flags))
#endif
		if (test_bit(HCI_SLSIDEV_READY, &hsdev->flags))
			skb_queue_purge(&hsdev->txq);
	return 0;
}

static int hci_slsi_open(struct hci_dev *hdev)
{
	struct hci_slsidev *hsdev = hci_get_drvdata(hdev);

	BT_DBG("%s %p\n", hdev->name, hdev);
	hdev->flush = hci_slsi_flush;

	if (is_any_hci_slsidev_ready(hsdev))
		return 0;

	return serdev_device_open(hsdev->serdev);
}

static int hci_slsi_close(struct hci_dev *hdev)
{
	struct hci_slsidev *hsdev = hci_get_drvdata(hdev);

	BT_DBG("hdev %p", hdev);
	if (test_bit(HCI_SLSIDEV_READY, &hsdev->flags))
		slsi_bt_clear(hsdev);

	if (is_any_hci_slsidev_ready(hsdev))
		return 0;

	hdev->flush = NULL;
	serdev_device_close(hsdev->serdev);
	return 0;
}

/* hci_slsi_send_frame - write frame to slsi_bt
 * slsi_bt, the top of htr, sends frame to bt through its own transport layers.
 * Finally, hci_slsi_send_skb transmitts to serdev.
 */
static int hci_slsi_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_slsidev *hsdev = hci_get_drvdata(hdev);
	struct hci_trans *htr;
	struct sk_buff *nskb;

	TR_DBG("pkt_type: %u, len: %zu\n", hci_skb_pkt_type(skb), skb->len);
	if (!test_bit(HCI_SLSIDEV_READY, &hsdev->flags))
		return -ENXIO;

	if (test_bit(HCI_SLSIDEV_STOP_SEND_FRAME, &hsdev->flags)) {
		kfree_skb(skb);
		return 0;
	}

	nskb = __alloc_hci_pkt_skb(skb->len, 0);
	if (nskb) {
		unsigned char type = hci_skb_pkt_type(skb);
		SET_HCI_PKT_TYPE(nskb, type);
		SET_HCI_PKT_TR_TYPE(nskb, HCI_TRANS_HCI);

		skb_put_data(nskb, skb->data, skb->len);
	}
	kfree_skb(skb);

	htr = hci_trans_get_next(hsdev->top_htr);
	if (!htr) {
		TR_WARNING("htr is null\n");
		kfree_skb(nskb);
		return -EIO;
	}
	clear_bit(HCI_SLSIDEV_TX_WAKEUP, &hsdev->flags);
	htr->send_skb(htr, nskb);

	return 0;
}

static int slsi_bt_hdev_shutdown(struct hci_dev *hdev)
{
	struct hci_slsidev *hsdev = hci_get_drvdata(hdev);

	return slsi_bt_shutdown(hsdev);
}

static int hci_slsi_setup(struct hci_dev *hdev)
{
	struct hci_slsidev *hsdev = hci_get_drvdata(hdev);
	int err;

	serdev_device_set_baudrate(hsdev->serdev, 4800000);
	serdev_device_set_flow_control(hsdev->serdev, true);

	err = slsi_bt_setup(hsdev);
	if (err)
		return err;

	set_bit(HCI_QUIRK_NON_PERSISTENT_SETUP, &hdev->quirks);
	hdev->shutdown = slsi_bt_hdev_shutdown;
	return 0;
}

static const struct serdev_device_ops hci_slsi_serdev_ops = {
	.receive_buf = slsi_bt_receive_buf,
	.write_wakeup = hci_slsi_write_wakeup,
};

static int hci_slsi_register_dev(struct hci_slsidev *hsdev)
{
	struct hci_dev *hdev;
	int err;

	serdev_device_set_client_ops(hsdev->serdev, &hci_slsi_serdev_ops);
	skb_queue_head_init(&hsdev->txq);

	hdev = hci_alloc_dev_priv(0);
	if (!hdev) {
		BT_ERR("Can't allocate HCI device\n");
		return -ENOMEM;
	}
	hsdev->hdev = hdev;
	hci_set_drvdata(hdev, hsdev);
	hdev->bus = HCI_UART;

	INIT_WORK(&hsdev->write_work, hci_slsi_write_work);

	hdev->open  = hci_slsi_open;
	hdev->close = hci_slsi_close;
	hdev->send  = hci_slsi_send_frame;
	hdev->setup = hci_slsi_setup;
	SET_HCIDEV_DEV(hdev, &hsdev->serdev->dev);

	err = hci_register_dev(hdev);
	if (err) {
		BT_ERR("Can't register HCI device");
		hci_free_dev(hdev);
		return -ENODEV;
	}
	return 0;
}

void hci_slsi_unregister_device(struct hci_slsidev *hsdev)
{
	BT_INFO("\n");
	hci_unregister_dev(hsdev->hdev);
	hci_free_dev(hsdev->hdev);
	hsdev->flags = 0;
}

static int slsi_serdev_probe(struct serdev_device *serdev)
{
	BT_INFO("\n");

	hci_slsidev = devm_kzalloc(&serdev->dev,
				   sizeof(struct hci_slsidev), GFP_KERNEL);
	if (!hci_slsidev)
		return -ENOMEM;

	hci_slsidev->serdev = serdev;
	serdev_device_set_drvdata(serdev, hci_slsidev);

	return hci_slsi_register_dev(hci_slsidev);
}

static void slsi_serdev_remove(struct serdev_device *serdev)
{
	hci_slsi_unregister_device(hci_slsidev);
	devm_kfree(&serdev->dev, hci_slsidev);
	hci_slsidev = NULL;
}

static const struct of_device_id slsi_bluetooth_of_match[] = {
	{ .compatible = "samsung,s6375-bt",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, slsi_bluetooth_of_match);

static struct serdev_device_driver slsi_serdev_driver = {
	.probe = slsi_serdev_probe,
	.remove = slsi_serdev_remove,
	.driver = {
		.name = "hci_slsi_bt",
		.of_match_table = slsi_bluetooth_of_match,
	},
};

int hci_slsi_init(void)
{
	BT_INFO("\n");
	return serdev_device_driver_register(&slsi_serdev_driver);
}

int hci_slsi_deinit(void)
{
	BT_INFO("deinit\n");
	serdev_device_driver_unregister(&slsi_serdev_driver);
	return 0;
}

MODULE_LICENSE("GPL");
