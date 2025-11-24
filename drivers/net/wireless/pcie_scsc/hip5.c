/******************************************************************************
 *
 * Copyright (c) 2014 - 2024 Samsung Electronics Co., Ltd. All rights reserved
 *
 *****************************************************************************/

#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <pcie_scsc/scsc_mx.h>
#include <pcie_scsc/scsc_mifram.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/rtc.h>
#include <pcie_scsc/scsc_logring.h>
#include <pcie_scsc/scsc_warn.h>

#include "hip5.h"
#include "hip4_sampler.h"
#include "mbulk.h"
#include "dev.h"
#include "load_manager.h"
#include "debug.h"
#include "mgt.h"

#ifdef CONFIG_SCSC_WLAN_TX_API
#include "tx_api.h"
#endif
#ifdef CONFIG_SCSC_WLAN_HOST_DPD
#include "dpd_mmap.h"
#endif

#ifdef CONFIG_SLSI_WLAN_LPC
#include "local_packet_capture.h"
#endif

struct hip5_tx_dma_desc {
	u16           len;
	u16           offset;
	u16           dma_map_len;
	dma_addr_t    skb_dma_addr;
	mbulk_colour  colour;
	struct sk_buff *skb;
};

/* rs_latency - Rate limit state for tx latency logs */
static DEFINE_RATELIMIT_STATE(rs_latency, (1 * HZ), 5);

#if defined(CONFIG_SCSC_PCIE_PAEAN_X86) || defined(CONFIG_SOC_S5E9925)
static bool hip5_scoreboard_in_ramrp = false;
#else
static bool hip5_scoreboard_in_ramrp = false;
#endif
module_param(hip5_scoreboard_in_ramrp, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip5_scoreboard_in_ramrp, "scoreboard location (default: Y in RAMRP, N: in DRAM)");

#if defined(CONFIG_SCSC_BB_REDWOOD)
static bool hip5_tx_zero_copy = true;
#else
static bool hip5_tx_zero_copy = false;
#endif
module_param(hip5_tx_zero_copy, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip5_tx_zero_copy, "Tx zero copy (default: enable)");

static uint hip5_tx_zero_copy_tx_slots = SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS;
module_param(hip5_tx_zero_copy_tx_slots, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip5_tx_zero_copy_tx_slots, "num of TX buffers in zero copy mode (max: 4096)");

#if IS_ENABLED(CONFIG_SCSC_LOGRING)
static bool hip4_dynamic_logging = true;
module_param(hip4_dynamic_logging, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip4_dynamic_logging, "Dynamic logging, logring is disabled if tput > hip4_qos_med_tput_in_mbps. (default: Y)");

static int hip4_dynamic_logging_mid_tput_in_mbps = 50;
module_param(hip4_dynamic_logging_mid_tput_in_mbps, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip4_dynamic_logging_mid_tput_in_mbps, "throughput (in Mbps) to apply MID dynamic logring logging");

static int hip4_dynamic_logging_tput_in_mbps = 350;
module_param(hip4_dynamic_logging_tput_in_mbps, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip4_dynamic_logging_tput_in_mbps, "throughput (in Mbps) to apply dynamic logring logging");
#endif

#ifdef CONFIG_SCSC_QOS
static bool hip4_qos_enable = true;
module_param(hip4_qos_enable, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip4_qos_enable, "enable HIP4 PM QoS. (default: Y)");

static int hip4_qos_max_tput_in_mbps = 750;
module_param(hip4_qos_max_tput_in_mbps, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip4_qos_max_tput_in_mbps, "throughput (in Mbps) to apply Max PM QoS");

static int hip4_qos_med_tput_in_mbps = 700;
module_param(hip4_qos_med_tput_in_mbps, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip4_qos_med_tput_in_mbps, "throughput (in Mbps) to apply Median PM QoS");
#endif

#ifdef CONFIG_SCSC_WLAN_NAPI_PER_NETDEV
/* MAX_NAPI_POLL_STORM. Max NAPI poll allowed when MA NAPI is not reading packets */
#define MAX_NAPI_POLL_STORM            128

static uint hip5_rx_skb_len_max = 2048;
module_param(hip5_rx_skb_len_max, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip5_rx_skb_len_max, "MAX number of SKBs allowed in the Netdev SKB queue");

static uint hip5_watchdog_timeout_in_ms = 50;
module_param(hip5_watchdog_timeout_in_ms, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip5_watchdog_timeout_in_ms, "Timeout (in ms) to trigger the HIP5 watchdog");
#endif
#ifdef CONFIG_SCSC_PCIE_CHIP
#define IS_BIDIRECTIONAL_HIGH_OVERRIDE_STATE_TRANSITION(s1, s2) (((s1) ^ (s2)) == 1)

static int hip5_pcie_claim_delay = 50;
module_param(hip5_pcie_claim_delay, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hip5_pcie_claim_delay, "delay (in ms) for pcie claim in case of resume");
#endif

static ktime_t intr_received_fb;
static ktime_t bh_init_fb;
static ktime_t bh_end_fb;
static ktime_t intr_received_ctrl;
static ktime_t bh_init_ctrl;
static ktime_t bh_end_ctrl;
static ktime_t intr_received_data;
static ktime_t bh_init_data;
static ktime_t bh_end_data;
static ktime_t send;
static ktime_t closing;

enum rw {
	widx,
	ridx,
};

/*offset of F/W owned indices */
#define FW_OWN_OFS      (128)
/**
 * HIP queue indices layout in the scoreboard (SC-505612-DD). v3
 *
 * The queue indices which owned by the host are only writable by the host.
 * F/W can only read them. And vice versa.
 */
static int q_idx_layout[24][2] = {
	{                0,  FW_OWN_OFS + 0},	/* HIP5_MIF_Q_FH_CTRL  : 0 */
	{                4,  FW_OWN_OFS + 4},	/* HIP5_MIF_Q_FH_PRI0  : 1 */
	{                8,  FW_OWN_OFS + 8},	/* HIP5_MIF_Q_FH_SKB0  : 2 */
	{  FW_OWN_OFS + 12,              12},	/* HIP5_MIF_Q_FH_RFBC  : 3 */
	{  FW_OWN_OFS + 16,              16},   /* HIP5_MIF_Q_FH_RFBD0 : 4 */
	{  FW_OWN_OFS + 20,              20},	/* HIP5_MIF_Q_TH_CTRL  : 5 */
	{  FW_OWN_OFS + 24,              24},	/* HIP5_MIF_Q_TH_DAT0  : 6 */
	{               28, FW_OWN_OFS + 28},	/* HIP5_MIF_Q_TH_RFBC  : 7 */
	{               32, FW_OWN_OFS + 32},	/* HIP5_MIF_Q_TH_RFBD0 : 8 */
	{				36,	FW_OWN_OFS + 36},	/* HIP5_MIF_Q_FH_PRI1  : 9 */
	{				40,	FW_OWN_OFS + 40},	/* HIP5_MIF_Q_FH_SKB1  : 10 */
	{  FW_OWN_OFS + 44,				 44},	/* HIP5_MIF_Q_FH_RFBD1 : 11 */
	{  FW_OWN_OFS + 48,				 48},	/* HIP5_MIF_Q_TH_DAT1  : 12 */
	{               52, FW_OWN_OFS + 52},	/* HIP5_MIF_Q_TH_RFBD1 : 13 */
	{               56, FW_OWN_OFS + 56},	/* HIP5_MIF_Q_FH_DAT0  : 14 */
	{				60,	FW_OWN_OFS + 60},	/* HIP5_MIF_Q_FH_DAT1  : 15 */
	{				64,	FW_OWN_OFS + 64},	/* HIP5_MIF_Q_FH_DAT2  : 16 */
	{               68, FW_OWN_OFS + 68},	/* HIP5_MIF_Q_FH_DAT3  : 17 */
	{               72, FW_OWN_OFS + 72},	/* HIP5_MIF_Q_FH_DAT4  : 18 */
	{				76,	FW_OWN_OFS + 76},	/* HIP5_MIF_Q_FH_DAT5  : 19 */
	{				80,	FW_OWN_OFS + 80},	/* HIP5_MIF_Q_FH_DAT6  : 20 */
	{               84, FW_OWN_OFS + 84},	/* HIP5_MIF_Q_FH_DAT7  : 21 */
	{               88, FW_OWN_OFS + 88},	/* HIP5_MIF_Q_FH_DAT8  : 22 */
	{				92,	FW_OWN_OFS + 92},	/* HIP5_MIF_Q_FH_DAT9  : 23 */
};

/* Timeout for Wakelocks in HIP  */
#define SLSI_HIP_WAKELOCK_TIME_OUT_IN_MS   (1000)

#define HIP5_TH_IDX_ERR_NUM_RETRY    4
#define FB_NO_SPC_NUM_RET    100
#define FB_NO_SPC_SLEEP_MS   10
#define FB_NO_SPC_DELAY_US   1000
#define NAPI_NO_SPC_DELAY_US 1000

#ifdef CONFIG_SCSC_PCIE_CHIP
void slsi_trigger_pcie_claim(u32 intr_host, struct slsi_hip *hip, enum slsi_pcie_claim_reason reason)
{
	struct hip_priv	*hip_priv	= hip->hip_priv;
	struct slsi_dev	*sdev		= container_of(hip, struct slsi_dev, hip);

	if (atomic_read(&hip->is_hip_priv_invalid)) {
		SLSI_WARN_NODEV("hip_priv is null. cannot queue work\n");
		return;
	}

	/* check if PCIE is claimed or not. */
	read_lock_bh(&hip_priv->rw_pcie);
	if (!test_bit(0, hip_priv->pcie_claim_bit)) {
		read_unlock_bh(&hip_priv->rw_pcie);
		if (reason == SLSI_IRQ_SET_BIT) {
			/* PCIE has not claimed. Set fromhost interrupt bitmap. */
			u32 fromhost_intr = intr_host - hip->hip_priv->intr_from_host_ctrl;

			set_bit(fromhost_intr, hip_priv->fromhost_intr_bit);
		} else if (reason == SLSI_IRQ_MASK_BIT) {
			/* PCIE has not claimed. Set tohost interrupt bitmap. */
			u32 tohost_mask_intr = intr_host - hip->hip_priv->intr_to_host_ctrl;

			set_bit(tohost_mask_intr, hip_priv->tohost_intr_mask_bit);
		} else if (reason == SLSI_IRQ_UNMASK_BIT) {
			/* PCIE has not claimed. Set tohost interrupt bitmap. */
			u32 tohost_unmask_intr = intr_host - hip->hip_priv->intr_to_host_ctrl;

			set_bit(tohost_unmask_intr, hip_priv->tohost_intr_unmask_bit);
		}
		write_lock_bh(&hip_priv->pcie_ctrl_reason);
		hip_priv->pcie_ctrl.reason |= (1U << reason);
		write_unlock_bh(&hip_priv->pcie_ctrl_reason);
		queue_work(hip_priv->pcie_wq, &hip_priv->pcie_ctrl.pcie_work);
	} else {
		if (reason == SLSI_IRQ_SET_BIT) {
			/* PCIE has already claimed. Set interrupt bit. */
			scsc_service_mifintrbit_bit_set(sdev->service, intr_host, SCSC_MIFINTR_TARGET_WLAN);
		} else if (reason == SLSI_IRQ_MASK_BIT) {
			/* PCIE has already claimed. mask interrupt bit. */
			scsc_service_mifintrbit_bit_mask(sdev->service, intr_host);
		} else if (reason == SLSI_IRQ_UNMASK_BIT) {
			/* PCIE has already claimed. Unmask interrupt bit. */
			scsc_service_mifintrbit_bit_unmask(sdev->service, intr_host);
		}
		read_unlock_bh(&hip_priv->rw_pcie);
	}
}
#endif

static void slsi_ctrl_intrbit(u32 intr_host, struct slsi_hip *hip, enum slsi_pcie_claim_reason reason)
{
#ifdef CONFIG_SCSC_PCIE_CHIP
		slsi_trigger_pcie_claim(intr_host, hip, reason);
#else
		/* Trigger interrupt bit set/mask/unmask regardless of whether PCIE is claimed or not */
		struct slsi_dev	*sdev = container_of(hip, struct slsi_dev, hip);

		switch (reason) {
		case SLSI_IRQ_SET_BIT:
			scsc_service_mifintrbit_bit_set(sdev->service, intr_host, SCSC_MIFINTR_TARGET_WLAN);
			break;
		case SLSI_IRQ_MASK_BIT:
			scsc_service_mifintrbit_bit_mask(sdev->service, intr_host);
			break;
		case SLSI_IRQ_UNMASK_BIT:
			scsc_service_mifintrbit_bit_unmask(sdev->service, intr_host);
			break;
		default:
			SLSI_WARN_NODEV("invalid claim reason\n");
			break;
		}
#endif
}

/* Update scoreboard index */
static void hip5_update_index(struct slsi_hip *hip, u32 q, enum rw r_w, u16 value)
{
	struct hip_priv    *hip_priv = hip->hip_priv;

	write_lock_bh(&hip_priv->rw_scoreboard);
	if (hip5_scoreboard_in_ramrp) {
		writel(value, hip->hip_priv->scbrd_ramrp_base + q_idx_layout[q][r_w]);
	} else {
		*((u16 *)(hip->hip_priv->scbrd_base + q_idx_layout[q][r_w])) = value;

		/* Memory barrier when updating shared mailbox/memory */
		wmb();
	}
	SCSC_HIP4_SAMPLER_Q(hip_priv->minor, q, r_w, value, 0);
	write_unlock_bh(&hip_priv->rw_scoreboard);
}

/* Read scoreboard index */
static u16 hip5_read_index(struct slsi_hip *hip, u32 q, enum rw r_w)
{
	struct hip_priv    *hip_priv = hip->hip_priv;
	u32                 value = 0;

	read_lock_bh(&hip_priv->rw_scoreboard);
	if (hip->hip_control->init.magic_number != (HIP5_CONFIG_INIT_MAGIC_NUM)) {
		SLSI_ERR_NODEV("incorrect magic_number (expected:0x%x, got:0x%x)\n",
			(HIP5_CONFIG_INIT_MAGIC_NUM),
			hip->hip_control->init.magic_number);
		read_unlock_bh(&hip_priv->rw_scoreboard);
		return 0xFFFF;
	}
	if (hip5_scoreboard_in_ramrp) {
		value = readl(hip->hip_priv->scbrd_ramrp_base + q_idx_layout[q][r_w]);
	} else {
		value = *((u16 *)(hip->hip_priv->scbrd_base + q_idx_layout[q][r_w]));

		/* Memory barrier when reading shared mailbox/memory */
		dma_rmb();
	}
	read_unlock_bh(&hip_priv->rw_scoreboard);
	return value;
}

int slsi_hip_wlan_get_rtc_time(struct rtc_time *tm)
{
	struct rtc_device *rtc = NULL;
	int err;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (!rtc) {
		SLSI_ERR_NODEV("Can not find RTC Device\n");
		return -ENXIO;
	}
	err = rtc_read_time(rtc, tm);

	rtc_class_close(rtc);

	if (err < 0)
		return err;

	return 0;
}

static void hip5_dump_dbg(struct slsi_hip *hip, struct mbulk *m, struct sk_buff *skb, struct scsc_service *service)
{
	unsigned int        i = 0;
	scsc_mifram_ref     ref;

	SLSI_ERR_NODEV("intr_to_host_ctrl 0x%x\n", hip->hip_priv->intr_to_host_ctrl);
	SLSI_ERR_NODEV("intr_to_host_fb 0x%x\n", hip->hip_priv->intr_to_host_ctrl_fb);
	SLSI_ERR_NODEV("intr_to_host_dat 0x%x\n", hip->hip_priv->intr_to_host_data1);
	SLSI_ERR_NODEV("intr_to_host_dat2 0x%x\n", hip->hip_priv->intr_to_host_data2);
	SLSI_ERR_NODEV("intr_from_host_ctrl 0x%x\n", hip->hip_priv->intr_from_host_ctrl);
	SLSI_ERR_NODEV("intr_from_host_data 0x%x\n", hip->hip_priv->intr_from_host_data);
#ifdef CONFIG_SCSC_WLAN_HOST_DPD
	SLSI_ERR_NODEV("intr_to_host_dpd 0x%x\n", hip->hip_priv->intr_to_host_dpd);
#endif

	/* Print scoreboard */
	for (i = 0; i <= HIP5_MIF_Q_FH_DAT0; i++) {
		SLSI_ERR_NODEV("Q%dW 0x%x\n", i, hip5_read_index(hip, i, widx));
		SLSI_ERR_NODEV("Q%dR 0x%x\n", i, hip5_read_index(hip, i, ridx));
	}

	if (service)
		scsc_mx_service_mif_dump_registers(service);

	if (m && service) {
		if (scsc_mx_service_mif_ptr_to_addr(service, m, &ref))
			return;
		SLSI_ERR_NODEV("m: %p 0x%x\n", m, ref);
		print_hex_dump(KERN_ERR, SCSC_PREFIX "mbulk ", DUMP_PREFIX_NONE, 16, 1, m, sizeof(struct mbulk), 0);
	}
	if (m && mbulk_has_signal(m))
		print_hex_dump(KERN_ERR, SCSC_PREFIX "sig   ", DUMP_PREFIX_NONE, 16, 1, mbulk_get_signal(m),
			       MBULK_SEG_SIG_BUFSIZE(m), 0);
	if (skb) {
		SLSI_ERR_NODEV("skb: 0x%X\n", slsi_skb_cb_get(skb)->skb_dma_addr);
		print_hex_dump(KERN_ERR, SCSC_PREFIX "skb   ", DUMP_PREFIX_NONE, 16, 1, skb->data, skb->len > 0xff ? 0xff : skb->len, 0);
	}

	SLSI_ERR_NODEV("time: now          %lld\n", ktime_to_ns(ktime_get()));
	SLSI_ERR_NODEV("time: send         %lld\n", ktime_to_ns(send));
	SLSI_ERR_NODEV("time: intr_fb      %lld\n", ktime_to_ns(intr_received_fb));
	SLSI_ERR_NODEV("time: bh_init_fb   %lld\n", ktime_to_ns(bh_init_fb));
	SLSI_ERR_NODEV("time: bh_end_fb    %lld\n", ktime_to_ns(bh_end_fb));
	SLSI_ERR_NODEV("time: intr_ctrl    %lld\n", ktime_to_ns(intr_received_ctrl));
	SLSI_ERR_NODEV("time: bh_init_ctrl %lld\n", ktime_to_ns(bh_init_ctrl));
	SLSI_ERR_NODEV("time: bh_end_ctrl  %lld\n", ktime_to_ns(bh_end_ctrl));
	SLSI_ERR_NODEV("time: intr_data    %lld\n", ktime_to_ns(intr_received_data));
	SLSI_ERR_NODEV("time: bh_init_data %lld\n", ktime_to_ns(bh_init_data));
	SLSI_ERR_NODEV("time: bh_end_data  %lld\n", ktime_to_ns(bh_end_data));
	SLSI_ERR_NODEV("time: closing      %lld\n", ktime_to_ns(closing));
}

/* Transform skb to mbulk (fapi_signal + payload) */
static struct mbulk *hip5_skb_to_mbulk(struct hip_priv *hip, struct sk_buff *skb, bool ctrl_packet, mbulk_colour colour)
{
	struct mbulk        *m = NULL;
	void                *b_data = NULL;
	size_t              payload = 0;
	u8                  pool_id = ctrl_packet ? MBULK_POOL_ID_CTRL : MBULK_POOL_ID_DATA;
	u8                  headroom = 0, tailroom = 0;
	enum mbulk_class    clas = ctrl_packet ? MBULK_CLASS_FROM_HOST_CTL : MBULK_CLASS_FROM_HOST_DAT;
	struct slsi_skb_cb *cb = slsi_skb_cb_get(skb);

	payload = skb->len - cb->sig_length;

	/* Get headroom/tailroom */
	headroom = hip->unidat_req_headroom;
	tailroom = hip->unidat_req_tailroom;

	/* Allocate mbulk */
	if (payload > 0) {
		/* If signal include payload, add headroom and tailroom */
		m = mbulk_with_signal_alloc_by_pool(pool_id, colour, clas, cb->sig_length + 4,
						    payload + headroom + tailroom);
		if (!m)
			return NULL;
		if (!mbulk_reserve_head(m, headroom))
			return NULL;
	} else {
		/* If it is only a signal do not add headroom */
		m = mbulk_with_signal_alloc_by_pool(pool_id, colour, clas, cb->sig_length + 4, 0);
		if (!m)
			return NULL;
	}

	/* Copy payload */
	/* If the signal has payload memcpy the data */
	if (payload > 0) {
		/* Get head pointer */
		b_data = mbulk_dat_rw(m);
		if (!b_data) {
			mbulk_free_virt_host(m);
			return NULL;
		}

		/* Copy payload skipping the signal data */
		memcpy(b_data, skb->data + cb->sig_length, payload);
		mbulk_append_tail(m, payload);
	}
	m->flag |= MBULK_F_OBOUND;
	return m;
}

static void hip5_opt_log_wakeup_signal(struct slsi_dev *sdev, struct sk_buff *skb, bool ctrl_packet)
{
	SLSI_INFO(sdev, "WI-FI wake up by %s frame (vif:%d, sig_id:0x%4X)\n",
		ctrl_packet ? "MLME" : "DATA",
		fapi_get_vif(skb),
		fapi_get_sigid(skb));

	slsi_skb_cb_get(skb)->wakeup = true;
	if (ctrl_packet)
		SCSC_BIN_TAG_INFO(BIN_WIFI_PM, skb->data, skb->len > 128 ? 128 : skb->len);
	else
		slsi_dump_eth_packet(sdev, skb);
}

static struct sk_buff *hip5_rx_zero_copy_get_skb_from_addr(struct slsi_dev *sdev, struct hip_priv *hip_priv, u32 addr)
{
	dma_addr_t skb_dma_addr;
	struct sk_buff *skb;
	struct hip5_skb_meta *skb_meta;
	u16 i;

	if (!addr) {
		SLSI_ERR_NODEV("addr is NULL\n");
		return NULL;
	}

	skb_dma_addr = addr;
	skb_dma_addr = (skb_dma_addr & ~0x1) << 4;
	dma_unmap_single(sdev->dev, skb_dma_addr, HIP5_DAT_MBULK_SIZE, DMA_FROM_DEVICE);

	skb_meta = (struct hip5_skb_meta *)phys_to_virt(skb_dma_addr - SLSI_NETIF_SKB_HEADROOM);
	i = skb_meta->idx;

	/* Check if it a match? i.e. the address at index matches with the returned buffer address */
	if (i < MAX_NUM) {
		if (atomic_read(&hip_priv->rx_skb_table[i].in_use) &&
			hip_priv->rx_skb_table[i].skb_dma_addr == addr) {
			skb = hip_priv->rx_skb_table[i].skb;
			atomic_set(&hip_priv->rx_skb_table[i].in_use, 0);
			return skb;
		}
	}

	/* No match yet. Search in SKB look-up table to find an entry */
	for (i = 0; i < MAX_NUM; i++)
		if (hip_priv->rx_skb_table[i].skb_dma_addr == addr)
			break;

	if (i < MAX_NUM) {
		skb = hip_priv->rx_skb_table[i].skb;
		atomic_set(&hip_priv->rx_skb_table[i].in_use, 0);
		return skb;
	}
	return NULL;
}

