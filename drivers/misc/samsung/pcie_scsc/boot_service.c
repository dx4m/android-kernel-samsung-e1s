/****************************************************************************
 *
 * Copyright (c) 2014 - 2022 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#include <linux/module.h>

#include <linux/slab.h>
#include <linux/delay.h>
#include <pcie_scsc/scsc_logring.h>
#include <pcie_scsc/scsc_mx.h>

/* State */
#define BOOT_NOT_STARTED		0
#define BOOT_STARTED			1
#define BOOT_FINISHED_OK		2
#define BOOT_FINISHED_KO		3
#define BOOT_LAST_STATE			BOOT_FINISHED_KO

static const char *boot_states[] = {
	"BOOT_NOT_STARTED",
	"BOOT_STARTED",
	"BOOT_FINISHED_OK",
	"BOOT_FINISHED_KO"
};

struct scsc_mx_boot {
	struct scsc_service_client	boot_service_client;
	struct scsc_service		*boot_service;
	struct scsc_mx			*mx;
	bool				started;
	u8				state;
};

static struct scsc_mx_boot *boot_ins;

static void boot_service_work_start_func(struct work_struct *work);

static DECLARE_WORK(boot_service_work_start, boot_service_work_start_func);
static DEFINE_MUTEX(boot_lock);

static int scsc_boot_srv_manual_set_param_cb(const char *buffer,
					  const struct kernel_param *kp)
{
	if (!boot_ins) {
		SCSC_TAG_ERR(BOOT_SERVICE, "Boot instance not created\n");
		return -EIO;
	}


	SCSC_TAG_ERR(BOOT_SERVICE, "Manually triggering Boot service\n");
	schedule_work(&boot_service_work_start);

	return 0;
}

static int scsc_boot_srv_manual_get_param_cb(char *buffer,
					  const struct kernel_param *kp)
{
	if (!boot_ins) {
		SCSC_TAG_ERR(BOOT_SERVICE, "Boot instance not created\n");
		return -EIO;
	}

	return sprintf(buffer, "%s\n", boot_states[boot_ins->state]);
}

static struct kernel_param_ops scsc_boot_srv_manual_ops = {
	.set = scsc_boot_srv_manual_set_param_cb,
	.get = scsc_boot_srv_manual_get_param_cb,
};

