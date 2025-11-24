// SPDX-License-Identifier: GPL-2.0
/****************************************************************************
 *
 * Copyright (c) 2012 - 2023 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#include <linux/types.h>
#include <linux/notifier.h>
#include <soc/samsung/shm_ipc.h>
#include <dt-bindings/soc/samsung/exynos-cpif.h>

#include "debug.h"
#include "mlme.h"
#include "dctas.h"

int dctas_link_notifier(struct notifier_block *nb, unsigned long state, void *buf);

/**
 * peterson_lock - Acquire the Peterson lock.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function attempts to acquire the Peterson lock by setting Wi-Fi flag and waiting for CP to release their flags.
 * If the lock cannot be acquired within a specified timeout period, the function returns -ETIMEDOUT.
 * It also takes a wakelock to prevent the system from going into suspend mode while the lock is held.
 *
 * Returns 0 on success, or -ETIMEDOUT if the lock could not be acquired within the specified timeout period.
 */
static inline bool peterson_lock(struct slsi_dev *sdev)
{
	unsigned long start;

	slsi_wake_lock(&(sdev)->dctas.wlan_wl_dctas);

	CP_SHMEM_WRITE_U8((sdev), OFFSET_PETERSON_FLAG_WLBT, true);
	CP_SHMEM_WRITE_U8((sdev), OFFSET_PETERSON_TURN, PETERSON_TURN_CP);

	start = jiffies;
	while (CP_SHMEM_READ_U8((sdev), OFFSET_PETERSON_FLAG_CP) && CP_SHMEM_READ_U8((sdev), OFFSET_PETERSON_TURN) ==
	       PETERSON_TURN_CP) {
		if (unlikely(time_after(jiffies, start + msecs_to_jiffies(TIMEOUT_PETERSON_LOCK)))) {
			CP_SHMEM_WRITE_U8((sdev), OFFSET_PETERSON_FLAG_WLBT, false);
			slsi_wake_unlock(&(sdev)->dctas.wlan_wl_dctas);
			return -ETIMEDOUT;
		}
		cpu_relax();
	}

	return 0;
}

/**
 * peterson_unlock - Release the Peterson lock.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function releases the Peterson lock by clearing the Wi-Fi's flag.
 * It also releases the wakelock taken by peterson_lock().
 */
static inline void peterson_unlock(struct slsi_dev *sdev)
{
	CP_SHMEM_WRITE_U8((sdev), OFFSET_PETERSON_FLAG_WLBT, false);

	slsi_wake_unlock(&(sdev)->dctas.wlan_wl_dctas);
}

/**
 * register_link_notifier - Register the link status change notifier.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function registers the link status change notifier.
 */
static void register_link_notifier(struct slsi_dev *sdev)
{
	SLSI_INFO(sdev, "register link notifier\n");

	sdev->dctas.link_notifier.notifier_call = dctas_link_notifier;
	pcie_mif_register_link_status_notifier(&sdev->dctas.link_notifier);
}

/**
 * unregister_link_notifier - Unregister the link status change notifier.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function unregisters the link status change notifier.
 */
static void unregister_link_notifier(struct slsi_dev *sdev)
{
	SLSI_INFO(sdev, "un-register link notifier\n");

	pcie_mif_unregister_link_status_notifier(&sdev->dctas.link_notifier);
}

/**
 * is_dctas_update_needed - Check if DCTAS update is needed.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function checks if there is a need to update the DCTAS state based on the current and previous states.
 * Returns true if an update is needed, otherwise returns false.
 */
