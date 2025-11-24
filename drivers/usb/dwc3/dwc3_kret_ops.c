// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/sched.h>
#include "core.h"
#include "gadget.h"
#include "dwc3-exynos.h"
#include "exynos_usb_tpmon.h"
#include "exynos-otg.h"

#include <linux/platform_device.h>
#include "../host/xhci.h"
#include "../../../sound/usb/exynos_usb_audio.h"

extern struct dwc3_exynos *g_dwc3_exynos;
#if IS_ENABLED(CONFIG_SND_EXYNOS_USB_AUDIO_MODULE)
extern struct hcd_hw_info *g_hwinfo;
#endif

struct kprobe_data {
	void *x0;
	void *x1;
	void *x2;
};

static int entry_dwc3_gadget_ep_queue(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	// pr_info("%s+++\n", __func__);

#if IS_ENABLED(CONFIG_USB_EXYNOS_TPMON_MODULE)
	// mainline code - func. format
	// gadget.c:1995:static int dwc3_gadget_ep_queue(struct usb_ep *ep,	=> regs[0]
	// 				struct usb_request *request,		=> regs[1]
	//				gfp_t gfp_flags)	 		=> regs[2]
	struct usb_request *request = (struct usb_request *)regs->regs[1];
	struct dwc3_request *req = to_dwc3_request(request);
	int *dummy_data = NULL;


	// pr_info("[TP] check TP for u1/u2 onoff at %s function (w/ kretprobe)\n", __func__);

	// func. format: void usb_tpmon_check_tp(void *data, struct dwc3_request *req)
	usb_tpmon_check_tp(dummy_data, req);
#endif

	return 0;
}

static int exit_dwc3_gadget_ep_queue(struct kretprobe_instance *ri,
				   struct pt_regs *regs)
{
	// pr_info("%s---\n", __func__);

	return 0;
}


static int entry_storage_probe(struct kretprobe_instance *ri,
					struct pt_regs *regs)
{
	pr_info("%s +++\n", __func__);
	g_dwc3_exynos->is_perf = 1;
	queue_work(g_dwc3_exynos->cpufreq_wq, &g_dwc3_exynos->cpufreq_work);

	return 0;
}

static int entry_usb_stor_disconnect(struct kretprobe_instance *ri,
					struct pt_regs *regs)
{
	pr_info("%s +++\n", __func__);
	g_dwc3_exynos->is_perf = 0;
	queue_work(g_dwc3_exynos->cpufreq_wq, &g_dwc3_exynos->cpufreq_work);

	return 0;
}

#define ENTRY_EXIT(name) {\
	.handler = exit_##name,\
	.entry_handler = entry_##name,\
	.data_size = sizeof(struct kprobe_data),\
	.maxactive = 8,\
	.kp.symbol_name = #name,\
}

#define ENTRY(name) {\
	.entry_handler = entry_##name,\
	.data_size = sizeof(struct kprobe_data),\
	.maxactive = 8,\
	.kp.symbol_name = #name,\
}

static struct kretprobe dwc3_kret_probes[] = {
	ENTRY_EXIT(dwc3_gadget_ep_queue),
	ENTRY(storage_probe),
	ENTRY(usb_stor_disconnect),
};

int dwc3_kretprobe_init(void)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(dwc3_kret_probes); i++) {
		ret = register_kretprobe(&dwc3_kret_probes[i]);
		if (ret < 0) {
			pr_err("register_kretprobe failed, returned %d\n", ret);
			return ret;
		}
	}

	return 0;
}

void dwc3_kretprobe_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dwc3_kret_probes); i++)
		unregister_kretprobe(&dwc3_kret_probes[i]);
}

MODULE_SOFTDEP("pre:dwc3-exynos-usb");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 EXYNOS Glue Layer function handler");
