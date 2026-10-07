// SPDX-License-Identifier: GPL-2.0-only
/*
 * ROC1 UFS host controller support, ported from the 4.14 BSP.
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 * Copyright (C) 2026 Evarentha
 */

#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "ufshcd.h"
#include "ufshcd-pltfrm.h"
#include "unipro.h"

struct roc1_ufs_reset {
	struct regmap *map;
	u32 reg;
	u32 mask;
};

struct roc1_ufs_host {
	void __iomem *utp;
	void __iomem *unipro;
	void __iomem *ao;
	void __iomem *mphy;
	void __iomem *analog;
	struct roc1_ufs_reset ap;
	struct roc1_ufs_reset anlg;
	struct roc1_ufs_reset aon;
};

static void roc1_ufs_rmwl(void __iomem *base, u32 mask, u32 value, u32 offset)
{
	writel((readl(base + offset) & ~mask) | (value & mask), base + offset);
}

static int roc1_ufs_get_reset(struct device *dev, const char *name,
			    struct roc1_ufs_reset *reset)
{
	u32 args[2];

	reset->map = syscon_regmap_lookup_by_phandle_args(dev->of_node, name, 2, args);
	if (IS_ERR(reset->map))
		return dev_err_probe(dev, PTR_ERR(reset->map), "missing %s\n", name);
	reset->reg = args[0];
	reset->mask = args[1];
	return 0;
}

static int roc1_ufs_reset_pulse(struct roc1_ufs_reset *reset, bool active_low)
{
	int ret;

	ret = regmap_update_bits(reset->map, reset->reg, reset->mask,
				active_low ? 0 : reset->mask);
	if (ret)
		return ret;
	return regmap_update_bits(reset->map, reset->reg, reset->mask,
				 active_low ? reset->mask : 0);
}

static int roc1_ufs_reset(struct ufs_hba *hba)
{
	struct roc1_ufs_host *host = ufshcd_get_variant(hba);
	u32 mask;
	int ret;

	/* Preserve the ROC1 reset order and PHY programming from the BSP. */
	ret = roc1_ufs_reset_pulse(&host->anlg, true);
	if (ret)
		return ret;
	ret = roc1_ufs_reset_pulse(&host->ap, false);
	if (ret)
		return ret;
	ret = roc1_ufs_reset_pulse(&host->aon, false);
	if (ret)
		return ret;

	roc1_ufs_rmwl(host->unipro, BIT(0), BIT(0), 0x40);
	roc1_ufs_rmwl(host->unipro, BIT(0), 0, 0x40);
	roc1_ufs_rmwl(host->unipro, BIT(1), BIT(1), 0x84);
	roc1_ufs_rmwl(host->unipro, BIT(1), 0, 0x84);
	roc1_ufs_rmwl(host->unipro, BIT(4), BIT(4), 0xc0);
	roc1_ufs_rmwl(host->unipro, BIT(4), 0, 0xc0);
	roc1_ufs_rmwl(host->unipro, BIT(2), BIT(2), 0xd0);
	roc1_ufs_rmwl(host->unipro, BIT(2), 0, 0xd0);

	mask = BIT(12) | BIT(28);
	ufshcd_rmwl(hba, mask, mask, 0xb0);
	roc1_ufs_rmwl(host->utp, BIT(0) | BIT(1), 0, 0x100);
	roc1_ufs_rmwl(host->utp, BIT(0) | BIT(1), BIT(0) | BIT(1), 0x100);
	/* UTP FIFO: 512 bytes. */
	roc1_ufs_rmwl(host->utp, 0xff, 4, 0x104);
	ufshcd_rmwl(hba, mask, 0, 0xb0);
	roc1_ufs_rmwl(host->ao, BIT(1), BIT(1), 0x1c);
	roc1_ufs_rmwl(host->ao, BIT(1), 0, 0x1c);

	roc1_ufs_rmwl(host->analog, BIT(3), 0, 0x14);
	udelay(100);
	roc1_ufs_rmwl(host->analog, BIT(3), BIT(3), 0x14);
	roc1_ufs_rmwl(host->analog, 0xff, 0xff, 0x08);
	roc1_ufs_rmwl(host->mphy, BIT(15), BIT(15), 0x8c);
	roc1_ufs_rmwl(host->mphy, 0x1f << 16, BIT(16), 0xd0);
	roc1_ufs_rmwl(host->mphy, BIT(24), BIT(24), 0x1c);
	roc1_ufs_rmwl(host->mphy, 7 << 14, 4 << 14, 0x1c);

	mask = GENMASK(29, 24);
	roc1_ufs_rmwl(host->unipro, mask, mask, 0x3c);
	udelay(100);
	roc1_ufs_rmwl(host->unipro, mask, 0, 0x3c);
	roc1_ufs_rmwl(host->unipro, 0xf, 0xa, 0x18c);
	ufshcd_writel(hba, UIC_CMD_DME_HIBER_EXIT, REG_UIC_COMMAND);
	udelay(100);
	roc1_ufs_rmwl(host->unipro, BIT(0), BIT(0), 0x148);
	return 0;
}

