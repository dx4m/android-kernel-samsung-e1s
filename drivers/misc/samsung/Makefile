# Needed since this subdir is symlinked in the main Kernel tree
# without this our samsung subdir is NOT cleaned.
clean-files := built-in.o

obj-$(CONFIG_GPIO_DEBUG)		+= gpio_debug/
obj-$(CONFIG_SAMSUNG_KIC)		+= kic/
ifeq ($(CONFIG_SOC_S5E9945),y)
obj-$(CONFIG_SCSC_CORE)			+= pcie_scsc/
else
obj-$(CONFIG_SCSC_CORE)                 += scsc/
endif
obj-$(CONFIG_SCSC_BT)			+= scsc_bt/
obj-$(CONFIG_SLSI_BT)			+= slsi_bt/
obj-$(CONFIG_HARDENING_TEST)	+= hardening/