static void hip5_opt_skb_add_to_frag_list(struct sk_buff *skb, struct sk_buff *frag_skb)
{
	struct sk_buff **frag_tail;

	/* Initialize frag_list if not done yet */
	if (!skb_shinfo(skb)->frag_list)
		skb_frag_list_init(skb);

	/* Find the end of the frag_list */
	frag_tail = &skb_shinfo(skb)->frag_list;
	while (*frag_tail)
		frag_tail = &(*frag_tail)->next;

	/* Add the new fragment to the end of the list */
	*frag_tail = frag_skb;

	/* Update the main SKB's length */
	skb->data_len += frag_skb->len;
	skb->len += frag_skb->len;

	/* Update the main SKB's fragment count */
	skb->truesize += frag_skb->truesize;
}

static int hip5_opt_hip_signal_to_skb(struct slsi_dev *sdev,
                                                struct scsc_service *service,
                                                struct hip_priv *hip_priv,
                                                struct hip5_hip_control *ctrl,
                                                enum hip5_hip_q_conf conf,
                                                scsc_mifram_ref *to_free,
                                                u16 *idx_r,
                                                u16 *idx_w,
                                                u16 *skb_cnt,
                                                struct sk_buff_head *skb_list)
{
	struct hip5_opt_hip_signal *hip_signal;
	struct hip5_opt_bulk_desc  *bulk_desc = NULL;
	struct sk_buff             *skb = NULL;
	struct slsi_skb_cb         *cb;
	void                       *sig_ptr, *mem;
	u8                         num_bd = 0, free = 0, padding = 0;

	if (conf == HIP5_MIF_Q_TH_DAT0)
		hip_signal = (struct hip5_opt_hip_signal *)(&ctrl->q_tlv[2].array[*idx_r]);
	else if (conf == HIP5_MIF_Q_TH_CTRL)
		hip_signal = (struct hip5_opt_hip_signal *)(&ctrl->q_tlv[4].array[*idx_r]);
	else
		return -EINVAL;

	num_bd = hip_signal->num_bd;
	sig_ptr = (void *)hip_signal + 4; /* first 4 bytes in HIP entry is the entry header */
	padding = (8 - ((hip_signal->sig_len + 4)  % 8)) & 0x7;

	SLSI_DBG4(sdev, SLSI_HIP, "SIGNAL: idx_r:%d, idx_w:%d, ID:0x%4X, num_bd:%d, sig_len:%d, wake_up:%d\n",
				*idx_r,
				*idx_w,
				le16_to_cpu(((struct fapi_signal *)(sig_ptr))->id),
				num_bd,
				hip_signal->sig_len,
				hip_signal->wake_up);

	if (hip_signal->num_bd > HIP5_OPT_NUM_BD_PER_SIGNAL_MAX) {
		SLSI_ERR_NODEV("invalid num_bd %d\n", hip_signal->num_bd);
		return -EINVAL;
	}

	bulk_desc = (struct hip5_opt_bulk_desc *) (sig_ptr + hip_signal->sig_len + padding);
	if ((*idx_r == (MAX_NUM - 1)) && (hip_signal->sig_len > 52)) {
		/* wrap around, bulk desc at beginning of buffer */
		bulk_desc = (struct hip5_opt_bulk_desc *) (&ctrl->q_tlv[4].array[0]);
	}

	/* check if signal is complete or not? */
	if (hip_signal->num_bd > 3) {
		u8 i = 0;
		u16 todo = 0;
		u16 idx_w_temp = (*idx_r);

		for (i = 0; i < (hip_signal->num_bd - 3); i = i + 8) {
			/* one write index for every 8 BDs */
			idx_w_temp = idx_w_temp + 1;
			idx_w_temp &= (MAX_NUM - 1);
		}

		if (idx_w_temp == (*idx_w)) {
			/* signal is not fully written yet */
			return -EFAULT;
		}

		todo = (((*idx_w) - idx_w_temp) & (MAX_NUM - 1));
		if (todo > (MAX_NUM >> 1)) {
			return -EFAULT;
		}
	}

	/* signals with no Bulk data */
	/* TODO: review for signals that are longer than 64 bytes, extends to 2 HIP entries */
	if (!num_bd) {
		skb = alloc_skb(hip_signal->sig_len, GFP_ATOMIC);
		if (!skb) {
			SLSI_ERR_NODEV("Error allocating skb %d bytes\n", hip_signal->sig_len);
			return -ENOMEM;
		}

		memcpy(skb_put(skb, hip_signal->sig_len), sig_ptr, hip_signal->sig_len);
		cb = slsi_skb_cb_init(skb);
		cb->sig_length = hip_signal->sig_len;
		cb->data_length = cb->sig_length;

		if (hip_signal->wake_up)
			hip5_opt_log_wakeup_signal(sdev, skb, true);
		__skb_queue_tail(skb_list, skb);
		return 0;
	}

	while (num_bd) {
		if (!(bulk_desc->flag & HIP5_OPT_BD_CHAIN_START)) {
			/* single BD i.e. one BD for one MPDU */
			SLSI_DBG4(sdev, SLSI_HIP, "BD: buf_addr:0x%X, offset:%d, data_len:%d, flag:%d\n",
				bulk_desc->buf_addr,
				bulk_desc->offset,
				bulk_desc->data_len,
				bulk_desc->flag);

			if (HIP5_OPT_BD_BUF_ADDR_IS_SKB(bulk_desc->buf_addr)) {
				skb = hip5_rx_zero_copy_get_skb_from_addr(sdev, hip_priv, bulk_desc->buf_addr);
				if (!skb) {
					SLSI_ERR_NODEV("Error unmapping SKB from DMA addr 0x%X\n", bulk_desc->buf_addr);
					num_bd--;
					bulk_desc++;
					if((void *) bulk_desc == ((void *)(&ctrl->q_tlv[2].array[MAX_NUM]))) {
						bulk_desc = (struct hip5_opt_bulk_desc *)(&ctrl->q_tlv[2].array[0]);
					}
					continue;
				}

				cb = slsi_skb_cb_init(skb);
				cb->sig_length = hip_signal->sig_len;
				cb->data_length = cb->sig_length;
				*skb_cnt = (*skb_cnt) + 1;

				if (skb_headroom(skb) < hip_signal->sig_len) {
					SLSI_ERR_NODEV("Insufficient headroom:%d, sig_len:%d; trigger firmware PANIC\n", skb_headroom(skb), hip_signal->sig_len);
					SLSI_FW_BUG_ON_WQ(sdev, 1);
					return -EFAULT;
				}
				skb_put(skb, bulk_desc->data_len);
				skb_pull(skb, bulk_desc->offset);
				cb->data_length += bulk_desc->data_len;
				skb_push(skb, hip_signal->sig_len);
				memcpy(skb->data, sig_ptr, hip_signal->sig_len);
			} else {
				skb = alloc_skb(hip_signal->sig_len + bulk_desc->data_len, GFP_ATOMIC);
				if (!skb) {
					SLSI_ERR_NODEV("Error allocating skb %d bytes\n", hip_signal->sig_len + bulk_desc->data_len);
					if (conf == HIP5_MIF_Q_TH_CTRL) {
						/* Control plane: return Error without updating BD in to be freed list */
						return -ENOMEM;
					}
					/* Data plane:
					 * SKB could not be allocated, but the BD still needs to be freed
					 * and the index needs updated correctly
					 */
					to_free[free++] = bulk_desc->buf_addr;
					num_bd--;
					bulk_desc++;
					if((void *) bulk_desc == ((void *)(&ctrl->q_tlv[2].array[MAX_NUM]))) {
						bulk_desc = (struct hip5_opt_bulk_desc *)(&ctrl->q_tlv[2].array[0]);
					}
					continue;
				}
				memcpy(skb_put(skb, hip_signal->sig_len), sig_ptr, hip_signal->sig_len);

				cb = slsi_skb_cb_init(skb);
				cb->sig_length = hip_signal->sig_len;
				cb->data_length = cb->sig_length;

				mem = scsc_mx_service_mif_addr_to_ptr(service, bulk_desc->buf_addr);
				if (!mem) {
					SLSI_ERR_NODEV("bulk_desc->buf_addr NULL\n");
					SLSI_ERR_NODEV("T-H stall due to un-recoverable BD; trigger Panic\n");
					SLSI_FW_BUG_ON_WQ(sdev, 1);
					consume_skb(skb);
					return -EFAULT;
				}
#if defined(CONFIG_WLBT_RX_CACHEABLE) && !defined(CONFIG_WLBT_IOCC)
				dma_unmap_single(sdev->dev, (hip_priv->hip->mem_start + bulk_desc->buf_addr), HIP5_DAT_MBULK_SIZE, DMA_FROM_DEVICE);
#endif
				/* Track buf_addr that should be freed */
				to_free[free++] = bulk_desc->buf_addr;

				if (bulk_desc->data_len)
					fapi_append_data(skb, mem + bulk_desc->offset, bulk_desc->data_len);
			}
			if (hip_signal->wake_up && skb_queue_empty(skb_list)) {
				/* It is the first frame in the list that caused the wake-up */
				hip5_opt_log_wakeup_signal(sdev, skb, (conf == HIP5_MIF_Q_TH_CTRL) ? true : false);
			}
			__skb_queue_tail(skb_list, skb);

			num_bd--;
			bulk_desc++;
			if((void *) bulk_desc == ((void *)(&ctrl->q_tlv[2].array[MAX_NUM - 1]) + 64)) {
				bulk_desc = (struct hip5_opt_bulk_desc *)(&ctrl->q_tlv[2].array[0]);
			}
		} else {
			/* chained BDs i.e. multiple BDs for one MPDU */
			struct hip5_opt_bulk_desc *bd_temp = NULL;
			struct sk_buff *skb_frag = NULL;
			struct sk_buff *skb = NULL;

			do {
				bd_temp = bulk_desc;
				SLSI_DBG4(sdev, SLSI_HIP, "BD chain: buf_addr:0x%X, offset:%d, data_len:%d, flag:%d\n",
					bulk_desc->buf_addr,
					bulk_desc->offset,
					bulk_desc->data_len,
					bulk_desc->flag);

				if (HIP5_OPT_BD_BUF_ADDR_IS_SKB(bulk_desc->buf_addr)) {
					skb_frag = hip5_rx_zero_copy_get_skb_from_addr(sdev, hip_priv, bulk_desc->buf_addr);
					if (!skb_frag) {
						SLSI_ERR_NODEV("T-H stall due to un-recoverable chained BD; trigger Panic\n");
						SLSI_FW_BUG_ON_WQ(sdev, 1);
						if (skb)
							consume_skb(skb);
						return -EFAULT;
					}
					*skb_cnt = (*skb_cnt) + 1;
					if (!skb) {
						/* prepare the parent SKB */
						skb = skb_frag;
						cb = slsi_skb_cb_init(skb);
						cb->sig_length = hip_signal->sig_len;
						cb->data_length = cb->sig_length;
						/* store the buffer address for Debug purpose */
						cb->skb_dma_addr = bulk_desc->buf_addr;

						if (skb_headroom(skb) < hip_signal->sig_len) {
							SLSI_ERR_NODEV("Insufficient headroom:%d, sig_len:%d; trigger firmware PANIC\n", skb_headroom(skb), hip_signal->sig_len);
							SLSI_FW_BUG_ON_WQ(sdev, 1);
							consume_skb(skb);
							return -EFAULT;
						}
						skb_put(skb, bulk_desc->data_len);
						skb_pull(skb, bulk_desc->offset);
						cb->data_length += bulk_desc->data_len;
						skb_push(skb, hip_signal->sig_len);
						memcpy(skb->data, sig_ptr, hip_signal->sig_len);
					} else {
						/* prepare the SKB fragment */
						skb_put(skb_frag, bulk_desc->data_len);
						skb_pull(skb_frag, bulk_desc->offset);
						cb->data_length += bulk_desc->data_len;
						hip5_opt_skb_add_to_frag_list(skb, skb_frag);
					}
				} else {
					skb_frag = alloc_skb(hip_signal->sig_len + bulk_desc->data_len, GFP_ATOMIC);
					if (!skb_frag) {
						SLSI_ERR_NODEV("Error allocating skb %d bytes\n", hip_signal->sig_len + bulk_desc->data_len);
						/* SKB could not be allocated, the A-MSDU can't be formed correctly
						 * but the BD still needs to be freed and the scoreboard index needs
						 * updated correctly.
						 */
						to_free[free++] = bulk_desc->buf_addr;
						num_bd--;
						/* move to next Buffer descriptor */
						bulk_desc++;
						if((void *) bulk_desc == ((void *)(&ctrl->q_tlv[2].array[MAX_NUM - 1]) + 64)) {
							bulk_desc = (struct hip5_opt_bulk_desc *)(&ctrl->q_tlv[2].array[0]);
						}
						continue;
					}
					mem = scsc_mx_service_mif_addr_to_ptr(service, bulk_desc->buf_addr);
					if (!mem) {
						SLSI_ERR_NODEV("bulk_desc->buf_addr NULL\n");
						SLSI_ERR_NODEV("T-H stall due to un-recoverable chained BD; trigger Panic\n");
						SLSI_FW_BUG_ON_WQ(sdev, 1);
						consume_skb(skb_frag);
						if (skb)
							consume_skb(skb);
						return -EFAULT;
					}
#if defined(CONFIG_WLBT_RX_CACHEABLE) && !defined(CONFIG_WLBT_IOCC)
					dma_unmap_single(sdev->dev, (hip_priv->hip->mem_start + bulk_desc->buf_addr), HIP5_DAT_MBULK_SIZE, DMA_FROM_DEVICE);
#endif
					/* Track buf_addr that should be freed */
					to_free[free++] = bulk_desc->buf_addr;

					if (!skb) {
						skb = skb_frag;
						memcpy(skb_put(skb_frag, hip_signal->sig_len), sig_ptr, hip_signal->sig_len);

						cb = slsi_skb_cb_init(skb);
						cb->sig_length = hip_signal->sig_len;
						cb->data_length = cb->sig_length;
						/* store the buffer address for Debug purpose */
						cb->skb_dma_addr = bulk_desc->buf_addr;
						if (bulk_desc->data_len)
							fapi_append_data(skb_frag, mem + bulk_desc->offset, bulk_desc->data_len);
					} else {
						if (bulk_desc->data_len) {
							cb->data_length += bulk_desc->data_len;
							fapi_append_data(skb_frag, mem + bulk_desc->offset, bulk_desc->data_len);
							hip5_opt_skb_add_to_frag_list(skb, skb_frag);
						}
					}
				}
				bulk_desc++;
				if((void *) bulk_desc == ((void *)(&ctrl->q_tlv[2].array[MAX_NUM - 1]) + 64)) {
					bulk_desc = (struct hip5_opt_bulk_desc *)(&ctrl->q_tlv[2].array[0]);
				}
				num_bd--;
			} while ((bd_temp->flag & HIP5_OPT_BD_CHAIN_START));

			if (skb) {
				if (hip_signal->wake_up && skb_queue_empty(skb_list)) {
					/* It is the first frame in the list that caused the wake-up */
					hip5_opt_log_wakeup_signal(sdev, skb, (conf == HIP5_MIF_Q_TH_CTRL) ? true : false);
				}
				__skb_queue_tail(skb_list, skb);
			}
		}
	}

	/* update read index for chained HIP signals */
	if (hip_signal->sig_len >= 58) {
		/* TODO: signal length for more than 2 HIP entries */
		*idx_r = (*idx_r) + 1;
		*idx_r &= (MAX_NUM - 1);
	}

	if (hip_signal->num_bd > 3) {
		u8 i = 0;

		for (i = 0; i < (hip_signal->num_bd - 3); i = i + 8) {
			/* Increase index */
			*idx_r = (*idx_r) + 1;
			*idx_r &= (MAX_NUM - 1);
		}
	}
	return 0;
}

static inline mbulk_colour get_colour_from_skb(struct sk_buff *skb)
{
	mbulk_colour colour = 0;
	u8 vif_index = 0;

	vif_index = fapi_get_vif(skb);
	if (skb->dev) {
		struct netdev_vif *ndev_vif = netdev_priv(skb->dev);

		if (ndev_vif->iftype != NL80211_IFTYPE_AP_VLAN)
			vif_index = ndev_vif->ifnum;
	}
#ifndef CONFIG_SCSC_WLAN_TX_API
	SLSI_MBULK_COLOUR_SET(colour,
	vif_index,
	slsi_skb_cb_get(skb)->peer_idx,
	slsi_frame_priority_to_ac_queue(skb->priority));
#else
	SLSI_MBULK_COLOUR_SET(colour, vif_index, slsi_frame_priority_to_ac_queue(skb->priority));
#endif
	return colour;
}

static inline int hip5_opt_skb_to_bd(struct hip_priv *hip_priv, struct sk_buff *skb, struct hip5_opt_bulk_desc *bulk_desc)
{
	mbulk_colour colour = get_colour_from_skb(skb);
	struct hip5_skb_meta skb_meta;
	u16 i = 0;

	/* find an index in look up table */
	for (i = hip_priv->tx_skb_table_last_idx; i < SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS; i++)
		if (!atomic_read(&hip_priv->tx_skb_table[i].in_use))
			break;

	if(i >= SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS) {
		/* search from start of table now */
		for (i = 0; i < SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS; i++)
		if (!atomic_read(&hip_priv->tx_skb_table[i].in_use))
			break;
	}

	if(i >= SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS) {
		SLSI_ERR_NODEV("no space in SKB look-up table\n");
		return -ENOMEM;
	}

	hip_priv->tx_skb_free_cnt--;
	SCSC_HIP4_SAMPLER_MBULK(hip_priv->minor, (hip_priv->tx_skb_free_cnt & 0xFF00) >> 8, (hip_priv->tx_skb_free_cnt & 0xFF), MBULK_POOL_ID_DATA, hip5_tx_zero_copy_tx_slots);

	/* write SKB table index info at skb->head */
	skb_meta.idx = i;
	memcpy(skb->head, &skb_meta, sizeof(struct hip5_skb_meta));

	skb_pull(skb, fapi_sig_size(ma_unitdata_req));
	/* store the Tx SKB */
	hip_priv->tx_skb_table[i].skb_dma_addr = ((slsi_skb_cb_get(skb)->skb_dma_addr>> 4) | 0x1);
	hip_priv->tx_skb_table[i].dma_map_len = slsi_skb_cb_get(skb)->dma_map_len;
	hip_priv->tx_skb_table[i].colour = colour;
	hip_priv->tx_skb_table[i].skb = skb;
	hip_priv->tx_skb_table_last_idx = i;
	atomic_set(&hip_priv->tx_skb_table[i].in_use, 1);

	bulk_desc->buf_addr = ((slsi_skb_cb_get(skb)->skb_dma_addr>> 4) | 0x1);
	bulk_desc->buf_sz = 2; /* 512 * (1 << buf_sz) */
	bulk_desc->data_len = skb->len;
	bulk_desc->flag = 0;
	bulk_desc->offset = slsi_skb_cb_get(skb)->offset + fapi_sig_size(ma_unitdata_req);
	return 0;
}

static inline int hip5_opt_zero_copy_dma_desc_to_bd(struct hip_priv *hip_priv, struct hip5_tx_dma_desc * dma_desc, struct hip5_opt_bulk_desc *bulk_desc)
{
	u16 i = 0;

	/* find an index in look up table */
	for (i = 0; i < SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS; i++)
		if (!atomic_read(&hip_priv->tx_skb_table[i].in_use))
			break;

	if(i >= SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS) {
		SLSI_ERR_NODEV("no space in SKB look-up table\n");
		return -ENOMEM;
	}

	hip_priv->tx_skb_free_cnt--;
	SCSC_HIP4_SAMPLER_MBULK(hip_priv->minor, (hip_priv->tx_skb_free_cnt & 0xFF00) >> 8, (hip_priv->tx_skb_free_cnt & 0xFF), MBULK_POOL_ID_DATA, hip5_tx_zero_copy_tx_slots);

	/* store the Tx SKB */
	hip_priv->tx_skb_table[i].skb_dma_addr = ((dma_desc->skb_dma_addr >> 4) | 0x1);
	hip_priv->tx_skb_table[i].dma_map_len = dma_desc->dma_map_len;
	hip_priv->tx_skb_table[i].colour = dma_desc->colour;
	hip_priv->tx_skb_table[i].skb = dma_desc->skb;
	atomic_set(&hip_priv->tx_skb_table[i].in_use, 1);

	bulk_desc->buf_addr = (((dma_desc->skb_dma_addr >> 4) | 0x1));
	if (dma_desc->len >= HIP5_DAT_MBULK_SIZE)
		bulk_desc->buf_sz = 3; /* 512 * (1 << buf_sz) = 4K */
	else
		bulk_desc->buf_sz = 2; /* 512 * (1 << buf_sz) = 2K */
	bulk_desc->data_len = dma_desc->len;
	bulk_desc->flag = 0;
	bulk_desc->offset = dma_desc->offset;
	return 0;
}

static int hip5_opt_hip_signal_add(struct slsi_hip *hip, enum hip5_hip_q_conf conf, scsc_mifram_ref phy_m, struct scsc_service *service, void *m_ptr, struct sk_buff *skb_fapi, bool zero_copy)
{
	struct hip5_hip_control  *ctrl = hip->hip_control;
	struct hip_priv          *hip_priv = hip->hip_priv;
	u16                       idx_w;
	u16                       idx_r;
	struct mbulk             *m = m_ptr;
	struct hip5_opt_hip_signal *hip5_q_entry = NULL;
	struct hip5_opt_bulk_desc        *bd = NULL;
	u8                        padding;

	/* Read the current q write pointer */
	idx_w = hip5_read_index(hip, conf, widx);
	/* Read the current q read pointer */
	idx_r = hip5_read_index(hip, conf, ridx);

	if ((idx_r == 0xFFFF) || (idx_w == 0xFFFF)) {
		SLSI_ERR_NODEV("invalid Index\n");
		return -EINVAL;
	}

	if (idx_w >= MAX_NUM) {
		SLSI_ERR_NODEV("invalid Write index (q:%d, idx_w:%d)\n", conf, idx_w);
		return -EINVAL;
	}

	SCSC_HIP4_SAMPLER_Q(hip_priv->minor, conf, widx, idx_w, 1);
	SCSC_HIP4_SAMPLER_Q(hip_priv->minor, conf, ridx, idx_r, 1);

	/* Is queue full? */
	if (idx_r == ((idx_w + 1) & (MAX_NUM - 1))) {
		SLSI_ERR_NODEV("no space\n");
		return -ENOSPC;
	}

	/* build the HIP entry */
	if (conf == HIP5_MIF_Q_FH_CTRL)
		hip5_q_entry = (struct hip5_opt_hip_signal *)&ctrl->q_tlv[3].array[idx_w];
	else if (conf == HIP5_MIF_Q_FH_DAT0)
		hip5_q_entry = (struct hip5_opt_hip_signal *)&ctrl->q_tlv[0].array[idx_w];
	else
		hip5_q_entry = (struct hip5_opt_hip_signal *)&ctrl->q_tlv[1].array[idx_w];

	/* HIP signal header */
	hip5_q_entry->sig_format = 0; /* not in use */
	hip5_q_entry->reserved = 0;   /* not in use */
	if (m_ptr)
		hip5_q_entry->num_bd = 1;
	else
		hip5_q_entry->num_bd = 0;
	hip5_q_entry->sig_len = skb_fapi->len;

	/* FAPI signal */
	memcpy((void *)hip5_q_entry + 4, skb_fapi->data, skb_fapi->len);

	/* padding for bulk descriptor to start on 8 byte aligned */
	padding = (8 - ((skb_fapi->len + 4)  % 8)) & 0x7;

	bd = (struct hip5_opt_bulk_desc *) ((void *)hip5_q_entry + 4 + skb_fapi->len + padding);

	/* check for wrap-around if large signal length will need more than 1 HIP entries */
	if (conf == HIP5_MIF_Q_FH_CTRL)
		if((void *) bd >= ((void *)&ctrl->q_tlv[3].array[MAX_NUM])) {
			bd = (struct hip5_opt_bulk_desc *)&ctrl->q_tlv[3].array[0];
		}

	if (zero_copy) {
		if (hip5_opt_zero_copy_dma_desc_to_bd(hip_priv, (struct hip5_tx_dma_desc *)m_ptr, bd)) {
			SLSI_WARN_NODEV("no space in SKB lookup\n");
			return -ENOSPC;
		}
	} else {
		/* bulk descriptor(s) */
		if (m_ptr) {
			bd->buf_addr = phy_m;
			bd->buf_sz = 2; /* 512 * (1<<buf_sz) */
			bd->data_len = m->len;
			bd->flag = m->flag;
			bd->offset = m->head + 20;
		}
	}

	/* Memory barrier before updating shared mailbox */
	wmb();
	SCSC_HIP4_SAMPLER_QREF(hip_priv->minor, phy_m, conf);

	/* Increase index */
	idx_w++;
	idx_w &= (MAX_NUM - 1);

	if ((HIP5_OPT_HIP_HEADER_LEN + hip5_q_entry->sig_len + padding + (hip5_q_entry->num_bd * sizeof(struct hip5_opt_bulk_desc))) >
		(sizeof(struct hip5_opt_hip_signal))) {
		/* TODO: handle signal length that can be larger than 2 HIP entries;
		 * there is no such signal now.
		 */
		idx_w++;
		idx_w &= (MAX_NUM - 1);
	}

	/* Update the scoreboard */
	hip5_update_index(hip, conf, widx, idx_w);

	send = ktime_get();
	if (conf == HIP5_MIF_Q_FH_CTRL) {
		slsi_ctrl_intrbit(hip_priv->intr_from_host_ctrl, hip, SLSI_IRQ_SET_BIT);
	}
	return 0;
}