static int roc1_ufs_init(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct roc1_ufs_host *host;
	struct resource *res;
	int ret;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;
	host->utp = devm_platform_ioremap_resource_byname(pdev, "ufsutp_reg");
	if (IS_ERR(host->utp))
		return PTR_ERR(host->utp);
	host->unipro = devm_platform_ioremap_resource_byname(pdev, "unipro_reg");
	if (IS_ERR(host->unipro))
		return PTR_ERR(host->unipro);
	host->ao = devm_platform_ioremap_resource_byname(pdev, "ufs_ao_reg");
	if (IS_ERR(host->ao))
		return PTR_ERR(host->ao);
	/* These windows are also described by ROC1 clock/syscon providers. */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mphy_reg");
	if (!res)
		return -ENODEV;
	host->mphy = devm_ioremap(dev, res->start, resource_size(res));
	if (!host->mphy)
		return -ENOMEM;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "analog_reg");
	if (!res)
		return -ENODEV;
	host->analog = devm_ioremap(dev, res->start, resource_size(res));
	if (!host->analog)
		return -ENOMEM;
	ret = roc1_ufs_get_reset(dev, "ap-apb-ufs-rst", &host->ap);
	if (ret)
		return ret;
	ret = roc1_ufs_get_reset(dev, "anlg-mphy-ufs-rst", &host->anlg);
	if (ret)
		return ret;
	ret = roc1_ufs_get_reset(dev, "aon-apb-ufs-rst", &host->aon);
	if (ret)
		return ret;
	ufshcd_set_variant(hba, host);
	hba->quirks |= UFSHCD_QUIRK_BROKEN_UFS_HCI_VERSION |
		       UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS;
	hba->caps |= UFSHCD_CAP_CLK_GATING | UFSHCD_CAP_HIBERN8_WITH_CLK_GATING;
	return 0;
}

static u32 roc1_ufs_hci_version(struct ufs_hba *hba)
{
	return 0x00000210; /* UFSHCI 2.1: ROC1 reports an incorrect version. */
}

static int roc1_ufs_hce_notify(struct ufs_hba *hba,
			       enum ufs_notify_change_status status)
{
	return status == PRE_CHANGE ? roc1_ufs_reset(hba) : 0;
}

static int roc1_ufs_link_notify(struct ufs_hba *hba,
				enum ufs_notify_change_status status)
{
	struct roc1_ufs_host *host = ufshcd_get_variant(hba);
	int ret;

	if (status != PRE_CHANGE)
		return 0;
	if (ufshcd_get_local_unipro_ver(hba) != UFS_UNIPRO_VER_1_41) {
		ret = ufshcd_dme_set(hba, UIC_ARG_MIB(PA_LOCAL_TX_LCC_ENABLE), 0);
		if (ret)
			return ret;
	}
	roc1_ufs_rmwl(host->unipro, BIT(16), 0, 0x820);
	roc1_ufs_rmwl(host->unipro, BIT(16), BIT(16), 0x1e0);
	return 0;
}

