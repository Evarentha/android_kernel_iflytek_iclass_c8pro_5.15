/*
 * aw8898.c — Awinic AW8898 SmartPA dual-chip codec driver
 *
 * Reconstructed from factory v1.1.5 behaviour:
 *   Left  (0x34): DAI "aw8898-l-aif", registers codec with both DAIs
 *   Right (0x35): DAI "aw8898-r-aif", cross-referenced by left
 *   cfg.bin format: [reg_lo] [reg_hi] [val_lo] [val_hi]
 *
 * Both chips are configured together on SMTPA BE open.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm.h>

#define AW8898_L_I2C_NAME  "aw8898_l_smartpa"
#define AW8898_R_I2C_NAME  "aw8898_r_smartpa"
#define AW8898_VERSION     "v1.1.5-iflytek"

#define AW8898_REG_ID      0x00
#define AW8898_CHIPID      0x1702

/* --- Per-chip state --- */
struct aw8898 {
	struct i2c_client *i2c;
	struct device      *dev;
	int                 reset_gpio;
	unsigned int        flags;      /* bit0=START_ON_MUTE, bit1=SKIP_IRQ */
	int                 chipid;
	bool                power_on;
	bool                init_done;
	char                cfg_name[64];
};

/* Global reference to the right-channel chip for cross-calls */
static struct aw8898 *g_aw8898_r;

/* ------------------------------------------------------------------ */
/*  I2C helpers                                                        */
/* ------------------------------------------------------------------ */
static int aw8898_i2c_write(struct aw8898 *aw, u16 reg, u16 val)
{
	u8 buf[3];
	buf[0] = reg & 0xff;
	buf[1] = (val >> 8) & 0xff;
	buf[2] = val & 0xff;
	return i2c_master_send(aw->i2c, buf, 3);
}

static int aw8898_i2c_read(struct aw8898 *aw, u16 reg, u16 *val)
{
	int ret;
	u8 wbuf[1] = { reg & 0xff };
	u8 rbuf[2];

	ret = i2c_master_send(aw->i2c, wbuf, 1);
	if (ret < 0)
		return ret;
	ret = i2c_master_recv(aw->i2c, rbuf, 2);
	if (ret < 0)
		return ret;
	*val = (rbuf[0] << 8) | rbuf[1];
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Hardware reset                                                     */
/* ------------------------------------------------------------------ */
static void aw8898_hw_reset(struct aw8898 *aw)
{
	if (!gpio_is_valid(aw->reset_gpio))
		return;
	gpio_set_value_cansleep(aw->reset_gpio, 0);
	msleep(5);
	gpio_set_value_cansleep(aw->reset_gpio, 1);
	msleep(5);
}

/* ------------------------------------------------------------------ */
/*  Config-file loading                                               */
/*  Format (factory-verified): [reg_lo] [reg_hi] [val_lo] [val_hi]    */
/* ------------------------------------------------------------------ */
static void aw8898_container_update(struct aw8898 *aw, u8 reg, u16 val)
{
	dev_dbg(aw->dev, "container_update: reg=0x%02x, val=0x%04x\n",
		reg, val);
	aw8898_i2c_write(aw, reg, val);
}

static int aw8898_load_cfg(struct aw8898 *aw)
{
	const struct firmware *fw;
	const u8 *p;
	int i, ret;

	ret = request_firmware(&fw, aw->cfg_name, aw->dev);
	if (ret < 0) {
		dev_info(aw->dev, "no cfg %s (deferred), ret=%d\n",
			 aw->cfg_name, ret);
		return ret;
	}

	dev_info(aw->dev, "loaded %s - size: %zu\n", aw->cfg_name, fw->size);

	p = fw->data;
	/* cfg format: [reg_addr(1)] [0x00(1)] [val_lo(1)] [val_hi(1)] — little-endian */
	for (i = 0; i + 3 < (int)fw->size; i += 4) {
		u8 reg = p[i];
		u16 val = ((u16)p[i + 3] << 8) | p[i + 2];
		aw8898_container_update(aw, reg, val);
	}

	release_firmware(fw);
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Cold-start / power / mute — factory sequence                     */
/* ------------------------------------------------------------------ */
static void aw8898_cold_start(struct aw8898 *aw)
{
	dev_info(aw->dev, "cold_start enter\n");
	aw8898_hw_reset(aw);
	usleep_range(2000, 2500);
	aw8898_load_cfg(aw);
	aw->init_done = true;
}

static void aw8898_mute(struct aw8898 *aw, int mute_state)
{
	dev_info(aw->dev, "mute: mute state=%d\n", mute_state);
	/* Mute/unmute via register — cfg handles actual I2S path.
	 * We just toggle the PA power. */
	if (mute_state) {
		/* power-down PA */
		aw8898_i2c_write(aw, 0x0004, 0x0002);
	} else {
		/* power-up PA (read current first) */
		u16 val = 0;
		aw8898_i2c_read(aw, 0x0004, &val);
		val &= ~0x0002u;
		aw8898_i2c_write(aw, 0x0004, val);
	}
}

/* ------------------------------------------------------------------ */
/*  DAI operations                                                     */
/* ------------------------------------------------------------------ */
static int aw8898_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	dev_info(dai->dev, "%s: fmt=0x%04x\n", __func__, fmt);
	/* factory always passes 0x4001 — accept it */
	return 0;
}

static int aw8898_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct aw8898 *aw = snd_soc_component_get_drvdata(dai->component);

	dev_info(aw->dev, "%s: enter\n", __func__);

	/* Left chip: also start right chip if present */
	if (g_aw8898_r && g_aw8898_r != aw) {
		dev_info(g_aw8898_r->dev, "%s: enter\n", __func__);
		if (!g_aw8898_r->init_done)
			aw8898_cold_start(g_aw8898_r);
		aw8898_mute(g_aw8898_r, 0);
	}

	if (!aw->init_done)
		aw8898_cold_start(aw);
	aw8898_mute(aw, 0);

	aw->power_on = true;
	return 0;
}

