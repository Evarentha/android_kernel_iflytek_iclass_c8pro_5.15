ROC1 source layout
==================

The ROC1 port targets Android 9 with built-in platform drivers. Its configuration
is not a GKI configuration merely because a BSP build.config filename contains
``gki``. The established board defconfig and DT build target names are stable.

Configuration
-------------

``sprd_roc1_defconfig`` is the inherited reference-platform selection;
``iflytek_iclass-c8pro_defconfig`` and ``iflytek_istudy-c10_defconfig`` select the
product configurations. They are serialized with ``make savedefconfig`` using
the matching source tree and Clang toolchain. Regeneration must preserve the
effective configuration, including the BSP's subsequent configuration fragments.
The standard and KernelSU trees intentionally differ in the C8Pro KSU selection.

Device trees
------------

* ``sprd/ud710/`` contains the reference ROC1/UD710 platform descriptions.
  ``soc/`` and ``platform/`` hold in-place component includes; ``boards/`` holds
  reference-board machine and modem selections.
* ``sprd/sc2730/sc2730-roc1.dtsi`` is the inherited ROC1 PMIC description.
  It is separate from ``sprd/sc2730/sc2730.dtsi`` used by newer platforms.
* Reference-board ``sprd/ud710-*.dts`` and overlay targets retain their names.
* Product entry points remain ``iflytek/iclass-c8pro.dts`` and
  ``iflytek/istudy-c10.dts``. Each product directory holds its own component
  descriptions, including audio, thermal, reserved memory, panel and SoC buses.
  These reflect the product hardware and its migrated bindings, not a wholesale
  substitution of the older reference-platform descriptions.

The product fragments are included at their original nesting positions. They
are not standalone overlays. Preserve include ordering, explicit phandles and
node/property ordering when updating them. The first structural normalization
was checked against byte-identical DTB/DTBO output and matching effective kernel
configurations. Source organization does not establish hardware support for
the inherited reference boards or the unported product candidates.

Device-specific publication must exclude sibling product fragment directories
as well as their DTS entry points and defconfigs, and retain a matching DTS
Makefile. Use the project's publication entry point for this operation.
