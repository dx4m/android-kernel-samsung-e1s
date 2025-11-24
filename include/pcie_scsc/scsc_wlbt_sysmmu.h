#ifndef __SCSC_WLBT_SYSMMU__
#define __SCSC_WLBT_SYSMMU__
#include <linux/version.h>

#if IS_ENABLED(CONFIG_EXYNOS_HSI_IOMMU)
#include <soc/samsung/exynos-hsi-iommu-exp.h>

void register_mxman_force_panic(struct iommu_domain *dom, int (*fp)(void));
void unregister_mxman_force_panic(struct iommu_domain *dom);
#else
void register_mxman_force_panic(int (*fp)(void));
#endif

#endif
