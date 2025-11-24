#include "pci_platform.h"
#include "bus.h"
#include "debug.h"
#include <linux/of_gpio.h>

static struct cnss_msi_config msi_config = {
	.total_vectors = 32,
	.total_users = MSI_USERS,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 10, .base_vector = 3 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 13 },
		{ .name = "DP", .num_vectors = 18, .base_vector = 14 },
	},
};

static int cnss_smmu_fault_handler(struct notifier_block *nb, unsigned long action,
	void *data)
{
	struct cnss_plat_data *plat_priv =
		container_of(nb, struct cnss_plat_data, smmu_fault_nb);
	struct cnss_pci_data *pci_priv = (struct cnss_pci_data *)plat_priv->bus_priv;

	cnss_pr_err("Exynos SysMMU fault happened\n");

	pci_priv->is_smmu_fault = true;
	cnss_pci_update_status(pci_priv, CNSS_FW_DOWN);
	cnss_force_fw_assert(&pci_priv->pci_dev->dev);

	return NOTIFY_DONE;
}

static int cnss_pci_set_link_up(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv =
		cnss_bus_dev_to_plat_priv(&pci_priv->pci_dev->dev);

	return exynos_pcie_rc_poweron_speed(plat_priv->rc_num, pci_priv->def_link_speed);
}

static int cnss_pci_set_link_down(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv =
		cnss_bus_dev_to_plat_priv(&pci_priv->pci_dev->dev);

	exynos_pcie_pm_suspend(plat_priv->rc_num);
	return 0;
}

#if defined(CONFIG_SOC_S5E9945) //for root platfrom
static inline void cnss_exynos_resouce_state(struct cnss_plat_data * plat_priv)
{
	struct list_head *vreg_list = NULL;
	struct cnss_vreg_info *vreg = NULL;

	cnss_pr_err("Value of clk_req_gpio: %d\n",
		cnss_get_input_gpio_value(plat_priv, plat_priv->clk_req_gpio));

	vreg_list = &plat_priv->vreg_list;
	list_for_each_entry(vreg, vreg_list, list) {
		if (IS_ERR_OR_NULL(vreg->reg))
			continue;

		cnss_pr_err("Regulator %s is enabled: %d\n",
			vreg->cfg.name, regulator_is_enabled(vreg->reg));
	}

	return;
}
#else // for solomon platform
static void cnss_exynos_resouce_state(struct cnss_plat_data * plat_priv)
{
	struct list_head *vreg_list = NULL;
	struct cnss_vreg_info *vreg = NULL;
	struct device *dev = NULL;

	u8 dst = 0;
	int ret = 0;

	dev = &plat_priv->plat_dev->dev;
	ret = exynos_esca_read_reg(dev->of_node, 5, 2, 0x35, &dst);
	if (ret)
		cnss_pr_err("Failed to get exynos CLK state %d\n", ret);

	cnss_pr_err("Value of clk_req_gpio: %d clk state : %d\n",
	        cnss_get_input_gpio_value(plat_priv, plat_priv->clk_req_gpio), dst);

	vreg_list = &plat_priv->vreg_list;
	list_for_each_entry(vreg, vreg_list, list) {
		if (IS_ERR_OR_NULL(vreg->reg))
			continue;

		cnss_pr_err("Regulator %s is enabled: %d\n",
			vreg->cfg.name, regulator_is_enabled(vreg->reg));
	}

	return;
}
#endif

int cnss_set_pci_link(struct cnss_pci_data *pci_priv, bool link_up)
{
	int ret = 0, retry = 0;
	struct cnss_plat_data *plat_priv;
	int clk_req_gpio;

	plat_priv = pci_priv->plat_priv;
	clk_req_gpio = plat_priv->clk_req_gpio;

	cnss_pr_vdbg("%s PCI link\n", link_up ? "Resuming" : "Suspending");

	if (link_up) {
retry:
		ret = cnss_pci_set_link_up(pci_priv);
		if (ret && retry++ < LINK_TRAINING_RETRY_MAX_TIMES) {
			cnss_exynos_resouce_state(plat_priv);
			cnss_pr_dbg("Retry PCI link training #%d\n", retry);
			if (pci_priv->pci_link_down_ind)
				msleep(LINK_TRAINING_RETRY_DELAY_MS * retry);
			goto retry;
		}
	} else {
		ret = cnss_pci_set_link_down(pci_priv);
	}

	return ret;
}

