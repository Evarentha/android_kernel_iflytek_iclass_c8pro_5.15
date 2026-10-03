# iFLYTEK product device trees
#
# Each base tree is maintained from its matching factory DTB. Overlays are
# rebuilt from the factory hardware content; both the base and the overlay
# are compiled with -@ so the base carries __symbols__ and the overlay
# carries __fixups__/__symbols__, exactly like the 4.14 factory artifacts
# that the BSP dtc -M merge check was validated against.
#
# NOTE: unlike 4.14 (where __build depends on the legacy $(always) variable
# and dtbo-y is collected), the 5.15 kbuild collects only dtb-y into
# always-y, and scripts/Makefile.dtbo picks the .dtbo entries out of dtb-y
# via their matching <overlay>-base variable.  Register the overlays the
# same way the sprd/ ROC1 boards do so every flow (make dtbs, make dtbo and
# the BSP make_dtb/make_dtbo invocations) produces them.

dtb-$(CONFIG_ARCH_SPRD) += iclass-c8pro.dtb
dtb-$(CONFIG_ARCH_SPRD) += iclass-c8pro-overlay.dtbo
iclass-c8pro-overlay.dtbo-base := iclass-c8pro.dtb
DTC_FLAGS_iclass-c8pro += -@
DTC_FLAGS_iclass-c8pro-overlay += -@

dtb-$(CONFIG_ARCH_SPRD) += istudy-c10.dtb
dtb-$(CONFIG_ARCH_SPRD) += istudy-c10-overlay.dtbo
istudy-c10-overlay.dtbo-base := istudy-c10.dtb
DTC_FLAGS_istudy-c10 += -@
DTC_FLAGS_istudy-c10-overlay += -@

dtb-$(CONFIG_ARCH_SPRD) += iclass-c6s.dtb
dtb-$(CONFIG_ARCH_SPRD) += iclass-c6s-overlay.dtbo
iclass-c6s-overlay.dtbo-base := iclass-c6s.dtb
DTC_FLAGS_iclass-c6s += -@
DTC_FLAGS_iclass-c6s-overlay += -@
