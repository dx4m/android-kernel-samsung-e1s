/*****************************************************************************
 *
 * Copyright (c) 2012 - 2018 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef __SLSI_HIP_H__
#define __SLSI_HIP_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/rtc.h>

#include <pcie_scsc/scsc_mifram.h>

#ifdef CONFIG_SCSC_WLAN_HIP5
#include "hip5.h"
#else
#include "hip4.h"
#endif

#define SLSI_HIP_PARAM_SLOT_COUNT 2

/**
 * enum slsi_hip_state - HIP state
 * @SLSI_HIP_STATE_STOPPED  : (default) state, avoid running the HIP
 * @SLSI_HIP_STATE_STARTING : HIP is being initialised, avoid running the HIP
 * @SLSI_HIP_STATE_STARTED  : HIP cycles can run
 * @SLSI_HIP_STATE_STOPPING : HIP is being de-initialised, avoid running the HIP
 * @SLSI_HIP_STATE_BLOCKED  : HIP TX CMD53 failure or WLAN subsystem crashed indication from Hydra,
 *                            avoid running the HIP
 *
 * This enum type is used to determine if HIP is ready for running or not.
 */
enum slsi_hip_state {
	SLSI_HIP_STATE_STOPPED,
	SLSI_HIP_STATE_STARTING,
	SLSI_HIP_STATE_STARTED,
	SLSI_HIP_STATE_STOPPING,
	SLSI_HIP_STATE_BLOCKED
};

/**
 * struct slsi_card_info - chip and HIP core lib information that exposed to OS layer.
 * @chip_id         : chip id
 * @fw_build 	    : firmware build
 * @fw_hip_version  : firmware HIP version
 * @sdio_block_size : sdio block size
 */
struct slsi_card_info {
	u16 chip_id;
	u32 fw_build;
	u16 fw_hip_version;
	u32 sdio_block_size;
};

/**
 * struct slsi_hip - structure for HIP.
 * @sdev        : Instance of sdev. This argument cannot be null.
 * @card_info   : Chip and HIP core lib information.
 * @hip_mutex   : Lock for concurrence of hip related operation.
 * @hip_state   : refer to enum slsi_hip_state.
 * @hip_priv    : Instance of hip_priv struct. It is private to the HIP implementation.
 * @hip_ref     : Reference of the start of shared memory allocated for wlan.
 * @hip_control : Instance of hip_control struct.
 *
 * Deprecated : card_info
 *
 * This struct is the outermost structure
 * which manages several structures that control the hip operation.
 */
struct slsi_hip {
	struct slsi_dev         *sdev;
	struct slsi_card_info   card_info;

	struct mutex            hip_mutex;
	/* refer to enum slsi_hip_state */
	atomic_t                hip_state;

	struct hip_priv        *hip_priv;
	scsc_mifram_ref         hip_ref;
	/* to prevent the use of the resources of the hip_priv when the hip_priv is invalid. */
	atomic_t		is_hip_priv_invalid;
#ifdef CONFIG_SCSC_WLAN_HIP5
	struct hip5_hip_control *hip_control;
#if defined(CONFIG_WLBT_RX_CACHEABLE)
        uintptr_t               mem_start;
#endif
#else
	struct hip4_hip_control *hip_control;
#endif
};

int slsi_hip_pre_allocate_hip_priv(struct slsi_dev *sdev);
void slsi_hip_free_hip_priv(struct slsi_dev *sdev);

/**
 * slsi_hip_cm_register() - Register wlan service notifier.
 * @sdev : Instance of sdev. This argument cannot be null.
 * @dev  : struct device for wiphy.
 *
 * This function is called when the device is attached.
 * In this function,
 * 	- hip_state and hip_mutex in slsi_hip struct are initialized.
 * 	- wlan service notifier is registered to check whether async wlan servie message is received.
 *
 * Context: Process context.
 * Return: 0
 */
int slsi_hip_cm_register(struct slsi_dev *sdev, struct device *dev);


