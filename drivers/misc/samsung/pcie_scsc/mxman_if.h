/****************************************************************************
 *
 * Copyright (c) 2014 - 2019 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/
#ifndef _MAXWELL_IF_H
#define _MAXWELL_IF_H
#include <pcie_scsc/scsc_mx.h>

struct mxman;

enum mxman_state {
	MXMAN_STATE_STOPPED = 0,
	MXMAN_STATE_STARTING,
	MXMAN_STATE_STARTED_WPAN,
	MXMAN_STATE_STARTED_WLAN,
	MXMAN_STATE_STARTED_WLAN_WPAN,
	MXMAN_STATE_FAILED_PMU,
	MXMAN_STATE_FAILED_WLAN,
	MXMAN_STATE_FAILED_WPAN,
	MXMAN_STATE_FAILED_WLAN_WPAN,
	MXMAN_STATE_FROZEN,
};

/*
 * Mxman interface
 */
void mxman_if_control_suspend_gpio(struct mxman *mxman, u8 value);

/* Open subsystem. Returns 0 if success otherwise fail */
int mxman_if_open(struct mxman *mxman, enum scsc_subsystem sub, void *data, size_t data_sz);
/* Close subsystem */
void mxman_if_close(struct mxman *mxman, enum scsc_subsystem sub);

/* Get current mxman_state */
int mxman_if_get_state(struct mxman *mxman);
/* Returns the local_clock of the last panic time. Returns 0 if panic time
 * hasn't been recorded  */
u64 mxman_if_get_last_panic_time(struct mxman *mxman);
/* Returns the last panic signaled to the host. Returns 0 if panic code
 * hasn't been recorded*/
u32 mxman_if_get_panic_code(struct mxman *mxman);
/* Returns the last panic code signaled to the host. Returns 0 if
 * panic code lenght hasn't been recorded*/
u32 mxman_if_get_panic_code(struct mxman *mxman);
/* Returns the last panic record length signaled to the host. Returns 0 if
 * panic recoed lenght hasn't been recorded*/
u16 mxman_if_get_last_panic_rec_sz(struct mxman *mxman);
/* Returns the pointer to last panic record signaled to the host. Returns NULL */
u32 *mxman_if_get_last_panic_rec(struct mxman *mxman);
/* Returns the last subsystem that caused an error*/
u16 mxman_if_get_last_syserr_subsys(struct mxman *mxman);
/* Prints out the last panic record */
void mxman_if_show_last_panic(struct mxman *mxman);

/* Set the time of the last recovery time */
void mxman_if_set_last_syserr_recovery_time(struct mxman *mxman, enum scsc_subsystem sub, unsigned long value);

/* Set recovery is in progress */
void mxman_if_set_syserr_recovery_in_progress(struct mxman *mxman, bool value);
/* Returns recovery is in progress */
bool mxman_if_get_syserr_recovery_in_progress(struct mxman *mxman);

/* Send a force_panic to WLBT cores */
int mxman_if_force_panic(struct mxman *mxman, enum scsc_subsystem sub);
#if IS_ENABLED(CONFIG_SCSC_BB_REDWOOD)
int mxman_if_force_panic_active(void);
#endif
/* Send a force_panic to WLBT cores */
void mxman_if_fail(struct mxman *mxman, u16 scsc_panic_code, const char *reason, enum scsc_subsystem id);

/* Check if subsystem is in failed state */
bool mxman_if_subsys_in_failed_state(struct mxman *mxman, enum scsc_subsystem sub);

/* Send message to Lerna end point */
int mxman_if_lerna_send(struct mxman *mxman, void *data, u32 size);

/* Reinit recovery_completion */
void mxman_if_reinit_completion(struct mxman *mxman);
/* Wait for recovery_completion */
int mxman_if_wait_for_completion_timeout(struct mxman *mxman, u32 ms);

/* Check if subsystem is active */
bool mxman_if_subsys_active(struct mxman *mxman, enum scsc_subsystem sub);

/* Check if any users are active */
bool mxman_if_users_active(struct mxman *mxman);

#if defined(CONFIG_WLBT_SPLIT_RECOVERY)
/* Complete indep_err_work_comletion */
void mxman_if_indep_recovery_err_completion(struct mxman *mxman);

/* Check if addtional fw panic occured during independent subsystem recovery */
int mxman_if_check_start_failure_during_indep_recovery(struct mxman *mxman);

/* Check if independent subsystem recovery is in progress */
bool mxman_if_warm_reset_in_progress(void);

/* Check if any types of recovery is not in progress */
bool mxman_if_check_recovery_state_none(void);

/* 	Notify Success or failure of receiving START command
	to the mxman thread for independent subsystem recovery.*/
void mxman_if_notify_result_to_start_cmd(struct mxman *mxman, bool success);
#endif

#if defined(CONFIG_HDM_WLBT_SUPPORT)
/* hdm_wlan/bt_support for test */
bool mxman_if_check_sysfs_for_hdm(enum scsc_subsystem sub);
#endif
#endif