static int hip5_q_add_signal(struct slsi_hip *hip, enum hip5_hip_q_conf conf, scsc_mifram_ref phy_m, struct scsc_service *service)
{
	struct hip5_hip_control *ctrl = hip->hip_control;
	struct hip_priv        *hip_priv = hip->hip_priv;
	u16                      idx_w;
	u16                      idx_r;

	/* Read the current q write pointer */
	idx_w = hip5_read_index(hip, conf, widx);
	/* Read the current q read pointer */
	idx_r = hip5_read_index(hip, conf, ridx);

	if ((idx_r >= MAX_NUM) || (idx_w >= MAX_NUM)) {
		SLSI_WARN_NODEV("invalid scoreboard index (r:%d, w:%d)\n", idx_r, idx_w);
		return -EINVAL;
	}

	SCSC_HIP4_SAMPLER_Q(hip_priv->minor, conf, widx, idx_w, 1);
	SCSC_HIP4_SAMPLER_Q(hip_priv->minor, conf, ridx, idx_r, 1);

	/* Queue is full */
	if (idx_r == ((idx_w + 1) & (MAX_NUM - 1))) {
		SLSI_INFO_NODEV("q[%d] is full\n", conf);
		return -ENOSPC;
	}

	/* Update array */
	ctrl->q[conf].array[idx_w] = phy_m;

	/* Memory barrier before updating shared mailbox */
	wmb();
	SCSC_HIP4_SAMPLER_QREF(hip_priv->minor, phy_m, conf);

	/* Increase index */
	idx_w++;
	idx_w &= (MAX_NUM - 1);

	/* Update the scoreboard */
	hip5_update_index(hip, conf, widx, idx_w);

	send = ktime_get();
	slsi_ctrl_intrbit(hip_priv->intr_from_host_ctrl, hip, SLSI_IRQ_SET_BIT);

	return 0;
}

static void hip5_rx_zero_copy_refill_skb_buffers(struct slsi_dev *sdev, struct scsc_service *service, struct slsi_hip *hip, u16 num_skbs)
{
	struct hip5_hip_control *ctrl = hip->hip_control;
	struct hip_priv         *hip_priv = hip->hip_priv;
	struct sk_buff *skb;
	struct hip5_skb_meta skb_meta;
	dma_addr_t skb_dma_addr;
	u16 idx_r, idx_w, space;
	u8 retry = FB_NO_SPC_NUM_RET;
	u16 i = 0;

	/* check the space in HIP queue */
	idx_r = hip5_read_index(hip, HIP5_MIF_Q_FH_SKB0, ridx);
	idx_w = hip5_read_index(hip, HIP5_MIF_Q_FH_SKB0, widx);

	if ((idx_r >= MAX_NUM) || (idx_w >= MAX_NUM)) {
		SLSI_ERR_NODEV("invalid scoreboard Index\n");
		space = 0;
	} else {
		space = ((idx_r - idx_w) & (MAX_NUM - 1));
		/* space 0 indicates the queue is empty i.e. full space to write to */
		if (!space)
			space = MAX_NUM;
	}
	SLSI_DBG4(sdev, SLSI_HIP, "r:%d w:%d space:%d, count:%d\n", idx_r, idx_w, space, num_skbs);

	/* Is there enough space to write, if not then wait */
	while ((space <= num_skbs) && (retry > 0)) {
		SLSI_WARN_NODEV("not enough space in FH-SKB queue, retry of %d/%d\n", ((FB_NO_SPC_NUM_RET + 1) - retry), FB_NO_SPC_NUM_RET);
		udelay(FB_NO_SPC_DELAY_US);
		retry--;

		if (retry == 0) {
			SLSI_ERR_NODEV("FH-SKB queue not freed for %d us; trigger Panic\n", FB_NO_SPC_NUM_RET * FB_NO_SPC_DELAY_US);
			SLSI_FW_BUG_ON_WQ(sdev, 1);
			return;
		}

		idx_r = hip5_read_index(hip, HIP5_MIF_Q_FH_SKB0, ridx);
		idx_w = hip5_read_index(hip, HIP5_MIF_Q_FH_SKB0, widx);
		if ((idx_r >= MAX_NUM) || (idx_w >= MAX_NUM)) {
			SLSI_ERR_NODEV("invalid scoreboard Index\n");
			space = 0;
		} else {
			space = ((idx_r - idx_w) & (MAX_NUM - 1));
			/* space 0 indicates the queue is empty i.e. full space to write to */
			if (!space)
				space = MAX_NUM;
		}
		SLSI_DBG4(sdev, SLSI_HIP, "RETRY: r:%d w:%d space:%d\n", idx_r, idx_w, space);
	}

	while (num_skbs--) {
		/* find an index in look up table */
		for (i = hip_priv->rx_skb_table_last_idx; i < MAX_NUM; i++)
			if (!atomic_read(&hip_priv->rx_skb_table[i].in_use))
				break;

		if(i >= MAX_NUM) {
			/* search from start of table now */
			for (i = 0; i < MAX_NUM; i++)
				if (!atomic_read(&hip_priv->rx_skb_table[i].in_use))
					break;
		}

		/* unlikely, but ensure not to overwrite on in-use SKBs */
		if(i >= MAX_NUM) {
			SLSI_ERR_NODEV("no space in RX SKB look-up table\n");
			break;
		}

		/* alloc a new SKB */
		skb = alloc_skb(HIP5_DAT_MBULK_SIZE, GFP_ATOMIC | GFP_DMA);
		if (!skb) {
			u8 retry_alloc_skb = FB_NO_SPC_NUM_RET;
			SLSI_ERR_NODEV("fail to alloc new SKB\n");
			while (!skb && (retry_alloc_skb > 0)) {
				SLSI_WARN_NODEV("fail to alloc SKB, retry of %d/%d\n", ((FB_NO_SPC_NUM_RET + 1) - retry_alloc_skb), FB_NO_SPC_NUM_RET);
				udelay(FB_NO_SPC_DELAY_US);
				retry_alloc_skb--;
				skb = alloc_skb(HIP5_DAT_MBULK_SIZE, GFP_ATOMIC | GFP_DMA);
			}
			if (!skb) {
				SLSI_ERR_NODEV("fail to alloc new SKB for Refill\n");
				break;
			}
		}

		/* Get DMA'able address of SKB */
		skb_reserve(skb, SLSI_NETIF_SKB_HEADROOM);

		/* write index in the SKB look up table info at skb->head */
		skb_meta.idx = i;
		memcpy(skb->head, &skb_meta, sizeof(struct hip5_skb_meta));

		skb_dma_addr = dma_map_single(scsc_service_get_pcie_dev(), skb->data, HIP5_DAT_MBULK_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(scsc_service_get_pcie_dev(), skb_dma_addr)) {
			SLSI_ERR_NODEV("DMA mapping error\n");
			consume_skb(skb);
			break;
		}

		hip_priv->rx_skb_table_last_idx = i;
		atomic_set(&hip_priv->rx_skb_table[i].in_use, 1);
		hip_priv->rx_skb_table[i].skb_dma_addr = ((skb_dma_addr >> 4) | 0x1);
		hip_priv->rx_skb_table[i].skb = skb;

		SLSI_DBG4(sdev, SLSI_HIP, "i:%d, idx_w:%d, SKB:0x%X\n", i, idx_w, hip_priv->rx_skb_table[i].skb_dma_addr);
		/* Update HIP array */
		ctrl->q[HIP5_MIF_Q_FH_SKB0].array[idx_w] = hip_priv->rx_skb_table[i].skb_dma_addr;

		/* Increase index */
		idx_w++;
		idx_w &= (MAX_NUM - 1);
	}

	/* Update the scoreboard */
	hip5_update_index(hip, HIP5_MIF_Q_FH_SKB0, widx, idx_w);
}

#ifdef CONFIG_SLSI_WLAN_LPC
static void hip5_lpc_work(struct work_struct *data)
{
	struct hip_priv        *hip_priv = slsi_lbm_get_hip_priv_from_work(data);
	struct slsi_hip        *hip;
	struct slsi_dev        *sdev;
	int                     ret;

	if (!hip_priv || !hip_priv->hip) {
		SLSI_ERR_NODEV("hip_priv or hip_priv->hip is Null\n");
		return;
	}

	hip = hip_priv->hip;
	sdev = container_of(hip, struct slsi_dev, hip);

	if (!sdev || !sdev->service) {
		SLSI_ERR_NODEV("sdev or sdev->service is Null\n");
		return;
	}

	ret = slsi_lpc_get_packets_info(sdev);

	if (ret <= 0) {
		SLSI_DBG2(sdev, SLSI_HIP, "There are still packets to process.\n");
		slsi_lbm_run_bh(hip->hip_priv->bh_lpc);
	}
}
#endif

static void hip5_wq_ctrl(struct work_struct *data)
{
	struct hip_priv        *hip_priv = slsi_lbm_get_hip_priv_from_work(data);
	struct slsi_hip        *hip;
	struct hip5_hip_control *ctrl;
	struct scsc_service     *service;
	struct slsi_dev         *sdev;
	u8						retry;
	u16                      idx_r;
	u16                      idx_w;
	scsc_mifram_ref         ref;
	int err = 0;
	u16 num_skbs = 0;

	if (!hip_priv || !hip_priv->hip) {
		SLSI_ERR_NODEV("hip_priv or hip_priv->hip is Null\n");
		return;
	}

	hip = hip_priv->hip;
	ctrl = hip->hip_control;

	if (!ctrl) {
		SLSI_ERR_NODEV("hip->hip_control is Null\n");
		return;
	}
	sdev = container_of(hip, struct slsi_dev, hip);

	if (!sdev || !sdev->service) {
		SLSI_ERR_NODEV("sdev or sdev->service is Null\n");
		return;
	}

	service = sdev->service;
	SCSC_HIP4_SAMPLER_INT_BH(hip->hip_priv->minor, 1);
	bh_init_ctrl = ktime_get();

	idx_r = hip5_read_index(hip, HIP5_MIF_Q_TH_CTRL, ridx);
	idx_w = hip5_read_index(hip, HIP5_MIF_Q_TH_CTRL, widx);

	if ((idx_r >= MAX_NUM) || (idx_w >= MAX_NUM)) {
		SLSI_WARN(sdev, "invalid scoreboard index (r:%d, w:%d)\n", idx_r, idx_w);
		bh_end_ctrl = ktime_get();
		return;
	}
	SLSI_DBG2(sdev, SLSI_HIP, "r:%d w:%d todo:%hhu\n", idx_r, idx_w, ((idx_w - idx_r) & (MAX_NUM - 1)));

	if (idx_r != idx_w) {
		SCSC_HIP4_SAMPLER_Q(hip_priv->minor, HIP5_MIF_Q_TH_CTRL, ridx, idx_r, 1);
		SCSC_HIP4_SAMPLER_Q(hip_priv->minor, HIP5_MIF_Q_TH_CTRL, widx, idx_w, 1);
	}

	/* Catch-up with idx_w */
	while (idx_r != idx_w) {
		struct sk_buff_head skb_list;
		scsc_mifram_ref to_free[MBULK_MAX_CHAIN + 1] = { 0 };
		u8              i = 0;
		u8              tries = 0;
		u8              work_done = 0;

		spin_lock_bh(&hip_priv->rx_lock);
		__skb_queue_head_init(&skb_list);
		dma_rmb();

		while (tries < 5) {
			if (hip5_opt_hip_signal_to_skb(sdev, service, hip_priv, ctrl, HIP5_MIF_Q_TH_CTRL, to_free, &idx_r, &idx_w, &num_skbs, &skb_list)) {
				tries++;
				SLSI_ERR_NODEV("Ctrl: Error parsing or allocating skb (tries:%d)\n", tries);
				udelay(FB_NO_SPC_DELAY_US);
				continue;
			}
			work_done = 1;
			break;
		}

		if (!work_done) {
			/* return without updating the read index */
			SLSI_DBG1(sdev, SLSI_HIP, "Ctrl: Error parsing; return without read_index update\n");
			spin_unlock_bh(&hip_priv->rx_lock);
			goto out;
		}

		while (!skb_queue_empty(&skb_list)) {
			struct sk_buff *rx_skb;

			rx_skb = __skb_dequeue(&skb_list);
			err =  slsi_hip_rx(sdev, rx_skb);
			if (err < 0) {
				SLSI_ERR_NODEV("Ctrl: Error detected slsi_hip_rx err:%d\n", err);
				hip5_dump_dbg(hip, NULL, rx_skb, service);
				kfree_skb(rx_skb);
			}
		}

		/* Increase index */
		idx_r++;
		idx_r &= (MAX_NUM - 1);

		/* Go through the list of references to free */
		while ((ref = to_free[i++])) {
			/* Set the number of retries */
			retry = FB_NO_SPC_NUM_RET;
			/* return to the firmware */
			while (hip5_q_add_signal(hip, HIP5_MIF_Q_TH_RFBC, ref, service) && (!atomic_read(&hip->hip_priv->closing)) && (retry > 0)) {
				SLSI_WARN_NODEV("Ctrl: Not enough space in FB, retry of %d/%d\n",
						((FB_NO_SPC_NUM_RET + 1) - retry), FB_NO_SPC_NUM_RET);
				spin_unlock_bh(&hip_priv->rx_lock);
				msleep(FB_NO_SPC_SLEEP_MS);
				spin_lock_bh(&hip_priv->rx_lock);
				retry--;
				if (retry == 0)
					SLSI_ERR_NODEV("Ctrl: FB has not been freed for %d ms\n", FB_NO_SPC_NUM_RET * FB_NO_SPC_SLEEP_MS);
				SCSC_HIP4_SAMPLER_QFULL(hip_priv->minor, HIP5_MIF_Q_TH_RFBC);
			}
		}
		hip5_update_index(hip, HIP5_MIF_Q_TH_CTRL, ridx, idx_r);

		if (num_skbs) {
			SLSI_DBG4(sdev, SLSI_HIP, "refill SKBs count:%d\n", num_skbs);
			hip5_rx_zero_copy_refill_skb_buffers(sdev, service, hip, num_skbs);
		}
		spin_unlock_bh(&hip_priv->rx_lock);
	}
	slsi_ctrl_intrbit(hip_priv->intr_from_host_ctrl, hip, SLSI_IRQ_SET_BIT);
out:
	if (slsi_wake_lock_active(&hip->hip_priv->wake_lock_ctrl))
		slsi_wake_unlock(&hip->hip_priv->wake_lock_ctrl);
	if (!atomic_read(&hip->hip_priv->closing))
		slsi_ctrl_intrbit(hip_priv->intr_to_host_ctrl, hip, SLSI_IRQ_UNMASK_BIT);
	SCSC_HIP4_SAMPLER_INT_OUT_BH(hip->hip_priv->minor, 1);
	bh_end_ctrl = ktime_get();
}

static void hip5_irq_handler_stub(int irq, void *data)
{
	struct slsi_hip    *hip = (struct slsi_hip *)data;
	struct slsi_dev     *sdev = container_of(hip, struct slsi_dev, hip);

	if (atomic_read(&hip->hip_priv->closing)) {
		SLSI_ERR_NODEV("interrupt received while hip is closed: %d\n", irq);
		scsc_service_mifintrbit_bit_mask(sdev->service, irq);
		scsc_service_mifintrbit_bit_clear(sdev->service, irq);
		return;
	}

	SLSI_ERR_NODEV("interrupt received: %d\n", irq);
	scsc_service_mifintrbit_bit_mask(sdev->service, irq);
	/* Clear interrupt */
	scsc_service_mifintrbit_bit_clear(sdev->service, irq);
}

static void hip5_irq_handler_ctrl(int irq, void *data)
{
	struct slsi_hip    *hip = (struct slsi_hip *)data;
	struct slsi_dev     *sdev = container_of(hip, struct slsi_dev, hip);

	if (atomic_read(&hip->hip_priv->closing)) {
		SLSI_ERR_NODEV("interrupt received while hip is closed: %d\n", irq);
		scsc_service_mifintrbit_bit_mask(sdev->service, irq);
		scsc_service_mifintrbit_bit_clear(sdev->service, irq);
		return;
	}

	SCSC_HIP4_SAMPLER_INT(hip->hip_priv->minor, 1);
	intr_received_ctrl = ktime_get();
	if (!slsi_wake_lock_active(&hip->hip_priv->wake_lock_ctrl)) {
		slsi_wake_lock_timeout(&hip->hip_priv->wake_lock_ctrl, msecs_to_jiffies(SLSI_HIP_WAKELOCK_TIME_OUT_IN_MS));
	}

	scsc_service_mifintrbit_bit_mask(sdev->service, irq);
	slsi_lbm_run_bh(hip->hip_priv->bh_ctl);

	/* Clear interrupt */
	scsc_service_mifintrbit_bit_clear(sdev->service, irq);
	SCSC_HIP4_SAMPLER_INT_OUT(hip->hip_priv->minor, 1);
}

void slsi_hip_sched_wq_ctrl(struct slsi_hip *hip)
{
	struct slsi_dev *sdev = NULL;

	if (!hip || !hip->hip_priv)
		return;

	sdev = container_of(hip, struct slsi_dev, hip);
	if (!sdev || !sdev->service)
		return;

	if (atomic_read(&hip->hip_priv->closing))
		return;

#ifdef CONFIG_SCSC_PCIE_CHIP
#if IS_ENABLED(CONFIG_SCSC_LOG_COLLECTION)
	SLSI_INFO(sdev, "collect sable log\n");
	scsc_log_collector_schedule_collection(SCSC_LOG_HOST_WLAN, SCSC_LOG_HOST_WLAN_REASON_CONNECT_ERR);
#endif
	if (slsi_pcie_lock(hip, SLSI_IRQ_SET_BIT)) {
		scsc_service_irqdump(sdev->service);
		slsi_pcie_unlock(hip, SLSI_IRQ_SET_BIT);
	}
#endif
	hip5_dump_dbg(hip, NULL, NULL, sdev->service);

	if (!slsi_wake_lock_active(&hip->hip_priv->wake_lock_ctrl))
		slsi_wake_lock_timeout(&hip->hip_priv->wake_lock_ctrl,
				       msecs_to_jiffies(SLSI_HIP_WAKELOCK_TIME_OUT_IN_MS));

	SLSI_DBG1(sdev, SLSI_HIP, "Trigger wq for skipped ctrl BH\n");

	slsi_lbm_run_bh(hip->hip_priv->bh_ctl);
}

static mbulk_colour hip5_opt_zero_copy_free_buffer(struct slsi_dev *sdev, struct hip_priv *hip_priv, u32 addr)
{
	struct hip5_skb_meta *skb_meta;
	dma_addr_t dma_addr;
	u32 colour;
	u16 i;

	if (!addr) {
		SLSI_ERR(sdev, "addr is NULL\n");
		return 0;
	}

	hip_priv->tx_skb_free_cnt++;
	SCSC_HIP4_SAMPLER_MBULK(hip_priv->minor, (hip_priv->tx_skb_free_cnt & 0xFF00) >> 8, (hip_priv->tx_skb_free_cnt & 0xFF), MBULK_POOL_ID_DATA, hip5_tx_zero_copy_tx_slots);

	/* try to get the index to look-up table from the buffer itself */
	dma_addr = addr;
	dma_addr = (dma_addr & ~0x1) << 4;
	skb_meta = (struct hip5_skb_meta *)phys_to_virt(dma_addr);
	i = skb_meta->idx;

	/* Check if it a match? i.e. the address at index matches with the returned buffer address */
	if (i < SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS) {
		if (atomic_read(&hip_priv->tx_skb_table[i].in_use) &&
			hip_priv->tx_skb_table[i].skb_dma_addr == addr) {
			struct sk_buff* skb = hip_priv->tx_skb_table[i].skb;
			if (!skb) {
				SLSI_WARN(sdev, "skb NULL\n");
				return 0;
			}
			hip_priv->tx_skb_table[i].skb = NULL;
			colour = hip_priv->tx_skb_table[i].colour;
			dma_unmap_single(scsc_service_get_pcie_dev(), dma_addr, hip_priv->tx_skb_table[i].dma_map_len, DMA_TO_DEVICE);
			consume_skb(skb);
			atomic_set(&hip_priv->tx_skb_table[i].in_use, 0);
			return colour;
		}
	}

	/* If no match, search in SKB look-up table */
	for (i = 0; i < SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS; i++)
		if (atomic_read(&hip_priv->tx_skb_table[i].in_use) &&
			hip_priv->tx_skb_table[i].skb_dma_addr == addr)
			break;

	if (i < SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS) {
		struct sk_buff* skb = hip_priv->tx_skb_table[i].skb;
		if (!skb) {
			SLSI_WARN(sdev, "skb NULL\n");
			return 0;
		}
		hip_priv->tx_skb_table[i].skb = NULL;
		colour = hip_priv->tx_skb_table[i].colour;
		dma_unmap_single(scsc_service_get_pcie_dev(), dma_addr, hip_priv->tx_skb_table[i].dma_map_len, DMA_TO_DEVICE);
		consume_skb(skb);
		atomic_set(&hip_priv->tx_skb_table[i].in_use, 0);
		return colour;
	}
	return 0;
}