/**
 * slsi_hip_cm_unregister() - Unregister wlan service notifier.
 * @sdev : Instance of sdev. This argument cannot be null.
 *
 * This function is called when the device is detached or
 * cdev is not created normally while attaching device.
 * In this function,
 * 	- Wlan service notifier is unregistered.
 * 	- hip_mutex in slsi_hip struct is destroyed.
 *
 * Context: Process context.
 * Return: void
 */
void slsi_hip_cm_unregister(struct slsi_dev *sdev);

/**
 * slsi_hip_start() - Start HIP.
 * @sdev : Instance of sdev. This argument cannot be null.
 *
 * This function is executed during the process of starting the wlan service.
 * In this function,
 * 	- update hip state to SLSI_HIP_STATE_STARTING.
 * 	- call slsi_hip_init() to initialize hip resources.
 * 	- update hip state to SLSI_HIP_STATE_STARTED if hip_init is successfully completed.
 * 	- call slsi_traffic_mon_init() to initialize traffic monitor.
 *
 * Context: Process context.
 * 	    Caller holds
 *	    sdev->start_stop_mutex and slsi_start_mutex.
 * Return:
 * * %0 - OK
 * * %-EINVAL - hip_init() is failed or Maxwell core does not exist.
 */
int slsi_hip_start(struct slsi_dev *sdev);

/**
 * slsi_hip_setup_ext() - Setup HIP.
 * @sdev : Instance of sdev. This argument cannot be null.
 *
 * This function wraps around the function where the actual hip setup takes place.
 * In this function,
 * 	- call slsi_hip_setup which is a function that sets up the hip resources.
 * 	- call slsi_lbm_setup which is a function that sets up the load balance manager.
 * 	- above two functions are wrapped around by hip_mutex.
 *
 * Context: Process context. Takes and releases sdev->hip.hip_mutex.
 * 	    Caller holds
 * 	    sdev->start_stop_mutex and slsi_start_mutex.
 * Return:
 * * %0 - OK
 * * %-EIO - slsi_hip_setup() is failed.
 */
int slsi_hip_setup_ext(struct slsi_dev *sdev);

/**
 * slsi_hip_stop() - Stop HIP.
 * @sdev : Instance of sdev. This argument cannot be null.
 *
 * This function is called to deinitialize HIP when the chip must be stopped
 * or some errors occur during the start of the wlan service.
 *
 * In this function,
 * 	- update hip state to SLSI_HIP_STATE_STOPPING.
 * 	- call slsi_hip_deinit() to deinitialize hip resources.
 * 	- update hip state to SLSI_HIP_STATE_STOPPED.
 * 	- call slsi_traffic_mon_deinit() to deinitialize traffic monitor.
 *
 * Context: Process context. Takes and releases sdev->hip.hip_mutex.
 * 	    Caller holds
 * 	    sdev->start_stop_mutex and slsi_start_mutex.
 * Return:
 * * %0 - OK
 * * %-EINVAL - hip_init() is failed or Maxwell core does not exist.
 */
int slsi_hip_stop(struct slsi_dev *sdev);

#ifdef CONFIG_SCSC_SMAPPER
int slsi_hip_consume_smapper_entry(struct slsi_dev *sdev, struct sk_buff *skb);
void *slsi_hip_get_skb_data_from_smapper(struct slsi_dev *sdev, struct sk_buff *skb);
struct sk_buff *slsi_hip_get_skb_from_smapper(struct slsi_dev *sdev, struct sk_buff *skb);
#endif

#ifdef CONFIG_SCSC_WLAN_RX_NAPI
/**
 * slsi_hip_reprocess_skipped_ctrl_bh() - Reschedule mlme_req when cfm message from fw are omitted.
 * @sdev : Instance of sdev. This argument cannot be null.
 *
 * If the cfm message for the transmitted mlme_req is not received within timout, this function
 * should be called to reschedule mlme request to deal with unexpected timeout.
 *
 * In this function, call slsi_hip_sched_wq_ctrl to reschedule to ctrl packet workqueue.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    sdev->sig_wait->mutex
 * Return : void
 */
void slsi_hip_reprocess_skipped_ctrl_bh(struct slsi_dev *sdev);
#else
void slsi_hip_reprocess_skipped_data_bh(struct slsi_dev *sdev);
#endif