static int roc1_ufs_power_notify(struct ufs_hba *hba,
				 enum ufs_notify_change_status status,
				 struct ufs_pa_layer_attr *max,
				 struct ufs_pa_layer_attr *req)
{
	if (status != PRE_CHANGE)
		return 0;
	if (!max || !req || max->pwr_rx == SLOW_MODE || max->pwr_tx == SLOW_MODE)
		return -EINVAL;
	req->gear_rx = min_t(u32, max->gear_rx, UFS_HS_G3);
	req->gear_tx = min_t(u32, max->gear_tx, UFS_HS_G3);
	req->lane_rx = min_t(u32, max->lane_rx, 1);
	req->lane_tx = min_t(u32, max->lane_tx, 1);
	req->pwr_rx = FAST_MODE;
	req->pwr_tx = FAST_MODE;
	req->hs_rate = PA_HS_MODE_B;
	return 0;
}

static void roc1_ufs_hibern8_notify(struct ufs_hba *hba, enum uic_cmd_dme cmd,
				   enum ufs_notify_change_status status)
{
	struct roc1_ufs_host *host = ufshcd_get_variant(hba);

	if (status == PRE_CHANGE) {
		if (cmd == UIC_CMD_DME_HIBER_ENTER)
			roc1_ufs_rmwl(host->unipro, BIT(21), BIT(21), 0x1c);
		else if (cmd == UIC_CMD_DME_HIBER_EXIT)
			roc1_ufs_rmwl(host->mphy, BIT(5), 0, 0x38);
	} else if (status == POST_CHANGE) {
		if (cmd == UIC_CMD_DME_HIBER_EXIT)
			roc1_ufs_rmwl(host->unipro, BIT(21), 0, 0x1c);
		else if (cmd == UIC_CMD_DME_HIBER_ENTER)
			roc1_ufs_rmwl(host->mphy, BIT(5), BIT(5), 0x38);
	}
}

static const struct ufs_hba_variant_ops roc1_ufs_ops = {
	.name = "roc1",
	.init = roc1_ufs_init,
	.get_ufs_hci_version = roc1_ufs_hci_version,
	.hce_enable_notify = roc1_ufs_hce_notify,
	.link_startup_notify = roc1_ufs_link_notify,
	.pwr_change_notify = roc1_ufs_power_notify,
	.hibern8_notify = roc1_ufs_hibern8_notify,
};

static int roc1_ufs_probe(struct platform_device *pdev)
{
	int ret = ufshcd_pltfrm_init(pdev, &roc1_ufs_ops);

	if (!ret) {
		struct ufs_hba *hba = platform_get_drvdata(pdev);

		/* Set these after the core installs its default PM levels. */
		hba->rpm_lvl = UFS_PM_LVL_1;
		hba->spm_lvl = UFS_PM_LVL_5;
		device_enable_async_suspend(&pdev->dev);
	}
	return ret;
}

static int roc1_ufs_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba = platform_get_drvdata(pdev);
	int ret;

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return ret;
	ufshcd_remove(hba);
	pm_runtime_put_noidle(&pdev->dev);
	return 0;
}

static const struct of_device_id roc1_ufs_match[] = {
	{ .compatible = "sprd,roc1-ufshc" },
	{ }
};
MODULE_DEVICE_TABLE(of, roc1_ufs_match);

static const struct dev_pm_ops roc1_ufs_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufshcd_system_suspend, ufshcd_system_resume)
	SET_RUNTIME_PM_OPS(ufshcd_runtime_suspend, ufshcd_runtime_resume, NULL)
	.prepare = ufshcd_suspend_prepare,
	.complete = ufshcd_resume_complete,
};

static struct platform_driver roc1_ufs_driver = {
	.probe = roc1_ufs_probe,
	.remove = roc1_ufs_remove,
	.shutdown = ufshcd_pltfrm_shutdown,
	.driver = {
		.name = "ufshcd-roc1",
		.of_match_table = roc1_ufs_match,
		.pm = &roc1_ufs_pm_ops,
	},
};
module_platform_driver(roc1_ufs_driver);

MODULE_DESCRIPTION("ROC1 UFS host controller driver");
MODULE_AUTHOR("worryzu <worryzu@gmail.com> @LinearTeam");
MODULE_LICENSE("GPL v2");