static void hip5_tl_fb(unsigned long data)
{
	struct slsi_hip		*hip = (void *)data;
	struct hip_priv		*hip_priv = hip->hip_priv;
	struct hip5_hip_control *ctrl;
	struct scsc_service 	*service;
	struct slsi_dev 		*sdev;
	u16						idx_r;
	u16						idx_w;
	mbulk_colour            colour = 0;
	struct mbulk            *m;
	scsc_mifram_ref			ref;
	void                    *mem;
	u8 more = 0;

	if (!hip_priv || !hip_priv->hip) {
		SLSI_ERR_NODEV("hip_priv or hip_priv->hip is Null\n");
		return;
	}

	hip = hip_priv->hip;
	ctrl = hip->hip_control;

	if (!ctrl) {
		SLSI_ERR_NODEV("hip->hip_control is Null\n");
		return;
	}
	sdev = container_of(hip, struct slsi_dev, hip);

	if (!sdev || !sdev->service) {
		SLSI_ERR_NODEV("sdev or sdev->service is Null\n");
		return;
	}

	service = sdev->service;
	SCSC_HIP4_SAMPLER_INT_BH(hip->hip_priv->minor, 2);
	bh_init_fb = ktime_get();

	idx_r = hip5_read_index(hip, HIP5_MIF_Q_FH_RFBC, ridx);
	idx_w = hip5_read_index(hip, HIP5_MIF_Q_FH_RFBC, widx);

	if ((idx_r >= MAX_NUM) || (idx_w >= MAX_NUM)) {
		SLSI_WARN(sdev, "invalid scoreboard index (r:%d, w:%d)\n", idx_r, idx_w);
		bh_end_fb = ktime_get();
		return;
	}

	if (idx_r != idx_w) {
		SCSC_HIP4_SAMPLER_Q(hip_priv->minor, HIP5_MIF_Q_FH_RFBC, ridx, idx_r, 1);
		SCSC_HIP4_SAMPLER_Q(hip_priv->minor, HIP5_MIF_Q_FH_RFBC, widx, idx_w, 1);
	}

	spin_lock_bh(&hip_priv->rx_lock);
	while (idx_r != idx_w) {
		ref = ctrl->q[HIP5_MIF_Q_FH_RFBC].array[idx_r];
#ifdef CONFIG_SCSC_WLAN_HIP5_PROFILING
		SCSC_HIP4_SAMPLER_QREF(hip_priv->minor, ref, HIP5_MIF_Q_FH_RFBC);
#endif
		if (HIP5_OPT_BD_BUF_ADDR_IS_SKB(ref)) {
			colour = hip5_opt_zero_copy_free_buffer(sdev, hip_priv, ref);
		} else {
			mem = scsc_mx_service_mif_addr_to_ptr(service, ref);
			m = (struct mbulk *)mem;

			if (!m) {
				SLSI_ERR_NODEV("FB: Mbulk is NULL\n");
				goto consume_fb_mbulk;
			}

			/* Account ONLY for data RFB
			 * If the returned mbulk is for control pool, the call below returns 0
			 */
			colour = mbulk_get_colour(MBULK_POOL_ID_DATA, m);
			mbulk_free_virt_host(m);
		}

		/* Ignore return value */
#ifdef CONFIG_SCSC_WLAN_TX_API
		more = ((idx_r + 1 & (MAX_NUM - 1)) != idx_w);
		if (colour || (!more))
			slsi_hip_tx_done(sdev, colour, more);
#else
		if (colour)
			slsi_hip_tx_done(sdev,
					 SLSI_MBULK_COLOUR_GET_IFNUM(colour),
					 SLSI_MBULK_COLOUR_GET_PEER_IDX(colour),
					 SLSI_MBULK_COLOUR_GET_AC(colour));
#endif
consume_fb_mbulk:
		/* Increase index */
		idx_r++;
		idx_r &= (MAX_NUM - 1);
	}

	/* Update read index */
	hip5_update_index(hip, HIP5_MIF_Q_FH_RFBC, ridx, idx_r);

	if (slsi_wake_lock_active(&hip->hip_priv->wake_lock_tx))
		slsi_wake_unlock(&hip->hip_priv->wake_lock_tx);
	if (!atomic_read(&hip->hip_priv->closing))
		slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_ctrl_fb, hip, SLSI_IRQ_UNMASK_BIT);
	SCSC_HIP4_SAMPLER_INT_OUT_BH(hip->hip_priv->minor, 2);
	spin_unlock_bh(&hip_priv->rx_lock);
	bh_end_fb = ktime_get();
}

static void hip5_irq_handler_fb(int irq, void *data)
{
	struct slsi_hip    *hip = (struct slsi_hip *)data;
	struct slsi_dev     *sdev = container_of(hip, struct slsi_dev, hip);

	if (atomic_read(&hip->hip_priv->closing)) {
		SLSI_ERR_NODEV("interrupt received while hip is closed: %d\n", irq);
		scsc_service_mifintrbit_bit_mask(sdev->service, irq);
		scsc_service_mifintrbit_bit_clear(sdev->service, irq);
		return;
	}

	SCSC_HIP4_SAMPLER_INT(hip->hip_priv->minor, 2);
	intr_received_fb = ktime_get();

	if (!slsi_wake_lock_active(&hip->hip_priv->wake_lock_tx)) {
		slsi_wake_lock_timeout(&hip->hip_priv->wake_lock_tx, msecs_to_jiffies(SLSI_HIP_WAKELOCK_TIME_OUT_IN_MS));
	}
	scsc_service_mifintrbit_bit_mask(sdev->service, irq);
	slsi_lbm_run_bh(hip->hip_priv->bh_rfb);

	/* Clear interrupt */
	scsc_service_mifintrbit_bit_clear(sdev->service, irq);
	SCSC_HIP4_SAMPLER_INT_OUT(hip->hip_priv->minor, 2);
}

static void hip5_napi_complete(struct slsi_dev *sdev, struct napi_struct *napi, struct slsi_hip *hip)
{
	bh_end_data = ktime_get();
	napi_complete(napi);

	if (slsi_wake_lock_active(&hip->hip_priv->wake_lock_data))
		slsi_wake_unlock(&hip->hip_priv->wake_lock_data);
	if (!atomic_read(&hip->hip_priv->closing))
		slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_data1, hip, SLSI_IRQ_UNMASK_BIT);
}

static u16 hip5_opt_rx_free_buffer(struct slsi_dev *sdev, struct scsc_service *service, struct slsi_hip *hip, scsc_mifram_ref *to_free)
{
	struct hip5_hip_control *ctrl = hip->hip_control;
	scsc_mifram_ref ref;
	u16 idx_r, idx_w, space;
	u8 retry = FB_NO_SPC_NUM_RET;
	u8 num_buf = 0;
	u8 i = 0;

	/* count the num of buffers to free */
	while ((i <= HIP5_OPT_NUM_BD_PER_SIGNAL_MAX) && (ref = to_free[i++])) {
		num_buf++;
	}
	if (!num_buf)
		return 0;

	i = 0;
	/* check the space in HIP queue */
	idx_r = hip5_read_index(hip, HIP5_MIF_Q_TH_RFBD0, ridx);
	idx_w = hip5_read_index(hip, HIP5_MIF_Q_TH_RFBD0, widx);

	if ((idx_r >= MAX_NUM) || (idx_w >= MAX_NUM)) {
		SLSI_ERR_NODEV("invalid scoreboard Index\n");
		space = 0;
	} else {
		space = ((idx_r - idx_w) & (MAX_NUM - 1)) ? ((idx_r - idx_w) & (MAX_NUM - 1)) : MAX_NUM;
	}
	SLSI_DBG4(sdev, SLSI_HIP, "r:%d w:%d space:%d num_buf:%d\n", idx_r, idx_w, space, num_buf);

	/* Is there enough space to write, if not then wait */
	while ((space <= num_buf) && (retry > 0)) {
			SLSI_WARN_NODEV("Not enough space in FB, retry of %d/%d\n", ((FB_NO_SPC_NUM_RET + 1) - retry), FB_NO_SPC_NUM_RET);
			udelay(FB_NO_SPC_DELAY_US);
			retry--;

			if (retry == 0) {
				SLSI_ERR_NODEV("Dat: FB has not been freed for %d us; trigger Panic\n", FB_NO_SPC_NUM_RET * FB_NO_SPC_DELAY_US);
				SLSI_FW_BUG_ON_WQ(sdev, 1);
				return num_buf;
			}

			idx_r = hip5_read_index(hip, HIP5_MIF_Q_TH_RFBD0, ridx);
			idx_w = hip5_read_index(hip, HIP5_MIF_Q_TH_RFBD0, widx);
			if ((idx_r >= MAX_NUM) || (idx_w >= MAX_NUM)) {
				SLSI_ERR_NODEV("invalid scoreboard Index\n");
			} else {
				space = ((idx_r - idx_w) & (MAX_NUM - 1));
			}
			SLSI_DBG4(sdev, SLSI_HIP, "RETRY: r:%d w:%d todo:%d\n", idx_r, idx_w, space);
	}

	while ((idx_w < MAX_NUM) && (i <= num_buf) && (ref = to_free[i++])) {
		/* Update array */
		ctrl->q[HIP5_MIF_Q_TH_RFBD0].array[idx_w] = ref;

		/* Increase index */
		idx_w++;
		idx_w &= (MAX_NUM - 1);
	}

	/* Update the scoreboard */
	hip5_update_index(hip, HIP5_MIF_Q_TH_RFBD0, widx, idx_w);
	return num_buf;
}

#ifdef CONFIG_SCSC_WLAN_NAPI_PER_NETDEV
static void hip5_watchdog(struct timer_list *t)
{
	struct hip_priv        *hip_priv = from_timer(hip_priv, t, watchdog);

	SLSI_INFO_NODEV("\n");
	if (hip_priv->watchdog_active) {
		SLSI_INFO_NODEV("schedule data BH\n");
		slsi_lbm_run_bh(hip_priv->bh_dat);
	}
}

static bool space_avail_for_new_rx(struct slsi_dev *sdev)
{
	struct net_device    *dev;
	struct netdev_vif    *ndev_vif;
	u32 num_queued = 0;
	u16 i = 0;

	rcu_read_lock();
	for (i = 0; i <= CONFIG_SCSC_WLAN_MAX_INTERFACES; i++) {
		dev = slsi_get_netdev_rcu(sdev, i);
		if (!dev)
			continue;
		ndev_vif = netdev_priv(dev);
		if (ndev_vif && ndev_vif->activated)
			num_queued += skb_queue_len(&ndev_vif->rx_data_q);
	}
	rcu_read_unlock();

	if (num_queued > hip5_rx_skb_len_max) {
		SLSI_WARN(sdev, "MAX num of SKBs queued: %d\n", num_queued);
		return false;
	}
	return true;
}
#endif

static int hip5_napi_poll(struct napi_struct *napi, int budget)
{
	struct hip_priv        *hip_priv = slsi_lbm_get_hip_priv_from_napi(napi);
	struct slsi_hip        *hip;
	struct hip5_hip_control *ctrl;
	struct scsc_service     *service;
	struct slsi_dev         *sdev;
	u16                      idx_r;
	u16                      idx_w;
	u16                     todo = 0;
	u16                     num_bd = 0;
	int                     work_done = 0;
	int			hip_state;

	if (!hip_priv || !hip_priv->hip) {
		SLSI_ERR_NODEV("hip_priv or hip_priv->hip is Null\n");
		return 0;
	}

	hip = hip_priv->hip;
	if (!hip || !hip->hip_priv) {
		SLSI_ERR_NODEV("either hip or hip->hip_priv is Null\n");
		return 0;
	}

	ctrl = hip->hip_control;

	if (!ctrl) {
		SLSI_ERR_NODEV("hip->hip_control is Null\n");
		return 0;
	}
	sdev = container_of(hip, struct slsi_dev, hip);

	if (!sdev || !sdev->service) {
		SLSI_ERR_NODEV("sdev or sdev->service is Null\n");
		return 0;
	}

	hip_state = atomic_read(&sdev->hip.hip_state);

	if (hip_state != SLSI_HIP_STATE_STARTED) {
		SLSI_DBG3(sdev, SLSI_RX, "hip cycles can not run(hip state:%d)\n", hip_state);
		hip5_napi_complete(sdev, napi, hip);
		return 0;
	}

	spin_lock_bh(&hip_priv->rx_lock);
	SCSC_HIP4_SAMPLER_INT_BH(hip->hip_priv->minor, 0);
	if (ktime_compare(bh_init_data, bh_end_data) <= 0)
		bh_init_data = ktime_get();

#ifdef CONFIG_SCSC_WLAN_NAPI_PER_NETDEV
	if (hip_priv->napi_poll_storm_count >= MAX_NAPI_POLL_STORM) {
		SLSI_WARN(sdev, "MAX NAPI poll storm, do NAPI complete\n");
		hip->hip_priv->napi_poll_storm_count = 0;
		hip_priv->watchdog_active = true;
		mod_timer(&hip_priv->watchdog, jiffies + msecs_to_jiffies(hip5_watchdog_timeout_in_ms));
		hip5_napi_complete(sdev, napi, hip);
		goto end;
	}
	/* check for flow control */
	if (!space_avail_for_new_rx(sdev)) {
		SLSI_WARN(sdev, "NO space available in RX queue\n");
		/* delay the next NAPI poll to allow Netdev NAPI to catchup */
		udelay(NAPI_NO_SPC_DELAY_US);
		/* set work_done equal to budget to keep polling */
		work_done = budget;
		hip_priv->napi_poll_storm_count++;
		goto end;
	}
	hip_priv->watchdog_active = false;
	hip_priv->napi_poll_storm_count = 0;
#endif

	idx_r = hip5_read_index(hip, HIP5_MIF_Q_TH_DAT0, ridx);
	idx_w = hip5_read_index(hip, HIP5_MIF_Q_TH_DAT0, widx);

	service = sdev->service;

	if (idx_r == idx_w) {
		SLSI_DBG3(sdev, SLSI_RX, "nothing to do, NAPI Complete\n");
		hip5_napi_complete(sdev, napi, hip);
		goto end;
	}

	if ((idx_r >= MAX_NUM) || (idx_w >= MAX_NUM)) {
		SLSI_WARN(sdev, "invalid scoreboard index (r:%d, w:%d)\n", idx_r, idx_w);
		hip5_napi_complete(sdev, napi, hip);
		goto end;
	}

	SCSC_HIP4_SAMPLER_Q(hip_priv->minor, HIP5_MIF_Q_TH_DAT0, ridx, idx_r, 1);
	SCSC_HIP4_SAMPLER_Q(hip_priv->minor, HIP5_MIF_Q_TH_DAT0, widx, idx_w, 1);

	todo = ((idx_w - idx_r) & (MAX_NUM - 1));
	SLSI_DBG2(sdev, SLSI_RX, "r:%d w:%d todo:%d\n", idx_r, idx_w, todo);

	/* The write index written by firmware may arrive out of order
	 * at Host. This can cause the Host to read from wrong indexes.
	 *
	 * To mitigate this, apply a half buffer check to decide whether
	 * the write index is a valid one or not. More than half buffer
	 * size indicates that write index just arrived read out of order.
	 *
	 * As of now the firmware can send half buffer maximum (as we have
	 * 512 mbulk slots and 1024 entries). If the number of slots are changed
	 * the firmware must ensure that in one update it does not write more
	 * than half of the size of circular buffer.
	 */
	if (todo > (MAX_NUM >> 1)) {
		hip_priv->th_w_idx_err_count++;
		SLSI_WARN(sdev, "Out of order write index (r:%d, w:%d, retry: %d/%d)\n",
			idx_r,
			idx_w,
			hip_priv->th_w_idx_err_count,
			HIP5_TH_IDX_ERR_NUM_RETRY);

		if (hip_priv->th_w_idx_err_count >= HIP5_TH_IDX_ERR_NUM_RETRY) {
			SLSI_ERR_NODEV("T-H data stall due to invalid Write index; trigger Panic\n");
			SLSI_FW_BUG_ON_WQ(sdev, 1);
			hip5_napi_complete(sdev, napi, hip);
			goto end;
		}
		/* set work_done equal to budget to keep polling */
		work_done = budget;
		goto end;
	}
	hip_priv->th_w_idx_err_count = 0;

	/* Catch-up with idx_w */
	while (idx_r != idx_w) {
		struct sk_buff_head skb_list;
		u16 num_skbs = 0;
		/* list of mbulks to be freed */
		scsc_mifram_ref to_free[HIP5_OPT_NUM_BD_PER_SIGNAL_MAX + 1] = { 0 };
		int ret;

		__skb_queue_head_init(&skb_list);
		ret = hip5_opt_hip_signal_to_skb(sdev, service, hip_priv, ctrl, HIP5_MIF_Q_TH_DAT0, to_free, &idx_r, &idx_w,  &num_skbs, &skb_list);
		if (ret) {
			if (ret == -EFAULT) {
				/* set work_done to 0 to stop polling */
				work_done = 0;

				/* read index is not updated and No free buffer signal */
				break;
			}
			SLSI_ERR_NODEV("Dat: Error parsing or allocating skb\n");
			hip5_dump_dbg(hip, NULL, NULL, service);
			goto consume_dat_mbulk;
		}

		while (!skb_queue_empty(&skb_list)) {
			struct sk_buff *rx_skb;

			rx_skb = __skb_dequeue(&skb_list);
			if (slsi_hip_rx(sdev, rx_skb) < 0) {
				SLSI_ERR_NODEV("Dat: Error detected slsi_hip_rx\n");
				hip5_dump_dbg(hip, NULL, rx_skb, service);
				kfree_skb(rx_skb);
			}
		}
consume_dat_mbulk:
		/* Increase index */
		idx_r++;
		idx_r &= (MAX_NUM - 1);

		if (!atomic_read(&hip->hip_priv->closing)) {
			num_bd = hip5_opt_rx_free_buffer(sdev, service, hip, to_free);

			if (num_skbs)
				hip5_rx_zero_copy_refill_skb_buffers(sdev, service, hip, num_skbs);
		}
		work_done += num_bd;
		work_done += num_skbs;
		if (work_done >= budget) {
			/* We have consumed all the bugdet */
			work_done = budget;
			break;
		}
	}

	hip5_update_index(hip, HIP5_MIF_Q_TH_DAT0, ridx, idx_r);
	slsi_ctrl_intrbit(hip_priv->intr_from_host_ctrl, hip, SLSI_IRQ_SET_BIT);

	if (!work_done) {
		hip5_napi_complete(sdev, napi, hip);
	}
end:
	SLSI_DBG3(sdev, SLSI_RX, "work done:%d\n", work_done);
	SCSC_HIP4_SAMPLER_INT_OUT_BH(hip->hip_priv->minor, 0);
	spin_unlock_bh(&hip_priv->rx_lock);
	return work_done;
}

static void hip5_irq_handler_dat(int irq, void *data)
{
	struct slsi_hip    *hip = (struct slsi_hip *)data;
	struct slsi_dev    *sdev = container_of(hip, struct slsi_dev, hip);
	unsigned long      flags;

	if (!hip || !sdev || !sdev->service || !hip->hip_priv) {
		SLSI_ERR_NODEV("NULL HIP instance\n");
		return;
	}

	if (atomic_read(&hip->hip_priv->closing)) {
		SLSI_ERR_NODEV("interrupt received while HIP is closed: %d\n", irq);
		scsc_service_mifintrbit_bit_mask(sdev->service, irq);
		scsc_service_mifintrbit_bit_clear(sdev->service, irq);
		return;
	}

	SCSC_HIP4_SAMPLER_INT(hip->hip_priv->minor, 0);
	intr_received_data = ktime_get();

	if (!slsi_wake_lock_active(&hip->hip_priv->wake_lock_data)) {
		slsi_wake_lock_timeout(&hip->hip_priv->wake_lock_data, msecs_to_jiffies(SLSI_HIP_WAKELOCK_TIME_OUT_IN_MS));
	}

	/* Mask interrupt to avoid interrupt storm and let BH run */
	scsc_service_mifintrbit_bit_mask(sdev->service, hip->hip_priv->intr_to_host_data1);

#ifdef CONFIG_SLSI_WLAN_LPC
	if (slsi_lpc_is_lpc_enabled())
		slsi_lbm_run_bh(hip->hip_priv->bh_lpc);
#endif

	spin_lock_irqsave(&hip->hip_priv->napi_cpu_lock, flags);
	if (test_bit(SLSI_HIP_NAPI_STATE_ENABLED, &hip->hip_priv->bh_dat->bh_priv.napi.napi_state)) {
		slsi_lbm_run_bh(hip->hip_priv->bh_dat);
		scsc_service_mifintrbit_bit_clear(sdev->service, hip->hip_priv->intr_to_host_data1);
	}
	spin_unlock_irqrestore(&hip->hip_priv->napi_cpu_lock, flags);
	SCSC_HIP4_SAMPLER_INT_OUT(hip->hip_priv->minor, 0);
}

#ifdef CONFIG_SCSC_QOS
static void hip5_pm_qos_work(struct work_struct *data)
{
	struct hip_priv        *hip_priv = container_of(data, struct hip_priv, pm_qos_work);
	struct slsi_hip        *hip = hip_priv->hip;
	struct slsi_dev         *sdev = container_of(hip, struct slsi_dev, hip);
	u8 state;

	if (!sdev || !sdev->service) {
		WLBT_WARN_ON(1);
		return;
	}

	SLSI_DBG1(sdev, SLSI_HIP, "update to state %d\n", hip_priv->pm_qos_state);
	spin_lock_bh(&hip_priv->pm_qos_lock);
	state = hip_priv->pm_qos_state;
	spin_unlock_bh(&hip_priv->pm_qos_lock);
	scsc_service_pm_qos_update_request(sdev->service, state);
}

static void hip5_traffic_monitor_cb(void *client_ctx, u32 state, u32 tput_tx, u32 tput_rx)
{
	struct slsi_hip *hip = (struct slsi_hip *)client_ctx;
	struct slsi_dev *sdev;
	u8 state_before;

	if (!hip || !hip->hip_priv) {
		SLSI_WARN_NODEV("invalid HIP instance\n");
		return;
	}

	sdev = container_of(hip, struct slsi_dev, hip);
	if (!sdev)
		return;

	spin_lock_bh(&hip->hip_priv->pm_qos_lock);
	state_before = hip->hip_priv->pm_qos_state;

	SLSI_DBG1(sdev, SLSI_HIP, "state:%u --> event (state:%u, tput_tx:%u Mbps, tput_rx:%u Mbps)\n",
		state_before,
		state,
		SLSI_TRAFFIC_MON_BYTES_TO_MBPS(tput_tx),
		SLSI_TRAFFIC_MON_BYTES_TO_MBPS(tput_rx));

	if (state == TRAFFIC_MON_CLIENT_STATE_HIGH || state == TRAFFIC_MON_CLIENT_STATE_OVERRIDE)
		hip->hip_priv->pm_qos_state = SCSC_QOS_MAX;
	else if (state == TRAFFIC_MON_CLIENT_STATE_MID)
		hip->hip_priv->pm_qos_state = SCSC_QOS_MED;
	else
		hip->hip_priv->pm_qos_state = SCSC_QOS_DISABLED;

	spin_unlock_bh(&hip->hip_priv->pm_qos_lock);

	if (state_before != hip->hip_priv->pm_qos_state)
		schedule_work(&hip->hip_priv->pm_qos_work);
}
#endif

#ifdef CONFIG_SCSC_PCIE_CHIP
void slsi_print_pcie_lock_cnt(struct slsi_dev *sdev, struct hip_priv *hip_priv)
{
	SLSI_DBG1(sdev, SLSI_HIP, "reason : %d, claimed cnt: %d\n", SLSI_NON_IRQ,
		  atomic_read(&hip_priv->pcie_claim_non_irq_cnt));
	SLSI_DBG1(sdev, SLSI_HIP, "reason : %d, claimed cnt: %d\n", SLSI_IRQ_SET_BIT,
		  atomic_read(&hip_priv->pcie_claim_intrbit_set_cnt));
	SLSI_DBG1(sdev, SLSI_HIP, "reason : %d, claimed cnt: %d\n", SLSI_IRQ_MASK_BIT,
		  atomic_read(&hip_priv->pcie_claim_intrbit_mask_cnt));
}

