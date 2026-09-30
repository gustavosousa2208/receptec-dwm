/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "led_shell.h"

#define LED_SELFTEST_DURATION_MS 3000U

struct board_led {
	const struct gpio_dt_spec gpio;
	const char *name;
};

#define BOARD_LED(node_id) \
	{ \
		.gpio = GPIO_DT_SPEC_GET(node_id, gpios), \
		.name = DT_NODE_FULL_NAME(node_id), \
	},

/* This board group is the authoritative inventory. Disabled DT children are omitted. */
static const struct board_led board_leds[] = {
	DT_FOREACH_CHILD_STATUS_OKAY(DT_COMPAT_GET_ANY_STATUS_OKAY(gpio_leds), BOARD_LED)
};

#define ALL_LED_BITS ((uint32_t)((1U << ARRAY_SIZE(board_leds)) - 1U))

static atomic_t led_override_mask;
static atomic_t led_override_values;
static atomic_t led_test_active;
static atomic_t led_test_saved_override_mask;
static atomic_t led_test_saved_override_values;
static size_t led_test_phase;
static uint8_t led_test_saved_states[ARRAY_SIZE(board_leds)];

static void led_test_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(led_test_work, led_test_work_handler);

#if DT_NODE_EXISTS(DT_ALIAS(led_blue))
static const struct gpio_dt_spec led_blue_alias =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_blue), gpios);
#define HAS_LED_BLUE_ALIAS 1
#else
#define HAS_LED_BLUE_ALIAS 0
#endif

#if DT_NODE_EXISTS(DT_ALIAS(led_green))
static const struct gpio_dt_spec led_green_alias =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_green), gpios);
#define HAS_LED_GREEN_ALIAS 1
#else
#define HAS_LED_GREEN_ALIAS 0
#endif

#if DT_NODE_EXISTS(DT_ALIAS(role_led))
static const struct gpio_dt_spec role_led_alias =
	GPIO_DT_SPEC_GET(DT_ALIAS(role_led), gpios);
#define HAS_ROLE_LED_ALIAS 1
#else
#define HAS_ROLE_LED_ALIAS 0
#endif

#if DT_NODE_EXISTS(DT_ALIAS(boot_power_led))
static const struct gpio_dt_spec boot_power_led_alias =
	GPIO_DT_SPEC_GET(DT_ALIAS(boot_power_led), gpios);
#define HAS_BOOT_POWER_LED_ALIAS 1
#else
#define HAS_BOOT_POWER_LED_ALIAS 0
#endif

#if DT_NODE_EXISTS(DT_ALIAS(external_power_led))
static const struct gpio_dt_spec external_power_led_alias =
	GPIO_DT_SPEC_GET(DT_ALIAS(external_power_led), gpios);
#define HAS_EXTERNAL_POWER_LED_ALIAS 1
#else
#define HAS_EXTERNAL_POWER_LED_ALIAS 0
#endif

static int led_index_for_gpio(const struct gpio_dt_spec *gpio)
{
	for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
		if (board_leds[i].gpio.port == gpio->port &&
		    board_leds[i].gpio.pin == gpio->pin) {
			return (int)i;
		}
	}
	return -ENOENT;
}

bool led_shell_is_overridden(const struct gpio_dt_spec *gpio)
{
	int index;

	if (gpio == NULL) {
		return false;
	}
	index = led_index_for_gpio(gpio);
	return index >= 0 && (atomic_get(&led_override_mask) & BIT(index)) != 0;
}

static int set_led_state(size_t index, bool on)
{
	return gpio_pin_set_dt(&board_leds[index].gpio, on ? 1 : 0);
}

static void set_startup_alias(const struct gpio_dt_spec *gpio)
{
	int index = led_index_for_gpio(gpio);

	if (index >= 0) {
		(void)set_led_state((size_t)index, true);
	}
}

static int led_shell_init(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
		if (!gpio_is_ready_dt(&board_leds[i].gpio)) {
			return -ENODEV;
		}

		int status = gpio_pin_configure_dt(&board_leds[i].gpio,
						  GPIO_OUTPUT_INACTIVE);

		if (status < 0) {
			return status;
		}
	}

#if HAS_BOOT_POWER_LED_ALIAS
	set_startup_alias(&boot_power_led_alias);
#endif
#if HAS_EXTERNAL_POWER_LED_ALIAS
	set_startup_alias(&external_power_led_alias);
#endif
#if HAS_ROLE_LED_ALIAS
	set_startup_alias(&role_led_alias);
#endif
	return 0;
}

