/******************************************************************************
 *                                                                            *
 * Copyright (c) 2024 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * S.LSI Bluetooth HCI driver                                            *
 *                                                                            *
 ******************************************************************************/
#ifndef __HCI_SLSI_H_
#define __HCI_SLSI_H_
#include "hci_trans.h"

int hci_slsi_init(void);
int hci_slsi_deinit(void);

void hci_slsi_resume(struct hci_trans *htr);
void hci_slsi_block(struct hci_trans *htr);

int hci_slsi_open_by_io(void);
int hci_slsi_close_by_io(void);

int hci_slsi_open_by_lr_wpan(struct hci_trans *htr);
int hci_slsi_close_by_lr_wpan(void);

#endif /* __HCI_SLSI_BT_H_ */