bool slsi_pcie_lock(struct slsi_hip *hip, enum slsi_pcie_claim_reason reason)
{
	struct hip_priv	*hip_priv	= hip->hip_priv;
	struct slsi_dev *sdev		= container_of(hip, struct slsi_dev, hip);

	if (atomic_read(&hip->is_hip_priv_invalid)) {
		SLSI_WARN_NODEV("hip_priv is null. cannot queue work\n");
		return false;
	}

	if (sdev->pcie_shutdown) {
		SLSI_WARN_NODEV("pcie is shutting down. Ignore queue work\n");
		return false;
	}

	read_lock_bh(&hip->hip_priv->rw_pcie);
	if (reason == SLSI_IRQ_SET_BIT)
		atomic_inc(&hip_priv->pcie_claim_intrbit_set_cnt);
	else if (reason == SLSI_IRQ_MASK_BIT)
		atomic_inc(&hip_priv->pcie_claim_intrbit_mask_cnt);
	else if (reason == SLSI_IRQ_UNMASK_BIT)
		atomic_inc(&hip_priv->pcie_claim_intrbit_unmask_cnt);
	else
		atomic_inc(&hip_priv->pcie_claim_non_irq_cnt);

	if (!test_bit(0, hip_priv->pcie_claim_bit)) {
		read_unlock_bh(&hip->hip_priv->rw_pcie);
		SLSI_DBG1(sdev, SLSI_HIP, "PCIE claimed by user\n");
		slsi_print_pcie_lock_cnt(sdev, hip_priv);
		queue_work(hip_priv->pcie_wq, &hip_priv->pcie_ctrl.pcie_work);
		/* wait for a work to finish above queueing instance for sync. */
		flush_work(&hip_priv->pcie_ctrl.pcie_work);
	} else {
		SLSI_DBG1(sdev, SLSI_HIP, "PCIE already has claimed\n");
		slsi_print_pcie_lock_cnt(sdev, hip_priv);
		read_unlock_bh(&hip->hip_priv->rw_pcie);
	}

	return test_bit(0, hip_priv->pcie_claim_bit);
}

static void slsi_pcie_lock_atomic(struct slsi_hip *hip, enum slsi_pcie_claim_reason reason)
{
	struct hip_priv	*hip_priv	= hip->hip_priv;
	struct slsi_dev *sdev		= container_of(hip, struct slsi_dev, hip);

	if (atomic_read(&hip->is_hip_priv_invalid)) {
		SLSI_WARN_NODEV("hip_priv is null. cannot queue work\n");
		return;
	}

	if (sdev->pcie_shutdown) {
		SLSI_WARN_NODEV("pcie is shutting down. Ignore queue work\n");
		return;
	}

	if (reason == SLSI_IRQ_SET_BIT)
		atomic_inc(&hip_priv->pcie_claim_intrbit_set_cnt);
	else if (reason == SLSI_IRQ_MASK_BIT)
		atomic_inc(&hip_priv->pcie_claim_intrbit_mask_cnt);
	else if (reason == SLSI_IRQ_UNMASK_BIT)
		atomic_inc(&hip_priv->pcie_claim_intrbit_unmask_cnt);
	else
		atomic_inc(&hip_priv->pcie_claim_non_irq_cnt);

	SLSI_DBG1(sdev, SLSI_HIP, "PCIE claimed by user\n");
	slsi_print_pcie_lock_cnt(sdev, hip_priv);
}

void slsi_pcie_unlock(struct slsi_hip *hip, enum slsi_pcie_claim_reason reason)
{
	struct hip_priv	*hip_priv	= hip->hip_priv;
	struct slsi_dev *sdev		= container_of(hip, struct slsi_dev, hip);

	if (atomic_read(&hip->is_hip_priv_invalid)) {
		SLSI_WARN_NODEV("hip_priv is null. cannot queue work\n");
		return;
	}

	if (sdev->pcie_shutdown) {
		SLSI_WARN_NODEV("pcie is shutting down. Ignore queue work\n");
		return;
	}

	switch (reason) {
	case SLSI_NON_IRQ:
		if (!atomic_read(&hip_priv->pcie_claim_non_irq_cnt)) {
			SLSI_WARN(sdev, "PCIE claim / release is not in pair.\n");
		} else {
			atomic_dec(&hip_priv->pcie_claim_non_irq_cnt);
			SLSI_DBG1(sdev, SLSI_HIP, "PCIE released by user. claimed cnt: %d reason: %d\n",
				  atomic_read(&hip_priv->pcie_claim_non_irq_cnt), reason);
			if (!atomic_read(&hip_priv->pcie_claim_non_irq_cnt))
				queue_work(hip_priv->pcie_wq, &hip_priv->pcie_ctrl.pcie_work);
		}
		break;
	case SLSI_IRQ_SET_BIT:
		if (!atomic_read(&hip_priv->pcie_claim_intrbit_set_cnt)) {
			SLSI_WARN(sdev, "PCIE claim / release is not in pair.\n");
		} else {
			atomic_dec(&hip_priv->pcie_claim_intrbit_set_cnt);
			SLSI_DBG1(sdev, SLSI_HIP, "PCIE released by user. claimed cnt: %d reason: %d\n",
				  atomic_read(&hip_priv->pcie_claim_intrbit_set_cnt), reason);
			if (!atomic_read(&hip_priv->pcie_claim_intrbit_set_cnt))
				queue_work(hip_priv->pcie_wq, &hip_priv->pcie_ctrl.pcie_work);
		}
		break;
	case SLSI_IRQ_MASK_BIT:
		if (!atomic_read(&hip_priv->pcie_claim_intrbit_mask_cnt)) {
			SLSI_WARN(sdev, "PCIE claim / release is not in pair.\n");
		} else {
			atomic_dec(&hip_priv->pcie_claim_intrbit_mask_cnt);
			SLSI_DBG1(sdev, SLSI_HIP, "PCIE released by user. claimed cnt: %d reason: %d\n",
				  atomic_read(&hip_priv->pcie_claim_intrbit_mask_cnt), reason);
			if (!atomic_read(&hip_priv->pcie_claim_intrbit_mask_cnt))
				queue_work(hip_priv->pcie_wq, &hip_priv->pcie_ctrl.pcie_work);
		}
		break;
	case SLSI_IRQ_UNMASK_BIT:
		if (!atomic_read(&hip_priv->pcie_claim_intrbit_unmask_cnt)) {
			SLSI_WARN(sdev, "PCIE claim / release is not in pair.\n");
		} else {
			atomic_dec(&hip_priv->pcie_claim_intrbit_unmask_cnt);
			SLSI_DBG1(sdev, SLSI_HIP, "PCIE released by user. claimed cnt: %d reason: %d\n",
				  atomic_read(&hip_priv->pcie_claim_intrbit_unmask_cnt), reason);
			if (!atomic_read(&hip_priv->pcie_claim_intrbit_unmask_cnt))
				queue_work(hip_priv->pcie_wq, &hip_priv->pcie_ctrl.pcie_work);
		}
		break;
	default:
		SLSI_WARN(sdev, "Invalid reason: %d\n", reason);
	}
}

void slsi_pcie_claim_release(struct work_struct *work)
{
	struct pcie_control *pcie_ctrl = container_of(work, struct pcie_control, pcie_work);
	struct hip_priv     *hip_priv  = pcie_ctrl->hip_priv;
	struct slsi_hip	    *hip     = hip_priv->hip;
	struct slsi_dev     *sdev     = container_of(hip, struct slsi_dev, hip);
	u16 reason;
	u32 i = 0;
	u32 retry_cnt = 0;

	read_lock_bh(&hip_priv->pcie_ctrl_reason);
	reason = pcie_ctrl->reason;
	pcie_ctrl->reason = 0;
	read_unlock_bh(&hip_priv->pcie_ctrl_reason);

	if (reason) {
		if (!test_bit(0, hip_priv->pcie_claim_bit)) {
			while (scsc_mx_service_claim(WLAN_HOST)) {
				if (++ retry_cnt >= 3) {
					/* If you want to delete triggering fw panic,
					 * then bitmap should be restored.
					 */
					SLSI_ERR_NODEV("Failed to claim PCIE. claim reason:%d\n", reason);
					return;
				}
				udelay(hip5_pcie_claim_delay * 1000);
			}
			set_bit(0, hip_priv->pcie_claim_bit);
		}
		if (reason & (1U << SLSI_IRQ_SET_BIT)) {
			/* Check all of the fromhost_intr_bit bitmap and if set, execute interrupt, and clear bitmap.
			 * pcie_claim_bit should be checked before interrupt since pcie should be claimed.
			 */
			while ((i = find_first_bit(hip_priv->fromhost_intr_bit, FROMHOST_INTR_NUM)) != FROMHOST_INTR_NUM) {
				/* Interrupt clear */
				clear_bit(i, hip_priv->fromhost_intr_bit);
				/* Execute interrupt */
				scsc_service_mifintrbit_bit_set(sdev->service, i + hip->hip_priv->intr_from_host_ctrl,
								SCSC_MIFINTR_TARGET_WLAN);
			}
		}
		if (reason & (1U << SLSI_IRQ_MASK_BIT)) {
			/* Check all of the tohost_intr_mask_bit bitmap and if set, mask/unmask interrupt, and clear bitmap.
			 * pcie_claim_bit should be checked before unmasking/masking interrupt bit since pcie should be claimed.
			 */
			while ((i = find_first_bit(hip_priv->tohost_intr_mask_bit, TOHOST_INTR_NUM)) != TOHOST_INTR_NUM) {
				/* Interrupt clear */
				clear_bit(i, hip_priv->tohost_intr_mask_bit);
				/* Mask interrupt */
				scsc_service_mifintrbit_bit_mask(sdev->service, i + hip->hip_priv->intr_to_host_ctrl);
			}
		}
		if (reason & (1U << SLSI_IRQ_UNMASK_BIT)) {
			/* Check all of the tohost_intr_unmask_bit bitmap and if set, mask/unmask interrupt, and clear bitmap.
			 * pcie_claim_bit should be checked before unmasking/masking interrupt bit since pcie should be claimed.
			 */
			while ((i = find_first_bit(hip_priv->tohost_intr_unmask_bit, TOHOST_INTR_NUM)) != TOHOST_INTR_NUM) {
				/* Interrupt clear */
				clear_bit(i, hip_priv->tohost_intr_unmask_bit);
				/* Execute interrupt */
				scsc_service_mifintrbit_bit_unmask(sdev->service, i + hip->hip_priv->intr_to_host_ctrl);
			}
		}
	}
	/* If user function 'slsi_pcie_lock' called when PCIE is not claimed, then claim PCIE. */
	if ((atomic_read(&hip_priv->pcie_claim_non_irq_cnt) ||
	     atomic_read(&hip_priv->pcie_claim_intrbit_mask_cnt) ||
	     atomic_read(&hip_priv->pcie_claim_intrbit_unmask_cnt) ||
	     atomic_read(&hip_priv->pcie_claim_intrbit_set_cnt)) && !test_bit(0, hip_priv->pcie_claim_bit)) {
		while (scsc_mx_service_claim(WLAN_HOST)) {
			if (++ retry_cnt >= 3) {
				SLSI_ERR_NODEV("Failed to claim PCIE. cnts:%d, %d, %d, %d\n",
						atomic_read(&hip_priv->pcie_claim_non_irq_cnt),
						atomic_read(&hip_priv->pcie_claim_intrbit_mask_cnt),
						atomic_read(&hip_priv->pcie_claim_intrbit_unmask_cnt),
						atomic_read(&hip_priv->pcie_claim_intrbit_set_cnt));
				return;
			}
			udelay(hip5_pcie_claim_delay * 1000);
		}
		set_bit(0, hip_priv->pcie_claim_bit);
	}

	/* PCIE release when pcie claim count is zero */
	write_lock_bh(&hip->hip_priv->rw_pcie);
	if (!atomic_read(&hip_priv->pcie_claim_non_irq_cnt) &&
	    !atomic_read(&hip_priv->pcie_claim_intrbit_set_cnt) &&
	    !atomic_read(&hip_priv->pcie_claim_intrbit_mask_cnt) &&
	    !atomic_read(&hip_priv->pcie_claim_intrbit_unmask_cnt)) {
		if (test_bit(0, hip_priv->pcie_claim_bit)) {
			scsc_mx_service_release(WLAN_HOST);
			clear_bit(0, hip_priv->pcie_claim_bit);
		}
	}
	write_unlock_bh(&hip->hip_priv->rw_pcie);
}

static void pcie_traffic_monitor_cb(void *client_ctx, u32 state, u32 tput_tx, u32 tput_rx)
{
	struct hip_priv *hip_priv = (struct hip_priv *)client_ctx;
	struct slsi_hip *hip = NULL;
	struct slsi_dev *sdev = NULL;
	u32 old_state;

	if (!hip_priv || !hip_priv->hip) {
		SLSI_WARN_NODEV("invalid HIP instance\n");
		return;
	}
	hip = hip_priv->hip;

	if (atomic_read(&hip->is_hip_priv_invalid)) {
		SLSI_WARN_NODEV("hip_priv is null. cannot queue work\n");
		return;
	}

	if (!hip->hip_priv) {
		SLSI_WARN_NODEV("hip_priv leak is detected\n");
		return;
	}

	sdev = container_of(hip, struct slsi_dev, hip);
	if (!sdev)
		return;

	old_state = hip->hip_priv->pcie_ctrl.state;

	SLSI_DBG3(sdev, SLSI_HIP, "traffic monitor: event (current:%d new:%u, tput_tx:%u Mbps, tput_rx:%u Mbps)\n",
		  old_state,
		  state,
		  SLSI_TRAFFIC_MON_BYTES_TO_MBPS(tput_tx),
		  SLSI_TRAFFIC_MON_BYTES_TO_MBPS(tput_rx));

	if (state >= TRAFFIC_MON_CLIENT_STATE_HIGH && state <= TRAFFIC_MON_CLIENT_STATE_OVERRIDE) {
		/* To avoid duplicated pcie lock if state changed high to override or vice versa */
		if (!IS_BIDIRECTIONAL_HIGH_OVERRIDE_STATE_TRANSITION(state, old_state))
			slsi_pcie_lock_atomic(hip, SLSI_IRQ_SET_BIT);
	} else
		slsi_pcie_unlock(hip, SLSI_IRQ_SET_BIT);

	hip->hip_priv->pcie_ctrl.state = state;
}
#endif

#if IS_ENABLED(CONFIG_SCSC_LOGRING)
static void hip5_traffic_monitor_logring_cb(void *client_ctx, u32 state, u32 tput_tx, u32 tput_rx)
{
	struct hip_priv *hip_priv = (struct hip_priv *)client_ctx;
	struct slsi_hip *hip = NULL;
	struct slsi_dev *sdev = NULL;

	if (!hip_priv || !hip_priv->hip) {
		SLSI_WARN_NODEV("invalid HIP instance\n");
		return;
	}
	hip = hip_priv->hip;

	/**
	 * FIXME: slsi_hip_init is called without slsi_hip_deinit in some case.
	 * Now, old hip_priv is dangling pointer and its callback still live in traffic_monitor.
	 * In this case, old hip_priv->hip will point the valid hip as hip is in slsi_dev
	 * and its address never changes as long as sdev is not re-allocated.
	 *
	 * Hence, (client_ctx = old_hip_priv)->hip->hip_priv can be null.
	 * We should check, therefore, hip->hip_priv if none null.
	 * This is temporal WA until we find why hip_priv is not properly
	 * freed and re-allocated by slsi_hip_deinit
	 */
	if (!hip->hip_priv) {
		SLSI_WARN_NODEV("hip_priv leak is detected\n");
		return;
	}

	sdev = container_of(hip, struct slsi_dev, hip);
	if (!sdev)
		return;

	SLSI_DBG1(sdev, SLSI_HIP, "event (state:%u, tput_tx:%u Mbps, tput_rx:%u Mbps)\n",
		state,
		SLSI_TRAFFIC_MON_BYTES_TO_MBPS(tput_tx),
		SLSI_TRAFFIC_MON_BYTES_TO_MBPS(tput_rx));
	if (state == TRAFFIC_MON_CLIENT_STATE_HIGH) {
		if (hip4_dynamic_logging) {
			scsc_logring_enable(false);
		}
	} else if (state == TRAFFIC_MON_CLIENT_STATE_MID || state == TRAFFIC_MON_CLIENT_STATE_OVERRIDE) {
		if (hip4_dynamic_logging) {
			/* turn off WLAN logring only */
			slsi_dbg_on_off(false);
		}
	} else {
		slsi_dbg_on_off(true);
		scsc_logring_enable(true);
	}
}
#endif

static int slsi_hip_init_control_table(struct slsi_dev *sdev, struct slsi_hip *hip, void *hip_ptr,
				       struct hip5_hip_control *hip_control, struct scsc_service *service)
{
	scsc_mifram_ref ref, ref_scoreboard;
	u32             total_mib_len;
	u32             mib_file_offset;
	int             i = 0;
	uintptr_t       scrb_ref = 0;

#if defined(CONFIG_SCSC_PCIE_PAEAN_X86) || defined(CONFIG_SOC_S5E9925)
	/* Initialize scoreboard */
	if (hip5_scoreboard_in_ramrp) {
		hip->hip_priv->scbrd_ramrp_base = scsc_mx_service_get_ramrp_ptr(sdev->service);

		/* let firmware know scoreboard is in RAMRP, by setting the LSB of scoreboard location field */
		scrb_ref = scrb_ref | 0x1;

	} else {
		if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->scoreboard, &ref_scoreboard))
			return -EFAULT;
	}
#else
	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->scoreboard, &ref_scoreboard))
		return -EFAULT;
#endif

	/* Calculate total space used by wlan*.hcf files */
	for (i = 0, total_mib_len = 0; i < SLSI_WLAN_MAX_MIB_FILE; i++)
		total_mib_len += sdev->mib[i].mib_len;

	/* Copy MIB content in shared memory if any */
	/* Clear the area to avoid picking up old values */
	memset(hip_ptr + HIP5_WLAN_MIB_OFFSET, 0, HIP5_WLAN_MIB_SIZE);

	if (total_mib_len && total_mib_len < HIP5_WLAN_MIB_SIZE) {
		SLSI_INFO_NODEV("Loading MIB into shared memory, size (%d)\n", total_mib_len);
		/* Load each MIB file into shared DRAM region */
		for (i = 0, mib_file_offset = 0;
		     i < SLSI_WLAN_MAX_MIB_FILE && sdev->mib[i].mib_file_name;
		     i++) {
			SLSI_INFO_NODEV("Loading MIB %d into shared memory, offset (%d), size (%d), total (%d)\n",
					i, mib_file_offset, sdev->mib[i].mib_len, total_mib_len);
			if (sdev->mib[i].mib_len) {
				memcpy((u8 *)hip_ptr + HIP5_WLAN_MIB_OFFSET + mib_file_offset, sdev->mib[i].mib_data,
				       sdev->mib[i].mib_len);
				mib_file_offset += sdev->mib[i].mib_len;
			}
		}
		hip_control->config_v4.mib_loc      = hip->hip_ref + HIP5_WLAN_MIB_OFFSET;
		hip_control->config_v4.mib_sz       = total_mib_len;
		hip_control->config_v5.mib_loc      = hip->hip_ref + HIP5_WLAN_MIB_OFFSET;
		hip_control->config_v5.mib_sz       = total_mib_len;
	} else {
		SLSI_ERR_NODEV("MIB size (%d), is bigger than the MIB AREA (%d) or 0. Aborting memcpy\n",
			       total_mib_len, HIP5_WLAN_MIB_SIZE);
		hip_control->config_v4.mib_loc      = 0;
		hip_control->config_v4.mib_sz       = 0;
		hip_control->config_v5.mib_loc      = 0;
		hip_control->config_v5.mib_sz       = 0;
		total_mib_len = 0;
	}

	/* Initialize hip_control table for version 4 */
	/***** VERSION 4 *******/
	hip_control->config_v4.magic_number    = 0xcaba0401;
	hip_control->config_v4.hip_config_ver  = 4;
	hip_control->config_v4.config_len      = sizeof(struct hip5_hip_config_version_1);
	hip_control->config_v4.host_cache_line = 64;
	hip_control->config_v4.host_buf_loc    = hip->hip_ref + HIP5_WLAN_TX_OFFSET;
	hip_control->config_v4.host_buf_sz     = HIP5_WLAN_TX_SIZE;
	hip_control->config_v4.fw_buf_loc      = hip->hip_ref + HIP5_WLAN_RX_OFFSET;
	hip_control->config_v4.fw_buf_sz       = HIP5_WLAN_RX_SIZE;
	hip_control->config_v4.log_config_loc  = 0;

	if (hip5_scoreboard_in_ramrp)
		hip_control->config_v4.scbrd_loc = (u32)scrb_ref;
	else
		hip_control->config_v4.scbrd_loc = (u32)ref_scoreboard;

	hip_control->config_v4.q_num = 24;

	/* Initialize q relative positions */
	for (i = 0; i < MIF_HIP_CFG_Q_NUM; i++) {
		hip_control->config_v4.q_cfg[i].q_len = MAX_NUM;
		hip_control->config_v4.q_cfg[i].q_idx_sz = 2;
		if (i > 13) {
			hip_control->config_v4.q_cfg[i].q_entry_sz = 64;
		} else {
			hip_control->config_v4.q_cfg[i].q_entry_sz = 4;
			if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q[i].array, &ref))
				return -EFAULT;
		}
		hip_control->config_v4.q_cfg[i].q_loc = (u32)ref;
	}

	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q_tlv[0].array, &ref))
		return -EFAULT;
	hip_control->config_v4.q_cfg[HIP5_MIF_Q_FH_DAT0].q_loc = (u32)ref;

	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q_tlv[1].array, &ref))
		return -EFAULT;
	hip_control->config_v4.q_cfg[HIP5_MIF_Q_FH_DAT1].q_loc = (u32)ref;

	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q_tlv[2].array, &ref))
		return -EFAULT;

	hip_control->config_v4.q_cfg[HIP5_MIF_Q_TH_DAT0].q_loc = (u32)ref;
	hip_control->config_v4.q_cfg[HIP5_MIF_Q_TH_DAT0].q_entry_sz = 64;

	/* initialize interrupts */
	hip_control->config_v4.q_cfg[HIP5_MIF_Q_FH_CTRL].int_n = hip->hip_priv->intr_from_host_ctrl;
	hip_control->config_v4.q_cfg[HIP5_MIF_Q_TH_RFBC].int_n = hip->hip_priv->intr_from_host_ctrl;
	hip_control->config_v4.q_cfg[HIP5_MIF_Q_TH_RFBD0].int_n = hip->hip_priv->intr_from_host_ctrl;
	hip_control->config_v4.q_cfg[HIP5_MIF_Q_TH_CTRL].int_n = hip->hip_priv->intr_to_host_ctrl;
	hip_control->config_v4.q_cfg[HIP5_MIF_Q_FH_RFBC].int_n = hip->hip_priv->intr_to_host_ctrl_fb;

	for (i = HIP5_MIF_Q_FH_DAT0; i < MIF_HIP_CFG_Q_NUM; i++)
		hip_control->config_v4.q_cfg[i].int_n = hip->hip_priv->intr_from_host_data;

	hip_control->config_v4.q_cfg[HIP5_MIF_Q_TH_DAT0].int_n = hip->hip_priv->intr_to_host_data1;
	hip_control->config_v4.q_cfg[HIP5_MIF_Q_TH_DAT1].int_n = hip->hip_priv->intr_to_host_data2;

	/***** END VERSION 4 *******/

	/* Initialize hip_control table for version 5 */

	/***** VERSION 5 *******/
	hip_control->config_v5.magic_number = 0xcaba0401;
	hip_control->config_v5.hip_config_ver = 5;
	hip_control->config_v5.config_len = sizeof(struct hip5_hip_config_version_2);
	hip_control->config_v5.host_cache_line = 64;
	hip_control->config_v5.host_buf_loc = hip->hip_ref + HIP5_WLAN_TX_OFFSET;
	if (hip5_tx_zero_copy) {
		hip_control->config_v5.host_buf_sz	= HIP5_WLAN_ZERO_COPY_TX_SIZE;
#if defined(CONFIG_WLBT_RX_CACHEABLE)
		hip_control->config_v5.fw_buf_loc	= (16 * 1024 * 1024);
		hip_control->config_v5.fw_buf_sz	= (4 * 1024 * 1024);
#else
		hip_control->config_v5.fw_buf_loc	= hip->hip_ref + HIP5_WLAN_ZERO_COPY_RX_OFFSET;
		hip_control->config_v5.fw_buf_sz	= HIP5_WLAN_ZERO_COPY_RX_SIZE;
#endif
	} else {
		hip_control->config_v5.host_buf_sz	= HIP5_WLAN_TX_SIZE;
		hip_control->config_v5.fw_buf_loc	= hip->hip_ref + HIP5_WLAN_RX_OFFSET;
		hip_control->config_v5.fw_buf_sz	= HIP5_WLAN_RX_SIZE;
	}
	hip_control->config_v5.log_config_loc = 0;

	if (hip5_scoreboard_in_ramrp) {
		hip_control->config_v5.scbrd_loc = (u32)scrb_ref;
	} else {
		hip_control->config_v5.scbrd_loc = (u32)ref_scoreboard;
	}

	hip_control->config_v5.q_num = 24;

	/* Initialize q relative positions */
	for (i = 0; i < MIF_HIP_CFG_Q_NUM; i++) {
		hip_control->config_v5.q_cfg[i].q_len = MAX_NUM;
		hip_control->config_v5.q_cfg[i].q_idx_sz = 2;
		if (i > HIP5_MIF_Q_TH_RFBD1) {
			hip_control->config_v5.q_cfg[i].q_entry_sz = 64;
		} else {
			hip_control->config_v5.q_cfg[i].q_entry_sz = 4;
		}
		if (i < HIP5_MIF_Q_FH_DAT0) {
			if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q[i].array, &ref)) {
				return -EFAULT;
			}
		}
		hip_control->config_v5.q_cfg[i].q_loc = (u32)ref;
	}

	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q_tlv[0].array, &ref)) {
		return -EFAULT;
	}
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_FH_DAT0].q_loc = (u32)ref;

	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q_tlv[1].array, &ref)) {
		return -EFAULT;
	}
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_FH_DAT1].q_loc = (u32)ref;

	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q_tlv[2].array, &ref)) {
			return -EFAULT;
	}
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_TH_DAT0].q_loc = (u32)ref;
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_TH_DAT0].q_entry_sz = 64;

	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q_tlv[3].array, &ref)) {
			return -EFAULT;
	}
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_FH_CTRL].q_loc = (u32)ref;
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_FH_CTRL].q_entry_sz = 64;

	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->q_tlv[4].array, &ref)) {
			return -EFAULT;
	}
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_TH_CTRL].q_loc = (u32)ref;
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_TH_CTRL].q_entry_sz = 64;

	/* initialize interrupts */
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_FH_CTRL].int_n = hip->hip_priv->intr_from_host_ctrl;
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_TH_RFBC].int_n = hip->hip_priv->intr_from_host_ctrl;
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_TH_RFBD0].int_n = hip->hip_priv->intr_from_host_ctrl;
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_TH_CTRL].int_n = hip->hip_priv->intr_to_host_ctrl;
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_FH_RFBC].int_n = hip->hip_priv->intr_to_host_ctrl_fb;

	for (i = HIP5_MIF_Q_FH_DAT0; i < MIF_HIP_CFG_Q_NUM; i++) {
		hip_control->config_v5.q_cfg[i].int_n = hip->hip_priv->intr_from_host_data;
	}

	hip_control->config_v5.q_cfg[HIP5_MIF_Q_TH_DAT0].int_n = hip->hip_priv->intr_to_host_data1;
	hip_control->config_v5.q_cfg[HIP5_MIF_Q_TH_DAT1].int_n = hip->hip_priv->intr_to_host_data2;

