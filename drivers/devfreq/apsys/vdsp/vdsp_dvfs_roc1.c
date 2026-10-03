// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018 Spreadtrum Communications Inc. */

#include <linux/io.h>
#include <linux/of.h>
#include "sprd_dvfs_vdsp.h"
#include "../sys/apsys_dvfs_roc1.h"

static const struct vdsp_dvfs_map_cfg map_table[] = {
	{0, VOLT70, VDSP_CLK_INDEX_192M, VDSP_CLK192M, EDAP_DIV_1, M0_DIV_3},
	{1, VOLT70, VDSP_CLK_INDEX_307M2, VDSP_CLK307M2, EDAP_DIV_1, M0_DIV_3},
	{2, VOLT75, VDSP_CLK_INDEX_468M, VDSP_CLK468M, EDAP_DIV_1, M0_DIV_3},
	{3, VOLT80, VDSP_CLK_INDEX_614M4, VDSP_CLK614M4, EDAP_DIV_1, M0_DIV_3},
	{4, VOLT80, VDSP_CLK_INDEX_702M, VDSP_CLK702M, EDAP_DIV_1, M0_DIV_3},
	{5, VOLT80, VDSP_CLK_INDEX_768M, VDSP_CLK768M, EDAP_DIV_1, M0_DIV_3},
};

#define ROC1_REG(vdsp, member) \
	((void __iomem *)(vdsp)->apsys->apsys_base + \
	 offsetof(struct apsys_dvfs_reg, member))

static void roc1_update(struct vdsp_dvfs *vdsp, unsigned int offset,
			u32 mask, u32 value)
{
	void __iomem *reg = (void __iomem *)vdsp->apsys->apsys_base + offset;

	mutex_lock(&vdsp->apsys->reg_lock);
	writel((readl(reg) & ~mask) | (value & mask), reg);
	mutex_unlock(&vdsp->apsys->reg_lock);
}

#define ROC1_UPDATE(vdsp, member, mask, value) \
	roc1_update(vdsp, offsetof(struct apsys_dvfs_reg, member), mask, value)

static void vdsp_hw_dfs_en(struct vdsp_dvfs *vdsp, bool enable)
{
	ROC1_UPDATE(vdsp, ap_dfs_en_ctrl, BIT(2), enable ? BIT(2) : 0);
}

static int set_vdsp_work_index(struct vdsp_dvfs *vdsp, int index)
{
	if (index < 0 || index >= ARRAY_SIZE(map_table))
		return -EINVAL;
	writel(index, ROC1_REG(vdsp, vdsp_dvfs_index_cfg));
	return 0;
}

static int get_vdsp_work_index(struct vdsp_dvfs *vdsp)
{
	return readl(ROC1_REG(vdsp, vdsp_dvfs_index_cfg));
}

static void set_vdsp_idle_index(struct vdsp_dvfs *vdsp, int index)
{
	if (index >= 0 && index < ARRAY_SIZE(map_table))
		writel(index, ROC1_REG(vdsp, vdsp_dvfs_index_idle_cfg));
}

static int get_vdsp_idle_index(struct vdsp_dvfs *vdsp)
{
	return readl(ROC1_REG(vdsp, vdsp_dvfs_index_idle_cfg));
}

static int set_vdsp_work_freq(struct vdsp_dvfs *vdsp, u32 freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(map_table); i++)
		if (map_table[i].clk_rate == freq)
			return set_vdsp_work_index(vdsp, i);
	return -EINVAL;
}

static void set_vdsp_idle_freq(struct vdsp_dvfs *vdsp, u32 freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(map_table); i++) {
		if (map_table[i].clk_rate == freq) {
			set_vdsp_idle_index(vdsp, i);
			return;
		}
	}
}

static u32 index_to_freq(int index)
{
	if (index < 0 || index >= ARRAY_SIZE(map_table))
		return 0;
	return map_table[index].clk_rate;
}

static u32 get_vdsp_work_freq(struct vdsp_dvfs *vdsp)
{
	return index_to_freq(get_vdsp_work_index(vdsp));
}

static u32 get_vdsp_idle_freq(struct vdsp_dvfs *vdsp)
{
	return index_to_freq(get_vdsp_idle_index(vdsp));
}

static void set_vdsp_gfree_wait_delay(struct vdsp_dvfs *vdsp, u32 delay)
{
	ROC1_UPDATE(vdsp, ap_gfree_wait_delay_cfg, GENMASK(29, 20), delay << 20);
}

static void set_vdsp_freq_upd_en_byp(struct vdsp_dvfs *vdsp, bool enable)
{
	ROC1_UPDATE(vdsp, ap_freq_update_bypass, BIT(2), enable ? BIT(2) : 0);
}

static void set_vdsp_freq_upd_delay_en(struct vdsp_dvfs *vdsp, bool enable)
{
	ROC1_UPDATE(vdsp, ap_freq_upd_type_cfg, BIT(1), enable ? BIT(1) : 0);
}

static void set_vdsp_freq_upd_hdsk_en(struct vdsp_dvfs *vdsp, bool enable)
{
	ROC1_UPDATE(vdsp, ap_freq_upd_type_cfg, BIT(0), enable ? BIT(0) : 0);
}

static void set_vdsp_dvfs_swtrig_en(struct vdsp_dvfs *vdsp, bool enable)
{
	ROC1_UPDATE(vdsp, ap_sw_trig_ctrl, BIT(0), enable ? BIT(0) : 0);
}