static void aw8898_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct aw8898 *aw = snd_soc_component_get_drvdata(dai->component);

	dev_info(aw->dev, "%s: enter\n", __func__);
	aw8898_mute(aw, 1);
	aw->power_on = false;
	aw->init_done = false;

	/* Also mute right */
	if (g_aw8898_r && g_aw8898_r != aw) {
		aw8898_mute(g_aw8898_r, 1);
		g_aw8898_r->power_on = false;
		g_aw8898_r->init_done = false;
	}
}

static struct snd_soc_dai_ops aw8898_dai_ops = {
	.set_fmt   = aw8898_set_fmt,
	.startup   = aw8898_startup,
	.shutdown  = aw8898_shutdown,
};

/* Left-codec DAIs: aw8898-l-aif (used by SMTPA BE) + aw8898-r-aif  */
static struct snd_soc_dai_driver aw8898_l_dai[] = {
	{
		.name = "aw8898-l-aif",
		.id   = 0,
		.playback = {
			.stream_name    = "AW8898-L Playback",
			.channels_min   = 1,
			.channels_max   = 2,
			.rates          = SNDRV_PCM_RATE_8000_48000,
			.formats        = SNDRV_PCM_FMTBIT_S16_LE |
					  SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops = &aw8898_dai_ops,
	},
};

/* Right-codec DAI (registered but no BE binds to it directly;
 * started together with left via cross-call). */
static struct snd_soc_dai_driver aw8898_r_dai[] = {
	{
		.name = "aw8898-r-aif",
		.id   = 0,
		.playback = {
			.stream_name    = "AW8898-R Playback",
			.channels_min   = 1,
			.channels_max   = 2,
			.rates          = SNDRV_PCM_RATE_8000_48000,
			.formats        = SNDRV_PCM_FMTBIT_S16_LE |
					  SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops = &aw8898_dai_ops,
	},
};

/* ------------------------------------------------------------------ */
/*  Mixer control: aw8898_l_speaker_switch (for audio HAL)           */
/* ------------------------------------------------------------------ */
static int aw8898_spk_switch_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw8898 *aw = snd_soc_component_get_drvdata(codec);
	ucontrol->value.integer.value[0] = aw->power_on ? 1 : 0;
	return 0;
}

static int aw8898_spk_switch_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct aw8898 *aw = snd_soc_component_get_drvdata(codec);
	int enable = !!ucontrol->value.integer.value[0];

	if (aw->power_on == enable)
		return 0;

	aw->power_on = enable;
	dev_info(aw->dev, "aw8898 speaker switch: %s\n",
		 enable ? "On" : "Off");

	if (enable) {
		if (!aw->init_done)
			aw8898_cold_start(aw);
		aw8898_mute(aw, 0);
		if (g_aw8898_r) {
			if (!g_aw8898_r->init_done)
				aw8898_cold_start(g_aw8898_r);
			aw8898_mute(g_aw8898_r, 0);
		}
	} else {
		aw8898_mute(aw, 1);
		if (g_aw8898_r)
			aw8898_mute(g_aw8898_r, 1);
	}
	return 1;
}

static const char * const aw8898_switch_text[] = { "Off", "On" };
static const struct soc_enum aw8898_spk_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(aw8898_switch_text), aw8898_switch_text);
static const struct snd_kcontrol_new aw8898_controls[] = {
	SOC_ENUM_EXT("aw8898_l_speaker_switch", aw8898_spk_enum,
		     aw8898_spk_switch_get, aw8898_spk_switch_put),
};