/* Momentary boot of WLBT FW */
module_param_cb(boot_srv_manual, &scsc_boot_srv_manual_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(boot_srv_manual, "Trigger the WLBT boot service");

static u8 boot_service_failure_cb(struct scsc_service_client *client, struct mx_syserr_decode *err)
{
	SCSC_TAG_DEBUG(BOOT_SERVICE, "OK\n");
	return err->level;
}


static bool boot_service_stop_cb(struct scsc_service_client *client, struct mx_syserr_decode *err)
{
	SCSC_TAG_DEBUG(BOOT_SERVICE, "OK\n");
	return false;
}

static void boot_service_reset_cb(struct scsc_service_client *client, u8 level, u16 scsc_syserr_code)
{
	SCSC_TAG_ERR(BOOT_SERVICE, "OK\n");
}


static void boot_service_stop_close_services(void)
{
	int r;

	mutex_lock(&boot_lock);

	if (!boot_ins->started) {
		SCSC_TAG_ERR(BOOT_SERVICE,"Already stopped\n");
		goto done;
	}

	if (boot_ins->boot_service) {
		r = scsc_mx_service_stop(boot_ins->boot_service);
		if (r)
			SCSC_TAG_ERR(BOOT_SERVICE, "scsc_mx_service_stop(boot_service) failed err: %d\n", r);
		r = scsc_mx_service_close(boot_ins->boot_service);
		if (r)
			SCSC_TAG_ERR(BOOT_SERVICE, "scsc_mx_service_close failed err: %d\n", r);
		boot_ins->boot_service = NULL;
	}

	boot_ins->started = false;
done:
	mutex_unlock(&boot_lock);
}

static bool boot_service_open_start_services(struct scsc_mx *mx)
{
	struct scsc_service *boot_service = NULL;
	int  r;
	int ret = 0;
	scsc_mifram_ref conf_ref = (scsc_mifram_ref)0;

	mutex_lock(&boot_lock);

	if (boot_ins->started) {
		SCSC_TAG_ERR(BOOT_SERVICE,"Already started\n");
		ret = -EIO;
		goto done;
	}

	boot_service = scsc_mx_service_open(mx, SCSC_SERVICE_ID_NULL, &boot_ins->boot_service_client, &r);
	if (!boot_service) {
		SCSC_TAG_ERR(BOOT_SERVICE, "scsc_mx_service_open for boot_service failed %d\n", r);
		ret = -EIO;
		goto done;
	}

	r = scsc_mx_service_start(boot_service, conf_ref);
	if (r) {
		SCSC_TAG_ERR(BOOT_SERVICE, "scsc_mx_service_start for boot_service failed\n");
		r = scsc_mx_service_close(boot_service);
		if (r)
			SCSC_TAG_ERR(BOOT_SERVICE, "scsc_mx_service_close for boot_service %d failed\n", r);
		ret = -EIO;
		goto done;
	}

	boot_ins->boot_service = boot_service;
	boot_ins->started = true;
done:
	mutex_unlock(&boot_lock);
	return ret;
}

static void boot_service_work_start_func(struct work_struct *work)
{
	if (!boot_ins->mx)
		return;

	if (scsc_mx_service_users_active(boot_ins->mx)) {
		SCSC_TAG_ERR(BOOT_SERVICE, "Boot service can't run and update FW when other services are running. Stop both WLAN and BT services completely before triggering boot_srv_manual\n");
		boot_ins->state = BOOT_FINISHED_KO;
		return;
	}

	scsc_mx_service_lock_open(boot_ins->mx, SCSC_SERVICE_ID_NULL);
	boot_ins->state = BOOT_STARTED;

	if (boot_service_open_start_services(boot_ins->mx)) {
		SCSC_TAG_ERR(BOOT_SERVICE, "Error starting service\n");
		boot_ins->state = BOOT_FINISHED_KO;
	} else {
		SCSC_TAG_ERR(BOOT_SERVICE, "Boot service started succesfully\n");
		boot_ins->state = BOOT_FINISHED_OK;
	}

	if (boot_ins->started == false) {
		SCSC_TAG_ERR(BOOT_SERVICE, "Unable to Enable Boot service. Aborting\n");
		boot_ins->state = BOOT_FINISHED_KO;
		goto done_ko_closed;
	}

	boot_service_stop_close_services();
done_ko_closed:
	scsc_mx_service_unlock_open(boot_ins->mx, SCSC_SERVICE_ID_NULL);
	return;
}

/* Start service(s) and leave running until module unload */
void boot_module_probe(struct scsc_mx_module_client *module_client, struct scsc_mx *mx, enum scsc_module_client_reason reason)
{

	if (reason == SCSC_MODULE_CLIENT_REASON_RECOVERY_WPAN ||
	    reason == SCSC_MODULE_CLIENT_REASON_RECOVERY_WLAN) {
		SCSC_TAG_ERR(BOOT_SERVICE,"Ignore reason code %d\n", reason);
		return;
	}

	boot_ins = kzalloc(sizeof(struct scsc_mx_boot), GFP_KERNEL);
	if (!boot_ins)
		return;

	boot_ins->boot_service_client.failure_notification = boot_service_failure_cb;
	boot_ins->boot_service_client.stop_on_failure_v2   = boot_service_stop_cb;
	boot_ins->boot_service_client.failure_reset_v2     = boot_service_reset_cb;
	boot_ins->mx = mx;
	boot_ins->started = false;
	boot_ins->state = BOOT_NOT_STARTED;

	SCSC_TAG_ERR(BOOT_SERVICE, "OK\n");
}

void boot_module_remove(struct scsc_mx_module_client *module_client, struct scsc_mx *mx, enum scsc_module_client_reason reason)
{
	/* Avoid unused error */
	(void)module_client;

	SCSC_TAG_ERR(BOOT_SERVICE,"Remove\n");

	if (reason == SCSC_MODULE_CLIENT_REASON_RECOVERY_WPAN ||
	    reason == SCSC_MODULE_CLIENT_REASON_RECOVERY_WLAN) {
		SCSC_TAG_ERR(BOOT_SERVICE,"Ignore reason code %d\n", reason);
		return;
	}

	if (!boot_ins)
		return;
	if (boot_ins->mx != mx) {
		SCSC_TAG_ERR(BOOT_SERVICE, "test->mx != mx\n");
		return;
	}

	/* Cancel any delayed start attempt */
	cancel_work_sync(&boot_service_work_start);

	boot_service_stop_close_services();
	scsc_mx_service_unlock_open(boot_ins->mx, SCSC_SERVICE_ID_NULL);
	/* de-allocate boot instance */
	kfree(boot_ins);
	SCSC_TAG_DEBUG(BOOT_SERVICE, "OK\n");
}


/* Client driver registration */
static struct scsc_mx_module_client boot_service = {
	.name = "MX Boot service",
	.probe = boot_module_probe,
	.remove = boot_module_remove,
};

static int __init scsc_boot_service_module_init(void)
{
	int r;

	SCSC_TAG_DEBUG(BOOT_SERVICE, "Init\n");

#if defined(CONFIG_SEC_FACTORY_INTERPOSER)
	SCSC_TAG_INFO(BOOT_SERVICE, "No wlbt device\n");
	return 0;
#endif

	r = scsc_mx_module_register_client_module(&boot_service);
	if (r) {
		SCSC_TAG_ERR(BOOT_SERVICE, "scsc_mx_module_register_client_module failed: r=%d\n", r);
		return r;
	}

	return 0;
}

static void __exit scsc_boot_service_module_exit(void)
{
#if defined(CONFIG_SEC_FACTORY_INTERPOSER)
	SCSC_TAG_INFO(BOOT_SERVICE, "No wlbt device\n");
	return;
#endif
	scsc_mx_module_unregister_client_module(&boot_service);
}

late_initcall(scsc_boot_service_module_init);
module_exit(scsc_boot_service_module_exit);

MODULE_DESCRIPTION("Boot service");
MODULE_AUTHOR("SCSC");
MODULE_LICENSE("GPL");