static int get_vdsp_dvfs_table(struct ip_dvfs_map_cfg *table)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(map_table); i++) {
		table[i].map_index = map_table[i].map_index;
		table[i].volt_level = map_table[i].volt_level;
		table[i].clk_level = map_table[i].clk_level;
		table[i].clk_rate = map_table[i].clk_rate;
		table[i].volt_val = roc1_apsys_val_to_volt(map_table[i].volt_level);
	}
	return i;
}

static void get_vdsp_dvfs_status(struct vdsp_dvfs *vdsp,
				struct ip_dvfs_status *status)
{
	u32 volt, clk;

	mutex_lock(&vdsp->apsys->reg_lock);
	volt = readl(ROC1_REG(vdsp, ap_dvfs_voltage_dbg));
	clk = readl(ROC1_REG(vdsp, ap_dvfs_cgm_cfg_dbg));
	mutex_unlock(&vdsp->apsys->reg_lock);
	status->apsys_cur_volt = roc1_apsys_val_to_volt((volt >> 12) & 7);
	status->vsp_vote_volt = roc1_apsys_val_to_volt((volt >> 6) & 7);
	status->dpu_vote_volt = roc1_apsys_val_to_volt((volt >> 3) & 7);
	status->vdsp_vote_volt = roc1_apsys_val_to_volt((volt >> 9) & 7);
	status->vsp_cur_freq = roc1_vsp_val_to_freq((clk >> 3) & 3);
	status->dpu_cur_freq = roc1_dpu_val_to_freq(clk & 7);
	status->vdsp_cur_freq = roc1_vdsp_val_to_freq((clk >> 5) & 7);
	status->vdsp_edap_div = (clk >> 8) & 3;
	status->vdsp_m0_div = (clk >> 10) & 3;
}

static int vdsp_dvfs_parse_dt(struct vdsp_dvfs *vdsp, struct device_node *np)
{
	struct ip_dvfs_coffe *cfg = &vdsp->dvfs_coffe;

	cfg->gfree_wait_delay = 0x100;
	cfg->freq_upd_hdsk_en = 1;
	cfg->freq_upd_delay_en = 1;
	of_property_read_u32(np, "sprd,gfree-wait-delay", &cfg->gfree_wait_delay);
	of_property_read_u32(np, "sprd,freq-upd-hdsk-en", &cfg->freq_upd_hdsk_en);
	of_property_read_u32(np, "sprd,freq-upd-delay-en", &cfg->freq_upd_delay_en);
	of_property_read_u32(np, "sprd,freq-upd-en-byp", &cfg->freq_upd_en_byp);
	of_property_read_u32(np, "sprd,sw-trig-en", &cfg->sw_trig_en);
	return 0;
}

static int vdsp_dvfs_init(struct vdsp_dvfs *vdsp)
{
	struct ip_dvfs_coffe *cfg = &vdsp->dvfs_coffe;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(map_table); i++)
		writel(map_table[i].clk_level | (map_table[i].edap_div << 3) |
		       (map_table[i].m0_div << 5) | (map_table[i].volt_level << 7),
		       ROC1_REG(vdsp, vdsp_index0_map) + i * sizeof(u32));
	set_vdsp_gfree_wait_delay(vdsp, cfg->gfree_wait_delay);
	set_vdsp_freq_upd_hdsk_en(vdsp, cfg->freq_upd_hdsk_en);
	set_vdsp_freq_upd_delay_en(vdsp, cfg->freq_upd_delay_en);
	set_vdsp_freq_upd_en_byp(vdsp, cfg->freq_upd_en_byp);
	set_vdsp_dvfs_swtrig_en(vdsp, cfg->sw_trig_en);
	ret = set_vdsp_work_index(vdsp, cfg->work_index_def);
	if (ret || cfg->idle_index_def >= ARRAY_SIZE(map_table))
		return -EINVAL;
	set_vdsp_idle_index(vdsp, cfg->idle_index_def);
	vdsp_hw_dfs_en(vdsp, cfg->hw_dfs_en);
	return 0;
}

const struct vdsp_dvfs_ops roc1_vdsp_dvfs_ops = {
	.parse_dt = vdsp_dvfs_parse_dt,
	.dvfs_init = vdsp_dvfs_init,
	.hw_dfs_en = vdsp_hw_dfs_en,
	.set_work_freq = set_vdsp_work_freq,
	.get_work_freq = get_vdsp_work_freq,
	.set_idle_freq = set_vdsp_idle_freq,
	.get_idle_freq = get_vdsp_idle_freq,
	.set_work_index = set_vdsp_work_index,
	.get_work_index = get_vdsp_work_index,
	.set_idle_index = set_vdsp_idle_index,
	.get_idle_index = get_vdsp_idle_index,
	.get_dvfs_table = get_vdsp_dvfs_table,
	.get_dvfs_status = get_vdsp_dvfs_status,
	.set_gfree_wait_delay = set_vdsp_gfree_wait_delay,
	.set_freq_upd_en_byp = set_vdsp_freq_upd_en_byp,
	.set_freq_upd_delay_en = set_vdsp_freq_upd_delay_en,
	.set_freq_upd_hdsk_en = set_vdsp_freq_upd_hdsk_en,
	.set_dvfs_swtrig_en = set_vdsp_dvfs_swtrig_en,
};
