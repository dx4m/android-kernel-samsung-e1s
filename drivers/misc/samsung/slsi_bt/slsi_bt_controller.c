/******************************************************************************
 *                                                                            *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * S.LSI Bluetooth Service Control Driver                                     *
 *                                                                            *
 * This driver is tightly coupled with scsc maxwell driver and BT controller's*
 * data structure.                                                            *
 *                                                                            *
 ******************************************************************************/
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/random.h>
#ifndef CONFIG_SCSC_PCIE_PAEAN_X86
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>

#include <soc/samsung/exynos-cpupm.h>
#endif

#if IS_ENABLED(CONFIG_SCSC_PCIE_PAEAN_X86)
#define exynos_update_ip_idle_status(a, b)
#endif

#if defined(CONFIG_ARCH_EXYNOS) || defined(CONFIG_ARCH_EXYNOS9)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
#include <soc/samsung/exynos/exynos-soc.h>
#else
#include <linux/soc/samsung/exynos-soc.h>
#endif
#endif

#include "scsc_mx_module.h"
#include IN_SCSC_INC(scsc_wakelock.h)
#include IN_SCSC_INC(api/bhcd.h)
#if IS_ENABLED(CONFIG_SCSC_LOG_COLLECTION)
#include IN_SCSC_INC(scsc_log_collector.h)
#endif
#include IN_SCSC_SRC(scsc_mx_impl.h)
#if IS_ENABLED(CONFIG_SCSC_WLBTD)
#include IN_SCSC_SRC(scsc_wlbtd.h)
#endif

#include "slsi_bt_io.h"
#include "slsi_bt_err.h"
#include "slsi_bt_controller.h"
#include "hci_pkt.h"
#include "hci_trans.h"
#include "slsi_bt_log.h"
#include "slsi_bt_qos.h"

#include <linux/completion.h>
/******************************************************************************
 * Internal macro & definition
 ******************************************************************************/
#define SLSI_BT_SERVICE_CLOSE_RETRY 40

#define BSMHCD_ALIGNMENT                        (32)

#ifndef CONFIG_SCSC_PCIE_PAEAN_X86
#define INVALID_GPIO                            (-1)
#endif
/******************************************************************************
 * Static variables
 ******************************************************************************/
/* The context for controlling SCSC Maxwell Bluetooth Service */
static struct {
	struct mutex                   lock;

	struct device                  *mxdevice;
	struct scsc_mx                 *mx;

	struct scsc_wake_lock          wake_lock;       /* common & write */
	struct scsc_wake_lock          recv_wake_lock;

	struct mx_syserr_decode        last_error;

	struct {
		unsigned int           en0_l;
		unsigned int           en0_h;
		unsigned int           en1_l;
		unsigned int           en1_h;
	} firm_log;

#ifndef CONFIG_SCSC_PCIE_PAEAN_X86
	int                            wake_src;
	int                            wake_irq;
	bool                           is_platform_driver;
#endif

	/* worker for start/stop mx service */
	struct workqueue_struct        *wq;

	struct hci_trans               *htr;

	int                            idle_ip;

	char                           *loaded_hcf;
} common_srv;

static struct wpan_service {
	enum scsc_service_id           service_id;
	char                           service_name[15];
	struct scsc_service            *service;
	scsc_mifram_ref                bhcd_ref;	/* Host Control Data */
	struct work_struct             work_start, work_stop;

	bool                           is_running;

	struct slsi_bt_qos             *qos;

	enum {
		MSG_SERVICE_START,
		MSG_SERVICE_STOP,
	};
	atomic_t msg;

	enum {
		STATE_STOP,
		STATE_STARTING,
		STATE_RUNNING,
		STATE_STOPPING,
	} state;

	struct completion              state_changed;
} bt_srv;

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
static struct wpan_service lr_wpan_srv;
#endif

/******************************************************************************
 * Module parameters
 ******************************************************************************/
static int service_start_count;
module_param(service_start_count, int, S_IRUGO);
MODULE_PARM_DESC(service_start_count,
		"Track how many times the BT service has been started");

