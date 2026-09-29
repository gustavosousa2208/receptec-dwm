/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stdint.h>

#include "uwb_radio.h"

/* Blue is common to both carriers; only the Type2AB carrier has a green LED. */
static const struct gpio_dt_spec led_blue =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_blue), gpios);

#if !defined(CONFIG_BOARD_RECEPTEC_QORVO)
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_green), gpios);

static void heartbeat_expired(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	(void)gpio_pin_toggle_dt(&led_green);
}

static K_TIMER_DEFINE(heartbeat_timer, heartbeat_expired, NULL);
#endif

static void indicators_start(k_timeout_t half_period)
{
#if defined(CONFIG_BOARD_RECEPTEC_QORVO)
	ARG_UNUSED(half_period);
	if (gpio_is_ready_dt(&led_blue)) {
		(void)gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_ACTIVE);
	} else {
		printk("Blue status LED unavailable\n");
	}
#else
	if (gpio_is_ready_dt(&led_blue) && gpio_is_ready_dt(&led_green) &&
	    gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_ACTIVE) == 0 &&
	    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE) == 0) {
		k_timer_start(&heartbeat_timer, half_period, half_period);
	} else {
		printk("Status LEDs unavailable\n");
	}
#endif
}

int main(void)
{
	uint8_t id[8];
	k_timeout_t heartbeat = K_MSEC(500);
	ssize_t length = hwinfo_get_device_id(id, sizeof(id));

	printk("Atletec RECEPTEC firmware\n");
	printk("Board: %s\n", CONFIG_BOARD_TARGET);
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
		heartbeat = K_MSEC(100);
	}
#else
	printk("UWB radio: disabled by configuration\n");
#endif
	indicators_start(heartbeat);
	return 0;
}