#ifdef CONFIG_SCSC_WLAN_HOST_DPD
	if (hip5_tx_zero_copy) {
		hip_control->config_v5.dpd_buf_loc = hip->hip_ref + HIP5_WLAN_ZERO_COPY_DPD_BUF_OFFSET;
		hip_control->config_v5.dpd_buf_sz = HIP5_WLAN_ZERO_COPY_DPD_BUF_SIZE;
		slsi_wlan_dpd_mmap_set_buffer(sdev, (hip_ptr + HIP5_WLAN_ZERO_COPY_DPD_BUF_OFFSET), HIP5_WLAN_ZERO_COPY_DPD_BUF_SIZE);
	} else {
		hip_control->config_v5.dpd_buf_loc = hip->hip_ref + HIP5_WLAN_DPD_BUF_OFFSET;
		hip_control->config_v5.dpd_buf_sz = HIP5_WLAN_DPD_BUF_SIZE;
		slsi_wlan_dpd_mmap_set_buffer(sdev, (hip_ptr + HIP5_WLAN_DPD_BUF_OFFSET), HIP5_WLAN_DPD_BUF_SIZE);
	}
	hip_control->config_v5.intr_from_host_dpd = hip->hip_priv->intr_from_host_dpd;
	hip_control->config_v5.intr_to_host_dpd = hip->hip_priv->intr_to_host_dpd;
#endif
	for (i = 0; i < HIP5_CONFIG_RESERVED_BYTES_NUM; i++)
		hip_control->config_v5.reserved[i] = 0;
	/***** END VERSION 5 *******/

	/* Initialzie hip_init configuration */
	hip_control->init.magic_number = HIP5_CONFIG_INIT_MAGIC_NUM;
	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->config_v4, &ref))
		return -EFAULT;
	hip_control->init.version_a_ref = ref;

	if (scsc_mx_service_mif_ptr_to_addr(service, &hip_control->config_v5, &ref))
		return -EFAULT;
	hip_control->init.version_b_ref = ref;
	/* End hip_init configuration */
	return 0;
}

#ifdef CONFIG_SCSC_WLAN_HOST_DPD
int slsi_hip_from_host_dpd_intr_set(struct scsc_service *service, struct slsi_hip *hip)
{
	if (!hip || !service || !hip->hip_priv) {
		SLSI_WARN_NODEV("invalid HIP instance\n");
		return -ENODEV;
	}
	slsi_ctrl_intrbit(hip->hip_priv->intr_from_host_dpd, hip, SLSI_IRQ_SET_BIT);
	return 0;
}

static void hip5_irq_handler_dpd(int irq, void *data) {
	struct slsi_hip    *hip = (struct slsi_hip *)data;
	struct slsi_dev    *sdev = container_of(hip, struct slsi_dev, hip);

	if (!hip || !sdev || !sdev->service || !hip->hip_priv) {
		SLSI_WARN_NODEV("invalid HIP instance\n");
		return;
	}
	/* Clear interrupt */
	scsc_service_mifintrbit_bit_mask(sdev->service, irq);
	scsc_service_mifintrbit_bit_clear(sdev->service, irq);

	slsi_wlan_dpd_mmap_user_space_event(SLSI_WLAN_DPD_DRV_MSG_ID_WLAN_INTR);
	scsc_service_mifintrbit_bit_unmask(sdev->service, irq);
}
#endif