static bool is_dctas_update_needed(struct slsi_dev *sdev)
{
	// In phase 2 of implementation the driver should pass WLBT POLICY to fw if it has decreased, even if MIFless
	if (sdev->dctas.fw_cp_state.policy != sdev->dctas.cp_cp_state.policy) {
		if (sdev->dctas.link_state)
			goto update;
		sdev->dctas.update_deferred = true;
	}

	if (sdev->dctas.cp_cp_state.state == DCTAS_STATE_NOT_ASSIGNED &&
	    sdev->dctas.fw_cp_state.state != DCTAS_STATE_NOT_ASSIGNED) {
		if (sdev->dctas.link_state)
			goto update;
		sdev->dctas.update_deferred = true;
	}

	if (sdev->dctas.cp_cp_state.state != DCTAS_STATE_NOT_ASSIGNED &&
	    sdev->dctas.fw_cp_state.state == DCTAS_STATE_NOT_ASSIGNED)
		goto update;

	if (sdev->dctas.cp_cp_state.average != sdev->dctas.fw_cp_state.average ||
	    sdev->dctas.cp_cp_state.s_upper != sdev->dctas.fw_cp_state.s_upper) {
		if (sdev->dctas.link_state)
			goto update;
		sdev->dctas.update_deferred = true;
	}

	return false;

update:
	sdev->dctas.update_deferred = false;
	return true;
}

/**
 * set_dctas - Set the DCTAS state.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function sets the DCTAS state using the MLME-SAR-SET-DCTAS primitive and stores the current CP state
 * in the FW state.
 */
static void set_dctas(struct slsi_dev *sdev)
{
	/**
	 * The values are passed in a new primitive MLME-SAR-SET-DCTAS.req
	 * (Natural8 STATE, Natural8 STATE_DURATION, Natural8 POLICY, Natural8 AVERAGE, Natural8 S_UPPER);
	 * all values are passed even if only one of them has changed since last .req
	 * (except if only STATE_DURATION has changed).
	 */
	slsi_mlme_sar_set_dctas_req(sdev);

	// store cp_cp_state to fw_cp_state
	memcpy(&sdev->dctas.fw_cp_state, &sdev->dctas.cp_cp_state, sizeof(struct slsi_cp_state));
}

/**
 * get_cp_baseaddr - Get the base address of the CP shared memory region.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function retrieves the base address of the CP shared memory region and initializes the cp_base_addr field.
 * Returns 0 on success, otherwise returns -1.
 */
static int get_cp_baseaddr(struct slsi_dev *sdev)
{
	if (cp_shmem_get_base(SHMEM_WIFI)) {
		sdev->dctas.cp_base_addr =
			cp_shmem_get_nc_region(cp_shmem_get_base(SHMEM_WIFI), cp_shmem_get_size(SHMEM_WIFI));
		if (sdev->dctas.cp_base_addr)
			return 0;
	}
	return -1;
}

/**
 * dctas_read_cp_work - Worker function for reading CP state.
 * @work: Pointer to the work structure.
 *
 * This function reads the CP state from the shared memory region and updates the internal state accordingly.
 * It also schedules the next read operation based on the elapsed time.
 */
