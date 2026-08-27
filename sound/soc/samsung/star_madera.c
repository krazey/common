// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sound machine driver for Exynos9810 Star devices.
 *
 * Copyright (c) 2026 Mathias Gluszczynski <admin@krazey.de>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/mfd/madera/core.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <sound/pcm_params.h>
#include <sound/samsung/abox.h>
#include <sound/soc.h>

#include "../codecs/madera.h"

#define STAR_NUM_RDMA		8
#define STAR_NUM_WDMA		5
#define STAR_NUM_UAIF		4
#define STAR_NUM_SIFS		3
#define STAR_NUM_FE		(STAR_NUM_RDMA + STAR_NUM_WDMA)
#define STAR_NUM_BE		(STAR_NUM_UAIF + 1 + STAR_NUM_SIFS)
#define STAR_NUM_LINKS		(STAR_NUM_FE + STAR_NUM_BE)
#define STAR_NUM_ABOX_COMPONENTS	6

#define STAR_PMU_DEBUG		0x0a00
#define STAR_CLKOUT_DISABLE	BIT(0)
#define STAR_CLKOUT_SEL		GENMASK(13, 8)
#define STAR_CLKOUT_TCXO	FIELD_PREP(STAR_CLKOUT_SEL, 1)

#define STAR_MCLK_RATE		26000000
#define STAR_SYSCLK_RATE	98304000
#define STAR_DSPCLK_RATE	147456000

struct star_madera {
	struct snd_soc_card card;
	struct snd_soc_dai_link links[STAR_NUM_LINKS];
	struct snd_soc_dai_link_component cpus[STAR_NUM_LINKS];
	struct snd_soc_dai_link_component codecs[STAR_NUM_LINKS];
	struct snd_soc_dai_link_component platforms[STAR_NUM_LINKS];
	struct snd_soc_codec_conf codec_conf[STAR_NUM_ABOX_COMPONENTS];
	struct regmap *pmu;
};

static const char * const star_rdma_names[STAR_NUM_RDMA] = {
	"RDMA0", "RDMA1", "RDMA2", "RDMA3",
	"RDMA4", "RDMA5", "RDMA6", "RDMA7",
};

static const char * const star_wdma_names[STAR_NUM_WDMA] = {
	"WDMA0", "WDMA1", "WDMA2", "WDMA3", "WDMA4",
};

static const char * const star_uaif_names[STAR_NUM_UAIF] = {
	"UAIF0", "UAIF1", "UAIF2", "UAIF3",
};

static const char * const star_sifs_names[STAR_NUM_SIFS] = {
	"SIFS0", "SIFS1", "SIFS2",
};

static const struct snd_soc_dapm_widget star_widgets[] = {
	SND_SOC_DAPM_INPUT("HEADSETMIC"),
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_INPUT("DMIC3"),
	SND_SOC_DAPM_INPUT("DMIC4"),
	SND_SOC_DAPM_INPUT("FM"),
	SND_SOC_DAPM_INPUT("BLUETOOTH MIC"),
	SND_SOC_DAPM_INPUT("VINPUT1"),
	SND_SOC_DAPM_INPUT("VINPUTCALL"),
	SND_SOC_DAPM_OUTPUT("RECEIVER"),
	SND_SOC_DAPM_OUTPUT("HEADPHONE"),
	SND_SOC_DAPM_OUTPUT("SPEAKER"),
	SND_SOC_DAPM_OUTPUT("BLUETOOTH SPK"),
	SND_SOC_DAPM_OUTPUT("VOUTPUT"),
	SND_SOC_DAPM_OUTPUT("VOUTPUTCALL"),
};

static void star_of_node_put(void *data)
{
	of_node_put(data);
}

static struct device_node *star_parse_phandle(struct device *dev,
		struct device_node *np, const char *property, int index)
{
	struct device_node *node;
	int ret;