static int _cnss_pci_prevent_l1(struct cnss_pci_data *pci_priv)
{
	return 0;
}

static void _cnss_pci_allow_l1(struct cnss_pci_data *pci_priv)
{
	return;
}

int cnss_pci_prevent_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	int ret;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&pci_priv->bus_lock);
	ret = __cnss_pci_prevent_l1(dev);
	mutex_unlock(&pci_priv->bus_lock);

	return ret;
}
EXPORT_SYMBOL(cnss_pci_prevent_l1);

int __cnss_pci_prevent_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	int ret;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		cnss_pr_err("PCIe link is in suspend state\n");
		return -EIO;
	}

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_err("PCIe link is down\n");
		return -EIO;
	}

	ret = _cnss_pci_prevent_l1(pci_priv);
	if (ret == -EIO) {
		cnss_pr_err("Failed to prevent PCIe L1, considered as link down\n");
		cnss_pci_link_down(dev);
	}

	return ret;
}

void cnss_pci_allow_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return;
	}

	mutex_lock(&pci_priv->bus_lock);
	__cnss_pci_allow_l1(dev);
	mutex_unlock(&pci_priv->bus_lock);
}
EXPORT_SYMBOL(cnss_pci_allow_l1);

void __cnss_pci_allow_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return;
	}

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		cnss_pr_err("PCIe link is in suspend state\n");
		return;
	}

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_err("PCIe link is down\n");
		return;
	}

	_cnss_pci_allow_l1(pci_priv);
}

int _cnss_pci_enumerate(struct cnss_plat_data *plat_priv, u32 rc_num)
{
    int ret = 0;

    cnss_pr_dbg("Trying to enumerate with PCIe RC%x\n", rc_num);
    ret = exynos_pcie_rc_poweron_speed(rc_num, 2);
    return ret;
}
int cnss_pci_assert_perst(struct cnss_pci_data *pci_priv)
{
    struct cnss_plat_data *plat_priv =
        cnss_bus_dev_to_plat_priv(&pci_priv->pci_dev->dev);

    return exynos_pcie_set_perst(plat_priv->rc_num, false);
 }
 
int cnss_pci_init_smmu(struct cnss_pci_data *pci_priv)
{
	return 0;
}

int cnss_pci_of_reserved_mem_device_init(struct cnss_pci_data *pci_priv)
{
	struct device *dev_pci = &pci_priv->pci_dev->dev;
	struct cnss_plat_data *plat_priv =
		cnss_bus_dev_to_plat_priv(dev_pci);
	int ret;

	ret = of_reserved_mem_device_init_by_idx(dev_pci,
			(&plat_priv->plat_dev->dev)->of_node, 0);
	if (ret)
		cnss_pr_err("Failed to init reserved mem device, err = %d\n",
				ret);
	if (dev_pci->cma_area)
		cnss_pr_dbg("CMA area is %s\n",
			    cma_get_name(dev_pci->cma_area));

	return ret;
}