void dctas_read_cp_work(struct work_struct *work)
{
	struct slsi_dctas *dctas = container_of((struct delayed_work *)work, struct slsi_dctas, read_cp_work);
	struct slsi_dev *sdev = container_of(dctas, struct slsi_dev, dctas);
	unsigned long now = jiffies;
	unsigned long elapsed, next_delay;
	static struct slsi_cp_state last_cp_state = {0xff, 0xff, 0xff, 0xff};

	SLSI_MUTEX_LOCK(sdev->dctas_mutex);

	if (peterson_lock(sdev)) {
		SLSI_ERR(sdev, "peterson_lock() failed\n");
		SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
		schedule_delayed_work(&sdev->dctas.read_cp_work, msecs_to_jiffies(INTERVAL_CP_READ_MS));
		return;
	}
	memcpy_fromio(&sdev->dctas.cp_cp_state.state, sdev->dctas.cp_base_addr + OFFSET_CP_STATE,
		      sizeof(struct slsi_cp_state));
	peterson_unlock(sdev);

	if (last_cp_state.state != sdev->dctas.cp_cp_state.state)
		sdev->dctas.cp_state_duration = 0;
	else
		if (sdev->dctas.cp_state_duration < 255)
			sdev->dctas.cp_state_duration++;

	if (last_cp_state.state != sdev->dctas.cp_cp_state.state ||
	    last_cp_state.policy != sdev->dctas.cp_cp_state.policy ||
	    last_cp_state.average != sdev->dctas.cp_cp_state.average ||
	    last_cp_state.s_upper != sdev->dctas.cp_cp_state.s_upper) {
		SLSI_INFO(sdev, "state : %d, policy : %d, average : %d, s_upper : %d, duration : %d\n",
			  sdev->dctas.cp_cp_state.state, sdev->dctas.cp_cp_state.policy,
			  sdev->dctas.cp_cp_state.average, sdev->dctas.cp_cp_state.s_upper,
			  sdev->dctas.cp_state_duration);
		memcpy(&last_cp_state, &sdev->dctas.cp_cp_state, sizeof(struct slsi_cp_state));
	}

	if (is_dctas_update_needed(sdev) && sdev->wlan_service_on)
		set_dctas(sdev);

	SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);

	elapsed = jiffies - now;
	if (elapsed >= msecs_to_jiffies(INTERVAL_CP_READ_MS))
		next_delay = 1;
	else
		next_delay = msecs_to_jiffies(INTERVAL_CP_READ_MS) - elapsed;
	schedule_delayed_work(&sdev->dctas.read_cp_work, next_delay);
}

void dctas_deferred_work(struct work_struct *work)
{
	struct slsi_dctas *dctas = container_of((struct work_struct *)work, struct slsi_dctas, deferred_work);
	struct slsi_dev *sdev = container_of(dctas, struct slsi_dev, dctas);

	if (!sdev->wlan_service_on) {
		SLSI_INFO(sdev, "Postpone update to next time. wlan_service_on : %d\n", sdev->wlan_service_on);
		return;
	}

	SLSI_MUTEX_LOCK(sdev->dctas_mutex);

	set_dctas(sdev);
	sdev->dctas.update_deferred = false;

	SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
}

/**
 * dctas_mifless_work - Worker function for handling mifless operations.
 * @work: Pointer to the work structure.
 *
 * This function sets DCTAS_AVERAGE_LOW in the shared memory region if the mifless state persists for
 * a certain period of time.
 */
void dctas_mifless_work(struct work_struct *work)
{
	struct slsi_dctas *dctas = container_of((struct delayed_work *)work, struct slsi_dctas, mifless_work);
	struct slsi_dev *sdev = container_of(dctas, struct slsi_dev, dctas);

	SLSI_MUTEX_LOCK(sdev->dctas_mutex);

	if (sdev->dctas.wlbt_state.state == DCTAS_STATE_UNUSED && sdev->dctas.wlbt_state.average == DCTAS_AVERAGE_LOW) {
		SLSI_INFO(sdev, "Already state : %d, average : %d\n",
			  sdev->dctas.wlbt_state.state, sdev->dctas.wlbt_state.average);
		SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
		return;
	}
	sdev->dctas.wlbt_state.state = DCTAS_STATE_UNUSED;
	sdev->dctas.wlbt_state.average = DCTAS_AVERAGE_LOW;

	SLSI_INFO(sdev, "Set state : %d, average : %d\n", sdev->dctas.wlbt_state.state, sdev->dctas.wlbt_state.average);

	if (peterson_lock(sdev)) {
		SLSI_ERR(sdev, "peterson_lock() failed\n");
		SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
		return;
	}
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_STATE, sdev->dctas.wlbt_state.state);
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_AVERAGE, sdev->dctas.wlbt_state.average);
	peterson_unlock(sdev);

	SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
}

/**
 * dctas_wlan_on - Function to handle WLAN ON event.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function sets the WLAN state to used and updates the shared memory region accordingly.
 */