	node = of_parse_phandle(np, property, index);
	if (!node)
		return ERR_PTR(-EINVAL);

	ret = devm_add_action_or_reset(dev, star_of_node_put, node);
	if (ret)
		return ERR_PTR(ret);

	return node;
}

static void star_set_components(struct star_madera *priv, int index,
		struct device_node *cpu_node, const char *cpu_dai,
		struct device_node *platform_node,
		struct device_node *codec_node, const char *codec_dai)
{
	struct snd_soc_dai_link *link = &priv->links[index];

	priv->cpus[index].of_node = cpu_node;
	priv->cpus[index].dai_name = cpu_dai;
	link->cpus = &priv->cpus[index];
	link->num_cpus = 1;

	if (platform_node) {
		priv->platforms[index].of_node = platform_node;
		link->platforms = &priv->platforms[index];
		link->num_platforms = 1;
	}

	if (codec_node) {
		priv->codecs[index].of_node = codec_node;
		priv->codecs[index].dai_name = codec_dai;
		link->codecs = &priv->codecs[index];
	} else {
		link->codecs = &snd_soc_dummy_dlc;
	}
	link->num_codecs = 1;
}

static int star_abox_fixup(struct snd_soc_pcm_runtime *rtd,
		struct snd_pcm_hw_params *params)
{
	int stream = SNDRV_PCM_STREAM_PLAYBACK;

	if (rtd->dai_link->capture_only) {
		stream = SNDRV_PCM_STREAM_CAPTURE;
	} else if (!rtd->dai_link->playback_only) {
		if (rtd->dpcm[SNDRV_PCM_STREAM_CAPTURE].users &&
		    !rtd->dpcm[SNDRV_PCM_STREAM_PLAYBACK].users)
			stream = SNDRV_PCM_STREAM_CAPTURE;
	}

	return abox_hw_params_fixup_helper(rtd, params, stream);
}

static int star_dsif_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	unsigned int tx_slot[] = { 0, 1 };
	int ret;

	ret = snd_soc_dai_set_bclk_ratio(cpu_dai, 64);
	if (ret)
		return ret;

	return snd_soc_dai_set_channel_map(cpu_dai, ARRAY_SIZE(tx_slot),
			tx_slot, 0, NULL);
}

static const struct snd_soc_ops star_dsif_ops = {
	.hw_params = star_dsif_hw_params,
};

static int star_uaif0_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component;
	unsigned int async_rate;
	unsigned int ref_rate;
	int ret;

	component = snd_soc_rtd_to_codec(rtd, 0)->component;
	async_rate = params_rate(params) % 8000 ? 90316800 :
			STAR_SYSCLK_RATE;
	ref_rate = params_rate(params) * params_width(params) * 2;

	ret = snd_soc_component_set_sysclk(component,
			MADERA_CLK_ASYNCCLK_1, MADERA_CLK_SRC_FLL2,
			async_rate, SND_SOC_CLOCK_IN);
	if (ret)
		return ret;

	return snd_soc_component_set_pll(component, MADERA_FLL2_REFCLK,
			MADERA_FLL_SRC_AIF1BCLK, ref_rate, async_rate);
}

static int star_uaif0_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	return snd_soc_dai_set_tristate(snd_soc_rtd_to_cpu(rtd, 0), 0);
}

static void star_uaif0_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	snd_soc_dai_set_tristate(snd_soc_rtd_to_cpu(rtd, 0), 1);
}

static const struct snd_soc_ops star_uaif0_ops = {
	.hw_params = star_uaif0_hw_params,
	.prepare = star_uaif0_prepare,
	.shutdown = star_uaif0_shutdown,
};

