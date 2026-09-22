#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/shell/shell.h>

struct board_led {
	const struct gpio_dt_spec gpio;
	const char *name;
};

#define BOARD_LED(alias)                                                                    \
	{                                                                                     \
		.gpio = GPIO_DT_SPEC_GET(DT_ALIAS(alias), gpios),                              \
		.name = DT_PROP(DT_ALIAS(alias), label),                                       \
	}

static const struct board_led board_leds[] = {
	BOARD_LED(led0),
	BOARD_LED(led1),
	BOARD_LED(led2),
	BOARD_LED(led3),
	BOARD_LED(external_blue_led),
	BOARD_LED(external_green_led),
};

#define BOOT_POWER_LED_INDEX 0U
#define EXTERNAL_POWER_LED_INDEX 5U

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

	/* Keep both the module and carrier-board power indicators on at boot. */
	int status = gpio_pin_set_dt(&board_leds[BOOT_POWER_LED_INDEX].gpio, 1);

	if (status < 0) {
		return status;
	}

	return gpio_pin_set_dt(&board_leds[EXTERNAL_POWER_LED_INDEX].gpio, 1);
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

		shell_print(shell, "LED %u: %-12s P%u.%02u logical=%s raw=%s", (unsigned int)i,
			    board_leds[i].name, gpio->port == DEVICE_DT_GET(DT_NODELABEL(gpio0)) ? 0U : 1U,
			    gpio->pin, logical != 0 ? "on" : "off",
			    gpio_pin_get_raw(gpio->port, gpio->pin) != 0 ? "high" : "low");
	}

	return 0;
}

static int apply_led_operation(const struct shell *shell, size_t index, const char *operation)
{
	const struct board_led *led = &board_leds[index];
	int status;

	if (strcmp(operation, "on") == 0) {
		status = gpio_pin_set_dt(&led->gpio, 1);
	} else if (strcmp(operation, "off") == 0) {
		status = gpio_pin_set_dt(&led->gpio, 0);
	} else if (strcmp(operation, "toggle") == 0) {
		status = gpio_pin_toggle_dt(&led->gpio);
	} else if (strcmp(operation, "high") == 0) {
		status = gpio_pin_set_raw(led->gpio.port, led->gpio.pin, 1);
	} else if (strcmp(operation, "low") == 0) {
		status = gpio_pin_set_raw(led->gpio.port, led->gpio.pin, 0);
	} else {
		shell_error(shell, "operation must be on, off, toggle, high, or low");
		return -EINVAL;
	}

	if (status < 0) {
		shell_error(shell, "LED %u (%s) operation failed: %d", (unsigned int)index,
			    led->name, status);
		return status;
	}

	shell_print(shell, "LED %u (%s): %s", (unsigned int)index, led->name, operation);
	return 0;
}

static int cmd_led_control(const struct shell *shell, size_t argc, char **argv)
{
	const char *target = argv[1];
	const char *operation = argv[2];
	int status = 0;

	ARG_UNUSED(argc);

	if (strcmp(target, "all") == 0) {
		for (size_t i = 0U; i < ARRAY_SIZE(board_leds); ++i) {
			int current_status = apply_led_operation(shell, i, operation);

			if (current_status < 0) {
				status = current_status;
			}
		}
		return status;
	}

	if (target[0] < '0' || target[0] >= '0' + ARRAY_SIZE(board_leds) || target[1] != '\0') {
		shell_error(shell, "target must be 0 through %u, or all",
			    (unsigned int)ARRAY_SIZE(board_leds) - 1U);
		return -EINVAL;
	}

	return apply_led_operation(shell, (size_t)(target[0] - '0'), operation);
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_led,
	SHELL_CMD(list, NULL, "List LED names, pins, and current states", cmd_led_list),
	SHELL_CMD_ARG(control, NULL,
		      "Control LEDs: control <0..5|all> <on|off|toggle|high|low>",
		      cmd_led_control, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(led, &sub_led, "Inspect and control the four board LEDs", NULL);
