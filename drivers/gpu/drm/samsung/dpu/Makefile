# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.
#

ifdef CONFIG_SOC_S5E9945
cal-dir := cal_rt
endif

ifdef CONFIG_SOC_S5E9935
cal-dir := cal_qd
endif

ifdef CONFIG_SOC_S5E9925
cal-dir := cal_pm
endif

ifdef CONFIG_SOC_S5E8825
cal-dir := cal_py
endif

ifdef CONFIG_SOC_S5E8535
cal-dir := cal_rc
endif

ifdef CONFIG_SOC_S5E8835
cal-dir := cal_qt
endif

ifdef CONFIG_SOC_S5E8845
cal-dir := cal_rs
endif

ifdef CONFIG_DRM_MCD_HDR
hdr-dir := mcd_cal
else
hdr-dir := $(dpu-dir)
endif

ifndef CONFIG_EXYNOS_DPU_USE_DUAL_DRV
subdir-ccflags-y			:= -I$(srctree)/$(src)			\
					   -I$(srctree)/$(src)/include		\
					   -I$(srctree)/$(src)/cal_common	\
					   -I$(srctree)/$(src)/displayport	\
					   -I$(srctree)/$(src)/$(cal-dir)

subdir-ccflags-$(CONFIG_DRM_MCD_COMMON)	+= -I$(srctree)/$(src)/mcd_cal

exynos-drm-y := exynos_drm_drv.o  exynos_drm_fb.o exynos_drm_format.o		\
		exynos_drm_crtc.o exynos_drm_plane.o exynos_drm_encoder.o 	\
		exynos_drm_connector.o exynos_drm_debug.o exynos_drm_gem.o	\
		exynos_drm_tui.o exynos_drm_partial.o				\
		exynos_drm_recovery.o exynos_drm_freq_hop.o exynos_drm_dsc.o	\
		exynos_drm_hibernation.o exynos_drm_sfr_dma.o			\
		exynos_drm_bridge.o

exynos-drm-$(CONFIG_DRM_MCD_COMMON) += mcd_cal/mcd_drm_dsim.o mcd_cal/mcd_drm_helper.o

exynos-drm-$(CONFIG_DRM_FBDEV_EMULATION) += exynos_drm_fbdev.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_DECON)	+= exynos_drm_decon.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_DPP)	+= exynos_drm_dpp.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_DSI)	+= exynos_drm_dsim.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_WB)	+= exynos_drm_writeback.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_DP)	+= displayport/exynos_drm_dp.o		\
					   displayport/exynos_drm_dp_edid.o	\
					   displayport/exynos_drm_dp_hdcp13.o	\
					   displayport/exynos_drm_dp_hdcp22_if.o\
					   $(cal-dir)/dp_reg.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_DQE)	+= exynos_drm_dqe.o			\
					   $(cal-dir)/dqe_reg.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_HDR)	+= exynos_drm_hdr.o			\
					   $(cal-dir)/hdr_reg.o

exynos-drm-$(CONFIG_EXYNOS_DMA_DSIMFC)	+= exynos_drm_dsimfc.o			\
					   $(cal-dir)/dsimfc_reg.o

exynos-drm-y				+= $(cal-dir)/decon_reg.o		\
					   $(cal-dir)/dsim_reg.o		\
					   $(cal-dir)/dpp_reg.o			\

exynos-drm-$(CONFIG_EXYNOS_BTS)		+= exynos_drm_bts.o

exynos-drm-$(CONFIG_EXYNOS_GPU_PROFILER)	+= exynos_drm_profiler.o
else
dpu-dual-dir := ../dpu_dual
dpu-ccflag-path := $(srctree)/$(src)/$(dpu-dual-dir)

subdir-ccflags-y			+= -I$(dpu-ccflag-path)			\
					   -I$(dpu-ccflag-path)/include		\
					   -I$(dpu-ccflag-path)/cal_common	\
					   -I$(dpu-ccflag-path)/displayport	\
					   -I$(dpu-ccflag-path)/panel		\
					   -I$(dpu-ccflag-path)/$(cal-dir)

subdir-ccflags-$(CONFIG_DRM_MCD_COMMON)	+= -I$(dpu-ccflag-path)/mcd_cal