static irqreturn_t cnss_pci_wake_handler(int irq, void *data)
{
	struct cnss_pci_data *pci_priv = data;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	enum rpm_status status;
	struct device *dev;

	pci_priv->wake_counter++;
	cnss_pr_dbg("WLAN PCI wake IRQ (%u) is asserted #%u\n",
		    pci_priv->wake_irq, pci_priv->wake_counter);

	/* Make sure abort current suspend */
	cnss_pm_stay_awake(plat_priv);
	cnss_pm_relax(plat_priv);
	/* Above two pm* API calls will abort system suspend only when
	 * plat_dev->dev->ws is initiated by device_init_wakeup() API, and
	 * calling pm_system_wakeup() is just to guarantee system suspend
	 * can be aborted if it is not initiated in any case.
	 */
	pm_system_wakeup();

	dev = &pci_priv->pci_dev->dev;
	status = dev->power.runtime_status;

	if ((cnss_pci_get_monitor_wake_intr(pci_priv) &&
	     cnss_pci_get_auto_suspended(pci_priv)) ||
	    (status == RPM_SUSPENDING || status == RPM_SUSPENDED)) {
		cnss_pci_set_monitor_wake_intr(pci_priv, false);
		cnss_pci_pm_request_resume(pci_priv);
	}

	return IRQ_HANDLED;
}

/**
 * cnss_pci_wake_gpio_init() - Setup PCI wake GPIO for WLAN
 * @pci_priv: driver PCI bus context pointer
 *
 * This function initializes WLAN PCI wake GPIO and corresponding
 * interrupt. It should be used in non-MSM platforms whose PCIe
 * root complex driver doesn't handle the GPIO.
 *
 * Return: 0 for success or skip, negative value for error
 */
int cnss_pci_wake_gpio_init(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct device *dev = &plat_priv->plat_dev->dev;
	int ret = 0;

	pci_priv->wake_gpio = of_get_named_gpio(dev->of_node,
						"wlan-pci-wake-gpio", 0);
	if (pci_priv->wake_gpio < 0)
		goto out;

	cnss_pr_dbg("Get PCI wake GPIO (%d) from device node\n",
		    pci_priv->wake_gpio);

	ret = gpio_request(pci_priv->wake_gpio, "wlan_pci_wake_gpio");
	if (ret) {
		cnss_pr_err("Failed to request PCI wake GPIO, err = %d\n",
			    ret);
		goto out;
	}

	gpio_direction_input(pci_priv->wake_gpio);
	pci_priv->wake_irq = gpio_to_irq(pci_priv->wake_gpio);

	ret = request_irq(pci_priv->wake_irq, cnss_pci_wake_handler,
			  IRQF_TRIGGER_FALLING, "wlan_pci_wake_irq", pci_priv);
	if (ret) {
		cnss_pr_err("Failed to request PCI wake IRQ, err = %d\n", ret);
		goto free_gpio;
	}

	ret = enable_irq_wake(pci_priv->wake_irq);
	if (ret) {
		cnss_pr_err("Failed to enable PCI wake IRQ, err = %d\n", ret);
		goto free_irq;
	}

	return 0;

free_irq:
	free_irq(pci_priv->wake_irq, pci_priv);
free_gpio:
	gpio_free(pci_priv->wake_gpio);
out:
	return ret;
}

void cnss_pci_wake_gpio_deinit(struct cnss_pci_data *pci_priv)
{
	if (pci_priv->wake_gpio < 0)
		return;

	disable_irq_wake(pci_priv->wake_irq);
	free_irq(pci_priv->wake_irq, pci_priv);
	gpio_free(pci_priv->wake_gpio);
}

int cnss_pci_set_link_bandwidth(struct cnss_pci_data *pci_priv,
				u16 link_speed, u16 link_width)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv =
		cnss_bus_dev_to_plat_priv(&pci_priv->pci_dev->dev);

	exynos_pcie_l1ss_ctrl(0, PCIE_L1SS_CTRL_WIFI, plat_priv->rc_num);
	ret = exynos_pcie_rc_speed_change(plat_priv->rc_num, link_speed);
	exynos_pcie_l1ss_ctrl(1, PCIE_L1SS_CTRL_WIFI, plat_priv->rc_num);

	return ret;
}

int cnss_pci_set_max_link_speed(struct cnss_pci_data *pci_priv,
				u32 rc_num, u16 link_speed)
{
	return 0;
}