int slsi_hip_init(struct slsi_hip *hip)
{
	void                    *hip_ptr;
	struct hip5_hip_control *hip_control;
	struct scsc_service     *service;
	struct slsi_dev         *sdev = container_of(hip, struct slsi_dev, hip);
	int                     ret;
	u8                      i = 0;

	if (!sdev || !sdev->service)
		return -EINVAL;

	if (!sdev->hip_priv_buffer) {
		SLSI_ERR(sdev, "invalid hip_priv instance\n");
		return -ENOMEM;
	}
	hip->hip_priv = (struct hip_priv *)sdev->hip_priv_buffer;
	memset(hip->hip_priv, 0, sizeof(struct hip_priv));

	spin_lock_bh(&hip->hip_priv->tx_lock);
	atomic_set(&hip->is_hip_priv_invalid, 0);
	spin_unlock_bh(&hip->hip_priv->tx_lock);

	/* Initially set HIP as closed state.
	 * FW can raise IRQ between scsc_service_mifintrbit_register_tohost and
	 * scsc_service_mifintrbit_bit_mask.
	 * IRQ handler returns without doing anything until closing is cleared by HIP setup.
	 */
	atomic_set(&hip->hip_priv->closing, 1);

	/* check if TX zero copy can be supported? */
	if (hip5_tx_zero_copy) {
		if (dma_set_mask_and_coherent(scsc_service_get_pcie_dev(), DMA_BIT_MASK(64)) != 0) {
			SLSI_ERR(sdev, "failed to set DMA for dev; default to non-zero copy\n");
			hip5_tx_zero_copy = false;
		}
	}
	if (hip5_tx_zero_copy)
		hip->hip_priv->tx_skb_free_cnt = hip5_tx_zero_copy_tx_slots;

	SLSI_INFO_NODEV("HIP5_WLAN_CONFIG_SIZE (%d/%d)\n", sizeof(struct hip5_hip_control), HIP5_WLAN_CONFIG_SIZE);
	SLSI_INFO_NODEV("HIP5_WLAN_MIB_SIZE (%d)\n", HIP5_WLAN_MIB_SIZE);
#ifdef CONFIG_SCSC_WLAN_HOST_DPD
	if (hip5_tx_zero_copy) {
		SLSI_INFO_NODEV("HIP5_WLAN_DPD_BUF_SIZE (%d)\n", HIP5_WLAN_ZERO_COPY_DPD_BUF_SIZE);
	} else {
		SLSI_INFO_NODEV("HIP5_WLAN_DPD_BUF_SIZE (%d)\n", HIP5_WLAN_DPD_BUF_SIZE);
	}
#endif
	SLSI_INFO_NODEV("HIP5_WLAN_TX_CTL_SIZE (%d)\n", HIP5_WLAN_TX_CTRL_SIZE);
	SLSI_INFO_NODEV("HIP5_WLAN_TX_CTL_SLOTS (%d)\n", HIP5_CTL_SLOTS);
	if (hip5_tx_zero_copy) {
		SLSI_INFO_NODEV("HIP5_WLAN_ZERO_COPY_TX_DAT_SIZE (%d)\n", HIP5_WLAN_ZERO_COPY_TX_DATA_SIZE);
		SLSI_INFO_NODEV("HIP5_WLAN_ZERO_COPY_TX_DAT_SLOTS (%d)\n", hip5_tx_zero_copy_tx_slots);
#if defined(CONFIG_WLBT_RX_CACHEABLE)
		SLSI_INFO_NODEV("HIP5_WLAN_ZERO_COPY_RX_SIZE (%d)\n", (4 * 1024 * 1024));
#else
		SLSI_INFO_NODEV("HIP5_WLAN_ZERO_COPY_RX_SIZE (%d)\n", HIP5_WLAN_ZERO_COPY_RX_SIZE);
#endif
		SLSI_INFO_NODEV("HIP5_WLAN_ZERO_COPY_TOTAL_MEM (%d)\n", HIP5_WLAN_ZERO_COPY_TOTAL_MEM);
	} else {
		SLSI_INFO_NODEV("HIP5_WLAN_TX_DAT_SIZE (%d)\n", HIP5_WLAN_TX_DATA_SIZE);
		SLSI_INFO_NODEV("HIP5_WLAN_RX_SIZE (%d)\n", HIP5_WLAN_RX_SIZE);
		SLSI_INFO_NODEV("HIP5_WLAN_TOTAL_MEM (%d)\n", HIP5_WLAN_TOTAL_MEM);
		SLSI_INFO_NODEV("HIP5_TX_DAT_SLOTS (%d)\n", HIP5_DAT_SLOTS);
	}

#ifdef CONFIG_SCSC_WLAN_HIP4_PROFILING
	hip->hip_priv->minor = hip4_sampler_register_hip(sdev->maxwell_core);
	if (hip->hip_priv->minor < SCSC_HIP4_INTERFACES) {
		SLSI_DBG1_NODEV(SLSI_HIP, "registered with minor %d\n", hip->hip_priv->minor);
		sdev->minor_prof = hip->hip_priv->minor;
	} else {
		SLSI_DBG1_NODEV(SLSI_HIP, "hip_sampler is not enabled\n");
	}
#endif

	/* Used in the workqueue */
	hip->hip_priv->hip = hip;

	service = sdev->service;

	hip->hip_priv->host_pool_id_dat = MBULK_POOL_ID_DATA;
	hip->hip_priv->host_pool_id_ctl = MBULK_POOL_ID_CTRL;

	/* hip_ref contains the reference of the start of shared memory allocated for WLAN */
	/* hip_ptr is the kernel address of hip_ref*/
	hip_ptr = scsc_mx_service_mif_addr_to_ptr(service, hip->hip_ref);

#if defined(CONFIG_WLBT_RX_CACHEABLE)
	hip->mem_start = scsc_mx_service_mif_get_mem_start(service);
#endif
#ifdef CONFIG_SCSC_WLAN_DEBUG
	if (hip5_tx_zero_copy) {
		/* Configure mbulk allocator - Data QUEUES */
		ret = mbulk_pool_add(MBULK_POOL_ID_DATA, hip_ptr + HIP5_WLAN_TX_DATA_OFFSET,
				     hip_ptr + HIP5_WLAN_TX_DATA_OFFSET + HIP5_WLAN_ZERO_COPY_TX_DATA_SIZE,
				     (HIP5_DAT_MBULK_SIZE - sizeof(struct mbulk), 5,
				     hip->hip_priv->minor);
	} else {
		/* Configure mbulk allocator - Data QUEUES */
		ret = mbulk_pool_add(MBULK_POOL_ID_DATA, hip_ptr + HIP5_WLAN_TX_DATA_OFFSET,
				     hip_ptr + HIP5_WLAN_TX_DATA_OFFSET + HIP5_WLAN_TX_DATA_SIZE,
				     (HIP5_WLAN_TX_DATA_SIZE / HIP5_DAT_SLOTS) - sizeof(struct mbulk), 5,
				     hip->hip_priv->minor);
	}

	if (ret)
		return ret;

	/* Configure mbulk allocator - Control QUEUES */
	ret = mbulk_pool_add(MBULK_POOL_ID_CTRL, hip_ptr + HIP5_WLAN_TX_CTRL_OFFSET,
			     hip_ptr + HIP5_WLAN_TX_CTRL_OFFSET + HIP5_WLAN_TX_CTRL_SIZE,
			     (HIP5_WLAN_TX_CTRL_SIZE / HIP5_CTL_SLOTS) - sizeof(struct mbulk), 0,
			     hip->hip_priv->minor);
	if (ret)
		return ret;
#else
	if (hip5_tx_zero_copy) {
		/* Configure mbulk allocator - Data QUEUES */
		ret = mbulk_pool_add(MBULK_POOL_ID_DATA, hip_ptr + HIP5_WLAN_TX_DATA_OFFSET,
				     hip_ptr + HIP5_WLAN_TX_DATA_OFFSET + HIP5_WLAN_ZERO_COPY_TX_DATA_SIZE,
				     HIP5_DAT_MBULK_SIZE - sizeof(struct mbulk), 5);
	} else {
		/* Configure mbulk allocator - Data QUEUES */
		ret = mbulk_pool_add(MBULK_POOL_ID_DATA, hip_ptr + HIP5_WLAN_TX_DATA_OFFSET,
				     hip_ptr + HIP5_WLAN_TX_DATA_OFFSET + HIP5_WLAN_TX_DATA_SIZE,
				     (HIP5_WLAN_TX_DATA_SIZE / HIP5_DAT_SLOTS) - sizeof(struct mbulk), 5);
	}
	if (ret)
		return ret;

	/* Configure mbulk allocator - Control QUEUES */
	ret = mbulk_pool_add(MBULK_POOL_ID_CTRL, hip_ptr + HIP5_WLAN_TX_CTRL_OFFSET,
			     hip_ptr + HIP5_WLAN_TX_CTRL_OFFSET + HIP5_WLAN_TX_CTRL_SIZE,
			    (HIP5_WLAN_TX_CTRL_SIZE / HIP5_CTL_SLOTS) - sizeof(struct mbulk), 0);
	if (ret)
		return ret;
#endif

	/* Reset hip_control table */
	memset(hip_ptr, 0, sizeof(struct hip5_hip_control));

	/* Reset Sample q values sending 0xff */
	SCSC_HIP4_SAMPLER_RESET(hip->hip_priv->minor);

	/***** VERSION 4 *******/
	/* TOHOST Handler allocator */
	rcu_read_lock();
	hip->hip_priv->bh_dat = slsi_lbm_register_napi(sdev,
						       hip5_napi_poll,
						       hip->hip_priv->intr_to_host_data1,
						       NP_RX_0);
#ifdef CONFIG_SLSI_WLAN_LPC
	hip->hip_priv->bh_lpc = slsi_lbm_register_workqueue(sdev, hip5_lpc_work, -1);
	if (!hip->hip_priv->bh_lpc) {
		SLSI_ERR_NODEV("Error allocating bh_lpc\n");
	}
#endif
	if (hip->hip_priv->bh_dat) {
		slsi_lbm_register_cpu_affinity_control(hip->hip_priv->bh_dat, NP_RX_0);
		slsi_lbm_register_io_saturation_control(hip->hip_priv->bh_dat);
		hip->hip_priv->bh_dat->override_timer_start = false;
		hip->hip_priv->bh_dat->start_time = 0;
	} else {
		SLSI_ERR_NODEV("Error allocating bh\n");
		rcu_read_unlock();
		return -ENOMEM;
	}

	rcu_read_unlock();

#ifdef CONFIG_SCSC_PCIE_CHIP
	rwlock_init(&hip->hip_priv->rw_pcie);
	rwlock_init(&hip->hip_priv->pcie_ctrl_reason);
	bitmap_zero(hip->hip_priv->pcie_claim_bit, LAST_CLAIM_TYPE);
	bitmap_zero(hip->hip_priv->fromhost_intr_bit, LAST_CLAIM_TYPE);
	bitmap_zero(hip->hip_priv->tohost_intr_mask_bit, LAST_CLAIM_TYPE);
	bitmap_zero(hip->hip_priv->tohost_intr_unmask_bit, LAST_CLAIM_TYPE);
	hip->hip_priv->pcie_wq = create_singlethread_workqueue("pcie_workq");
	if (!hip->hip_priv->pcie_wq) {
		SLSI_ERR_NODEV("Error creating singlethread_workqueue\n");
		return -ENOMEM;
	}
	hip->hip_priv->pcie_ctrl.hip_priv = hip->hip_priv;
	hip->hip_priv->pcie_ctrl.reason = 0;
	INIT_WORK(&hip->hip_priv->pcie_ctrl.pcie_work, slsi_pcie_claim_release);
	/* pcie claim count for user pcie locking */
	atomic_set(&hip->hip_priv->pcie_claim_non_irq_cnt, 0);
	atomic_set(&hip->hip_priv->pcie_claim_intrbit_set_cnt, 0);
	atomic_set(&hip->hip_priv->pcie_claim_intrbit_mask_cnt, 0);
	atomic_set(&hip->hip_priv->pcie_claim_intrbit_unmask_cnt, 0);
	/* register traffic monitor to control PCIE cliam/release */
	if (!slsi_traffic_mon_client_register(sdev,
					      hip->hip_priv,
					      TRAFFIC_MON_CLIENT_MODE_EVENTS,
					      SLSI_TRAFFIC_MON_MBPS_TO_BYTES_PER_SEC(hip4_qos_med_tput_in_mbps),
					      SLSI_TRAFFIC_MON_MBPS_TO_BYTES_PER_SEC(hip4_qos_max_tput_in_mbps),
					      TRAFFIC_MON_DIR_DEFAULT,
					      pcie_traffic_monitor_cb)) {
		SLSI_WARN(sdev, "failed to add PCIE client to traffic monitor\n");
	}
#endif

	/* TOHOST Handler allocator */
	ret = scsc_service_mifintrbit_register_tohost(service, hip5_irq_handler_ctrl, hip, SCSC_MIFINTR_TARGET_WLAN, HIP5_IRQ_HANDLER_CTRL_TYPE);
	if (ret < 0) {
		SLSI_ERR_NODEV("Error allocating intr_to_host_ctrl\n");
		return ret;
	}

	hip->hip_priv->intr_to_host_ctrl = ret;
	/* Mask the interrupt to prevent intr been kicked during start */
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_ctrl, hip, SLSI_IRQ_MASK_BIT);

	ret = scsc_service_mifintrbit_register_tohost(service, hip5_irq_handler_fb, hip, SCSC_MIFINTR_TARGET_WLAN, HIP5_IRQ_HANDLER_FB_TYPE);
	if (ret < 0) {
		SLSI_ERR_NODEV("Error allocating intr_to_host_ctrl_fb\n");
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		return ret;
	}

	hip->hip_priv->intr_to_host_ctrl_fb = ret;
	/* Mask the interrupt to prevent intr been kicked during start */
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_ctrl_fb, hip, SLSI_IRQ_MASK_BIT);

	ret = scsc_service_mifintrbit_register_tohost(service, hip5_irq_handler_dat, hip, SCSC_MIFINTR_TARGET_WLAN, HIP5_IRQ_HANDLER_DAT_TYPE);
	if (ret < 0) {
		SLSI_ERR_NODEV("Error allocating intr_to_host_data1\n");
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl_fb, SCSC_MIFINTR_TARGET_WLAN);
		return ret;
	}

	hip->hip_priv->intr_to_host_data1 = ret;
	/* Mask the interrupt to prevent intr been kicked during start */
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_data1, hip, SLSI_IRQ_MASK_BIT);

	ret = scsc_service_mifintrbit_register_tohost(service, hip5_irq_handler_stub, hip, SCSC_MIFINTR_TARGET_WLAN, HIP5_IRQ_HANDLER_STUB_TYPE);
	if (ret < 0) {
		SLSI_ERR_NODEV("Error allocating intr_to_host_data2\n");
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl_fb, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data1, SCSC_MIFINTR_TARGET_WLAN);
		return ret;
	}

	hip->hip_priv->intr_to_host_data2 = ret;
	/* Mask the interrupt to prevent intr been kicked during start */
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_data2, hip, SLSI_IRQ_MASK_BIT);

	/* FROMHOST Handler allocator */
	ret = scsc_service_mifintrbit_alloc_fromhost(service, SCSC_MIFINTR_TARGET_WLAN);
	if (ret < 0) {
		SLSI_ERR_NODEV("Error allocating intr_from_host_ctrl\n");
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl_fb, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data1, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data2, SCSC_MIFINTR_TARGET_WLAN);
		return ret;
	}

	hip->hip_priv->intr_from_host_ctrl = ret;

	ret = scsc_service_mifintrbit_alloc_fromhost(service, SCSC_MIFINTR_TARGET_WLAN);
	if (ret < 0) {
		SLSI_ERR_NODEV("Error allocating intr_from_host_data\n");
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl_fb, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data1, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data2, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_free_fromhost(service, hip->hip_priv->intr_from_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		return ret;
	}

	hip->hip_priv->intr_from_host_data = ret;

#ifdef CONFIG_SCSC_WLAN_HOST_DPD
	/* TOHOST interrupt for DPD */
	ret = scsc_service_mifintrbit_register_tohost(service, hip5_irq_handler_dpd, hip, SCSC_MIFINTR_TARGET_WLAN, HIP5_IRQ_HANDLER_DPD_TYPE);
	if (ret < 0) {
		SLSI_ERR_NODEV("Error allocating intr_to_host_dpd\n");
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl_fb, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data1, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data2, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_free_fromhost(service, hip->hip_priv->intr_from_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_free_fromhost(service, hip->hip_priv->intr_from_host_data, SCSC_MIFINTR_TARGET_WLAN);
		return ret;
	}

	hip->hip_priv->intr_to_host_dpd = ret;
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_dpd, hip, SLSI_IRQ_MASK_BIT);
	/* FROMHOST interrupt for DPD */
	ret = scsc_service_mifintrbit_alloc_fromhost(service, SCSC_MIFINTR_TARGET_WLAN);
	if (ret < 0) {
		SLSI_ERR_NODEV("Error allocating intr_from_host_dpd\n");
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl_fb, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data1, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data2, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_dpd, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_free_fromhost(service, hip->hip_priv->intr_from_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
		scsc_service_mifintrbit_free_fromhost(service, hip->hip_priv->intr_from_host_data, SCSC_MIFINTR_TARGET_WLAN);
		return ret;
	}

	hip->hip_priv->intr_from_host_dpd = ret;
#endif

	/* Get hip_control pointer on shared memory  */
	hip_control = (struct hip5_hip_control *)(hip_ptr + HIP5_WLAN_CONFIG_OFFSET);

	ret = slsi_hip_init_control_table(sdev, hip, hip_ptr, hip_control, service);
	if (ret)
		return ret;

	hip->hip_control = hip_control;
	hip->hip_priv->scbrd_base = &hip_control->scoreboard;

	spin_lock_init(&hip->hip_priv->rx_lock);
	atomic_set(&hip->hip_priv->in_rx, 0);
	spin_lock_init(&hip->hip_priv->tx_lock);
	atomic_set(&hip->hip_priv->in_tx, 0);

	slsi_wake_lock_init(NULL, &hip->hip_priv->wake_lock_tx, "hip_wake_lock_tx");
	slsi_wake_lock_init(NULL, &hip->hip_priv->wake_lock_ctrl, "hip_wake_lock_ctrl");
	slsi_wake_lock_init(NULL, &hip->hip_priv->wake_lock_data, "hip_wake_lock_data");

	/* Init work structs */
	hip->hip_priv->hip_workq = create_singlethread_workqueue("hip5_work");
	if (!hip->hip_priv->hip_workq) {
		SLSI_ERR_NODEV("Error creating singlethread_workqueue\n");
		return -ENOMEM;
	}
	spin_lock_init(&hip->hip_priv->napi_cpu_lock);
	hip->hip_priv->bh_ctl = slsi_lbm_register_workqueue(sdev, hip5_wq_ctrl, -1);
	hip->hip_priv->bh_rfb = slsi_lbm_register_tasklet(sdev, hip5_tl_fb, -1);
	if (!hip->hip_priv->bh_ctl || !hip->hip_priv->bh_rfb) {
		SLSI_ERR_NODEV("Error allocating bh\n");
		return -ENOMEM;
	}
	rwlock_init(&hip->hip_priv->rw_scoreboard);

	/* init Shim layer for transmit aggregation */
	for (i = 0; i < SLSI_HIP_HIP5_OPT_TX_Q_MAX; i++) {
		skb_queue_head_init(&hip->hip_priv->hip5_opt_tx_q[i].tx_q);
	}

#ifdef CONFIG_SCSC_WLAN_NAPI_PER_NETDEV
	timer_setup(&hip->hip_priv->watchdog, hip5_watchdog, 0);
#endif

#ifndef CONFIG_SCSC_WLAN_TX_API
	atomic_set(&hip->hip_priv->gmod, HIP5_DAT_SLOTS);
	atomic_set(&hip->hip_priv->gactive, 1);
	spin_lock_init(&hip->hip_priv->gbot_lock);
	hip->hip_priv->saturated = 0;
#endif
#ifdef CONFIG_SCSC_QOS
	/* setup for PM QoS */
	spin_lock_init(&hip->hip_priv->pm_qos_lock);

	if (hip4_qos_enable) {
		if (!scsc_service_pm_qos_add_request(service, SCSC_QOS_DISABLED)) {
			/* register to traffic monitor for throughput events */
			if (slsi_traffic_mon_client_register(sdev,
												hip,
												TRAFFIC_MON_CLIENT_MODE_EVENTS,
												SLSI_TRAFFIC_MON_MBPS_TO_BYTES_PER_SEC(hip4_qos_med_tput_in_mbps),
												SLSI_TRAFFIC_MON_MBPS_TO_BYTES_PER_SEC(hip4_qos_max_tput_in_mbps),
												TRAFFIC_MON_DIR_DEFAULT,
												hip5_traffic_monitor_cb))
				SLSI_WARN(sdev, "failed to add PM QoS client to traffic monitor\n");
			else
				INIT_WORK(&hip->hip_priv->pm_qos_work, hip5_pm_qos_work);
		} else {
			SLSI_WARN(sdev, "failed to add PM QoS request\n");
		}
	}
#endif
#if IS_ENABLED(CONFIG_SCSC_LOGRING)
	/* register to traffic monitor for dynamic logring logging */
	if (slsi_traffic_mon_client_register(sdev,
					 hip->hip_priv,
					 TRAFFIC_MON_CLIENT_MODE_EVENTS,
					 SLSI_TRAFFIC_MON_MBPS_TO_BYTES_PER_SEC(hip4_dynamic_logging_mid_tput_in_mbps),
					 SLSI_TRAFFIC_MON_MBPS_TO_BYTES_PER_SEC(hip4_dynamic_logging_tput_in_mbps),
					 TRAFFIC_MON_DIR_DEFAULT,
					 hip5_traffic_monitor_logring_cb))
		SLSI_WARN(sdev, "failed to add Logring client to traffic monitor\n");
#endif
	return 0;
}

/**
 * This function returns the number of free slots available to
 * transmit control packet.
 */
int slsi_hip_free_control_slots_count(struct slsi_hip *hip)
{
	return mbulk_pool_get_free_count(MBULK_POOL_ID_CTRL);
}
#if defined(CONFIG_WLBT_RX_CACHEABLE)
#define WLAN_DRAM_SIZE  (20 * 1024 * 1024)
#else
#define WLAN_DRAM_SIZE  (16 * 1024 * 1024)
#endif
#define WLAN_DRAM_START 0x90000000
#define WLAN_DRAM_END   (WLAN_DRAM_START + WLAN_DRAM_SIZE)

static bool is_skb_dma_able(struct slsi_dev *sdev, struct hip_priv *hip_priv, struct sk_buff *skb)
{
	struct slsi_skb_cb *cb = slsi_skb_cb_get(skb);
	dma_addr_t skb_dma_addr;
	u16 skb_dma_map_len = 0;
	u16 offset = skb_headroom(skb);
	u16 move_start_addr_by = 0;

	if(!hip5_tx_zero_copy)
		return false;

	/* The offset to data is signalled to Firmware but with just 1-byte.
	 * On top of it, the firmware uses only 7-bits for offset in case of MBD.
	 * So the offset value should not be greater than 254.
	 *
	 * So if the Kernel passes an SKB which has offset more than 254, then
	 * move the SKB buffer start address by 64 (or multiples of it) to make
	 * the offset in correct HIP signal range.
	 */
	for(; (offset + fapi_sig_size(ma_unitdata_req)) > SLSI_HIP_HIP5_OPT_INVALID_OFFSET; offset -= 64) {
		SLSI_DBG4(sdev, SLSI_HIP, "SKB offset correction: move_start_addr_by:%d, offset:%d\n", move_start_addr_by, offset);
		move_start_addr_by += 64;
	}

	skb_dma_map_len = (skb_headroom(skb) - move_start_addr_by) + skb_headlen(skb) + hip_priv->unidat_req_tailroom;
	skb_dma_map_len = (skb_dma_map_len + (SLSI_HIP_HIP5_OPT_DMA_LEN_ALIGN - 1)) & ~(SLSI_HIP_HIP5_OPT_DMA_LEN_ALIGN - 1);
	skb_dma_addr = dma_map_single(scsc_service_get_pcie_dev(), skb->head + move_start_addr_by, skb_dma_map_len, DMA_TO_DEVICE);
	if (dma_mapping_error(scsc_service_get_pcie_dev(), skb_dma_addr)) {
		SLSI_ERR_NODEV("DMA mapping error\n");
		return false;
	}

	/* HIP compatibility flag for RX_ZERO_COPY indicates the firmware has no
	 * restriction for SKB address.
	 * If not, then check if the SKB address is in Firmware's PCIe address range
	 * and if so, instead of sending the SKB down copy it to the Firmware
	 * buffer
	 */
	if (!(hip_priv->compat_flag & MIF_HIP_COMPAT_FLAG_RX_ZERO_COPY)) {
		if (skb_dma_addr <= WLAN_DRAM_END && skb_dma_addr >= WLAN_DRAM_START) {
			SLSI_ERR_NODEV("SKB in WLAN DRAM range:0x%lX\n", skb_dma_addr);
			dma_unmap_single(scsc_service_get_pcie_dev(), skb_dma_addr, (skb_headroom(skb) - move_start_addr_by) + skb_headlen(skb), DMA_TO_DEVICE);
			return false;
		}
	}

	cb->offset = offset;
	cb->dma_map_len = skb_dma_map_len;
	cb->skb_dma_addr = skb_dma_addr;
	return true;
}

bool hip5_opt_aggr_check(struct slsi_dev *sdev, struct slsi_hip *hip, struct sk_buff *skb)
{
	struct hip_priv     *hip_priv = hip->hip_priv;
	u8 i = 0;

	if(!is_skb_dma_able(sdev, hip_priv, skb)) {
		return false;
	}

	spin_lock_bh(&hip_priv->tx_lock);
	for (i = 0; i < SLSI_HIP_HIP5_OPT_TX_Q_MAX; i++) {
		/* find a match */
		if (fapi_get_vif(skb) == hip_priv->hip5_opt_tx_q[i].vif_index &&
			fapi_get_u16(skb, u.ma_unitdata_req.flow_id) == hip_priv->hip5_opt_tx_q[i].flow_id &&
			fapi_get_u16(skb, u.ma_unitdata_req.configuration_option) == hip_priv->hip5_opt_tx_q[i].configuration_option) {
			if (ether_addr_equal(fapi_get_buff(skb, u.ma_unitdata_req.address), hip_priv->hip5_opt_tx_q[i].addr3)) {
				skb_queue_tail(&hip_priv->hip5_opt_tx_q[i].tx_q, skb);
				spin_unlock_bh(&hip->hip_priv->tx_lock);
				return true;
			}
		}
	}
	/* find an empty place */
	for (i = 0; i < SLSI_HIP_HIP5_OPT_TX_Q_MAX; i++) {
		if (!hip_priv->hip5_opt_tx_q[i].flow_id) {
			hip_priv->hip5_opt_tx_q[i].flow_id = fapi_get_u16(skb, u.ma_unitdata_req.flow_id);
			hip_priv->hip5_opt_tx_q[i].vif_index = fapi_get_vif(skb);
			hip_priv->hip5_opt_tx_q[i].configuration_option = fapi_get_u16(skb, u.ma_unitdata_req.configuration_option);
			memcpy(hip_priv->hip5_opt_tx_q[i].addr3, fapi_get_buff(skb, u.ma_unitdata_req.address), ETH_ALEN);
			SLSI_DBG1(sdev, SLSI_HIP, "%d: added " MACSTR ", vif_index:%d, flow_id:%d\n", i,
					MAC2STR(hip_priv->hip5_opt_tx_q[i].addr3), hip_priv->hip5_opt_tx_q[i].vif_index, hip_priv->hip5_opt_tx_q[i].flow_id);
			skb_queue_tail(&hip_priv->hip5_opt_tx_q[i].tx_q, skb);
			spin_unlock_bh(&hip->hip_priv->tx_lock);
			return true;
		} else {
			/* is it stale, if so delete */
			if (ktime_to_ms(ktime_sub(ktime_get(), hip_priv->hip5_opt_tx_q[i].last_sent)) > 1 * 1000) {
				if (skb_queue_empty(&hip_priv->hip5_opt_tx_q[i].tx_q)) {
					SLSI_DBG1(sdev, SLSI_HIP, "%d: deleted " MACSTR ", vif_index:%d, flow_id:%d\n", i,
							MAC2STR(hip_priv->hip5_opt_tx_q[i].addr3),
							hip_priv->hip5_opt_tx_q[i].vif_index,
							hip_priv->hip5_opt_tx_q[i].flow_id);
					hip_priv->hip5_opt_tx_q[i].flow_id = 0;
				}
			}
		}
	}
	spin_unlock_bh(&hip->hip_priv->tx_lock);
	return false;
}

/**
 * This function is in charge to transmit a frame through the HIP.
 * It does NOT take ownership of the SKB unless it successfully transmit it;
 * as a consequence skb is NOT freed on error.
 * We return ENOSPC on queue related troubles in order to trigger upper
 * layers of kernel to requeue/retry.
 * We free ONLY locally-allocated stuff.
 *
 * the vif_index, peer_index, priority fields are valid for data packets only
 */
int hip5_opt_tx_frame(struct slsi_hip *hip, struct sk_buff *skb, bool ctrl_packet, u8 vif_index, u8 peer_index, u8 priority)
{
	struct scsc_service 	  *service;
	struct netdev_vif *ndev_vif;
	scsc_mifram_ref 		  offset = 0;
	struct mbulk			  *m = NULL;
	struct hip5_tx_dma_desc   tx_dma_desc;
	mbulk_colour			  colour = 0;
	struct slsi_dev 		  *sdev = container_of(hip, struct slsi_dev, hip);
	struct fapi_signal_header *fapi_header;
	int 					  ret = 0;
	u16 					  tot_buf_size = 0;
	size_t              payload = 0;
	struct sk_buff *skb_fapi;
	struct sk_buff *skb_udi;
	struct slsi_skb_cb *cb = slsi_skb_cb_get(skb);

	memset(&tx_dma_desc, 0, sizeof(struct hip5_tx_dma_desc));
	spin_lock_bh(&hip->hip_priv->tx_lock);

	if (atomic_read(&hip->is_hip_priv_invalid)) {
		SLSI_WARN_NODEV("hip_priv is null. stop tx process\n");
		spin_unlock_bh(&hip->hip_priv->tx_lock);
		return -EFAULT;
	}

	atomic_set(&hip->hip_priv->in_tx, 1);

	if (!slsi_wake_lock_active(&hip->hip_priv->wake_lock_tx))
		slsi_wake_lock_timeout(&hip->hip_priv->wake_lock_tx, msecs_to_jiffies(SLSI_HIP_WAKELOCK_TIME_OUT_IN_MS));

	/* clone the skb for logging */
	skb_udi = skb_clone(skb, GFP_ATOMIC);

	service = sdev->service;
	fapi_header = (struct fapi_signal_header *)skb->data;

	if (fapi_is_ma(skb)) {
		u8 vif_index_colour = vif_index;

		if (skb->dev) {
			ndev_vif = netdev_priv(skb->dev);
			if (ndev_vif->iftype != NL80211_IFTYPE_AP_VLAN)
				vif_index_colour = ndev_vif->ifnum;
		}
#ifndef CONFIG_SCSC_WLAN_TX_API
		SLSI_MBULK_COLOUR_SET(colour, vif_index_colour, peer_index, priority);
#else
		SLSI_MBULK_COLOUR_SET(colour, vif_index_colour, priority);
#endif
	}

	payload = skb->len - cb->sig_length;

	/* In Mbulk library, signal length is calculated as cb->sig_length + 4 and then
	 * rounded up to 4-byte aligned). To add some guard add 8 bytes to cb->sig_length
	 */
	tot_buf_size = payload + (cb->sig_length + 8) + hip->hip_priv->unidat_req_headroom + hip->hip_priv->unidat_req_tailroom;

	/* create a Mbulk only if the Signal has a payload (bulk data) */
	if (payload) {
		 if (!ctrl_packet && cb->skb_dma_addr) {
			SLSI_DBG4(sdev, SLSI_HIP, "SKB: dma_addr:0x%lX, len:%d, headroom:%d\n", cb->skb_dma_addr, skb->len, skb_headroom(skb));
			tx_dma_desc.len = skb->len - fapi_sig_size(ma_unitdata_req);
			tx_dma_desc.colour = colour;
			tx_dma_desc.skb = skb;
			tx_dma_desc.offset =  cb->offset + fapi_sig_size(ma_unitdata_req);
			tx_dma_desc.dma_map_len = cb->dma_map_len;
			tx_dma_desc.skb_dma_addr = cb->skb_dma_addr;
		 } else if (ctrl_packet && tot_buf_size >= mbulk_pool_seg_size(MBULK_POOL_ID_CTRL)) {
			dma_addr_t skb_dma_addr;

			tx_dma_desc.len = skb->len - cb->sig_length;
			tx_dma_desc.skb = skb;
			tx_dma_desc.offset =  skb_headroom(skb) + cb->sig_length;

			skb_dma_addr = dma_map_single(scsc_service_get_pcie_dev(), skb->head, skb_headroom(skb) + skb_headlen(skb), DMA_TO_DEVICE);
			if (dma_mapping_error(scsc_service_get_pcie_dev(), skb_dma_addr)) {
				ret = -EFAULT;
				SLSI_ERR_NODEV("DMA mapping error\n");
				goto error;
			}
			tx_dma_desc.dma_map_len = cb->dma_map_len = skb_headroom(skb) + skb_headlen(skb);
			tx_dma_desc.skb_dma_addr = cb->skb_dma_addr = skb_dma_addr;
			SLSI_DBG1(sdev, SLSI_HIP, "SKB: sig_id:0x%4X, dma_addr:0x%lX, len:%d, headroom:%d\n",
				fapi_get_sigid(skb),
				cb->skb_dma_addr,
				skb->len - cb->sig_length,
				skb_headroom(skb));
		 } else {
			m = hip5_skb_to_mbulk(hip->hip_priv, skb, ctrl_packet, colour);
			if (!m) {
				SCSC_HIP4_SAMPLER_MFULL(hip->hip_priv->minor);
				ret = -ENOSPC;
				SLSI_ERR_NODEV("mbulk is NULL\n");
				goto error;
			}

			if (scsc_mx_service_mif_ptr_to_addr(service, m, &offset) < 0) {
				mbulk_free_virt_host(m);
				ret = -EFAULT;
				SLSI_ERR_NODEV("Incorrect reference memory\n");
				goto error;
			}
		}
	}
	skb_fapi = skb_clone(skb, GFP_ATOMIC);
	if (!skb_fapi) {
		SLSI_WARN(sdev, "fail to allocate memory for FAPI SKB\n");
		ret = -ENOMEM;
		goto error;
	}
	skb_trim(skb_fapi, cb->sig_length);

	if (payload && cb->skb_dma_addr) {
		if (hip5_opt_hip_signal_add(hip, ctrl_packet ? HIP5_MIF_Q_FH_CTRL : HIP5_MIF_Q_FH_DAT0, 0, service, &tx_dma_desc, skb_fapi, true)) {
			SLSI_ERR_NODEV("No space\n");
			/* As the SKB is mapped, only unmap here, the SKB will be requeued by TX module */
			dma_unmap_single(scsc_service_get_pcie_dev(), cb->skb_dma_addr, tx_dma_desc.dma_map_len, DMA_TO_DEVICE);
			consume_skb(skb_fapi);
			ret = -ENOSPC;
			goto error;
		}
	} else {
		if (hip5_opt_hip_signal_add(hip, ctrl_packet ? HIP5_MIF_Q_FH_CTRL : HIP5_MIF_Q_FH_DAT0, offset, service, m, skb_fapi, false)) {
			if (m)
				mbulk_free_virt_host(m);

			SLSI_ERR_NODEV("No space\n");
			consume_skb(skb_fapi);
			ret = -ENOSPC;
			goto error;
		}
	}
	consume_skb(skb_fapi);

	if (ctrl_packet) {
		/* Record control signal */
		SCSC_HIP4_SAMPLER_SIGNAL_CTRLTX(hip->hip_priv->minor, (fapi_header->id & 0xff00) >> 8, fapi_header->id & 0xff);
	} else {
		SCSC_HIP4_SAMPLER_VIF_PEER(hip->hip_priv->minor, 1, vif_index, peer_index);
	}

	/* Here we push a copy of the bare skb TRANSMITTED data also to the logring
	 * as a binary record. Note that bypassing UDI subsystem as a whole
	 * means we are losing:
	 *	 UDI filtering / UDI Header INFO / UDI QueuesFrames Throttling /
	 *	 UDI Skb Asynchronous processing
	 * We keep separated DATA/CTRL paths.
	 */
#ifndef CONFIG_SCSC_USER_IMAGE
	if (ctrl_packet)
		SLSI_BIN_TAG_DEBUG(BIN_WIFI_CTRL_TX, skb_udi->data, skb_headlen(skb_udi));
	else
		SLSI_BIN_TAG_DEBUG(BIN_WIFI_DATA_TX, skb_udi->data, skb_headlen(skb_udi));
#endif
	/* slsi_log_clients_log_signal_fast: skb is copied to all the log clients */
	slsi_log_clients_log_signal_fast(sdev, &sdev->log_clients, skb_udi, SLSI_LOG_DIRECTION_FROM_HOST);

	/* Hold on to the SKB if we are DMAing it */
	if (!cb->skb_dma_addr)
		consume_skb(skb);
	consume_skb(skb_udi);
	atomic_set(&hip->hip_priv->in_tx, 0);
	spin_unlock_bh(&hip->hip_priv->tx_lock);
	return 0;
error:
	if (slsi_wake_lock_active(&hip->hip_priv->wake_lock_tx))
		slsi_wake_unlock(&hip->hip_priv->wake_lock_tx);
	consume_skb(skb_udi);
	atomic_set(&hip->hip_priv->in_tx, 0);
	spin_unlock_bh(&hip->hip_priv->tx_lock);
	return ret;
}

int slsi_hip_transmit_frame(struct slsi_hip *hip, struct sk_buff *skb, bool ctrl_packet, u8 vif_index, u8 peer_index, u8 priority)
{
	struct slsi_dev           *sdev = container_of(hip, struct slsi_dev, hip);

	if (!hip || !sdev || !sdev->service || !skb || !hip->hip_priv)
		return -EINVAL;

	if (!ctrl_packet) {
		unsigned int enqueued_time = slsi_skb_cb_get(skb)->enqueued_time_ms;
		unsigned int now = jiffies_to_msecs(jiffies);

		if (now - enqueued_time > 2000 && __ratelimit(&rs_latency)) {
			int data_offset = fapi_sig_size(ma_unitdata_req) + sizeof(struct msduhdr);

			if (skb->protocol == htons(ETH_P_IP)) {
				struct iphdr *iph = (struct iphdr *)(skb->data + data_offset);

				if (iph->protocol == IPPROTO_TCP) {
					struct tcphdr *th = (struct tcphdr *)((unsigned char *)iph + iph->ihl * 4);
					u8 *frame = (u8 *)th;
					u16 dport = frame[2] << 8 | frame[3];

					SLSI_WARN(sdev, "latency:%u dst=%pI4 TCP dport:%u seq_num:%u\n",
						now - enqueued_time, &iph->daddr, dport, th->seq);
				} else {
					SLSI_WARN(sdev, "latency:%u dst=%pI4 proto:%u\n",
						now - enqueued_time, &iph->daddr, iph->protocol);
				}
			} else if (skb->protocol == htons(ETH_P_IPV6)) {
				struct ipv6hdr *ip6h = (struct ipv6hdr *)(skb->data + data_offset);

				if (ip6h->nexthdr == IPPROTO_TCP) {
					struct tcphdr *th = (struct tcphdr *)(ip6h + sizeof(struct ipv6hdr));
					u8 *frame = (u8 *)th;
					u16 dport = frame[2] << 8 | frame[3];

					SLSI_WARN(sdev, "latency:%u dst=%pI4 TCP dport:%u seq_num:%u\n",
						now - enqueued_time, &ip6h->daddr, dport, th->seq);
				} else {
					SLSI_WARN(sdev, "latency:%u dst=%pI4 proto:%u\n",
						now - enqueued_time, &ip6h->daddr, ip6h->nexthdr);
				}
			}
		}
	}

#ifdef CONFIG_SLSI_WLAN_LPC
	if (!ctrl_packet && !slsi_lpc_is_lpc_enabled() && hip5_opt_aggr_check(sdev, hip, skb))
		return 0;
#else
	if (!ctrl_packet) {
		if (hip5_opt_aggr_check(sdev, hip, skb)) {
			/* frame is queued and will be transmit in batch */
			return 0;
		}
	}
#endif
	return hip5_opt_tx_frame(hip, skb, ctrl_packet, vif_index, peer_index, priority);
}

static inline bool hip5_opt_space_avail_in_hip_q(struct slsi_dev *sdev, u16 idx_r, u16 idx_w, u16 num_skbs)
{
	if ((idx_r == 0xFFFF) || (idx_w == 0xFFFF)) {
		SLSI_WARN(sdev, "invalid Index\n");
		return false;
	}

	if (idx_w >= MAX_NUM) {
		SLSI_WARN(sdev, "invalid Write index (idx_w:%d)\n", idx_w);
		return false;
	}

	if (idx_r == ((idx_w + 1) & (MAX_NUM - 1))) {
		SLSI_WARN(sdev, "no space to write %d BDs (idx_r:%d, idx_w:%d)\n", num_skbs, idx_r, idx_w);
		return false;
	}

	/* do we need more than 1 HIP entry */
	if (num_skbs > 3) {
		u8 cnt = 0;
		u16 idx_w_temp = (idx_w + 1) & (MAX_NUM - 1);
		for (cnt = 0; cnt < ((num_skbs - 3) > 64 ? 64 : num_skbs); cnt = cnt + 8) {
			idx_w_temp = idx_w_temp + 1;
			idx_w_temp &= (MAX_NUM - 1);
			if (idx_r == idx_w_temp) {
				SLSI_WARN(sdev, "no space to write %d BDs (idx_r:%d, idx_w:%d)\n", (num_skbs - 3) > 64 ? 64 : num_skbs, idx_r, idx_w);
				return false;
			}
		}
	}
	return true;
}

void hip5_opt_aggr_tx_frame(struct scsc_service *service, struct slsi_hip *hip)
{
	struct hip_priv 		  *hip_priv = hip->hip_priv;
	struct slsi_dev 		  *sdev = container_of(hip, struct slsi_dev, hip);
	struct hip5_hip_control   *ctrl = hip->hip_control;
	struct hip5_opt_hip_signal *hip5_q_entry = NULL;
	struct hip5_opt_bulk_desc  *bulk_desc = NULL;
	u16 idx_w;
	u16 idx_r;
	u8 num_bd = 0;
	u8 i = 0;

	if (!slsi_wake_lock_active(&hip->hip_priv->wake_lock_tx))
		slsi_wake_lock_timeout(&hip->hip_priv->wake_lock_tx, msecs_to_jiffies(SLSI_HIP_WAKELOCK_TIME_OUT_IN_MS));

	spin_lock_bh(&hip->hip_priv->tx_lock);
	for (i = 0; i < SLSI_HIP_HIP5_OPT_TX_Q_MAX; i++) {
	do {
		u16 num_skbs = skb_queue_len(&hip_priv->hip5_opt_tx_q[i].tx_q);
		if (!num_skbs)
			break;

		/* We have SKBs to send. Check if HIP queue is full before dequeing SKB */
		idx_w = hip5_read_index(hip, HIP5_MIF_Q_FH_DAT0, widx);
		idx_r = hip5_read_index(hip, HIP5_MIF_Q_FH_DAT0, ridx);

		if (!hip5_opt_space_avail_in_hip_q(sdev, idx_r, idx_w, num_skbs)) {
			SLSI_WARN(sdev, "no space in HIP queue to send signal\n");
			goto tx_done;
		}

		while ((num_bd < HIP5_OPT_NUM_BD_PER_SIGNAL_MAX) && !skb_queue_empty(&hip_priv->hip5_opt_tx_q[i].tx_q)) {
			struct sk_buff *skb;
			u8 padding = 0;

			skb = __skb_dequeue(&hip_priv->hip5_opt_tx_q[i].tx_q);
			/* log to UDI */
			if (slsi_log_clients_has_any_client(sdev, &sdev->log_clients)) {
				slsi_log_clients_log_signal_fast(sdev, &sdev->log_clients, skb, SLSI_LOG_DIRECTION_FROM_HOST);
			}

			switch (num_bd) {
			case 0:
				/* new HIP signal */
				SLSI_DBG4(sdev, SLSI_HIP, "signal: idx_r:%d, idx_w:%d, vif:%d, sig_len:%d\n", idx_r, idx_w, fapi_get_vif(skb), slsi_skb_cb_get(skb)->sig_length);
				hip5_q_entry = (struct hip5_opt_hip_signal *)&ctrl->q_tlv[0].array[idx_w];
				memset(hip5_q_entry, 0, sizeof(struct hip5_opt_hip_signal));

				/* HIP signal header */
				hip5_q_entry->sig_format = 0; /* reserved */
				hip5_q_entry->reserved = 0;   /* reserved */

				/* FAPI signal */
				hip5_q_entry->sig_len = slsi_skb_cb_get(skb)->sig_length;
				memcpy((void *)hip5_q_entry + 4, skb->data, slsi_skb_cb_get(skb)->sig_length);

				/* padding for bulk descriptor to start on 8 byte aligned */
				padding = (8 - ((slsi_skb_cb_get(skb)->sig_length + 4)	% 8)) & 0x7;
				bulk_desc = (struct hip5_opt_bulk_desc *) ((void *)hip5_q_entry + 4 + slsi_skb_cb_get(skb)->sig_length + padding);
				fallthrough;
			default:
				/* add bulk descriptor to HIP signal */
				memset(bulk_desc, 0, sizeof(struct hip5_opt_bulk_desc));
				if (hip5_opt_skb_to_bd(hip_priv, skb, bulk_desc)) {
					SLSI_WARN(sdev, "failed to add BD\n");
					skb_queue_head(&hip_priv->hip5_opt_tx_q[i].tx_q, skb);
					if (!num_bd)
						goto tx_done;
					/* if atleast one BD is created, then send the signal with already created BDs */
					goto send_signal;
				}
				num_bd++;
				SLSI_DBG4(sdev, SLSI_HIP, "BD[%d]: buf_addr:0x%lX data_len:%d, offset:%d\n", num_bd, bulk_desc->buf_addr, bulk_desc->data_len, bulk_desc->offset);
				hip5_q_entry->num_bd++;
				bulk_desc++;
				if((void *) bulk_desc == ((void *)&ctrl->q_tlv[0].array[MAX_NUM - 1] + 64)) {
					bulk_desc = (struct hip5_opt_bulk_desc *)&ctrl->q_tlv[0].array[0];
				}
				break;
			}
		}
send_signal:
		/******* Update write index in HIP queue - START *******/
		/* Memory barrier before updating shared mailbox */
		wmb();

		/* Increase index */
		idx_w++;
		idx_w &= (MAX_NUM - 1);

		/* If num_bd is more than 3, increase by 1 for every remaining 8 BDs */
		if (hip5_q_entry->num_bd > 3) {
			u8 idx = 0;

			for (idx = 0; idx < (hip5_q_entry->num_bd - 3); idx = idx + 8) {
				idx_w++;
				idx_w &= (MAX_NUM - 1);
			}
		}
		hip5_update_index(hip, HIP5_MIF_Q_FH_DAT0, widx, idx_w);
		/******* Update write index in HIP queue - END *******/

		/* reset for next HIP queue */
		hip_priv->hip5_opt_tx_q[i].last_sent = ktime_get();
		num_bd = 0;
	} while (skb_queue_len(&hip_priv->hip5_opt_tx_q[i].tx_q));
	}
tx_done:
	if (slsi_wake_lock_active(&hip->hip_priv->wake_lock_tx))
		slsi_wake_unlock(&hip->hip_priv->wake_lock_tx);
	spin_unlock_bh(&hip->hip_priv->tx_lock);
}

void slsi_hip_from_host_intr_set(struct scsc_service *service, struct slsi_hip *hip)
{
	/* aggregate frames into a single HIP signal and trigger interrupt */
	hip5_opt_aggr_tx_frame(service, hip);

	slsi_ctrl_intrbit(hip->hip_priv->intr_from_host_ctrl, hip, SLSI_IRQ_SET_BIT);
}

static int hip5_rx_zero_copy_init_skb_buffers(struct slsi_dev *sdev, struct slsi_hip *hip, struct scsc_service *service)
{
	struct hip5_hip_control *ctrl = hip->hip_control;
	struct hip_priv *hip_priv = hip->hip_priv;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	u16 idx_w = 0;
	u16 i;

	for (i = 0; i < (MAX_NUM - 1); i++) {
		/* Alloc a new SKB */
		skb = alloc_skb(HIP5_DAT_MBULK_SIZE, GFP_KERNEL | GFP_DMA);
		if (!skb) {
			SLSI_ERR_NODEV("fail to alloc SKB\n");
			return -EFAULT;
		}

		skb_reserve(skb, SLSI_NETIF_SKB_HEADROOM);
		/* Get DMA'able address of SKB */
		dma_addr = dma_map_single(scsc_service_get_pcie_dev(), skb->data, HIP5_DAT_MBULK_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(scsc_service_get_pcie_dev(), dma_addr)) {
			SLSI_ERR_NODEV("DMA mapping error\n");
			consume_skb(skb);
			return -EFAULT;
		}
		hip_priv->rx_skb_table[i].skb_dma_addr = (dma_addr >> 4) | 0x1;
		SLSI_DBG4(sdev, SLSI_HIP, "i:%d SKB:0x%lX\n", i, hip_priv->rx_skb_table[i].skb_dma_addr);
		atomic_set(&hip_priv->rx_skb_table[i].in_use, 1);
		hip_priv->rx_skb_table[i].skb = skb;

		/* Write to from-host SKB queue */
		ctrl->q[HIP5_MIF_Q_FH_SKB0].array[idx_w] = hip_priv->rx_skb_table[i].skb_dma_addr;

		/* increase Write index */
		idx_w++;
		idx_w &= (MAX_NUM - 1);
	}

	/* Memory barrier before updating shared mailbox */
	wmb();

	/* Update the scoreboard */
	hip5_update_index(hip, HIP5_MIF_Q_FH_SKB0, widx, idx_w);
	scsc_service_mifintrbit_bit_set(service, hip->hip_priv->intr_from_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
	return 0;
}

/* HIP has been initialize, setup with values
 * provided by FW
 */
int slsi_hip_setup(struct slsi_hip *hip)
{
	struct slsi_dev     *sdev = container_of(hip, struct slsi_dev, hip);
	struct scsc_service *service;
	u32 hip_config_ver = 0;

	if (!sdev || !sdev->service)
		return -EIO;

	if (atomic_read(&sdev->hip.hip_state) != SLSI_HIP_STATE_STARTED &&
	    atomic_read(&sdev->hip.hip_state) != SLSI_HIP_STATE_BLOCKED)
		return -EIO;

	service = sdev->service;

	/* Get the Version reported by the FW */
	hip_config_ver = scsc_wifi_get_hip_config_version(&hip->hip_control->init);
	SLSI_INFO(sdev, "FW HIP config version %d\n", hip_config_ver);
	/* Check if the version is supported. And get the index */
	/* This is hardcoded and may change in future versions */
	if (hip_config_ver != 5) {
		SLSI_ERR_NODEV("FW HIP config version %d not supported\n", hip_config_ver);
		return -EIO;
	}

	/* version 5 */
	hip->hip_priv->unidat_req_headroom =
		scsc_wifi_get_hip_config_version_5_u8(&sdev->hip.hip_control->config_v5, unidat_req_headroom);
	hip->hip_priv->unidat_req_tailroom =
		scsc_wifi_get_hip_config_version_5_u8(&sdev->hip.hip_control->config_v5, unidat_req_tailroom);
	hip->hip_priv->version = 5;

	if (hip5_scoreboard_in_ramrp) {
		SLSI_INFO(sdev, "RAMRP offset: 0x%4X\n", (scsc_wifi_get_hip_config_u32(&hip->hip_control, scbrd_loc, 5) & ~1));
		hip->hip_priv->scbrd_ramrp_base = hip->hip_priv->scbrd_ramrp_base + (scsc_wifi_get_hip_config_u32(&hip->hip_control, scbrd_loc, 5) & ~1);
	}

#ifdef CONFIG_SCSC_WLAN_TX_API
	if (hip5_tx_zero_copy) {
		slsi_hip_setup_post(sdev, hip5_tx_zero_copy_tx_slots);
	}
#endif
	/* Unmask interrupts - now host should handle them */
	atomic_set(&sdev->debug_inds, 0);
	atomic_set(&hip->hip_priv->closing, 0);

	/* check if Firmware is compatible with RX zero copy */
	hip->hip_priv->compat_flag = scsc_wifi_get_hip_config_version_5_u32(&sdev->hip.hip_control->config_v5, compat_flag);
	SLSI_INFO(sdev, "compat_flag:0x%X\n", hip->hip_priv->compat_flag);
	if (hip->hip_priv->compat_flag & MIF_HIP_COMPAT_FLAG_RX_ZERO_COPY) {
		if (hip5_rx_zero_copy_init_skb_buffers(sdev, hip, service)) {
			SLSI_ERR_NODEV("fail to initialize SKB buffers for RX\n");
			return -ENOMEM;
		}
	} else
		SLSI_WARN_NODEV("no support for RX zero copy!!!\n");

	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_ctrl, hip, SLSI_IRQ_UNMASK_BIT);
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_ctrl_fb, hip, SLSI_IRQ_UNMASK_BIT);
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_data1, hip, SLSI_IRQ_UNMASK_BIT);
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_data2, hip, SLSI_IRQ_UNMASK_BIT);