exynos-drm-y := $(dpu-dual-dir)/exynos_drm_drv.o		\
		$(dpu-dual-dir)/exynos_drm_fb.o		\
		$(dpu-dual-dir)/exynos_drm_format.o		\
		$(dpu-dual-dir)/exynos_drm_crtc.o		\
		$(dpu-dual-dir)/exynos_drm_plane.o		\
		$(dpu-dual-dir)/exynos_drm_encoder.o		\
		$(dpu-dual-dir)/exynos_drm_connector.o	\
		$(dpu-dual-dir)/exynos_drm_debug.o		\
		$(dpu-dual-dir)/exynos_drm_gem.o		\
		$(dpu-dual-dir)/exynos_drm_tui.o		\
		$(dpu-dual-dir)/exynos_drm_partial.o		\
		$(dpu-dual-dir)/exynos_drm_recovery.o	\
		$(dpu-dual-dir)/exynos_drm_freq_hop.o	\
		$(dpu-dual-dir)/exynos_drm_dsc.o		\
		$(dpu-dual-dir)/exynos_drm_hibernation.o	\
		$(dpu-dual-dir)/exynos_drm_sfr_dma.o		\
		$(dpu-dual-dir)/exynos_drm_bridge.o		\
		$(dpu-dual-dir)/exynos_drm_fence.o

exynos-drm-$(CONFIG_DRM_MCD_COMMON) += $(dpu-dual-dir)/mcd_cal/mcd_drm_dsim.o
exynos-drm-$(CONFIG_DRM_MCD_COMMON) += $(dpu-dual-dir)/mcd_cal/mcd_drm_helper.o

exynos-drm-$(CONFIG_DRM_FBDEV_EMULATION) += $(dpu-dual-dir)/exynos_drm_fbdev.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_DECON)	+= $(dpu-dual-dir)/exynos_drm_decon.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_DPP)	+= $(dpu-dual-dir)/exynos_drm_dpp.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_DSI)	+= $(dpu-dual-dir)/exynos_drm_dsim.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_WB)	+= $(dpu-dual-dir)/exynos_drm_writeback.o
exynos-drm-$(CONFIG_DRM_SAMSUNG_DP)	+= $(dpu-dual-dir)/displayport/exynos_drm_dp.o \
					   $(dpu-dual-dir)/displayport/exynos_drm_dp_edid.o \
					   $(dpu-dual-dir)/displayport/exynos_drm_dp_hdcp13.o	\
					   $(dpu-dual-dir)/displayport/exynos_drm_dp_hdcp22_if.o\
					   $(dpu-dual-dir)/$(cal-dir)/dp_reg.o

exynos-drm-$(CONFIG_DRM_SAMSUNG_DQE)	+= $(dpu-dual-dir)/exynos_drm_dqe.o		\
					   $(dpu-dual-dir)/$(cal-dir)/dqe_reg.o

exynos-drm-$(CONFIG_DRM_SAMSUNG_HDR)	+= $(dpu-dual-dir)/exynos_drm_hdr.o		\
					   $(dpu-dual-dir)/$(cal-dir)/hdr_reg.o

exynos-drm-$(CONFIG_EXYNOS_DMA_DSIMFC)	+= $(dpu-dual-dir)/exynos_drm_dsimfc.o	\
					   $(dpu-dual-dir)/$(cal-dir)/dsimfc_reg.o

exynos-drm-y				+= $(dpu-dual-dir)/$(cal-dir)/decon_reg.o	\
					   $(dpu-dual-dir)/$(cal-dir)/dsim_reg.o	\
					   $(dpu-dual-dir)/$(cal-dir)/dpp_reg.o	\

exynos-drm-$(CONFIG_EXYNOS_BTS)		+= $(dpu-dual-dir)/exynos_drm_bts.o

exynos-drm-$(CONFIG_EXYNOS_GPU_PROFILER)	+= $(dpu-dual-dir)/exynos_drm_profiler.o
endif

obj-$(CONFIG_DRM_SAMSUNG_DPU)		+= exynos-drm.o

ifneq ($(filter y m,$(CONFIG_DRM_PANEL_MCD_COMMON)),)
subdir-ccflags-y += -I$(srctree)/drivers/gpu/drm/samsung/panel
obj-$(CONFIG_DRM_PANEL_MCD_COMMON)	+= panel/
else
obj-$(CONFIG_DRM_PANEL_SAMSUNG_COMMON)	+= panel/
endif
obj-$(CONFIG_SEC_DISPLAYPORT_EXT_FUNCTION)	+= dp_ext_func/
obj-$(CONFIG_DRM_SAMSUNG_KUNIT_TEST) 	+= tests/
