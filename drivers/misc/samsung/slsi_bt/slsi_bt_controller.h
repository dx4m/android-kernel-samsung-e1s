/******************************************************************************
 *                                                                            *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * S.LSI Bluetooth Service Control Driver                                     *
 *                                                                            *
 * This driver is tightly coupled with scsc maxwell driver and BT conroller's *
 * data structure.                                                            *
 *                                                                            *
 ******************************************************************************/
#ifndef __SLSI_BT_CONTROLLER_H__
#define __SLSI_BT_CONTROLLER_H__
#include "scsc_mx_module.h"
#include IN_SCSC_INC(scsc_mx.h)

#include "hci_trans.h"

/* BT Configuration file */
#define SLSI_BT_CONF      "bt.hcf"

#if IS_ENABLED(CONFIG_SCSC_BT_ADDRESS_IN_FILE)
#define SLSI_BT_ADDR      CONFIG_SCSC_BT_ADDRESS_FILENAME
#define SLSI_BT_ADDR_LEN  (6)
#endif

/* slsi bt control APIs */
int slsi_bt_controller_init(void);
void slsi_bt_controller_exit(void);

int slsi_bt_controller_start(enum scsc_service_id id);
int slsi_bt_controller_stop(enum scsc_service_id id);

int slsi_bt_controller_transport_configure(struct hci_trans *htr);

void *slsi_bt_controller_get_mx(void);
void *slsi_bt_controller_get_service(void);
void *slsi_bt_controller_get_qos(void);
struct slsi_bt_qos *slsi_bt_controller_get_lr_wpan_qos(void);
void slsi_bt_controller_update_fw_log_filter(unsigned long long en[2]);
size_t slsi_bt_controller_get_syserror_info(unsigned char *buf, size_t bsize);

#endif /*  __SLSI_BT_CONTROLLER_H__ */
