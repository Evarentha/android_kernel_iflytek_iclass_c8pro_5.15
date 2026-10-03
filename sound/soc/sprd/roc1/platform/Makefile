# SPRD Platform Support
SPRD_SOUND_TREE			:= $(srctree)/sound/soc/sprd/roc1/
SPRD_AUDIO_ASSISTANT_TREE	:= $(srctree)/drivers/misc/sprd_audio/
SPRD_MCDT_TREE			:= $(srctree)/drivers/misc/sprd_audio/mcdt/
ccflags-y	+= -I$(SPRD_AUDIO_ASSISTANT_TREE)/audiomem/ \
		   -I$(SPRD_AUDIO_ASSISTANT_TREE)/audiosipc/ \
		   -I$(SPRD_MCDT_TREE)


ifneq ($(CONFIG_SND_SOC_SPRD_AUDIO_TWO_STAGE_DMAENGINE_SURPPORT),)
	snd-soc-sprd-audio-dma-objs       := sprd-2stage-dmaengine-pcm.o
else
	snd-soc-sprd-audio-dma-objs := sprd-dmaengine-pcm.o \
	sprd-platform-pcm-routing.o sprd-compr-2stage-dma.o
endif

obj-$(CONFIG_SND_SOC_SPRD_ROC1)   += snd-soc-sprd-audio-dma.o