#ifdef CONFIG_SCSC_WLAN_HOST_DPD
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_dpd, hip, SLSI_IRQ_UNMASK_BIT);
	slsi_wlan_dpd_mmap_user_space_event(SLSI_WLAN_DPD_DRV_MSG_ID_WLAN_ON);
#endif
	SLSI_INFO_NODEV("HIP5 setup done\n");
	return 0;
}

/* On suspend hip needs to ensure that TH interrupts *are* unmasked */
void slsi_hip_suspend(struct slsi_hip *hip)
{
	struct slsi_dev *sdev;

	if (!hip || !hip->hip_priv)
		return;

	sdev = container_of(hip, struct slsi_dev, hip);

	if (atomic_read(&sdev->hip.hip_state) != SLSI_HIP_STATE_STARTED)
		return;

	memset(&sdev->suspend_tm, 0, sizeof(struct rtc_time));
	if (slsi_hip_wlan_get_rtc_time(&sdev->suspend_tm))
		SLSI_ERR_NODEV("Error reading rtc\n");

	slsi_log_client_msg(sdev, UDI_DRV_SUSPEND_IND, 0, NULL);
	SCSC_HIP4_SAMPLER_SUSPEND(hip->hip_priv->minor);

	atomic_set(&hip->hip_priv->in_suspend, 1);
}

void slsi_hip_resume(struct slsi_hip *hip)
{
	struct slsi_dev *sdev;
	if (!hip || !hip->hip_priv)
		return;

	sdev = container_of(hip, struct slsi_dev, hip);

	if (atomic_read(&sdev->hip.hip_state) != SLSI_HIP_STATE_STARTED)
		return;

	slsi_log_client_msg(sdev, UDI_DRV_RESUME_IND, 0, NULL);
	SCSC_HIP4_SAMPLER_RESUME(hip->hip_priv->minor);
	atomic_set(&hip->hip_priv->in_suspend, 0);
}

void slsi_hip_freeze(struct slsi_hip *hip)
{
	struct slsi_dev *sdev;
	struct scsc_service *service;

#ifdef CONFIG_SCSC_WLAN_HOST_DPD
	slsi_wlan_dpd_mmap_user_space_event(SLSI_WLAN_DPD_DRV_MSG_ID_WLAN_OFF);
#endif

	if (!hip || !hip->hip_priv)
		return;

	sdev = container_of(hip, struct slsi_dev, hip);
	if (!sdev || !sdev->service)
		return;

	if (atomic_read(&sdev->hip.hip_state) != SLSI_HIP_STATE_STARTED)
		return;

	service = sdev->service;

	closing = ktime_get();
	atomic_set(&hip->hip_priv->closing, 1);

	hip5_dump_dbg(hip, NULL, NULL, service);

	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_ctrl, hip, SLSI_IRQ_MASK_BIT);
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_ctrl_fb, hip, SLSI_IRQ_MASK_BIT);
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_data1, hip, SLSI_IRQ_MASK_BIT);
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_data2, hip, SLSI_IRQ_MASK_BIT);

	SLSI_INFO_NODEV("hip5 freeze done\n");
}

void slsi_hip_deinit(struct slsi_hip *hip)
{
	struct slsi_dev     *sdev;
	struct scsc_service *service;
	u16 i;

	if (!hip || !hip->hip_priv)
		return;

	sdev = container_of(hip, struct slsi_dev, hip);
	if (!sdev || !sdev->service)
		return;

	service = sdev->service;
#if IS_ENABLED(CONFIG_SCSC_LOGRING)
	slsi_traffic_mon_client_unregister(sdev, hip->hip_priv);
	/* Reenable logring in case was disabled */
	slsi_dbg_on_off(true);
	scsc_logring_enable(true);
#endif
#ifdef CONFIG_SCSC_QOS
	/* de-register with traffic monitor */
	slsi_traffic_mon_client_unregister(sdev, hip);
	scsc_service_pm_qos_remove_request(service);
#endif
#ifdef CONFIG_SCSC_BB_REDWOOD
	/* Reset TX zero copy parameter to Enable;
	 * it will be evaluated again in next WLAN service start
	 */
	hip5_tx_zero_copy = true;
#endif

	closing = ktime_get();
	atomic_set(&hip->hip_priv->closing, 1);

	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_ctrl, hip, SLSI_IRQ_MASK_BIT);
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_ctrl_fb, hip, SLSI_IRQ_MASK_BIT);
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_data1, hip, SLSI_IRQ_MASK_BIT);
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_data2, hip, SLSI_IRQ_MASK_BIT);

	slsi_lbm_unregister_bh(hip->hip_priv->bh_dat);
	slsi_lbm_unregister_bh(hip->hip_priv->bh_ctl);
	slsi_lbm_unregister_bh(hip->hip_priv->bh_rfb);

	hip->hip_priv->bh_dat = NULL;
	hip->hip_priv->bh_ctl = NULL;
	hip->hip_priv->bh_rfb = NULL;

#ifdef CONFIG_SLSI_WLAN_LPC
	slsi_lbm_unregister_bh(hip->hip_priv->bh_lpc);
	hip->hip_priv->bh_lpc = NULL;
#endif

	scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
	scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_ctrl_fb, SCSC_MIFINTR_TARGET_WLAN);
	scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data1, SCSC_MIFINTR_TARGET_WLAN);
	scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_data2, SCSC_MIFINTR_TARGET_WLAN);

	flush_workqueue(hip->hip_priv->hip_workq);
	destroy_workqueue(hip->hip_priv->hip_workq);

	spin_lock_bh(&hip->hip_priv->tx_lock);
	atomic_set(&hip->is_hip_priv_invalid, 1);
	spin_unlock_bh(&hip->hip_priv->tx_lock);

#ifdef CONFIG_SCSC_PCIE_CHIP
	flush_workqueue(hip->hip_priv->pcie_wq);
	destroy_workqueue(hip->hip_priv->pcie_wq);

	/* Check if PCIE claimed. It is possible that the last operation of the pcie wq work was pcie claim. */
	if (test_bit(0, hip->hip_priv->pcie_claim_bit)) {
		scsc_mx_service_release(WLAN_HOST);
		clear_bit(0, hip->hip_priv->pcie_claim_bit);
	}
#endif

	scsc_service_mifintrbit_free_fromhost(service, hip->hip_priv->intr_from_host_ctrl, SCSC_MIFINTR_TARGET_WLAN);
	scsc_service_mifintrbit_free_fromhost(service, hip->hip_priv->intr_from_host_data, SCSC_MIFINTR_TARGET_WLAN);

#ifdef CONFIG_SCSC_WLAN_HOST_DPD
	slsi_ctrl_intrbit(hip->hip_priv->intr_to_host_dpd, hip, SLSI_IRQ_MASK_BIT);
	scsc_service_mifintrbit_unregister_tohost(service, hip->hip_priv->intr_to_host_dpd, SCSC_MIFINTR_TARGET_WLAN);
	scsc_service_mifintrbit_free_fromhost(service, hip->hip_priv->intr_from_host_dpd, SCSC_MIFINTR_TARGET_WLAN);
	slsi_wlan_dpd_mmap_set_buffer(NULL, NULL, 0);
	slsi_wlan_dpd_mmap_user_space_event(SLSI_WLAN_DPD_DRV_MSG_ID_WLAN_OFF);
#endif

#ifdef CONFIG_SCSC_WLAN_NAPI_PER_NETDEV
	del_timer_sync(&hip->hip_priv->watchdog);
#endif

	slsi_wake_lock_destroy(&hip->hip_priv->wake_lock_tx);
	slsi_wake_lock_destroy(&hip->hip_priv->wake_lock_ctrl);
	slsi_wake_lock_destroy(&hip->hip_priv->wake_lock_data);

	/* If we get to that point with rx_lock/tx_lock claimed, trigger BUG() */
	WLBT_WARN_ON(atomic_read(&hip->hip_priv->in_tx));
	WLBT_WARN_ON(atomic_read(&hip->hip_priv->in_rx));

	/* free the SKBs for Tx and Rx */
	for (i = 0; i < MAX_NUM; i++) {
		if (atomic_read(&hip->hip_priv->rx_skb_table[i].in_use)) {
			dma_addr_t dma_addr;

			SLSI_DBG4(sdev, SLSI_HIP, "i:%d SKB:0x%lX\n", i, hip->hip_priv->rx_skb_table[i].skb_dma_addr);
			dma_addr = hip->hip_priv->rx_skb_table[i].skb_dma_addr;
			dma_addr = (dma_addr & ~0x1) << 4;
			dma_unmap_single(scsc_service_get_pcie_dev(), dma_addr, HIP5_DAT_MBULK_SIZE, DMA_FROM_DEVICE);
			consume_skb(hip->hip_priv->rx_skb_table[i].skb);
			atomic_set(&hip->hip_priv->rx_skb_table[i].in_use, 0);
		}
	}

	for (i = 0; i < SLSI_HIP_TX_ZERO_COPY_NUM_DATA_SLOTS; i++) {
		if (atomic_read(&hip->hip_priv->tx_skb_table[i].in_use)) {
			dma_addr_t dma_addr;

			SLSI_DBG4(sdev, SLSI_HIP, "i:%d SKB:0x%lX\n", i, hip->hip_priv->tx_skb_table[i].skb_dma_addr);
			dma_addr = hip->hip_priv->tx_skb_table[i].skb_dma_addr;
			dma_addr = (dma_addr & ~0x1) << 4;
			dma_unmap_single(scsc_service_get_pcie_dev(), dma_addr, hip->hip_priv->tx_skb_table[i].dma_map_len, DMA_TO_DEVICE);
			consume_skb(hip->hip_priv->tx_skb_table[i].skb);
			atomic_set(&hip->hip_priv->tx_skb_table[i].in_use, 0);
		}
	}

	hip->hip_priv = NULL;

	/* remove the pools */
	mbulk_pool_remove(MBULK_POOL_ID_DATA);
	mbulk_pool_remove(MBULK_POOL_ID_CTRL);
	SLSI_INFO_NODEV("HIP5 deinit done\n");
}
