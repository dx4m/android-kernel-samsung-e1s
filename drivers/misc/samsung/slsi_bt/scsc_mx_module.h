/******************************************************************************
 *                                                                            *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd. All rights reserved       *
 *                                                                            *
 * This is for compatibility with SCSC MX CORE driver location                *
 *                                                                            *
 ******************************************************************************/

#ifndef __SLSI_BT_SCSC_MX_MODULE_INCLUDE__
#define __SLSI_BT_SCSC_MX_MODULE_INCLUDE__
/*
 * The default value of CONFIG_SLSI_BT_SCSC_MODULE is scsc/. And it can be
 * pcie_scsc for projects that supports PCIe.
 */

#define MAKE_SCSC_SRC_PATH(path)    #path

#if IS_ENABLED(CONFIG_SLSI_BT_USE_SLSI_CORE_MODULE)
#if IS_ENABLED(CONFIG_SLSI_BT_USE_PCIE_SCSC_MODULE)
#define IN_SCSC_INC(_hdr_)      <pcie_scsc/_hdr_>
#define IN_SCSC_SRC(_hdr_)      MAKE_SCSC_SRC_PATH(../slsi_core/pcie_scsc/_hdr_)
#else
#define IN_SCSC_INC(_hdr_)      <scsc/_hdr_>
#define IN_SCSC_SRC(_hdr_)      MAKE_SCSC_SRC_PATH(../slsi_core/scsc/_hdr_)
#endif

#else /* CONFIG_SLSI_BT_USE_SLSI_CORE_MODULE */
#if IS_ENABLED(CONFIG_SLSI_BT_USE_PCIE_SCSC_MODULE)
#define IN_SCSC_INC(_hdr_)      <pcie_scsc/_hdr_>
#define IN_SCSC_SRC(_hdr_)      MAKE_SCSC_SRC_PATH(../pcie_scsc/_hdr_)
#else
#define IN_SCSC_INC(_hdr_)      <scsc/_hdr_>
#define IN_SCSC_SRC(_hdr_)      MAKE_SCSC_SRC_PATH(../scsc/_hdr_)
#endif
#endif /* CONFIG_SLSI_BT_USE_SLSI_CORE_MODULE */

#endif /* __SLSI_BT_SCSC_MX_MODULE_INCLUDE__ */