void dctas_wlan_on(struct slsi_dev *sdev)
{
	if (!sdev->dctas.dctas_initialized) {
		SLSI_ERR(sdev, "dctas_initialized : %\n", sdev->dctas.dctas_initialized);
		return;
	}

	SLSI_MUTEX_LOCK(sdev->dctas_mutex);

	sdev->dctas.wlbt_state.state = DCTAS_STATE_UNUSED;
	sdev->dctas.wlbt_state.off_time_mid = 0;
	sdev->dctas.wlbt_state.off_time_low = 0;

	SLSI_INFO(sdev, "Set state : %d\n", sdev->dctas.wlbt_state.state);

	if (peterson_lock(sdev)) {
		SLSI_ERR(sdev, "peterson_lock() failed\n");
		SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
		return;
	}
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_STATE, sdev->dctas.wlbt_state.state);
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_OFF_TIME_MID, sdev->dctas.wlbt_state.off_time_mid);
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_OFF_TIME_LOW, sdev->dctas.wlbt_state.off_time_low);
	peterson_unlock(sdev);

	memset(&sdev->dctas.fw_cp_state, 0xFF, sizeof(struct slsi_cp_state));

	SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
}

/**
 * dctas_wlan_off - Function to handle WLAN OFF event.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function sets the WLAN state to not assigned and updates the shared memory region accordingly.
 */
void dctas_wlan_off(struct slsi_dev *sdev)
{
	if (!sdev->dctas.dctas_initialized) {
		SLSI_ERR(sdev, "dctas_initialized : %\n", sdev->dctas.dctas_initialized);
		return;
	}

	SLSI_MUTEX_LOCK(sdev->dctas_mutex);

	sdev->dctas.wlbt_state.state = DCTAS_STATE_NOT_ASSIGNED;
	/** In phase 2 of implementation: Get OFF_TIME from fw if going off
	 *  (using new FAPI signals DJ: FAPI or MIB either would be good for host)
	 */
	sdev->dctas.wlbt_state.off_time_mid = DCTAS_MAX_OFF_TIME;
	sdev->dctas.wlbt_state.off_time_low = DCTAS_MAX_OFF_TIME;

	SLSI_INFO(sdev, "Set state : %d, off_time_mid : %d, off_time_low : %d\n", sdev->dctas.wlbt_state.state,
		  sdev->dctas.wlbt_state.off_time_mid,  sdev->dctas.wlbt_state.off_time_low);

	if (peterson_lock(sdev)) {
		SLSI_ERR(sdev, "peterson_lock() failed\n");
		SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
		return;
	}
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_STATE, sdev->dctas.wlbt_state.state);
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_OFF_TIME_MID, sdev->dctas.wlbt_state.off_time_mid);
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_OFF_TIME_LOW, sdev->dctas.wlbt_state.off_time_low);
	peterson_unlock(sdev);

	SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
}

/**
 * dctas_wlan_sar_ind - Function to handle WLAN SAR indication.
 * @sdev: Pointer to the slsi_dev structure.
 * @skb: Pointer to the socket buffer containing the SAR indication.
 *
 * This function handles WLAN SAR indication by updating the average value in the shared memory region.
 */
void dctas_wlan_sar_ind(struct slsi_dev *sdev, struct sk_buff *skb)
{
	if (!sdev->dctas.dctas_initialized) {
		SLSI_ERR(sdev, "dctas_initialized : %\n", sdev->dctas.dctas_initialized);
		return;
	}

	SLSI_MUTEX_LOCK(sdev->dctas_mutex);

	sdev->dctas.wlbt_state.average = (u8)fapi_get_u16(skb, u.mlme_sar_ind.sar);
	if (sdev->dctas.wlbt_state.average == DCTAS_AVERAGE_UNDERLOW) {
		sdev->dctas.wlbt_state.state = DCTAS_STATE_UNUSED;
		sdev->dctas.wlbt_state.average = DCTAS_AVERAGE_LOW;
	} else {
		sdev->dctas.wlbt_state.state = DCTAS_STATE_USED;
	}

	SLSI_INFO(sdev, "Set state : %d, average : %d\n", sdev->dctas.wlbt_state.state, sdev->dctas.wlbt_state.average);

	if (peterson_lock(sdev)) {
		SLSI_ERR(sdev, "peterson_lock() failed\n");
		SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
		return;
	}
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_STATE, sdev->dctas.wlbt_state.state);
	CP_SHMEM_WRITE_U8(sdev, OFFSET_WLBT_AVERAGE, sdev->dctas.wlbt_state.average);
	peterson_unlock(sdev);

	SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);
}

