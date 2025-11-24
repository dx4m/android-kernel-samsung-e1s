# SPDX-License-Identifier: GPL-2.0

# Do not instrument the tracer itself:

obj-$(CONFIG_IPC_LOGGING) += qcom_ipc_logging.o
qcom_ipc_logging-y := ipc_logging.o  ipc_logging_debug.o
qcom_ipc_logging-$(CONFIG_IPC_LOGGING_CDEV) += ipc_logging_cdev.o