/* Forward declaration */
struct sap_api;
struct sk_buff;

/**
 * slsi_hip_sap_register() - Register SAP with HIP layer.
 * @sap_api : Instance of sap_api. This instance is initialized while the dev is loaded.
 *
 * In this function, register SAP with HIP layer to handle SAP messages with fw.
 *
 * Todo: Return value of this fucntion need to be valid check in caller functions.
 * 	 This function returns -ENODEV when sap class is abnormal,
 *       But callers of this function do not valid check of this return value.
 *
 * Context: Process context.
 *
 * Return:
 * * %0 - OK
 * * %-ENODEV - sap class is abnormal.
 */
int slsi_hip_sap_register(struct sap_api *sap_api);

/**
 * slsi_hip_sap_unregister() - Unregister SAP with HIP layer.
 * @sap_api : Instance of sap_api. This instance is initialized while the dev is loaded.
 *
 * In this function, unregister SAP with HIP layer.
 *
 * Todo: Return value of this fucntion need to be valid check in caller functions.
 * 	 This function returns -ENODEV when sap class is abnormal,
 *       But callers of this function do not valid check of this return value.
 *
 * Context: Process context.
 *
 * Return:
 * * %0 - OK
 * * %-ENODEV - sap class is abnormal.
 */
int slsi_hip_sap_unregister(struct sap_api *sap_api);

/**
 * slsi_hip_rx() - SAP rx proxy.
 * @sdev : Instance of sdev. This argument cannot be null.
 * @skb  : received sk_buff.
 *
 * In this function, determines what the sap class of skb is and calls a proper sap handler.
 *
 * Context: Softirq context.
 * 	    Caller holds hip_priv->rx_lock.
 * Return:
 * * %0 - OK
 * * %-ENODEV - Not all SAPs are registered or something wrong with SAP handler.
 * * %-EIO - SAP class is abnormal or something wrong with SAP handler.
 */
int slsi_hip_rx(struct slsi_dev *sdev, struct sk_buff *skb);

/**
 * slsi_hip_sap_setup() - determine if SAP versions are supported.
 * @sdev : Instance of sdev. This argument cannot be null.
 *
 * In this function, determines if SAP versions are supported.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    sdev->start_stop_mutex and slsi_start_mutex.
 * Return:
 * * %0 - OK
 * * %-ENODEV - SAP version is not supported.
 */
int slsi_hip_sap_setup(struct slsi_dev *sdev);
/* Allow the SAP to act on a buffer in the free list. */
#ifdef CONFIG_SCSC_WLAN_TX_API
/**
 * slsi_hip_tx_done() - Call SAP txdone handler.
 * @sdev   : Instance of sdev. This argument cannot be null.
 * @colour : information of vif, ac queue to give fw.
 * @more   : Whether all the values written in FH_RFB have been read.
 * 	     If not, this value is true.
 *
 * Call sap_txdone handler function.
 * This function wraps actual implementation of tx_done.
 * This function is executed when the host receives an interrupt from fw that fw has freed mbulk.
 *
 * Context: Interrupt context.
 * 	    Caller holds
 * 	    hip_priv->rx_lock.
 * Return: sap txdone handler function address.
 */
int slsi_hip_tx_done(struct slsi_dev *sdev, u32 colour, bool more);
#else
int slsi_hip_tx_done(struct slsi_dev *sdev, u8 vif, u8 peer_index, u8 ac);
#endif

/**
 * slsi_hip_init() - Initialize HIP resources
 * @hip : slsi_hip structure in sdev.
 *
 * In this function, all resources of HIP is initialized for communication between host and fw.
 * Below are the main process running in this function.
 *
 * - Add a memory zone required for the mbulk pool list for each queue.
 * - Reset hip_control table.
 * - Register and initialize lbm and related resources.
 * - Register interrupt handlers handling fw interrupts (TOHOST, FROMHOST).
 * - Initialize workqueues and tasklets for hip functions.
 * - Register traffic monitor for QOS and logring if needed.
 *
 * Context: Process context.
 * 	    Caller holds
 *	    sdev->start_stop_mutex and slsi_start_mutex.
 * Return:
 * * %0 - OK
 * * %-ENOMEM - Cannot allocate memory for data structs.
 * * %-EIO - Interrupt bit allocation failed or mbulk pool adding failed.
 * * %-EINVAL - sdev or sdev->service is NULL.
 */