SYS_INIT(led_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static int cmd_led_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
		const struct gpio_dt_spec *gpio = &board_leds[i].gpio;
		int logical = gpio_pin_get_dt(gpio);

		if (logical < 0) {
			shell_error(shell, "LED %u (%s): read failed: %d", (unsigned int)i,
				    board_leds[i].name, logical);
			continue;
		}

		shell_print(shell, "LED %u: %-20s P%u.%02u logical=%s raw=%s",
			    (unsigned int)i, board_leds[i].name,
			    gpio->port == DEVICE_DT_GET(DT_NODELABEL(gpio0)) ? 0U : 1U,
			    gpio->pin, logical != 0 ? "on" : "off",
			    gpio_pin_get_raw(gpio->port, gpio->pin) != 0 ? "high" : "low");
	}
	return 0;
}

static int led_index_by_name(const char *name)
{
	for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
		if (strcmp(name, board_leds[i].name) == 0) {
			return (int)i;
		}
	}
	return -ENOENT;
}

static int parse_led_target(const char *target, uint32_t *mask)
{
	int index;
	size_t parsed = 0U;
	const char *digit = target;

	if (strcmp(target, "all") == 0) {
		*mask = ALL_LED_BITS;
		return 0;
	}
#if HAS_LED_BLUE_ALIAS
	if (strcmp(target, "blue") == 0) {
		index = led_index_for_gpio(&led_blue_alias);
		if (index >= 0) {
			*mask = BIT(index);
			return 0;
		}
	}
#endif
#if HAS_LED_GREEN_ALIAS
	if (strcmp(target, "green") == 0) {
		index = led_index_for_gpio(&led_green_alias);
		if (index >= 0) {
			*mask = BIT(index);
			return 0;
		}
	}
#endif
	index = led_index_by_name(target);
	if (index >= 0) {
		*mask = BIT(index);
		return 0;
	}
	if (*digit == '\0') {
		return -EINVAL;
	}
	while (*digit != '\0') {
		if (*digit < '0' || *digit > '9') {
			return -EINVAL;
		}
		parsed = parsed * 10U + (size_t)(*digit - '0');
		if (parsed >= ARRAY_SIZE(board_leds)) {
			return -EINVAL;
		}
		++digit;
	}
	*mask = BIT(parsed);
	return 0;
}

static int apply_led_operation(size_t index, const char *operation)
{
	const struct gpio_dt_spec *gpio = &board_leds[index].gpio;
	int status;
	int state;

	if (strcmp(operation, "on") == 0) {
		status = gpio_pin_set_dt(gpio, 1);
	} else if (strcmp(operation, "off") == 0) {
		status = gpio_pin_set_dt(gpio, 0);
	} else if (strcmp(operation, "toggle") == 0) {
		status = gpio_pin_toggle_dt(gpio);
	} else if (strcmp(operation, "high") == 0) {
		status = gpio_pin_set_raw(gpio->port, gpio->pin, 1);
	} else if (strcmp(operation, "low") == 0) {
		status = gpio_pin_set_raw(gpio->port, gpio->pin, 0);
	} else {
		return -EINVAL;
	}
	if (status < 0) {
		return status;
	}

	state = gpio_pin_get_dt(gpio);
	if (state < 0) {
		return state;
	}
	atomic_or(&led_override_mask, BIT(index));
	if (state != 0) {
		atomic_or(&led_override_values, BIT(index));
	} else {
		atomic_and(&led_override_values, (atomic_val_t)~BIT(index));
	}
	return 0;
}

static void cancel_led_test(void)
{
	struct k_work_sync sync;

	(void)k_work_cancel_delayable_sync(&led_test_work, &sync);
	if (atomic_get(&led_test_active) != 0) {
		for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
			(void)set_led_state(i, led_test_saved_states[i] != 0U);
		}
		atomic_set(&led_override_mask, atomic_get(&led_test_saved_override_mask));
		atomic_set(&led_override_values, atomic_get(&led_test_saved_override_values));
	}
	atomic_set(&led_test_active, 0);
}

static int cmd_led_control(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t selected;
	const char *operation;
	int status = 0;

	if (argc < 3U || parse_led_target(argv[1], &selected) < 0) {
		shell_error(shell, "usage: led control <index|name|all> <on|off|toggle|high|low>");
		return -EINVAL;
	}
	operation = argv[2];
	cancel_led_test();
	for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
		if ((selected & BIT(i)) == 0U) {
			continue;
		}
		int current = apply_led_operation(i, operation);
		if (current < 0) {
			shell_error(shell, "LED %u (%s) operation failed: %d",
				    (unsigned int)i, board_leds[i].name, current);
			status = current;
		} else {
			shell_print(shell, "LED %u (%s): %s", (unsigned int)i,
				    board_leds[i].name, operation);
		}
	}
	return status;
}