void cnss_pci_update_drv_supported(struct cnss_pci_data *pci_priv)
{
    pci_priv->drv_supported = false;
}

int cnss_pci_get_msi_assignment(struct cnss_pci_data *pci_priv)
{
	pci_priv->msi_config = &msi_config;

	return 0;
}

void cnss_dereg_pci_event(struct cnss_pci_data *pci_priv)
{
	exynos_pcie_deregister_event(&pci_priv->exynos_pci_event);
	exynos_pcie_deregister_event(&pci_priv->exynos_pci_cpl_event);
}

static void cnss_pci_event_cb(struct exynos_pcie_notify *notify)
{
	struct pci_dev *pci_dev;
	struct cnss_pci_data *pci_priv;
	struct cnss_plat_data *plat_priv;

	if (!notify)
		return;

	pci_dev = notify->user;
	if (!pci_dev)
		return;

	pci_priv = cnss_get_pci_priv(pci_dev);
	if (!pci_priv)
		return;

	plat_priv = pci_priv->plat_priv;
	switch (notify->event) {
	case EXYNOS_PCIE_EVENT_CPL_TIMEOUT:
		cnss_pr_err("Received PCI CPL timeout event, link possibly down\n");
		/* Fall through, handle it as link down */
        fallthrough;
	case EXYNOS_PCIE_EVENT_LINKDOWN:
		exynos_pcie_set_perst(plat_priv->rc_num, false);
		cnss_exynos_resouce_state(plat_priv);
		cnss_pci_handle_linkdown(pci_priv);
		break;
	default:
		cnss_pr_err("Received invalid PCI event: %d\n", notify->event);
	}
}

int cnss_reg_pci_event(struct cnss_pci_data *pci_priv)
{
	int ret = 0;

	struct exynos_pcie_register_event *pci_event;
	struct exynos_pcie_register_event *pci_cpl_event;
	struct cnss_plat_data *plat_priv;

	pci_event = &pci_priv->exynos_pci_event;
	pci_event->events = EXYNOS_PCIE_EVENT_LINKDOWN;
	pci_event->user = pci_priv->pci_dev;
	pci_event->mode = EXYNOS_PCIE_TRIGGER_CALLBACK;
	pci_event->callback = cnss_pci_event_cb;

	ret = exynos_pcie_register_event(pci_event);
	if (ret) {
		cnss_pr_err("Failed to register exynos PCI event, err = %d\n",
			    ret);
		goto out;
	}

	pci_cpl_event = &pci_priv->exynos_pci_cpl_event;
	pci_cpl_event->events = EXYNOS_PCIE_EVENT_CPL_TIMEOUT;
	pci_cpl_event->user = pci_priv->pci_dev;
	pci_cpl_event->mode = EXYNOS_PCIE_TRIGGER_CALLBACK;
	pci_cpl_event->callback = cnss_pci_event_cb;

	ret = exynos_pcie_register_event(pci_cpl_event);
	if (ret) {
		cnss_pr_err("Failed to register exynos PCI CPL event, err = %d\n",
			    ret);
		exynos_pcie_deregister_event(pci_event);
	}

	plat_priv = pci_priv->plat_priv;
	plat_priv->smmu_fault_nb.notifier_call = cnss_smmu_fault_handler;
	pcie_sysmmu_add_fault_handler(&plat_priv->smmu_fault_nb);

out:
	return ret;
}

int cnss_pci_disable_pc(struct cnss_pci_data *pci_priv, bool vote)
{
    return 0;
}

int cnss_wlan_adsp_pc_enable(struct cnss_pci_data *pci_priv,
			     bool control)
{
    return 0;
}

int _cnss_pci_get_reg_dump(struct cnss_pci_data *pci_priv,
			   u8 *buf, u32 len)
{
	return 0;
}

int cnss_pci_fmd_enable(struct cnss_pci_data *pci_priv)
{
    return 0;
}

MODULE_SOFTDEP("pre: exynos-pcie-iommu");