static int star_uaif0_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_soc_dapm_context *dapm;
	int ret;

	ret = snd_soc_component_set_pll(component, MADERA_FLL1_REFCLK,
			MADERA_FLL_SRC_MCLK1, STAR_MCLK_RATE,
			STAR_SYSCLK_RATE);
	if (ret)
		return dev_err_probe(rtd->dev, ret, "failed to start FLL1\n");

	ret = snd_soc_component_set_sysclk(component, MADERA_CLK_SYSCLK_1,
			MADERA_CLK_SRC_FLL1, STAR_SYSCLK_RATE,
			SND_SOC_CLOCK_IN);
	if (ret)
		return dev_err_probe(rtd->dev, ret, "failed to set SYSCLK\n");

	ret = snd_soc_component_set_sysclk(component,
			MADERA_CLK_ASYNCCLK_1, MADERA_CLK_SRC_FLL2,
			STAR_SYSCLK_RATE, SND_SOC_CLOCK_IN);
	if (ret)
		return dev_err_probe(rtd->dev, ret, "failed to set ASYNCCLK\n");

	ret = snd_soc_component_set_sysclk(component, MADERA_CLK_DSPCLK,
			MADERA_CLK_SRC_FLL1, STAR_DSPCLK_RATE,
			SND_SOC_CLOCK_IN);
	if (ret)
		return dev_err_probe(rtd->dev, ret, "failed to set DSPCLK\n");

	ret = snd_soc_component_set_sysclk(component, MADERA_CLK_OUTCLK,
			MADERA_OUTCLK_ASYNCCLK, 0, SND_SOC_CLOCK_IN);
	if (ret)
		return dev_err_probe(rtd->dev, ret, "failed to set OUTCLK\n");

	ret = snd_soc_dai_set_sysclk(codec_dai, MADERA_CLK_ASYNCCLK_1, 0, 0);
	if (ret)
		return dev_err_probe(rtd->dev, ret, "failed to clock AIF1\n");

	dapm = snd_soc_component_to_dapm(component);
	snd_soc_dapm_ignore_suspend(dapm, "AIF1 Playback");
	snd_soc_dapm_ignore_suspend(dapm, "AIF1 Capture");
	snd_soc_dapm_sync(dapm);

	dev_info(rtd->dev, "E981D: Star Madera clock tree initialized\n");
	return 0;
}

static int star_uaif2_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_dapm_context *dapm;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, MADERA_CLK_SYSCLK_1, 0, 0);
	if (ret)
		return dev_err_probe(rtd->dev, ret, "failed to clock AIF3\n");

	dapm = snd_soc_component_to_dapm(codec_dai->component);
	snd_soc_dapm_ignore_suspend(dapm, "AIF3 Playback");
	snd_soc_dapm_ignore_suspend(dapm, "AIF3 Capture");
	snd_soc_dapm_sync(dapm);

	return 0;
}

static int star_uaif3_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dapm_context *dapm;

	dapm = snd_soc_component_to_dapm(
			snd_soc_rtd_to_cpu(rtd, 0)->component);
	snd_soc_dapm_ignore_suspend(dapm, "UAIF3 Playback");
	snd_soc_dapm_ignore_suspend(dapm, "UAIF3 Capture");
	snd_soc_dapm_sync(dapm);

	return 0;
}

static int star_late_probe(struct snd_soc_card *card)
{
	static const char * const pins[] = {
		"VOUTPUT", "VINPUT1", "VOUTPUTCALL", "VINPUTCALL",
		"HEADSETMIC", "RECEIVER", "HEADPHONE", "SPEAKER",
		"BLUETOOTH MIC", "BLUETOOTH SPK", "DMIC1", "DMIC2",
		"DMIC3", "DMIC4", "FM",
	};
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dapm_context *dapm;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pins); i++)
		snd_soc_dapm_ignore_suspend(card->dapm, pins[i]);

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
	if (rtd) {
		dapm = snd_soc_component_to_dapm(
				snd_soc_rtd_to_cpu(rtd, 0)->component);
		for (i = 0; i < STAR_NUM_RDMA; i++) {
			char name[32];

			snprintf(name, sizeof(name), "RDMA%u Playback", i);
			snd_soc_dapm_ignore_suspend(dapm, name);
		}
		for (i = 0; i < STAR_NUM_WDMA; i++) {
			char name[32];

			snprintf(name, sizeof(name), "WDMA%u Capture", i);
			snd_soc_dapm_ignore_suspend(dapm, name);
		}
		snd_soc_dapm_sync(dapm);
	}

	snd_soc_dapm_sync(card->dapm);
	return 0;
}