static int cmd_led_set(const struct shell *shell, size_t argc, char **argv)
{
	const char *state;
	int status = 0;

	if (argc < 3U) {
		shell_error(shell, "usage: led set <on|off|toggle> <index|blue|green|name|all> [...]");
		return -EINVAL;
	}
	state = argv[1];
	if (strcmp(state, "on") != 0 && strcmp(state, "off") != 0 &&
	    strcmp(state, "toggle") != 0) {
		shell_error(shell, "state must be on, off, or toggle");
		return -EINVAL;
	}
	cancel_led_test();
	for (size_t arg = 2U; arg < argc; ++arg) {
		uint32_t selected;
		if (parse_led_target(argv[arg], &selected) < 0) {
			shell_error(shell, "unknown or unavailable LED '%s'", argv[arg]);
			status = -EINVAL;
			continue;
		}
		for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
			if ((selected & BIT(i)) == 0U) {
				continue;
			}
			int current = apply_led_operation(i, state);
			if (current < 0) {
				shell_error(shell, "LED %u (%s) operation failed: %d",
					    (unsigned int)i, board_leds[i].name, current);
				status = current;
			}
		}
	}
	return status;
}

static int cmd_led_auto(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	cancel_led_test();
	atomic_set(&led_override_mask, 0);
	atomic_set(&led_override_values, 0);
	for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
		(void)set_led_state(i, false);
	}
#if HAS_BOOT_POWER_LED_ALIAS
	set_startup_alias(&boot_power_led_alias);
#endif
#if HAS_EXTERNAL_POWER_LED_ALIAS
	set_startup_alias(&external_power_led_alias);
#endif
#if HAS_ROLE_LED_ALIAS
	set_startup_alias(&role_led_alias);
#endif
	shell_print(shell, "LED automatic behavior resumed");
	return 0;
}

static void led_test_work_handler(struct k_work *work)
{
	const size_t led_count = ARRAY_SIZE(board_leds);
	const size_t phase_count = led_count + 2U;
	const int step_ms = LED_SELFTEST_DURATION_MS / phase_count;
	ARG_UNUSED(work);

	if (atomic_get(&led_test_active) == 0) {
		return;
	}
	if (led_test_phase >= phase_count) {
		for (size_t i = 0U; i < led_count; ++i) {
			(void)set_led_state(i, led_test_saved_states[i] != 0U);
		}
		atomic_set(&led_test_active, 0);
		atomic_set(&led_override_mask, atomic_get(&led_test_saved_override_mask));
		atomic_set(&led_override_values, atomic_get(&led_test_saved_override_values));
		printk("LED self-test complete; previous LED states restored\n");
		return;
	}

	for (size_t i = 0U; i < led_count; ++i) {
		bool on = led_test_phase == led_count || led_test_phase == i;
		(void)set_led_state(i, on);
	}
	++led_test_phase;
	(void)k_work_reschedule(&led_test_work, K_MSEC(step_ms));
}

static int cmd_led_selftest(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	cancel_led_test();
	for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
		int state = gpio_pin_get_dt(&board_leds[i].gpio);
		if (state < 0) {
			shell_error(shell, "cannot read LED %u (%s): %d", (unsigned int)i,
				    board_leds[i].name, state);
			return state;
		}
		led_test_saved_states[i] = (uint8_t)state;
	}
	atomic_set(&led_test_saved_override_mask, atomic_get(&led_override_mask));
	atomic_set(&led_test_saved_override_values, atomic_get(&led_override_values));
	led_test_phase = 0U;
	atomic_set(&led_override_mask, (atomic_val_t)ALL_LED_BITS);
	atomic_set(&led_override_values, 0);
	atomic_set(&led_test_active, 1);
	(void)k_work_reschedule(&led_test_work, K_NO_WAIT);
	shell_print(shell, "LED self-test started for %u LEDs (3 seconds)",
		    (unsigned int)ARRAY_SIZE(board_leds));
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_led,
	SHELL_CMD(list, NULL, "List active board LEDs and their states", cmd_led_list),
	SHELL_CMD_ARG(control, NULL, "Control LED index/name: control <target> <operation>",
		      cmd_led_control, 3, 0),
	SHELL_CMD_ARG(set, NULL, "Set LEDs: set <on|off|toggle> <target> [...]",
		      cmd_led_set, 3, SHELL_OPT_ARG_CHECK_SKIP),
	SHELL_CMD(auto, NULL, "Resume automatic indicator behavior", cmd_led_auto),
	SHELL_CMD(selftest, NULL, "Run the 3-second self-test on active board LEDs",
		  cmd_led_selftest),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(led, &sub_led, "Inspect and control active board LEDs", NULL);