int slsi_hip_init(struct slsi_hip *hip);

/**
 * slsi_hip_setup() - Setup HIP resources with values provided by fw.
 * @hip : slsi_hip structure in sdev.
 *
 * After the HIP is initialized, some hip resources should be setup with values
 * provided by fw such as hip config version and room for fapi signal.
 *
 * And unmask interrupt bit to receive interrupt signal from fw.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    sdev->start_stop_mutex, slsi_start_mutex and sdev->hip.hip_mutex.
 * Return:
 * * %0 - OK
 * * %-EIO - sdev or service is NULL.
 * 	     hip state is abnormal.
 */
int slsi_hip_setup(struct slsi_hip *hip);

/**
 * slsi_hip_suspend() - Suspend HIP when host going into suspend mode.
 * @hip : slsi_hip structure in sdev.
 *
 * When host should go into suspend mode, HIP resources also need to be suspended.
 * hip_priv->in_suspend set to 1.
 * And check the rtc timer for unexpected situations.
 *
 * On suspend, hip needs to ensure that TH interrupts are unmasked.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    sdev->start_stop_mutex, slsi_start_mutex and sdev->hip.hip_mutex.
 * Return : void
 */
void slsi_hip_suspend(struct slsi_hip *hip);

/**
 * slsi_hip_resume() - Resume HIP when wifi service should freeze.
 * @hip : slsi_hip structure in sdev.
 *
 * Resume HIP from suspend mode.
 * hip_priv->in_suspend set to 0 If hip state is "SLSI_HIP_STATED_STARTED"
 * If not, function should be returned before setting the in_suspend to 0.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    sdev->hip.hip_mutex.
 * Return : void
 */
void slsi_hip_resume(struct slsi_hip *hip);

/**
 * slsi_hip_freeze() - Freeze HIP from suspend mode.
 * @hip : slsi_hip structure in sdev.
 *
 * When wifi servie should freeze, HIP also need to be freezed.
 * Interrupts are masked not to receive interrupts from fw.
 * Set hip_priv->closing to 1.
 *
 * If sdev or service is NULL or hip state is "SLSI_HIP_STATE_STARTED",
 * this function should be returned before masking interrupt bits.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    sdev->hip.hip_mutex.
 * Return : void
 */
void slsi_hip_freeze(struct slsi_hip *hip);

/**
 * slsi_hip_deinit() - Deinitialize HIP resources
 * @hip : slsi_hip structure in sdev.
 *
 * Deinitialize all resources of HIP.
 * Below are the main process running in this function.
 *
 * - Traffic monitor unregister
 * - Load balance manager unregister
 * - Mask interrupt bits and de-allocate bits.
 * - Remove workqueues and locks.
 * - Free hip_priv
 * - Remove Mbulk pools.
 *
 * Context: Process context.
 * 	    Caller holds
 *	    sdev->start_stop_mutex and slsi_start_mutex and sdev->hip.hip_mutex.
 * Return: void
 */
void slsi_hip_deinit(struct slsi_hip *hip);

/**
 * slsi_hip_free_control_slots_count() - Return the number of free slots to transmit ctrl packet.
 * @hip : slsi_hip structure in sdev.
 *
 * This function wraps mbluk_pool_get_free_count which counts frees lots available in mbulk pool
 * for ctrl packet transmission.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    ndev_vif->vif_mutex and ndev_vif->peer_lock.
 * Return : The number of free slots in mbulk_pool.
 */
int slsi_hip_free_control_slots_count(struct slsi_hip *hip);
#ifndef CONFIG_SCSC_WLAN_LOAD_BALANCE_MANAGER
void slsi_hip_napi_cpu_set(struct slsi_hip *hip, u8 napi_cpu, bool perf_mode);
#endif
#ifdef CONFIG_SCSC_WLAN_HOST_DPD
int slsi_hip_from_host_dpd_intr_set(struct scsc_service *service, struct slsi_hip *hip);
#endif

