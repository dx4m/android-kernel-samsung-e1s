clean-files := *.o *.ko

obj-$(CONFIG_SLSI_BT_KUNIT_TEST) += sample.o

# First, Do test slsi_bt_controller_unittest if you want, otherwise use mock_slsi_bt_controller
obj-$(CONFIG_SLSI_BT_KUNIT_TEST) += slsi_bt_controller_unittest.o
#obj-$(CONFIG_SLSI_BT_KUNIT_TEST) += mock_slsi_bt_controller.o

# Second, Do test slsi_bt_io_unittest if you want
obj-$(CONFIG_SLSI_BT_KUNIT_TEST) += slsi_bt_io_unittest.o

# QoS configs
ccflags-$(CONFIG_SLSI_BT_KUNIT_TEST) += -DCONFIG_SCSC_QOS
obj-$(CONFIG_SLSI_BT_KUNIT_TEST) += slsi_bt_qos_unittest.o

obj-$(CONFIG_SLSI_BT_KUNIT_TEST) += \
		hci_trans_unittest.o \
		hci_pkt_unittest.o \
		slsi_bt_log_unittest.o \
		slsi_bt_err_unittest.o \
		slsi_bt_property_unittest.o \

obj-$(CONFIG_SLSI_BT_H4) += hci_h4_unittest.o
obj-$(CONFIG_SLSI_BT_BCSP) += hci_bcsp_unittest.o
obj-$(CONFIG_SLSI_BT_USE_HCI_INTERFACE) += hci_slsi_unittest.o # need io and controller
obj-$(CONFIG_SLSI_BT_FASTPATH) += slsi_bt_fastpath_unittest.o
obj-$(CONFIG_SLSI_BT_LR_WPAN) += hci_lr_wpan_unittest.o

obj-$(CONFIG_SLSI_BT_KUNIT_TEST) += fake.o mock_scsc_service.o

## Belows are related to INDEPENDENT_SUBSYSTEM
ccflags-$(CONFIG_SLSI_BT_KUNIT_TEST) += -DCONFIG_SCSC_INDEPENDENT_SUBSYSTEM
ccflags-$(CONFIG_SLSI_BT_KUNIT_TEST) += -O1 -fipa-sra

GCOV_PROFILE := y
