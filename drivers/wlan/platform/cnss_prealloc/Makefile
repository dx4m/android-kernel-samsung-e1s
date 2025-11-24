# SPDX-License-Identifier: GPL-2.0-only

ccflags-y += -Wmissing-prototypes

ifeq ($(CONFIG_SOC_S5E9945), y) # for root platfrom
KERNEL_BASE_DIR = $(findstring bazel, $(CURDIR))
ifeq ($(KERNEL_BASE_DIR),)
    export WLAN_PLATFORM_ROOT = $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))/..
endif
endif

ifeq ($(CONFIG_CNSS_OUT_OF_TREE),y)
ccflags-y += -I$(WLAN_PLATFORM_ROOT)/inc
ccflags-y += -I$(WLAN_PLATFORM_ROOT)/cnss_utils
endif

obj-$(CONFIG_WCNSS_MEM_PRE_ALLOC) += cnss_prealloc.o
