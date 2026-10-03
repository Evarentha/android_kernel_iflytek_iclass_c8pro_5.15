SPRD_SND_TREE   := $(srctree)/sound/soc/sprd/roc1/
SPRD_AUDIO_ASSISTANT_TREE := $(srctree)/drivers/misc/sprd_audio/
ccflags-y += -I$(SPRD_AUDIO_ASSISTANT_TREE)/audiomem/ \
         -I$(SPRD_AUDIO_ASSISTANT_TREE)/audiosipc/ \
         -I$(SPRD_AUDIO_ASSISTANT_TREE)/agdsp_access/ \
         -I$(SPRD_SND_TREE)/include

obj-$(CONFIG_SPRD_HW_DEVICE_DVFS_AGCP)     += sprd_audcp_dvfs.o
