#include "../slsi_bt_controller.h"

#ifndef UNUSED
#define UNUSED(x)       ((void)(x))
#endif

static unsigned int fake_mx;
int slsi_bt_controller_init(void)
{
	scsc_mx_module_register_client_module(NULL);
	return 0;
}

void slsi_bt_controller_exit(void)
{
	scsc_mx_module_unregister_client_module(NULL);
}

int slsi_bt_controller_start(enum scsc_service_id id)
{
	UNUSED(id);
	return 0;
}

int slsi_bt_controller_stop(enum scsc_service_id id)
{
	UNUSED(id);
	return 0;
}

static int mock_hci_trans_send_skb(struct hci_trans *htr, struct sk_buff *skb)
{
	int ret = 0;

	UNUSED(htr);
	if (skb) {
		ret = skb->len;
		kfree_skb(skb);
	}
	return ret;
}

int slsi_bt_controller_transport_configure(struct hci_trans *htr)
{
	if (htr) {
		htr->send_skb = mock_hci_trans_send_skb;
	}
	return 0;
}

void *slsi_bt_controller_get_mx(void)
{
	return &fake_mx;
}

void *slsi_bt_controller_get_service(void)
{
	return &fake_mx;
}

void *slsi_bt_controller_get_qos(void)
{
	return NULL;
}

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
struct slsi_bt_qos *slsi_bt_controller_get_lr_wpan_qos(void)
{
	return NULL;
}
#endif

void slsi_bt_controller_update_fw_log_filter(unsigned long long en[2])
{
	UNUSED(en);
}

size_t slsi_bt_controller_get_syserror_info(unsigned char *buf, size_t bsize)
{
	UNUSED(buf);
	UNUSED(bsize);
	return 0;
}

int test_slsi_bt_controller_init(struct kunit *test)
{
	BT_INFO("slsi_bt_controller init\n");
	slsi_bt_controller_init();

	return 0;
}

void test_slsi_bt_controller_exit(struct kunit *test)
{
	BT_INFO("slsi_bt_controller exit\n");
	slsi_bt_controller_exit();
}