static void star_init_fe(struct star_madera *priv, int index,
		const char *name, struct device_node *abox,
		struct device_node *dma, bool playback)
{
	struct snd_soc_dai_link *link = &priv->links[index];

	link->name = name;
	link->stream_name = name;
	link->id = index;
	link->dynamic = 1;
	link->ignore_suspend = 1;
	link->trigger[SNDRV_PCM_STREAM_PLAYBACK] = SND_SOC_DPCM_TRIGGER_POST;
	link->trigger[SNDRV_PCM_STREAM_CAPTURE] = SND_SOC_DPCM_TRIGGER_PRE;
	if (playback)
		link->playback_only = 1;
	else
		link->capture_only = 1;
	star_set_components(priv, index, abox, name, dma, NULL, NULL);
}

static void star_init_be(struct star_madera *priv, int index,
		const char *name, struct device_node *cpu_node,
		const char *cpu_dai, struct device_node *codec_node,
		const char *codec_dai, unsigned int format,
		bool playback, bool capture)
{
	struct snd_soc_dai_link *link = &priv->links[index];

	link->name = name;
	link->stream_name = name;
	link->id = index;
	link->dai_fmt = format | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC;
	link->no_pcm = 1;
	link->ignore_suspend = 1;
	link->ignore_pmdown_time = 1;
	link->be_hw_params_fixup = star_abox_fixup;
	if (playback && !capture)
		link->playback_only = 1;
	else if (capture && !playback)
		link->capture_only = 1;
	star_set_components(priv, index, cpu_node, cpu_dai, NULL,
			codec_node, codec_dai);
}

static int star_init_links(struct platform_device *pdev,
		struct star_madera *priv)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *abox, *codec, *dsif;
	struct device_node *rdma[STAR_NUM_RDMA];
	struct device_node *wdma[STAR_NUM_WDMA];
	struct device_node *uaif[STAR_NUM_UAIF];
	unsigned int i;
	int index = 0;

	abox = star_parse_phandle(dev, np, "samsung,abox", 0);
	codec = star_parse_phandle(dev, np, "samsung,codec", 0);
	dsif = star_parse_phandle(dev, np, "samsung,dsif", 0);
	if (IS_ERR(abox) || IS_ERR(codec) || IS_ERR(dsif))
		return -EINVAL;

	for (i = 0; i < STAR_NUM_RDMA; i++) {
		rdma[i] = star_parse_phandle(dev, np, "samsung,rdma", i);
		if (IS_ERR(rdma[i]))
			return PTR_ERR(rdma[i]);
		star_init_fe(priv, index++, star_rdma_names[i], abox,
				rdma[i], true);
	}

	for (i = 0; i < STAR_NUM_WDMA; i++) {
		wdma[i] = star_parse_phandle(dev, np, "samsung,wdma", i);
		if (IS_ERR(wdma[i]))
			return PTR_ERR(wdma[i]);
		star_init_fe(priv, index++, star_wdma_names[i], abox,
				wdma[i], false);
	}

	for (i = 0; i < STAR_NUM_UAIF; i++) {
		uaif[i] = star_parse_phandle(dev, np, "samsung,uaif", i);
		if (IS_ERR(uaif[i]))
			return PTR_ERR(uaif[i]);
	}

	star_init_be(priv, index, "UAIF0", uaif[0], "UAIF0", codec,
			"cs47l92-aif1", SND_SOC_DAIFMT_I2S, true, true);
	priv->links[index].init = star_uaif0_init;
	priv->links[index++].ops = &star_uaif0_ops;

	star_init_be(priv, index++, "UAIF1", uaif[1], "UAIF1", NULL,
			NULL, SND_SOC_DAIFMT_I2S, true, true);

	star_init_be(priv, index, "UAIF2", uaif[2], "UAIF2", codec,
			"cs47l92-aif3", SND_SOC_DAIFMT_I2S, true, true);
	priv->links[index++].init = star_uaif2_init;

	star_init_be(priv, index, "UAIF3", uaif[3], "UAIF3", NULL,
			NULL, SND_SOC_DAIFMT_I2S, true, true);
	priv->links[index++].init = star_uaif3_init;

	star_init_be(priv, index, "DSIF", dsif, "DSIF", NULL, NULL,
			SND_SOC_DAIFMT_PDM, true, false);
	priv->links[index++].ops = &star_dsif_ops;

	for (i = 0; i < STAR_NUM_SIFS; i++)
		star_init_be(priv, index++, star_sifs_names[i], abox,
				star_sifs_names[i], NULL, NULL,
				SND_SOC_DAIFMT_I2S, true, true);

	priv->codec_conf[0].dlc.of_node = abox;
	for (i = 0; i < STAR_NUM_UAIF; i++)
		priv->codec_conf[i + 1].dlc.of_node = uaif[i];
	priv->codec_conf[5].dlc.of_node = dsif;
	for (i = 0; i < STAR_NUM_ABOX_COMPONENTS; i++)
		priv->codec_conf[i].name_prefix = "ABOX";

	return index == STAR_NUM_LINKS ? 0 : -EINVAL;
}

