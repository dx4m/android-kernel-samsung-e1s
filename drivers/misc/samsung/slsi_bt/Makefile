# Needed since this subdir is symlinked in the main Kernel tree
# without this our samsung subdir is NOT cleaned.
clean-files := *.o *.ko

#
# SLSI Bluetooth driver
ifeq ($(CONFIG_SLSI_BT_KUNIT_TEST),y)
obj-$(CONFIG_SLSI_BT_KUNIT_TEST) += ./kunit/
else

obj-$(CONFIG_SLSI_BT) += scsc_bt.o

scsc_bt-y += slsi_bt_io.o
scsc_bt-y += \
	slsi_bt_err.o\
	slsi_bt_controller.o\
	hci_pkt.o\
	hci_trans.o\
	slsi_bt_property.o\
	slsi_bt_log.o\

scsc_bt-$(CONFIG_SCSC_QOS) += slsi_bt_qos.o

scsc_bt-$(CONFIG_SLSI_BT_H4) += hci_h4.o
ifneq ($(CONFIG_SLSI_BT_USE_UART_INTERFACE), )
ccflags-y += -DCONFIG_SLSI_BT_USE_UART_INTERFACE
scsc_bt-$(CONFIG_SLSI_BT_USE_UART_INTERFACE) += slsi_bt_tty.o
endif
scsc_bt-$(CONFIG_SLSI_BT_BCSP) += hci_bcsp.o
ifneq ($(CONFIG_SLSI_BT_USE_HCI_INTERFACE), )
ccflags-y += -DCONFIG_SLSI_BT_USE_HCI_INTERFACE
scsc_bt-$(CONFIG_SLSI_BT_USE_HCI_INTERFACE) += hci_slsi.o
endif

ifneq ($(CONFIG_SLSI_BT_LR_WPAN), )
scsc_bt-$(CONFIG_SLSI_BT_LR_WPAN) += hci_lr_wpan.o
endif
ifneq ($(CONFIG_SLSI_BT_FASTPATH), )
scsc_bt-$(CONFIG_SLSI_BT_FASTPATH) += slsi_bt_fastpath.o
endif

ifeq ($(CONFIG_SLSI_BT_USE_MIF_INTERFACE), m)
ccflags-y += -DCONFIG_SLSI_BT_USE_MIF_INTERFACE
endif
ifneq ($(filter $(CONFIG_SLSI_BT_H4), y m),)
ccflags-y += -DCONFIG_SLSI_BT_H4
endif
ifneq ($(filter $(CONFIG_SLSI_BT_BCSP), y m),)
ccflags-y += -DCONFIG_SLSI_BT_BCSP
endif
ifneq ($(filter $(CONFIG_SLSI_BT_FASTPATH), y m),)
ccflags-y += -DCONFIG_SLSI_BT_FASTPATH
endif
ifneq ($(filter $(CONFIG_SLSI_BT_FWLOG_SNOOP), y m),)
ccflags-y += -DCONFIG_SLSI_BT_FWLOG_SNOOP
endif
ifneq ($(filter $(CONFIG_SLSI_BT_LR_WPAN), y m),)
ccflags-y += -DCONFIG_SLSI_BT_LR_WPAN
endif

ifneq ($(filter $(CONFIG_SCSC_LOG_COLLECTION), y m),)
ccflags-y += -DCONFIG_SCSC_LOG_COLLECTION
endif

ifeq ($(CONFIG_SCSC_WLBTD), y)
ccflags-y += -DCONFIG_SCSC_WLBTD
endif

ifeq ($(CONFIG_SCSC_PCIE_PAEAN_X86), y)
ccflags-y += -DCONFIG_SCSC_PCIE_PAEAN_X86
endif

ifneq ($(filter $(CONFIG_SLSI_BT_TTY_DEVICE_NAME), ),)
ccflags-y += -DCONFIG_SLSI_BT_TTY_DEVICE_NAME=\"$(CONFIG_SLSI_BT_TTY_DEVICE_NAME)\"
endif

## See sibling scsc/ Makefile for an explanation of the reasons of
## the following ifeq/else
ifneq ($(filter $(CONFIG_SCSC_INDEPENDENT_SUBSYSTEM), y m),)
ccflags-y += -DCONFIG_SCSC_INDEPENDENT_SUBSYSTEM
endif

ifeq ($(CONFIG_SCSC_LOGRING), m)
ccflags-y += -DCONFIG_SCSC_PRINTK
else
ccflags-$(CONFIG_SCSC_LOGRING) += -DCONFIG_SCSC_PRINTK
endif

ifneq ($(CONFIG_ARCH_EXYNOS), )
ccflags-y += -DCONFIG_EXYNOS_UNIQUE_ID
endif
ifneq ($(CONFIG_ARCH_EXYNOS9), )
ccflags-y += -DCONFIG_EXYNOS_UNIQUE_ID
endif
endif