static bool disable_service;
module_param(disable_service, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(disable_service, "Disables service startup");

static int firmware_control;
module_param(firmware_control, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(firmware_control, "Control how the firmware behaves");

static int firmware_control_reset;
module_param(firmware_control_reset, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(firmware_control_reset,
		 "Controls the resetting of the firmware_control variable");

#if IS_ENABLED(CONFIG_ARCH_EXYNOS)
static char bluetooth_address_fallback[] = "00:00:00:00:00:00";
module_param_string(bluetooth_address_fallback, bluetooth_address_fallback,
		    sizeof(bluetooth_address_fallback), 0444);
MODULE_PARM_DESC(bluetooth_address_fallback,
		 "Bluetooth address as proposed by the driver");
#endif

static u64 bluetooth_address;
module_param(bluetooth_address, ullong, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(bluetooth_address, "Bluetooth address");

static u64 disable_worker_for_mx;
module_param(disable_worker_for_mx, ullong, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(disable_worker_for_mx,
		 "Wait until scsc_mx_start/stop is finished.");

/******************************************************************************
 * Function implements
 ******************************************************************************/
#ifndef CONFIG_SCSC_PCIE_PAEAN_X86
irqreturn_t slsi_bt_controller_wakeup_threaded_isr(int irq, void *data)
{
	int gpio = *((int *)data);

	TR_DBG("bt signals ap wakeup: %d\n", gpio_get_value(gpio));
	if (gpio_get_value(gpio))
		wake_lock(&common_srv.recv_wake_lock);
	else
		wake_unlock(&common_srv.recv_wake_lock);
	return IRQ_HANDLED;
}

static irqreturn_t slsi_bt_controller_wakeup_pre_handler(int irq, void *data)
{
    // Simply call the threaded handler without doing anything else
    return IRQ_WAKE_THREAD;
}

static int slsi_bt_controller_register_wakeup_irq(void)
{
	int ret;

	BT_INFO("wakeup source: %d\n", common_srv.wake_src);
	if (!gpio_is_valid(common_srv.wake_src)) {
		BT_ERR("wakeup source is invalid.\n");
		return -EINVAL;
	}

	common_srv.wake_irq = gpio_to_irq(common_srv.wake_src);
	ret = devm_request_threaded_irq(common_srv.mxdevice, common_srv.wake_irq,
		slsi_bt_controller_wakeup_pre_handler,
		slsi_bt_controller_wakeup_threaded_isr,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
		"slsi-wpan", (void *)&common_srv.wake_src);
	if (ret) {
		BT_ERR("Failed to register wakeup isr: %d, irq: %d\n", ret,
			common_srv.wake_irq);
		common_srv.wake_irq = 0;
		return -EINVAL;
	}

	BT_DBG("enable irq: %d\n", common_srv.wake_irq);
	enable_irq_wake(common_srv.wake_irq);

	return ret;
}

static void slsi_bt_controller_unregister_wakeup_irq(void)
{
	if (!gpio_is_valid(common_srv.wake_src) || common_srv.wake_irq == 0)
		return;

	disable_irq_wake(common_srv.wake_irq);
	devm_free_irq(common_srv.mxdevice, common_srv.wake_irq,
			(void *)&common_srv.wake_src);
	common_srv.wake_irq = 0;

	if (wake_lock_active(&common_srv.recv_wake_lock))
		wake_unlock(&common_srv.recv_wake_lock);
}

static int slsi_bt_controller_suspend(struct device *dev)
{
	int is_pd = common_srv.is_platform_driver;
	int valid = is_pd ? gpio_is_valid(common_srv.wake_src) : 0;
	int value = valid ? gpio_get_value(common_srv.wake_src) : 0;

	BT_INFO("is pd=%d, is valid=%d, wake_irq=%d, value=%d\n",
			is_pd, valid, common_srv.wake_irq, value);

	if (common_srv.wake_irq && is_pd && valid && value) {
		BT_INFO("Refuse suspend. wpan has sending data\n");
		return -EBUSY;
	}
	return 0;
}
#endif

size_t slsi_bt_controller_get_syserror_info(unsigned char *buf, size_t bsize)
{
	size_t len = min(sizeof(common_srv.last_error), bsize);

	memcpy(buf, (void *)&common_srv.last_error, len);
	return len;
}

static u8 bt_mx_failure_notification(struct scsc_service_client *client,
					struct mx_syserr_decode *err)
{
	BT_INFO("Error level %d\n", err->level);
	return err->level;
}

static bool bt_mx_stop_on_failure(struct scsc_service_client *client,
				struct mx_syserr_decode *err)
{
	BT_INFO("Error level %d\n", err->level);
	common_srv.last_error = *err;
	slsi_bt_err(SLSI_BT_ERR_MX_FAIL);
	return false;
}

static void bt_mx_failure_reset(struct scsc_service_client *client, u8 level,
				u16 scsc_syserr_code)
{
	BT_ERR("MX manager requests reset\n");
	slsi_bt_err(SLSI_BT_ERR_MX_RESET);
}

static int bt_mx_ap_resumed(struct scsc_service_client *client)
{
	return 0;
}

static int bt_mx_ap_suspended(struct scsc_service_client *client)
{
	return 0;
}

static struct scsc_service_client bt_service_client = {
	.failure_notification =    bt_mx_failure_notification,
	.stop_on_failure_v2 =      bt_mx_stop_on_failure,
	.failure_reset_v2 =        bt_mx_failure_reset,
	.suspend =                 bt_mx_ap_suspended,
	.resume =                  bt_mx_ap_resumed,
};

static bool is_service_running(void) {
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	return bt_srv.is_running || lr_wpan_srv.is_running;
#else
	return bt_srv.is_running;
#endif
}

static int service_stop(struct wpan_service *srv)
{
	int ret;

	slsi_bt_qos_stop(srv->qos);
	srv->qos = NULL;

	ret = scsc_mx_service_stop(srv->service);

#ifndef CONFIG_SCSC_PCIE_PAEAN_X86
	if (!is_service_running())
		slsi_bt_controller_unregister_wakeup_irq();
#endif

	if (ret == 0 || ret == -EPERM)
		return 0;

	BT_ERR("scsc_mx_service_stop failed err: %d\n", ret);
	/* Only trigger recovery if the service_stop did not fail because
	 * recovery is already in progress. bt_stop_on_failure will be called */
	if (!slsi_bt_in_recovery_progress() && ret != -EILSEQ) {
		scsc_mx_service_service_failed(srv->service,
						"service stop failed");
		BT_DBG("force service fail complete\n");
		return ret;
	}

	return 0;
}

static int service_cleanup(struct wpan_service *srv)
{
	int retry, ret = 0;

	BT_DBG("stopping thread (service=%p)\n", srv->service);
	if (srv->bhcd_ref != 0) {
		scsc_mx_service_mifram_free(srv->service, srv->bhcd_ref);
		srv->bhcd_ref = 0;
	}

	/* If service_stop fails, then let recovery
	 * do the rest of the deinit later. */
	srv->is_running = false;
	ret = service_stop(srv);
	if (ret < 0) {
		BT_DBG("service stop failed. Recovery has been triggered\n");
		return -EIO;
	}
#if IS_ENABLED(CONFIG_SCSC_LOG_COLLECTION)
	if (!is_service_running())
		bt_hcf_collect_free();
#endif
	/* Try to close.
	 * The service close call shall remain blocked until close service is
	 * successful.
	 */
	BT_DBG("closing service...\n");
	for (retry = 1; retry <= SLSI_BT_SERVICE_CLOSE_RETRY; retry++) {
		ret = scsc_mx_service_close(srv->service);
		if (ret == 0 || ret == -EPERM)
			break;

		msleep(1000);
		BT_DBG("closing service... %d attempts\n", retry + 1);
	}

	if (retry != 0 && retry <= SLSI_BT_SERVICE_CLOSE_RETRY) {
		BT_DBG("scsc_mx_service_close closed after %d attempts\n", retry);
	} else {
		BT_ERR("scsc_mx_service_close failed %d times\n", retry);
	}

	srv->service = NULL;
	BT_DBG("complete\n");
	return 0;
}

int _slsi_bt_controller_stop(struct wpan_service *srv)
{
	int ret = 0;
	BT_INFO("Controller running status %u\n", is_service_running());

	mutex_lock(&common_srv.lock);

	if (srv->service && srv->is_running)
		ret = service_cleanup(srv);
	else
		BT_DBG("%s service is not running\n", srv->service_name);

	mutex_unlock(&common_srv.lock);
	return ret;
}

void *slsi_bt_controller_get_mx(void)
{
	if (common_srv.mx)
		return (void*)common_srv.mx;
	return NULL;
}

void *slsi_bt_controller_get_service(void)
{
	if (bt_srv.service)
		return (void*)bt_srv.service;
	return NULL;
}

void *slsi_bt_controller_get_qos(void)
{
	if (bt_srv.qos)
		return (void*)bt_srv.qos;
	return NULL;
}

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
struct slsi_bt_qos *slsi_bt_controller_get_lr_wpan_qos(void)
{
	return lr_wpan_srv.qos;
}
#endif

#ifdef SLSI_BT_ADDR
static void bt_address_get_cfg(struct bhcd_bluetooth_address *address)
{
	struct firmware *firm = NULL;
	unsigned int u[SLSI_BT_ADDR_LEN];
	int ret;

	BT_DBG("loading Bluetooth address conf file: " SLSI_BT_ADDR "\n");
	ret = mx140_request_file(mx, SLSI_BT_ADDR, &firm);
	if (ret != 0) {
		BT_DBG("Bluetooth address not found\n");
		return;
	} else if (firm == NULL) {
		BT_DBG("empty Bluetooth address\n");
		return;
	} else if (firm->size == 0) {
		BT_DBG("empty Bluetooth address\n");
		mx140_release_file(mx, firm);
		return;
	}

	/* Convert the data into a native format */
	ret = sscanf(firm->data, "%02X:%02X:%02X:%02X:%02X:%02X",
			&u[0], &u[1], &u[2], &u[3], &u[4], &u[5]);
	if (ret == SLSI_BT_ADDR_LEN) {
		address->lap = (u[3] << 16) | (u[4] << 8) | u[5];
		address->uap = u[2];
		address->nap = (u[0] << 8) | u[1];
	}

	if (ret != SLSI_BT_ADDR_LEN)
		BT_WARNING("data size incorrect = %zu\n", firm->size);

	/* Relase the configuration information */
	mx140_release_file(mx, firm);
}
#endif

static void bt_address_get(struct bhcd_bluetooth_address *address)
{
#if IS_ENABLED(CONFIG_ARCH_EXYNOS)
	address->nap = (exynos_soc_info.unique_id & 0x000000FFFF00) >> 8;
	address->uap = (exynos_soc_info.unique_id & 0x0000000000FF);
	address->lap = (exynos_soc_info.unique_id & 0xFFFFFF000000) >> 24;
#endif

	if (bluetooth_address) {
		BT_INFO("using stack supplied Bluetooth address\n");
		address->nap = (bluetooth_address & 0xFFFF00000000) >> 32;
		address->uap = (bluetooth_address & 0x0000FF000000) >> 24;
		address->lap = (bluetooth_address & 0x000000FFFFFF);
	}

#ifdef SLSI_BT_ADDR
	/* Over-write address, if there is SLSI_BT_ADDR */
	bt_address_get_cfg(address);
#endif

	BT_DBG("Bluetooth address: %04X:%02X:%06X\n",
		address->nap, address->uap, address->lap);

	/* Always print Bluetooth Address in Kernel log */
	printk(KERN_INFO "Bluetooth address: %04X:%02X:%06X\n",
		address->nap, address->uap, address->lap);
}

static void bt_boot_data_set(struct bhcd_boot *bdata,
			const struct firmware *firm)
{
	struct firm_log_filter   log_filter;

	if (bdata == NULL)
		return;

	bdata->total_length_tl.tag         = BHCD_TAG_TOTAL_LENGTH;
	bdata->total_length_tl.length      = sizeof(bdata->total_length);
	bdata->total_length                = sizeof(struct bhcd_boot);

	bdata->bt_address_tl.tag        = BHCD_TAG_BLUETOOTH_ADDRESS;
	bdata->bt_address_tl.length     = sizeof(bdata->bt_address);
	bt_address_get(&bdata->bt_address);

	log_filter = slsi_bt_log_filter_get();
	bdata->bt_log_enables_tl.tag    = BHCD_TAG_BTLOG_ENABLES;
	bdata->bt_log_enables_tl.length = sizeof(bdata->bt_log_enables);
	bdata->bt_log_enables[0]        = log_filter.en0_l;
	bdata->bt_log_enables[1]        = log_filter.en0_h;
	bdata->bt_log_enables[2]        = log_filter.en1_l;
	bdata->bt_log_enables[3]        = log_filter.en1_h;

	bdata->entropy_tl.tag           = BHCD_TAG_ENTROPY;
	bdata->entropy_tl.length        = sizeof(bdata->entropy);
	get_random_bytes(bdata->entropy, sizeof(bdata->entropy));

	bdata->config_tl.tag            = BHCD_TAG_CONFIGURATION;
	if (firm != NULL) {
		bdata->config_tl.length = firm->size;
		memcpy(&bdata->config, firm->data, firm->size);

		bdata->total_length       += firm->size;
	} else {
		bdata->config_tl.length = 0;
	}
}

#if IS_ENABLED(CONFIG_SCSC_WLBTD)
#ifndef CONFIG_SLSI_BT_HTF_CONVERTED_HTF_DIR
#define CONFIG_SLSI_BT_HTF_CONVERTED_HTF_DIR    "/data/vendor/wlbt"
#endif

/* Support product name to user */
static char *hcf_device_path = CONFIG_SCSC_WLBT_CONFIG_PLATFORM;
module_param_string(htf_device_path, CONFIG_SCSC_WLBT_CONFIG_PLATFORM,
			sizeof(CONFIG_SCSC_WLBT_CONFIG_PLATFORM), S_IRUGO);

static bool enable_auto_configcmd = false;
module_param(enable_auto_configcmd, bool, S_IRUGO | S_IWUSR);

static int request_conf_by_wlbtd(const struct firmware **firm,
				 const char *filename)
{
	char filepath[512];
	struct firmware *_firm = NULL;
	int ret;

	if (enable_auto_configcmd) {
		ret = call_wlbtd("configcmd.sh");
		BT_DBG("configcmd ret: %d\n", ret);
	}

	_firm = kmalloc(sizeof(struct firmware), GFP_KERNEL);
	if (!_firm)
		return -ENOMEM;

	BT_DBG("hcf_device_path: %s\n", hcf_device_path);
	ret = scnprintf(filepath, sizeof(filepath), "%s/%s",
			  CONFIG_SLSI_BT_HTF_CONVERTED_HTF_DIR, filename);
	BT_DBG("try to find %s\n", filepath);
	if (ret == sizeof(filepath)) {
		BT_WARNING("Please check the filepath length. %d\n", ret);
	}

	ret = call_wlbtd_request_binary(filepath, &_firm->data, &_firm->size);
	if (ret || _firm->size == 0) {
		BT_DBG("failed to load from %s\n", filepath);
		if (_firm->data)
			kfree(_firm->data);
		kfree(_firm);
		return ret ? ret : -ENOENT;
	}

	BT_DBG("HCF binary is loaded: %zu\n", _firm->size);
	_firm->priv = NULL;
	*firm = _firm;
	return 0;
}
#else
inline int request_conf_by_wlbtd(const struct firmware **firm,
				 const char *filename) {
    return 1;
}
#endif

static void release_conf(struct scsc_mx *mx, const struct firmware *firm)
{
	if (firm != NULL) {
		/* Converted HCF */
		if (firm->priv == NULL) {
			SCSC_TAG_INFO(BT_COMMON, "free HCF binary\n");
			if (firm->data)
				kfree(firm->data);
			kfree(firm);
			return;
		}
		mx140_file_release_conf(mx, firm);
	}
}

static const struct firmware *load_config(struct scsc_mx *mx)
{
	const struct firmware *firm = NULL;
	int err;

	/* try to find in /data/vendor/wlbt/... */
	err = request_conf_by_wlbtd(&firm, SLSI_BT_CONF);
	if (err == 0) {
		BT_INFO("hcf is loaded from wlbtd\n");
		common_srv.loaded_hcf = "by wlbtd";
		return firm;
	}

	BT_DBG("loading configuration: %s\n", SLSI_BT_CONF);
	err = mx140_file_request_conf(mx, &firm, "bluetooth", SLSI_BT_CONF);
	if (err) {
		BT_DBG("configuration not found\n");
		firm = NULL;
	}

	return firm;
}

/* If this returns 0 be sure to kfree boot_data later */
static struct bhcd_boot *bt_get_boot_data(struct scsc_mx *mx)
{
	const struct firmware *firm = load_config(mx);
	int config_size = (firm != NULL) ? firm->size : 0;
	struct bhcd_boot *bdata;

	bdata = kmalloc(sizeof(struct bhcd_boot) + config_size, GFP_KERNEL);
	if (bdata == NULL) {
		BT_ERR("kmalloc failed\n");
		release_conf(mx, firm);
		return NULL;
	}

	bt_boot_data_set(bdata, firm);
	common_srv.loaded_hcf = "in vendor directory";
	if (firm != NULL)
		release_conf(mx, firm);

	return bdata;
}

#ifndef CONFIG_SCSC_BT
/* If this returns 0 be sure to kfree boot_data later */
int scsc_bt_get_boot_data(struct bhcd_boot **boot_data_ptr)
{
	*boot_data_ptr = bt_get_boot_data(common_srv.mx);
	return (*boot_data_ptr != NULL) ? 0 : -ENOMEM;
}
EXPORT_SYMBOL(scsc_bt_get_boot_data);
#endif

static struct scsc_service* service_open(struct wpan_service *srv,
				struct scsc_mx *mx)
{
	struct scsc_service *service;
	struct bhcd_boot *bdata;
	int err;

	bdata = bt_get_boot_data(mx);
	if (bdata == NULL) {
		BT_ERR("get boot_data failed\n");
		return NULL;
	}

	BT_DBG("service open boot data %p, %d\n", bdata, bdata->total_length);
	service = scsc_mx_service_open_boot_data(mx, srv->service_id,
						 &bt_service_client,
						 &err,
						 bdata,
						 bdata->total_length);
#if IS_ENABLED(CONFIG_SCSC_LOG_COLLECTION)
	if (!is_service_running())
		bt_hcf_collect_store(bdata->config, bdata->config_tl.length);
#endif
	kfree(bdata);
	BT_DBG("service: %p, err: %d\n", service, err);
	if (err)
		return NULL;
	return service;
}

static void bhcd_setup(struct bhcd_start *bhcd)
{
	bhcd->total_length_tl.tag = BHCD_TAG_TOTAL_LENGTH;
	bhcd->total_length_tl.length = sizeof(bhcd->total_length);
	bhcd->total_length = sizeof(struct bhcd_start);

	bhcd->protocol_tl.tag = BHCD_TAG_PROTOCOL;
	bhcd->protocol_tl.length = sizeof(bhcd->protocol);
	bhcd->protocol.offset = 0;    /* UART */
	bhcd->protocol.length = 0;
}

static int bhcd_init(struct wpan_service *srv)
{
	struct bhcd_start *bhcd;
	int err = 0;

	/* Get shared memory region for the configuration structure */
	err = scsc_mx_service_mifram_alloc(srv->service, sizeof(*bhcd),
					   &srv->bhcd_ref, BSMHCD_ALIGNMENT);
	if (err) {
		BT_WARNING("mifram alloc failed\n");
		return -EINVAL;
	}
	BT_INFO("regions (hcd_ref=0x%08x)\n", srv->bhcd_ref);

	bhcd = (struct bhcd_start *)scsc_mx_service_mif_addr_to_ptr(
					srv->service, srv->bhcd_ref);
	if (bhcd == NULL) {
		BT_ERR("couldn't map kmem to bhcd_start_ref 0x%08x\n",
			(unsigned int)srv->bhcd_ref);
		return -ENOMEM;
	}

	memset(bhcd, 0, sizeof(struct bhcd_start));
	bhcd_setup(bhcd);

	return 0;
}

static int _slsi_bt_controller_start(struct wpan_service *srv)
{
	int err = 0;

	++service_start_count;
	if (disable_service) {
		BT_WARNING("service disabled\n");
		return -EBUSY;
	}

	if (slsi_bt_in_recovery_progress()) {
		BT_WARNING("recovery in progress\n");
		return -EFAULT;
	}

	mutex_lock(&common_srv.lock);

	if (common_srv.mx == NULL) {
		BT_WARNING("service probe not arrived\n");
		mutex_unlock(&common_srv.lock);
		return -EFAULT;
	}

	if (srv->is_running) {
		BT_WARNING("%s service is already running\n", srv->service_name);
		mutex_unlock(&common_srv.lock);
		return 0;
	}

	BT_DBG("open %s service id %d opened %d times\n",
		srv->service_name, srv->service_id, service_start_count);
	srv->service = service_open(srv, common_srv.mx);
	if (!srv->service) {
		BT_WARNING("service open failed %d\n", err);
		err = -EINVAL;
		goto exit;
	}

	err = bhcd_init(srv);
	if (err < 0) {
		BT_ERR("bhcd initialize failed %d\n", err);
		goto exit;
	}

	BT_DBG("start %s service. service: %p, bhcd_ref: 0x%08x\n",
		srv->service_name, srv->service, srv->bhcd_ref);
	err = scsc_mx_service_start(srv->service, srv->bhcd_ref);
	if (err < 0) {
		BT_ERR("scsc_mx_service_start err %d\n", err);
		goto exit;
	}

	BT_DBG("%s service running\n", srv->service_name);

	srv->qos = slsi_bt_qos_start(srv->service);

#ifndef CONFIG_SCSC_PCIE_PAEAN_X86
	if (!is_service_running())
		err = slsi_bt_controller_register_wakeup_irq();
#endif
	srv->is_running = true;

exit:
	if (err && srv->service)
		service_cleanup(srv);

	mutex_unlock(&common_srv.lock);

	BT_DBG("done. ret=%d\n", err);
	return err;
}

static void start_work(struct work_struct *work)
{
	int err = 0;
	struct wpan_service *srv = container_of(work, struct wpan_service,
				work_start);

	BT_INFO("Requested open and start bt service\n");
	if (atomic_read(&srv->msg) != MSG_SERVICE_START)
		return;

	err = _slsi_bt_controller_start(srv);
	if (err) {
		BT_WARNING("failed: %d\n", err);
		srv->state = STATE_STOP;
	} else
		srv->state = STATE_RUNNING;

	/* Complete if the latest msg has not been changed. */
	if (atomic_read(&srv->msg) == MSG_SERVICE_START)
		complete_all(&srv->state_changed);
}

static void stop_work(struct work_struct *work)
{
	int err = 0;
	struct wpan_service *srv = container_of(work, struct wpan_service,
				work_stop);

	BT_INFO("Requested stop and close bt service\n");
	if (atomic_read(&srv->msg) != MSG_SERVICE_STOP)
		return;

	err = _slsi_bt_controller_stop(srv);
	if (err)
		BT_WARNING("failed: %d\n", err);
	srv->state = STATE_STOP;

	/* Complete if the latest msg has not been changed. */
	if (atomic_read(&srv->msg) == MSG_SERVICE_STOP)
		complete_all(&srv->state_changed);
}

static inline int wait_for_complete(struct wpan_service *srv)
{
	return wait_for_completion_timeout(&srv->state_changed,
					   msecs_to_jiffies(200));
}

static int do_work(struct wpan_service *srv, int msg)
{
	int state = STATE_STOP;

	BT_DBG("work request=%d\n", msg);
	atomic_set(&srv->msg, msg);
	reinit_completion(&srv->state_changed);
	if (msg == MSG_SERVICE_START) {
		srv->state = STATE_STARTING;
		queue_work(common_srv.wq, &srv->work_start);
		state = STATE_RUNNING;
	} else if (msg == MSG_SERVICE_STOP) {
		srv->state = STATE_STOPPING;
		queue_work(common_srv.wq, &srv->work_stop);
		state = STATE_STOP;
	}

	if (wait_for_complete(srv) == 0) {
		BT_INFO("MX service will be done a later: %d\n", msg);
		return 0;
	}
	return srv->state == state ? 0 : -EFAULT;
}

int slsi_bt_controller_start(enum scsc_service_id id)
{
	struct wpan_service *srv;

	if (id == SCSC_SERVICE_ID_BT)
		srv = &bt_srv;
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	else if (id == SCSC_SERVICE_ID_LR_WPAN)
		srv = &lr_wpan_srv;
#endif
	else
		return -EFAULT;

	if (!disable_worker_for_mx)
		return do_work(srv, MSG_SERVICE_START);
	return _slsi_bt_controller_start(srv);
}

int slsi_bt_controller_stop(enum scsc_service_id id)
{
	struct wpan_service *srv;

	if (id == SCSC_SERVICE_ID_BT)
		srv = &bt_srv;
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	else if (id == SCSC_SERVICE_ID_LR_WPAN)
		srv = &lr_wpan_srv;
#endif
	else
		return -EFAULT;

	if (!disable_worker_for_mx)
		return do_work(srv, MSG_SERVICE_STOP);
	return _slsi_bt_controller_stop(srv);
}

/******************************************************************************
 * Module parameter function implements
 ******************************************************************************/
/* btlog string
 *
 * The string must be null-terminated, and may also include a single
 * newline before its terminating null. The string shall be given
 * as a hexadecimal number, but the first character may also be a
 * plus sign. The maximum number of Hexadecimal characters is 32
 * (128bits)
 */
#define BTLOG_BUF_PREFIX_LEN	        (2)  // 0x
#define BTLOG_BUF_MAX_OTHERS_LEN        (2)  // plus sign, newline
#define BTLOG_BUF_MAX_HEX_LEN           (32) // 128 bit
#define BTLOG_BUF_MAX_LEN               (BTLOG_BUF_PREFIX_LEN +\
					 BTLOG_BUF_MAX_HEX_LEN +\
					 BTLOG_BUF_MAX_OTHERS_LEN)
#define BTLOG_BUF_SPLIT_LEN             (BTLOG_BUF_MAX_HEX_LEN/2 +\
					 BTLOG_BUF_MAX_HEX_LEN)

#define SCSC_BTLOG_MAX_STRING_LEN       (37)
#define SCSC_BTLOG_BUF_LEN              (19)
#define SCSC_BTLOG_BUF_MAX_CHAR_TO_COPY (16)
#define SCSC_BTLOG_BUF_PREFIX_LEN        (2)

void slsi_bt_controller_update_fw_log_filter(unsigned long long en[2])
{
	common_srv.firm_log.en0_l = (u32) (en[0] & 0x00000000FFFFFFFF);
	common_srv.firm_log.en0_h = (u32)((en[0] & 0xFFFFFFFF00000000) >> 32);
	common_srv.firm_log.en1_l = (u32) (en[1] & 0x00000000FFFFFFFF);
	common_srv.firm_log.en1_h = (u32)((en[1] & 0xFFFFFFFF00000000) >> 32);
}

static ssize_t bt_controller_recv(struct hci_trans *htr,
			const char *data, size_t count, unsigned int flags)
{
	struct hci_trans *upper = hci_trans_get_prev(htr);
	size_t ret;

	if (!disable_worker_for_mx && bt_srv.state != STATE_RUNNING) {
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
		if (lr_wpan_srv.state != STATE_RUNNING)
#endif
		{
			BT_DBG("waiting for servce...\n");
			return count;
		}
	}

	if (upper == NULL) {
		TR_ERR("Drop %zu bytes. It does not have host.\n", count);
		return count;
	}

	ret = upper->recv(upper, data, count, flags);

	// It is unknown if the data is for BT for LR-WPAN, for this reason
	// qos for BT is used here. LR-WPAN will update qos later.
	// Need the lock as it is possible that BT is been disabled when
	// receiving LR-WPAN data in this function
	mutex_lock(&common_srv.lock);
	slsi_bt_qos_update(bt_srv.qos, (int)ret);
	mutex_unlock(&common_srv.lock);
	return ret;
}

static int bt_controller_send_skb(struct hci_trans *htr, struct sk_buff *skb)
{
	struct hci_trans *next = hci_trans_get_next(htr);

	if (!disable_worker_for_mx && bt_srv.state != STATE_RUNNING) {
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
		if (lr_wpan_srv.state != STATE_RUNNING)
#endif
		{
			BT_DBG("waiting for servce...\n");
			kfree_skb(skb);
			return -EIO;
		}
	}

	if (next) {
		int ret = 0;

		wake_lock(&common_srv.wake_lock);
		exynos_update_ip_idle_status(common_srv.idle_ip, 0);

		ret = next->send_skb(next, skb);

		exynos_update_ip_idle_status(common_srv.idle_ip, 1);
		wake_unlock(&common_srv.wake_lock);
		return ret;
	}

	kfree_skb(skb);
	return -EINVAL;
}

void slsi_bt_controller_htr_deinit(struct hci_trans *htr)
{
	htr->tdata = NULL;
	common_srv.htr = NULL;
}

int slsi_bt_controller_transport_configure(struct hci_trans *htr)
{
	if (htr) {
		htr->recv = bt_controller_recv;
		htr->deinit = slsi_bt_controller_htr_deinit;

		if (!disable_worker_for_mx)
			htr->send_skb = bt_controller_send_skb;

		common_srv.htr = htr;
		htr->tdata = &common_srv;
		return 0;
	}
	return -EINVAL;
}

/******************************************************************************
 * Driver registeration & creator
 ******************************************************************************/
#ifndef CONFIG_SCSC_PCIE_PAEAN_X86
static int slsi_bt_controller_pm_notifier(struct notifier_block *notifier,
                unsigned long pm_event, void *v)
{
	if (pm_event == PM_SUSPEND_PREPARE) {
		BT_INFO("slsi_bt suspend\n");
		hci_trans_suspend(common_srv.htr);
	} else if (pm_event == PM_POST_SUSPEND) {
		BT_INFO("slsi_bt resume\n");
		hci_trans_resume(common_srv.htr);
	}

	return NOTIFY_OK;
}

static struct notifier_block slsi_bt_controller_nb = {
	.notifier_call = slsi_bt_controller_pm_notifier,
};

static int slsi_bt_controller_platform_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int gpio;

	BT_INFO("platform driver probe\n");
	common_srv.wake_src = INVALID_GPIO;

	gpio = of_get_named_gpio(np, "bt2ap-wakeup-gpio", 0);
	if (gpio_is_valid(gpio)) {
		BT_DBG("bt uses wakeup source %d\n", gpio);
		common_srv.wake_src = gpio;
	}

	common_srv.idle_ip = exynos_get_idle_ip_index(dev_name(&pdev->dev), 1);

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void slsi_bt_controller_platform_remove(struct platform_device *pdev)
{
	common_srv.wake_src = INVALID_GPIO;
}
#else
static int slsi_bt_controller_platform_remove(struct platform_device
		*pdev)
{
	common_srv.wake_src = INVALID_GPIO;
	return 0;
}
#endif

static const struct dev_pm_ops platform_bt_controller_pm_ops = {
	.suspend	= slsi_bt_controller_suspend,
};

static const struct of_device_id slsi_bt_controller[] = {
	{ .compatible = "samsung,slsi-wpan" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, slsi_bt_controller);

static struct platform_driver platform_bt_controller_driver = {
	.probe  = slsi_bt_controller_platform_probe,
	.remove = slsi_bt_controller_platform_remove,
	.driver = {
		.name = "slsi-wpan",
		.owner = THIS_MODULE,
		.pm = &platform_bt_controller_pm_ops,
		.of_match_table = of_match_ptr(slsi_bt_controller),
	},
};
#endif
static void slsi_bt_controller_probe(struct scsc_mx_module_client *client,
		struct scsc_mx *mx, enum scsc_module_client_reason reason)
{
	BT_INFO("BT driver probe (%s %p) reason: %d\n", client->name, mx,
		reason);

	if (reason == SCSC_MODULE_CLIENT_REASON_HW_PROBE ||
	    reason == SCSC_MODULE_CLIENT_REASON_RECOVERY ||
	    reason == SCSC_MODULE_CLIENT_REASON_RECOVERY_WPAN) {

		mutex_lock(&common_srv.lock);
		common_srv.mxdevice = scsc_mx_get_device(mx);
		get_device(common_srv.mxdevice);
		common_srv.mx = mx;
		mutex_unlock(&common_srv.lock);
	}
}

static void slsi_bt_controller_remove(struct scsc_mx_module_client *client,
		struct scsc_mx *mx, enum scsc_module_client_reason reason)
{
	BT_INFO("BT controller remove (%s %p) reason: %d \n", client->name, mx,
		reason);

	if (reason == SCSC_MODULE_CLIENT_REASON_HW_REMOVE ||
	    reason == SCSC_MODULE_CLIENT_REASON_RECOVERY ||
	    reason == SCSC_MODULE_CLIENT_REASON_RECOVERY_WPAN) {

		mutex_lock(&common_srv.lock);
		put_device(common_srv.mxdevice);
		common_srv.mx = NULL;
		mutex_unlock(&common_srv.lock);
		BT_INFO("BT controller remove complete (%s %p)\n",
			client->name, mx);
	}
}

static struct scsc_mx_module_client bt_module_client = {
	.name = "BT driver",
	.probe = slsi_bt_controller_probe,
	.remove = slsi_bt_controller_remove,
};

int slsi_bt_controller_init(void)
{
	int ret;

	memset(&common_srv, 0, sizeof(common_srv));
	mutex_init(&common_srv.lock);
	ret = scsc_mx_module_register_client_module(&bt_module_client);
	if (ret) {
		BT_ERR("failed to retister scsc module: %d\n", ret);
		mutex_destroy(&common_srv.lock);
		return ret;
	}

	memset(&bt_srv, 0, sizeof(bt_srv));
	bt_srv.service_id = SCSC_SERVICE_ID_BT;
	strscpy(bt_srv.service_name, "Bluetooth", sizeof(bt_srv.service_name));

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	memset(&lr_wpan_srv, 0, sizeof(lr_wpan_srv));
	lr_wpan_srv.service_id = SCSC_SERVICE_ID_LR_WPAN;
	strscpy(lr_wpan_srv.service_name, "LR-WPAN",
			sizeof(lr_wpan_srv.service_name));
#endif
#ifndef CONFIG_SCSC_PCIE_PAEAN_X86
	BT_INFO("Register platform driver\n");
	ret = platform_driver_register(&platform_bt_controller_driver);
	if (ret)
		BT_WARNING("failed to retister platform driver: %d\n", ret);
	common_srv.is_platform_driver = (ret == 0);

	register_pm_notifier(&slsi_bt_controller_nb);
#endif

#if IS_ENABLED(CONFIG_ARCH_EXYNOS)
	sprintf(bluetooth_address_fallback,
		"%02llX:%02llX:%02llX:%02llX:%02llX:%02llX",
		(exynos_soc_info.unique_id & 0x000000FF0000) >> 16,
		(exynos_soc_info.unique_id & 0x00000000FF00) >> 8,
		(exynos_soc_info.unique_id & 0x0000000000FF) >> 0,
		(exynos_soc_info.unique_id & 0xFF0000000000) >> 40,
		(exynos_soc_info.unique_id & 0x00FF00000000) >> 32,
		(exynos_soc_info.unique_id & 0x0000FF000000) >> 24);
#endif

	slsi_bt_qos_service_init();

	common_srv.wq = create_singlethread_workqueue("common_srv_workqueu");
	if (common_srv.wq == NULL) {
		BT_ERR("Fail to create workqueue\n");
		slsi_bt_controller_exit();
		return -ENOMEM;
	}

	INIT_WORK(&bt_srv.work_start, start_work);
	INIT_WORK(&bt_srv.work_stop, stop_work);
	init_completion(&bt_srv.state_changed);

#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	INIT_WORK(&lr_wpan_srv.work_start, start_work);
	INIT_WORK(&lr_wpan_srv.work_stop, stop_work);
	init_completion(&lr_wpan_srv.state_changed);
#endif

	wake_lock_init(NULL, &common_srv.wake_lock.ws, "bt_ctrl_wake_lock");
	wake_lock_init(NULL, &common_srv.recv_wake_lock.ws, "bt_ctrl_recv_w_lock");

	BT_INFO("Done.\n");
	return 0;
}

void slsi_bt_controller_exit(void)
{
	if (common_srv.wq == NULL)
		return;

	wake_lock_destroy(&common_srv.wake_lock);
	wake_lock_destroy(&common_srv.recv_wake_lock);

	complete_all(&bt_srv.state_changed);
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	complete_all(&lr_wpan_srv.state_changed);
#endif

	flush_workqueue(common_srv.wq);
	destroy_workqueue(common_srv.wq);
	cancel_work_sync(&bt_srv.work_start);
	cancel_work_sync(&bt_srv.work_stop);
#if IS_ENABLED(CONFIG_SLSI_BT_LR_WPAN)
	cancel_work_sync(&lr_wpan_srv.work_start);
	cancel_work_sync(&lr_wpan_srv.work_stop);
#endif

	slsi_bt_qos_service_exit();
#ifndef CONFIG_SCSC_PCIE_PAEAN_X86
	unregister_pm_notifier(&slsi_bt_controller_nb);
	if (common_srv.is_platform_driver)
		platform_driver_unregister(&platform_bt_controller_driver);
#endif
	scsc_mx_module_unregister_client_module(&bt_module_client);
	common_srv.wq = NULL;

	mutex_destroy(&common_srv.lock);
}
