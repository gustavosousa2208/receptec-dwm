/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "led_shell.h"

#define LED_BLUE  BIT(0)
#define LED_GREEN BIT(1)
#define LED_ALL   (LED_BLUE | LED_GREEN)
#define LED_TEST_PHASE_MS 500U

static const struct gpio_dt_spec led_blue =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_blue), gpios);

#if DT_NODE_EXISTS(DT_ALIAS(led_green))
#define HAS_LED_GREEN 1
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_green), gpios);
#else
#define HAS_LED_GREEN 0
#endif

static atomic_t led_override_mask;
static atomic_t led_override_values;
static atomic_t led_test_active;
static uint8_t led_test_phase;
static void led_test_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(led_test_work, led_test_work_handler);

static void cancel_led_test(void)
{
	struct k_work_sync sync;

	(void)k_work_cancel_delayable_sync(&led_test_work, &sync);
	atomic_set(&led_test_active, 0);
}

static uint8_t available_leds(void)
{
	return LED_BLUE | (HAS_LED_GREEN ? LED_GREEN : 0U);
}

int receptec_led_shell_init(void)
{
	int status;

	if (!gpio_is_ready_dt(&led_blue)) {
		return -ENODEV;
	}
	status = gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
	if (status < 0) {
		return status;
	}
#if HAS_LED_GREEN
	if (!gpio_is_ready_dt(&led_green)) {
		return -ENODEV;
	}
	status = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	if (status < 0) {
		return status;
	}
#endif
	return 0;
}

bool receptec_led_shell_green_overridden(void)
{
	return (atomic_get(&led_override_mask) & LED_GREEN) != 0;
}

static int set_led_mask(uint8_t mask)
{
	int status = gpio_pin_set_dt(&led_blue, (mask & LED_BLUE) != 0U ? 1 : 0);

#if HAS_LED_GREEN
	int green_status = gpio_pin_set_dt(&led_green,
					   (mask & LED_GREEN) != 0U ? 1 : 0);
	if (status == 0) {
		status = green_status;
	}
#endif
	return status;
}

static int set_selected_leds(uint8_t selected, uint8_t values)
{
	int status = 0;

	if ((selected & LED_BLUE) != 0U) {
		status = gpio_pin_set_dt(&led_blue,
					 (values & LED_BLUE) != 0U ? 1 : 0);
	}
#if HAS_LED_GREEN
	if ((selected & LED_GREEN) != 0U) {
		int green_status = gpio_pin_set_dt(&led_green,
						  (values & LED_GREEN) != 0U ? 1 : 0);
		if (status == 0) {
			status = green_status;
		}
	}
#endif
	return status;
}

static int parse_led(const char *name, uint8_t *mask)
{
	if (strcmp(name, "blue") == 0) {
		*mask = LED_BLUE;
		return 0;
	}
#if HAS_LED_GREEN
	if (strcmp(name, "green") == 0) {
		*mask = LED_GREEN;
		return 0;
	}
#endif
	if (strcmp(name, "all") == 0) {
		*mask = available_leds();
		return 0;
	}
	return -EINVAL;
}

static int cmd_led_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(shell, "blue: %s", gpio_pin_get_dt(&led_blue) > 0 ? "on" : "off");
#if HAS_LED_GREEN
	shell_print(shell, "green: %s",
		    gpio_pin_get_dt(&led_green) > 0 ? "on" : "off");
#endif
	return 0;
}

static int cmd_led_set(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t selected = 0U;
	bool on;

	if (argc < 3U) {
		shell_error(shell, "usage: led set <on|off> <blue|green|all> [...]");
		return -EINVAL;
	}
	if (strcmp(argv[1], "on") == 0) {
		on = true;
	} else if (strcmp(argv[1], "off") == 0) {
		on = false;
	} else {
		shell_error(shell, "state must be on or off");
		return -EINVAL;
	}
	for (size_t i = 2U; i < argc; ++i) {
		uint8_t mask;
		if (parse_led(argv[i], &mask) < 0) {
			shell_error(shell, "unknown or unavailable LED '%s'", argv[i]);
			return -EINVAL;
		}
		selected |= mask;
	}

	cancel_led_test();
	if (on) {
		atomic_or(&led_override_values, selected);
	} else {
		atomic_and(&led_override_values, (atomic_val_t)~selected);
	}
	atomic_or(&led_override_mask, selected);
	return set_selected_leds(selected,
				 (uint8_t)atomic_get(&led_override_values));
}

static int cmd_led_auto(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	cancel_led_test();
	atomic_set(&led_override_mask, 0);
	atomic_set(&led_override_values, 0);
	(void)set_led_mask(LED_BLUE);
	shell_print(shell, "LED automatic behavior resumed");
	return 0;
}

static void led_test_work_handler(struct k_work *work)
{
	static const uint8_t phases[] = {
		LED_BLUE,
		LED_GREEN,
		LED_BLUE | LED_GREEN,
		0U,
		LED_ALL,
		0U,
	};

	ARG_UNUSED(work);
	if (atomic_get(&led_test_active) == 0) {
		return;
	}
	if (led_test_phase == ARRAY_SIZE(phases)) {
		atomic_set(&led_test_active, 0);
		atomic_set(&led_override_mask, 0);
		atomic_set(&led_override_values, 0);
		(void)set_led_mask(LED_BLUE);
		printk("LED self-test complete; automatic behavior resumed\n");
		return;
	}
	(void)set_led_mask(phases[led_test_phase++]);
	(void)k_work_reschedule(&led_test_work, K_MSEC(LED_TEST_PHASE_MS));
}

static int cmd_led_selftest(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	cancel_led_test();
	led_test_phase = 0U;
	atomic_set(&led_override_values, 0);
	atomic_set(&led_override_mask, available_leds());
	atomic_set(&led_test_active, 1);
	(void)k_work_reschedule(&led_test_work, K_NO_WAIT);
	shell_print(shell, "LED self-test started (3 seconds)");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_led,
	SHELL_CMD(list, NULL, "List LEDs and their states", cmd_led_list),
	SHELL_CMD_ARG(set, NULL, "Set LED state: set <on|off> <blue|green|all> [...]",
		      cmd_led_set, 3, 1),
	SHELL_CMD(auto, NULL, "Resume automatic indicator behavior", cmd_led_auto),
	SHELL_CMD(selftest, NULL, "Run the 3-second LED self-test", cmd_led_selftest),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(led, &sub_led, "RECEPTEC LED controls", NULL);
