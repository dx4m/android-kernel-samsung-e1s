/******************************************************************************
 *                                                                            *
 * Copyright (c) 2022 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * scsc_mx driver mock for bluetooth driver unit test                         *
 *                                                                            *
 ******************************************************************************/
#include "../../scsc/scsc_mx_impl.h"
#include "../../scsc/mxlog_transport.h"
#include "fake.h"

#ifndef UNUSED
#define UNUSED(x)       ((void)(x))
#endif

static unsigned int fake_mx;
static int fake_addr;

static char buffer_pool[120000];
struct fake_buffer_pool scsc_mx_fake_buffer_pool;

__weak int scsc_mx_module_register_client_module(struct scsc_mx_module_client *module_client)
{
	fake_buffer_init(&scsc_mx_fake_buffer_pool, buffer_pool, sizeof(buffer_pool));
	if (module_client)
		module_client->probe(module_client, (struct scsc_mx *)&fake_mx, 0);
	return 0;
}

__weak void scsc_mx_module_unregister_client_module(struct scsc_mx_module_client *module_client)
{
	if (module_client)
		module_client->remove(module_client, (struct scsc_mx *)&fake_mx, 0);
}

__weak int scsc_mx_service_start(struct scsc_service *service, scsc_mifram_ref ref)
{
	UNUSED(service);
	UNUSED(ref);
	return 0;
}

__weak int scsc_mx_service_stop(struct scsc_service *service)
{
	UNUSED(service);
	return 0;
}

__weak int scsc_mx_service_close(struct scsc_service *service)
{
	UNUSED(service);
	return 0;
}

__weak void scsc_mx_service_mifram_free(struct scsc_service *service, scsc_mifram_ref ref)
{
	UNUSED(service);
	fake_buffer_free((void *)(uintptr_t)ref);
}

__weak void scsc_mx_service_service_failed(struct scsc_service *service, const char *reason)
{
	UNUSED(service);
	UNUSED(reason);
}

__weak struct scsc_service *scsc_mx_service_open_boot_data(struct scsc_mx *mx, enum scsc_service_id id, struct scsc_service_client *client, int *status, void *data, size_t data_sz)
{
	UNUSED(mx);
	UNUSED(id);
	UNUSED(client);
	UNUSED(data);
	UNUSED(data_sz);
	*status = 0;
	return (struct scsc_service *)&fake_addr;
}

__weak void *scsc_mx_service_mif_addr_to_ptr(struct scsc_service *service, scsc_mifram_ref ref)
{
	UNUSED(service);
	return (void *)(uintptr_t)ref;
}

__weak void *scsc_mx_service_mif_addr_to_phys(struct scsc_service *service, scsc_mifram_ref ref)
{
	UNUSED(service);
	return (void *)(uintptr_t)ref;
}

__weak int scsc_mx_service_mif_ptr_to_addr(struct scsc_service *service, void *mem_ptr, scsc_mifram_ref *ref)
{
	UNUSED(service);
	*ref = (uintptr_t)mem_ptr;
	return 0;
}

__weak int scsc_mx_service_mifram_alloc(struct scsc_service *service, size_t nbytes, scsc_mifram_ref *ref, u32 align)
{
	UNUSED(service);
	UNUSED(nbytes);
	UNUSED(ref);
	UNUSED(align);

	*ref = (uintptr_t)fake_buffer_alloc(&scsc_mx_fake_buffer_pool, nbytes);
	return *ref ? 0 : -ENOMEM;
}

__weak struct scsc_bt_audio_abox *scsc_mx_service_get_bt_audio_abox(struct scsc_service *service)
{
	UNUSED(service);
	return NULL;
}

__weak void scsc_service_mifintrbit_bit_clear(struct scsc_service *service, int which_bit)
{
	UNUSED(service);
	UNUSED(which_bit);
}

__weak void scsc_service_mifintrbit_bit_set(struct scsc_service *service, int which_bit, enum scsc_mifintr_target dir)
{
	UNUSED(service);
	UNUSED(which_bit);
	UNUSED(dir);
}

__weak int scsc_service_mifintrbit_register_tohost(struct scsc_service *service, void (*handler)(int irq, void *data), void *data, enum scsc_mifintr_target dir, enum IRQ_TYPE irq_type)
{
	UNUSED(service);
	UNUSED(handler);
	UNUSED(data);
	UNUSED(dir);
	UNUSED(irq_type);
	return 0;
}

__weak int scsc_service_mifintrbit_unregister_tohost(struct scsc_service *service, int which_bit, enum scsc_mifintr_target dir)
{
	UNUSED(service);
	UNUSED(which_bit);
	UNUSED(dir);
	return 0;
}

__weak int scsc_service_mifintrbit_alloc_fromhost(struct scsc_service *service, enum scsc_mifintr_target dir)
{
	UNUSED(service);
	UNUSED(dir);
	return 0;
}

__weak int scsc_service_mifintrbit_free_fromhost(struct scsc_service *service, int which_bit, enum scsc_mifintr_target dir)
{
	UNUSED(service);
	UNUSED(which_bit);
	UNUSED(dir);
	return 0;
}

__weak int scsc_service_force_panic(struct scsc_service *service)
{
	UNUSED(service);
	return 0;
}

__weak int mx140_file_request_conf(struct scsc_mx *mx, const struct firmware **conf, const char *config_path, const char *filename)
{
	UNUSED(mx);
	UNUSED(conf);
	UNUSED(config_path);
	UNUSED(filename);
	return 0;
}

__weak void mx140_file_release_conf(struct scsc_mx *mx, const struct firmware *conf)
{
	UNUSED(mx);
	UNUSED(conf);
}

__weak struct device *scsc_mx_get_device(struct scsc_mx *mx)
{
	UNUSED(mx);
	return NULL;
}

__weak struct mxlog_transport *scsc_mx_get_mxlog_transport_wpan(struct scsc_mx *mx)
{
	UNUSED(mx);
	return NULL;
}

__weak bool mxman_recovery_disabled(void)
{
	return false;
}

#ifdef CONFIG_SCSC_QOS
__weak int scsc_service_pm_qos_add_request(struct scsc_service *service, enum scsc_qos_config config)
{
	UNUSED(service);
	UNUSED(config);
	return 0;
}

__weak int scsc_service_pm_qos_update_request(struct scsc_service *service, enum scsc_qos_config config)
{
	UNUSED(service);
	UNUSED(config);
	return 0;
}

__weak int scsc_service_pm_qos_remove_request(struct scsc_service *service)
{
	UNUSED(service);
	return 0;
}
#endif

__weak int mxlogger_register_global_observer_class(char *name, uint8_t class)
{
	UNUSED(name);
	UNUSED(class);
	return 0;
}

__weak int mxlogger_unregister_global_observer(char *name)
{
	UNUSED(name);
	return 0;
}

__weak void srvman_wake_up_mxlog_thread_for_fwsnoop(void *data)
{
	UNUSED(data);
}