/**
 * slsi_hip_from_host_intr_set() - Set interrupt bit to inform fw.
 * @service : instance of wlan service.
 * @hip : slsi_hip structure in sdev.
 *
 * Set from host interrupt bit to inform fw in tx path.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    txbp_priv.vif_lock
 * Return : void
 */
void slsi_hip_from_host_intr_set(struct scsc_service *service, struct slsi_hip *hip);

/**
 * slsi_hip_transmit_frame() -  Transmit a frame through the HIP.
 * @hip : slsi_hip structure in sdev.
 * @skb : sk_buff instance to transmit.
 * @ctrl_packet : determine if this pacekt is data packet or ctrl packet.
 * @vif_index : virtual interface index.
 * @peer_index : peer index.
 * @priority : priority of AC queues (AC_BK ~ AC_VO)
 *
 * Transmit frame through the HIP
 * It does not take ownership of the SKB unless it successfully transmit it.
 *
 * the transmission method varies slightly depending on
 * whether it is hip4, hip5, or tx zero copy.
 *
 * The vif_index, peer_index, priority fields are valid for data packets only
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    ndev_vif->peer_lock.
 * 	    txbp_priv.vif_lock with read_lock_bh()
 * Return:
 * * %0 - OK
 * * %-EINVAL - Invalid instance such as sdev, hip.
 * * %-ENOSPC - No space in mbulk pool, hip queue etc.
 * * %-EFAULT - Incorrect reference memory.
 * * %-ENOMEM - Fail to allocate memory.
 */
int slsi_hip_transmit_frame(struct slsi_hip *hip, struct sk_buff *skb, bool ctrl_packet, u8 vif_index, u8 peer_index, u8 priority);

/**
 * slsi_hip_wlan_get_rtc_time() -  Get rtc device to deal with suspend mode.
 * @tm : Intance of rtc_time structure.
 *
 * Find RTC device and get rtc time to know that rtc is working properly.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    sdev->start_stop_mutex, slsi_start_mutex and sdev->hip.hip_mutex.
 * Return:
 * * %0 - OK
 * * %-ENXIO - Can not find RTC device.
 * * %-ENODEV - error while reading rtc time.
 * * %-EINVAL - error while reading rtc time.
 */
int slsi_hip_wlan_get_rtc_time(struct rtc_time *tm);
#ifdef CONFIG_SCSC_WLAN_RX_NAPI

/**
 * slsi_hip_sched_wq_ctrl() - Trigger workqueue for skipped ctrl BH.
 * @hip : slsi_hip structure in sdev.
 *
 * Reschedule mlme request if cfm message for this request has not received.
 *
 * Context: Process context.
 * 	    Caller holds
 * 	    sdev->sig_wait->mutex.
 * Return: void
 */
void slsi_hip_sched_wq_ctrl(struct slsi_hip *hip);
#else
void hip4_sched_wq(struct slsi_hip *hip);
#endif

#ifdef CONFIG_SCSC_PCIE_CHIP
/**
 * slsi_pcie_lock() - keep pcie in claimed state.
 * @hip : slsi_hip structure in sdev.
 * @reason : PCIE claim reason
 *
 * If user want to keep pcie in claimed state for a while,
 * user can use this function.
 * This function should be used in process context.
 *
 * Context: Process context
 * Return:
 * * %false - It's not in the state that HOST can get a PCIE claim in.
 * * %true/false - test_bit(0, hip_priv->pcie_claim_bit).
 */
bool slsi_pcie_lock(struct slsi_hip *hip, enum slsi_pcie_claim_reason reason);

/**
 * slsi_pcie_unlock() - keep pcie in claimed state.
 * @hip : slsi_hip structure in sdev.
 * @reason : PCIE claim reason
 *
 * Release pcie when it is in claimed state with
 * slsi_pcie_lock/slsi_pcie_lock_atomic function.
 *
 * Context: Process context
 * Return: void
 */
void slsi_pcie_unlock(struct slsi_hip *hip, enum slsi_pcie_claim_reason reason);
#endif

void slsi_hip_resume_wrapper(struct slsi_dev *sdev);
#endif /* __SLSI_HIP_H__ */