/**
 * dctas_link_notifier - Callback function for link status change notifications.
 * @nb: Pointer to the notifier block structure.
 * @state: Current link state.
 * @buf: Buffer containing additional information about the link state.
 *
 * This function handles link status change notifications and updates the internal state accordingly.
 */
int dctas_link_notifier(struct notifier_block *nb, unsigned long state, void *buf)
{
	struct slsi_dev *sdev = container_of(nb, struct slsi_dev, dctas.link_notifier);

	SLSI_MUTEX_LOCK(sdev->dctas_mutex);

	sdev->dctas.link_state = (state == PCIE_LINK_STATE_ON) ? 1 : 0;
	SLSI_INFO(sdev, "link state : %d, sdev->dctas.link_state : %d\n", state, sdev->dctas.link_state);
	if (sdev->dctas.link_state) {
		cancel_delayed_work(&sdev->dctas.mifless_work);
		if (sdev->dctas.update_deferred)
			schedule_work(&sdev->dctas.deferred_work);
	} else {
		cancel_work(&sdev->dctas.deferred_work);
		schedule_delayed_work(&sdev->dctas.mifless_work, msecs_to_jiffies(DCTAS_MIFLESS_30S));
	}

	SLSI_MUTEX_UNLOCK(sdev->dctas_mutex);

	return 0;
}

/**
 * init_dctas - Initialize the DCTAS module.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function initializes the DCTAS module by creating mutexes, wakelocks, and scheduling the necessary work queues.
 */
void init_dctas(struct slsi_dev *sdev)
{
	SLSI_MUTEX_INIT(sdev->dctas_mutex);
	slsi_wake_lock_init(NULL, &sdev->dctas.wlan_wl_dctas, "wlan_dctas");

	memset(&sdev->dctas, 0, sizeof(struct slsi_dctas));

	if (get_cp_baseaddr(sdev)) {
		SLSI_ERR(sdev, "get cp base addr failed\n");
		return;
	}

	register_link_notifier(sdev);

	INIT_DELAYED_WORK(&sdev->dctas.read_cp_work, dctas_read_cp_work);
	schedule_delayed_work(&sdev->dctas.read_cp_work, msecs_to_jiffies(0));

	INIT_WORK(&sdev->dctas.deferred_work, dctas_deferred_work);
	INIT_DELAYED_WORK(&sdev->dctas.mifless_work, dctas_mifless_work);

	sdev->dctas.dctas_initialized = true;
}

/**
 * deinit_dctas - Deinitialize the DCTAS module.
 * @sdev: Pointer to the slsi_dev structure.
 *
 * This function deinitializes the DCTAS module by destroying the wakelock, canceling any scheduled work queues,
 * and unregistering the link status change notifier. It also sets the dctas_initialized flag to false to indicate that
 * the module is no longer initialized.
 */
void deinit_dctas(struct slsi_dev *sdev)
{
	slsi_wake_lock_destroy(&sdev->dctas.wlan_wl_dctas);

	if (!sdev->dctas.dctas_initialized)
		return;

	cancel_delayed_work_sync(&sdev->dctas.read_cp_work);
	cancel_delayed_work(&sdev->dctas.read_cp_work);
	cancel_work(&sdev->dctas.deferred_work);
	cancel_delayed_work_sync(&sdev->dctas.mifless_work);

	unregister_link_notifier(sdev);

	sdev->dctas.dctas_initialized = false;
}