/* ------------------------------------------------------------------ */
/*  Codec probe                                                        */
/* ------------------------------------------------------------------ */
static int aw8898_codec_probe(struct snd_soc_component *codec)
{
	struct aw8898 *aw = snd_soc_component_get_drvdata(codec);
	struct snd_soc_card *card = codec->card;

	dev_info(aw->dev, "%s: card=%s\n", __func__,
		 card ? card->name : "NULL");

	snd_soc_add_component_controls(codec, aw8898_controls,
				     ARRAY_SIZE(aw8898_controls));
	return 0;
}

static struct snd_soc_component_driver aw8898_codec_drv = {
	.probe = aw8898_codec_probe,
};

/* ------------------------------------------------------------------ */
/*  I2C probe                                                          */
/* ------------------------------------------------------------------ */
static int aw8898_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct aw8898 *aw;
	u16 chipid = 0;
	int ret;
	enum of_gpio_flags flags;
	bool is_left;

	is_left = of_device_is_compatible(i2c->dev.of_node,
					  "awinic,aw8898_l_smartpa");

	dev_info(&i2c->dev, "aw8898_i2c_probe enter (is_left=%d)\n",
		 is_left);

	aw = devm_kzalloc(&i2c->dev, sizeof(*aw), GFP_KERNEL);
	if (!aw)
		return -ENOMEM;

	aw->i2c = i2c;
	aw->dev = &i2c->dev;

	/* Reset GPIO */
	aw->reset_gpio = of_get_named_gpio_flags(i2c->dev.of_node,
						 "reset-gpio", 0, &flags);
	if (gpio_is_valid(aw->reset_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, aw->reset_gpio,
					    GPIOF_OUT_INIT_LOW,
					    is_left ? "aw8898_l_rst" :
						      "aw8898_r_rst");
		if (ret)
			dev_warn(&i2c->dev,
				 "reset gpio request failed: %d\n", ret);
		else
			aw8898_hw_reset(aw);
	}

	/* Read chip ID */
	ret = aw8898_i2c_read(aw, AW8898_REG_ID, &chipid);
	if (ret < 0) {
		dev_err(&i2c->dev, "failed to read chip id\n");
		return -EIO;
	}
	if (chipid != AW8898_CHIPID) {
		dev_err(&i2c->dev, "unsupported chip id: 0x%04x\n", chipid);
		return -ENODEV;
	}
	aw->chipid  = chipid;
	aw->flags   = 0x3; /* START_ON_MUTE | SKIP_IRQ — match factory */
	dev_info(&i2c->dev, "aw8898_%s detected, flags=0x%x\n",
		 is_left ? "l" : "r", aw->flags);

	i2c_set_clientdata(i2c, aw);

	/* Determine cfg name */
	if (is_left)
		snprintf(aw->cfg_name, sizeof(aw->cfg_name),
			 "aw8898_l_cfg.bin");
	else
		snprintf(aw->cfg_name, sizeof(aw->cfg_name),
			 "aw8898_r_cfg.bin");

	/* Register codec with appropriate DAI name(s) */
	if (is_left) {
		ret = snd_soc_register_component(&i2c->dev, &aw8898_codec_drv,
					     aw8898_l_dai,
					     ARRAY_SIZE(aw8898_l_dai));
	} else {
		ret = snd_soc_register_component(&i2c->dev, &aw8898_codec_drv,
					     aw8898_r_dai,
					     ARRAY_SIZE(aw8898_r_dai));
		g_aw8898_r = aw;       /* allow left to cross-call */
	}
	if (ret) {
		dev_err(&i2c->dev, "snd_soc_register_codec failed: %d\n",
			ret);
		return ret;
	}

	dev_info(&i2c->dev, "%s_i2c_probe completed successfully!\n",
		 is_left ? "aw8898_l" : "aw8898_r");
	return 0;
}

static int aw8898_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_component(&i2c->dev);
	if (of_device_is_compatible(i2c->dev.of_node,
				    "awinic,aw8898_r_smartpa"))
		g_aw8898_r = NULL;
	return 0;
}

static const struct of_device_id aw8898_dt_match[] = {
	{ .compatible = "awinic,aw8898_l_smartpa" },
	{ .compatible = "awinic,aw8898_r_smartpa" },
	{ }
};
MODULE_DEVICE_TABLE(of, aw8898_dt_match);

static struct i2c_driver aw8898_i2c_driver = {
	.driver = {
		.name           = AW8898_L_I2C_NAME,
		.of_match_table = of_match_ptr(aw8898_dt_match),
	},
	.probe  = aw8898_i2c_probe,
	.remove = aw8898_i2c_remove,
};

static int __init aw8898_i2c_init(void)
{
	pr_info("aw8898_l driver version %s\n", AW8898_VERSION);
	return i2c_add_driver(&aw8898_i2c_driver);
}
module_init(aw8898_i2c_init);

static void __exit aw8898_i2c_exit(void)
{
	i2c_del_driver(&aw8898_i2c_driver);
}
module_exit(aw8898_i2c_exit);

MODULE_DESCRIPTION("Awinic AW8898 L+R SmartPA codec driver");
MODULE_LICENSE("GPL v2");
