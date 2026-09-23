/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/printk.h>

#include <stdint.h>

#include "uwb_radio.h"

int main(void)
{
	uint8_t id[8];
	ssize_t length = hwinfo_get_device_id(id, sizeof(id));

	printk("RECEPTEC Type2AB baseline\n");
	printk("UARTE1: Pi GPIO14/15, 115200 8N1\n");
	if (length == (ssize_t)sizeof(id)) {
		printk("nRF ID:");
		for (size_t i = 0U; i < sizeof(id); ++i) {
			printk("%s%02X", i == 0U ? " " : ":", id[i]);
		}
		printk("\n");
	} else {
		printk("nRF ID unavailable: %d\n", (int)length);
	}
#if defined(CONFIG_RECEPTEC_UWB_RADIO)
	int ret = receptec_uwb_radio_init();
	if (ret == 0) {
		printk("UWB radio: initialized; use 'uwb peer list' and 'uwb sync <slot>'\n");
	} else {
		printk("UWB radio initialization failed: %d\n", ret);
	}
#else
	printk("UWB radio: disabled by configuration\n");
#endif
	return 0;
}