static int star_madera_probe(struct platform_device *pdev)
{
	struct star_madera *priv;
	struct snd_soc_card *card;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pmu = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
			"samsung,pmu-syscon");
	if (IS_ERR(priv->pmu))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->pmu),
				"failed to find PMU syscon\n");

	ret = regmap_update_bits(priv->pmu, STAR_PMU_DEBUG,
			STAR_CLKOUT_DISABLE | STAR_CLKOUT_SEL,
			STAR_CLKOUT_TCXO);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				"failed to enable codec MCLK\n");

	ret = star_init_links(pdev, priv);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				"failed to create DAI links\n");

	card = &priv->card;
	card->name = "Star-Madera";
	card->owner = THIS_MODULE;
	card->dev = &pdev->dev;
	card->dai_link = priv->links;
	card->num_links = STAR_NUM_LINKS;
	card->codec_conf = priv->codec_conf;
	card->num_configs = STAR_NUM_ABOX_COMPONENTS;
	card->dapm_widgets = star_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(star_widgets);
	card->late_probe = star_late_probe;
	card->fully_routed = true;
	snd_soc_card_set_drvdata(card, priv);

	ret = snd_soc_of_parse_audio_routing(card,
			"samsung,audio-routing");
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				"failed to parse audio routes\n");

	platform_set_drvdata(pdev, priv);
	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				"failed to register sound card\n");

	dev_info(&pdev->dev,
		 "E981D: Star Madera card registered with %u links\n",
		 card->num_links);
	return 0;
}

static const struct of_device_id star_madera_of_match[] = {
	{ .compatible = "samsung,exynos9810-star-madera" },
	{ }
};
MODULE_DEVICE_TABLE(of, star_madera_of_match);

static struct platform_driver star_madera_driver = {
	.probe = star_madera_probe,
	.driver = {
		.name = "exynos9810-star-madera",
		.of_match_table = star_madera_of_match,
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(star_madera_driver);

MODULE_DESCRIPTION("Exynos9810 Star Madera sound card");
MODULE_AUTHOR("Mathias Gluszczynski <admin@krazey.de>");
MODULE_LICENSE("GPL");
