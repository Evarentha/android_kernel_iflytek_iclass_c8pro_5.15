# SPDX-License-Identifier: GPL-2.0
# ROC1 1.11@5516664 profile, generated and validated against the factory DDK.
TOP := $(srctree)/$(src)
OUT := $(TOP)
bridge_base := $(TOP)/generated
PVRSRV_MODNAME := pvrsrvkm
PVRSRVKM_NAME := pvrsrvkm
PVR_SYSTEM := rgx_sprd
BOARD_NAME := roc1
SUPPORT_RGX := 1
SUPPORT_PHYSMEM_TEST := 1
VMM_TYPE := stub
FEATURE_PVR_DVFS := 1
PVR_HW_DVFS := 1
PVR_HANDLE_BACKEND := idr
SUPPORT_DMABUF_BRIDGE := 1
SUPPORT_SERVER_SYNC := 0
SUPPORT_SERVER_SYNC_IMPL := 1
SUPPORT_NATIVE_FENCE_SYNC := 1
PVR_USE_FENCE_SYNC_MODEL := 1
SUPPORT_DMA_FENCE := 1
SUPPORT_ANDROID_PLATFORM := 1
SUPPORT_ION := 1

ccflags-y += -include $(TOP)/config_kernel.h \
 -I$(TOP)/include -I$(TOP)/include/public -I$(TOP)/hwdefs/km \
 -I$(TOP)/services/include -I$(TOP)/services/include/shared \
 -I$(TOP)/services/shared/include -I$(TOP)/services/shared/common \
 -I$(TOP)/services/system/include -I$(TOP)/services/server/common \
 -I$(TOP)/services/server/devices/rgx -I$(TOP)/services/server/env/linux \
 -I$(TOP)/services/server/include -I$(TOP)/services/system/rgx_sprd
ccflags-y += -Wno-missing-prototypes -Wno-unused-parameter -Wno-sign-compare \
 -Wno-address-of-packed-member -Wno-unneeded-internal-declaration \
 -Wno-typedef-redefinition -Wno-unused-but-set-variable

include $(TOP)/services/server/env/linux/Kbuild.mk
obj-$(CONFIG_MITOCHODRIA_ROC1_PVR) += pvrsrvkm.o
