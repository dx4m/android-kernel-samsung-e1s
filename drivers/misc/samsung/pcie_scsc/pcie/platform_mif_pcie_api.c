#include "platform_mif_pcie_api.h"

static const char *events[7] = {"RST ON", "RST OFF", "CLAIM", "RELEASE", "PMU OFF", "PMU ON"};
static const char *states[5] = {"OFF", "ON", "SLEEP", "HOLD", "ERROR"};
enum pcie_ctrl_state state = PCIE_STATE_OFF;

static bool enable_link_fail_panic = true;
module_param(enable_link_fail_panic, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(enable_link_fail_panic, "Enables kernel panic when link up fail");

static struct platform_mif * g_platform;

inline int platform_mif_update_users(enum pcie_event_type event, struct platform_mif *platform)
{
	if (event == PCIE_EVT_CLAIM || event == PCIE_EVT_CLAIM_CB) {
		platform->host_users = 1;
	} else if (event == PCIE_EVT_RELEASE) {
		platform->host_users = 0;
	} else if (event == PCIE_EVT_PMU_ON_REQ) {
		platform->fw_users = 1;
	} else if (event == PCIE_EVT_PMU_OFF_REQ) {
		if (platform->fw_users) platform->fw_users -= 1;
	}

	return platform->fw_users + platform->host_users;
}

void platform_mif_set_g_platform(struct platform_mif* platform)
{
	g_platform = platform;
}

void platform_mif_pcie_control_fsm_prepare_fw(struct platform_mif *platform)
{
	/* FW users start with '1' as per design */
	platform->fw_users = 1;
	platform->off_req = false;
	state = PCIE_STATE_OFF;
}

int platform_mif_pcie_control_fsm(void *data)
{
	struct platform_mif *platform = data;
	struct event_record *ev_data;
	enum pcie_ctrl_state old_state;
	int ret;
	int current_users;
	unsigned long flags;

	ev_data =  kmalloc(sizeof(*ev_data), GFP_KERNEL);
	if (!ev_data)
		return -ENOMEM;

	while (true) {
		wait_event_interruptible(platform->event_wait_queue,
					 kthread_should_stop() ||
					 !kfifo_is_empty(&platform->ev_fifo) ||
				         kthread_should_park());

		if (kthread_should_park()) {
			SCSC_TAG_INFO_DEV(PLAT_MIF, platform->dev, "park thread\n");
			kthread_parkme();
		}
		if (kthread_should_stop()) {
			break;
		}

		spin_lock_irqsave(&platform->kfifo_lock, flags);
		ret =  kfifo_out(&platform->ev_fifo, ev_data, sizeof(*ev_data));
		old_state = state;
		spin_unlock_irqrestore(&platform->kfifo_lock, flags);

		if (!ret) {
			SCSC_TAG_INFO_DEV(PLAT_MIF, platform->dev, "no event to process\n");
			continue;
		}

		current_users = platform_mif_update_users(ev_data->event, platform);

		switch(state) {
		case PCIE_STATE_OFF:
			if (ev_data->event == PCIE_EVT_RST_ON) {
				ret = pcie_mif_poweron(platform->pcie);
				if (ret)
					state = PCIE_STATE_ERROR;
				else
					state = PCIE_STATE_ON;
			}
			break;

		case PCIE_STATE_ON:
			if (ev_data->event == PCIE_EVT_RST_OFF) {
				pcie_mif_poweroff(platform->pcie);
				state = PCIE_STATE_OFF;
			} else if (ev_data->event == PCIE_EVT_PMU_OFF_REQ) {
				if (current_users == 0) {
					SCSC_TAG_INFO_DEV(PLAT_MIF, platform->dev, "No PCIe users. Send PMU_AP_MSG_WLBT_PCIE_OFF_ACCEPT\n");
					pcie_set_mbox_pmu_pcie(platform->pcie, PMU_AP_MSG_WLBT_PCIE_OFF_ACCEPT);
					pcie_mif_poweroff(platform->pcie);
					state = PCIE_STATE_SLEEP;
				} else {
					SCSC_TAG_INFO_DEV(PLAT_MIF, platform->dev, "PCIe users. Send PMU_AP_MSG_WLBT_PCIE_OFF_REJECT\n");
					/* Since off is rejected, we have to
					 * increase the number of fw_users */
					platform->fw_users += 1;
					pcie_set_mbox_pmu_pcie(platform->pcie, PMU_AP_MSG_WLBT_PCIE_OFF_REJECT);
					state = PCIE_STATE_HOLD_HOST;
				}
				/* Required to be tested for CB calls */
				platform->off_req = false;
			}
			break;

		case PCIE_STATE_HOLD_HOST:
			/* PCIE is hold by the host */
			if (ev_data->event == PCIE_EVT_RST_OFF) {
				pcie_mif_poweroff(platform->pcie);
				state = PCIE_STATE_OFF;
			} else if (ev_data->event == PCIE_EVT_RELEASE && platform->host_users == 0) {
				/* If host releases the pcie usage, send the message
			 	* to PMU */
				SCSC_TAG_INFO_DEV(PLAT_MIF, platform->dev, "Host PCIe release. Send PMU_AP_MSG_WLBT_PCIE_OFF_REJECT_CANCEL\n");
				pcie_set_mbox_pmu_pcie(platform->pcie, PMU_AP_MSG_WLBT_PCIE_OFF_REJECT_CANCEL);
				state = PCIE_STATE_ON;
			}
			break;

		case PCIE_STATE_SLEEP:
			if (ev_data->event == PCIE_EVT_RST_OFF) {
				state = PCIE_STATE_OFF;
			} else if (ev_data->event == PCIE_EVT_CLAIM ||
				   ev_data->event == PCIE_EVT_CLAIM_CB ||
				   ev_data->event == PCIE_EVT_PMU_ON_REQ) {
				ret = pcie_mif_poweron(platform->pcie);
				if (ret)
					state = PCIE_STATE_ERROR;
				else
					state = PCIE_STATE_ON;
			}
			break;

		case PCIE_STATE_ERROR:
		default:
			break;
		}

		if (old_state != state) {
			SCSC_TAG_INFO_DEV(PLAT_MIF, platform->dev, "PCIE state (%s) -> (%s) on event %s. Users %d complete %d\n",
					  states[old_state], states[state], events[ev_data->event], current_users, ev_data->complete);
		}

		if (ev_data->event == PCIE_EVT_PMU_ON_REQ)
			wake_unlock(&platform->wakehash_wakelock);

		/* If State is in ERROR, skip the CB/completion so services
		 * can fail properly */
		if (state == PCIE_STATE_ERROR) {
			SCSC_TAG_ERR(PLAT_MIF, "PCIE_STATE_ERROR in turning PCIE On : ret = %d\n", ret);
#if IS_ENABLED(CONFIG_DEBUG_SNAPSHOT)
			if(enable_link_fail_panic)
				dbg_snapshot_do_dpm_policy(GO_S2D_ID);
#endif
		}

		if (ev_data->complete)
			complete(&platform->pcie_on);
	}

	kfree(ev_data);

	return 0;
}

int __platform_mif_send_event_to_fsm(struct platform_mif *platform, int event, bool comp)
{
	u32 val;
	struct event_record ev;

	unsigned long flags;

	spin_lock_irqsave(&platform->kfifo_lock, flags);
	ev.event = event;
	ev.complete = comp;
	/**
	 * TODO: we can move pcie_wakeup_cb, pcie_wakeup_cb_data_service, and pcie_wakeup_cb_data_dev
	 * from platform MIF
	 */
	ev.pcie_wakeup_cb = (event == PCIE_EVT_CLAIM_CB ? platform->pcie_wakeup_cb : NULL);
	ev.pcie_wakeup_cb_data_service = (event == PCIE_EVT_CLAIM_CB ? platform->pcie_wakeup_cb_data_service : NULL);
	ev.pcie_wakeup_cb_data_dev = (event == PCIE_EVT_CLAIM_CB ? platform->pcie_wakeup_cb_data_dev : NULL);
	val = kfifo_in(&platform->ev_fifo, &ev, sizeof(ev));
	wake_up_interruptible(&platform->event_wait_queue);
	spin_unlock_irqrestore(&platform->kfifo_lock, flags);

	return 0;
}



int platform_mif_send_event_to_fsm(struct platform_mif *platform, enum pcie_event_type event)
{
	return __platform_mif_send_event_to_fsm(platform, event, false);
}

int platform_mif_send_event_to_fsm_wait_completion(struct platform_mif *platform, enum pcie_event_type event)
{
	if (!platform->t)
		return -EIO;

	return __platform_mif_send_event_to_fsm(platform, event, true);
}

int scsc_pcie_claim(void)
{
	int ret = 0;

	/* TODO, this should be fixed and come from a mx instance!*/
	ret = platform_mif_send_event_to_fsm_wait_completion(g_platform, PCIE_EVT_CLAIM);
	return ret;
}
EXPORT_SYMBOL(scsc_pcie_claim);

void scsc_pcie_release(void)
{
	platform_mif_send_event_to_fsm(g_platform, PCIE_EVT_RELEASE);
}
EXPORT_SYMBOL(scsc_pcie_release);

void platform_mif_get_msi_range(struct scsc_mif_abs *interface, u8 *start, u8 *end,
				       enum scsc_mif_abs_target target)
{
	struct platform_mif *platform = platform_mif_from_mif_abs(interface);

	pcie_get_msi_range(platform->pcie, start, end, target);
}

int platform_mif_get_pcie_link_state(struct scsc_mif_abs *interface)
{
	struct platform_mif *platform = platform_mif_from_mif_abs(interface);

	return get_pcie_link_state(platform->pcie);
}

__iomem void *platform_get_ramrp_ptr(struct scsc_mif_abs *interface)
{
	struct platform_mif *platform = platform_mif_from_mif_abs(interface);

	return pcie_get_ramrp_ptr(platform->pcie);
}

int platform_get_ramrp_buff(struct scsc_mif_abs *interface, void** buff, int count, u64 offset)
{
	struct platform_mif *platform = platform_mif_from_mif_abs(interface);

	return pcie_get_ramrp_buff(platform->pcie, buff, count, offset);
}

int scsc_pcie_complete(void)
{
	/*
		When link up failed, it will return error,
		but we trigger kernel panic before timeout.
		So we don't use return value for now.
	*/
	if(!wait_for_completion_timeout(&g_platform->pcie_on, 5*HZ)) {
		SCSC_TAG_ERR_DEV(PLAT_MIF, g_platform->dev, "wait for complete timeout\n");
		return -EFAULT;
	}
	reinit_completion(&g_platform->pcie_on);
	return 0;
}
EXPORT_SYMBOL(scsc_pcie_complete);

void platform_mif_default_notifier(void)
{
	/* recovery notifier not registered */
	SCSC_TAG_INFO_DEV(PLAT_MIF, NULL, "Recovery notifier not registered\n");
}

void platform_mif_pcie_recovery_notifier(void)
{
	struct platform_mif *platform = (struct platform_mif *)data;

	if (platform->pcie_recovery_notifier != platform_mif_default_notifier)
		platform->pcie_recovery_notifier();
	else
		SCSC_TAG_DEBUG_DEV(PLAT_MIF, platform->dev, "MIF recovery notifier not registered\n");
}

void platform_mif_reg_pcie_recovery_notifier(struct scsc_mif_abs *interface, void (*notifier)(void))
{
	struct platform_mif *platform = platform_mif_from_mif_abs(interface);
	unsigned long flags;

	SCSC_TAG_INFO_DEV(PLAT_MIF, platform->dev, "Registering mif recovery notifier %pS in %p\n", notifier, interface);
	spin_lock_irqsave(&platform->mif_spinlock, flags);
	platform->pcie_recovery_notifier = notifier;
	spin_unlock_irqrestore(&platform->mif_spinlock, flags);
}

void platform_mif_unreg_pcie_recovery_notifier(struct scsc_mif_abs *interface)
{
	struct platform_mif *platform = platform_mif_from_mif_abs(interface);
	unsigned long flags;

	SCSC_TAG_INFO_DEV(PLAT_MIF, platform->dev, "Unregistering mif recovery notifier in %p\n", interface);
	spin_lock_irqsave(&platform->mif_spinlock, flags);
	platform->pcie_recovery_notifier = platform_mif_default_notifier;
	spin_unlock_irqrestore(&platform->mif_spinlock, flags);
}
