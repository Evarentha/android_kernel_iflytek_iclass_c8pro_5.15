# SPRD VBC Support
SPRD_SOUND_TREE := $(srctree)/sound/soc/sprd/roc1/
SPRD_AUDIO_ASSISTANT_TREE := $(srctree)/drivers/misc/sprd_audio/
SPRD_MCDT_TREE := $(srctree)/drivers/misc/sprd_audio/mcdt/
ccflags-y += -I$(SPRD_AUDIO_ASSISTANT_TREE)/audiosipc \
		-I$(SPRD_AUDIO_ASSISTANT_TREE)/audiomem \
	     -I$(SPRD_SOUND_TREE)/dai \
	     -I$(SPRD_SOUND_TREE)/platform \
	     -I$(SPRD_SOUND_TREE)/dai/vbc/ \
	     -I$(SPRD_MCDT_TREE)

snd-soc-sprd-vbc-v4-objs       := vbc-dai.o vbc-phy-v4.o sprd-fe-dai.o
obj-$(CONFIG_SND_SOC_SPRD_VBC_V4)    += snd-soc-sprd-vbc-v4.o
