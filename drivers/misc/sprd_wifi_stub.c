/*
 * Compatibility symbol required by the factory bcmdhd module.
 * (moved from drivers/soc/sprd/wifi_stub.c)
 */
#include <linux/export.h>

int wifi6_oob_wakelock;
EXPORT_SYMBOL(wifi6_oob_wakelock);
