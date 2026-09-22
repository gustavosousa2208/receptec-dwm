#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/printk.h>

#include <receptec/dw3110_radio.h>

#define DEVICE_ID_SIZE 8U
#define RECEPTEC_ROLE_LED_NODE DT_ALIAS(receptec_role_led)
#define COLETEC_ROLE_LED_NODE DT_ALIAS(coletec_role_led)

enum device_role {
	DEVICE_ROLE_COLETEC,
	DEVICE_ROLE_RECEPTEC,
};

static const struct gpio_dt_spec receptec_role_led =
	GPIO_DT_SPEC_GET(RECEPTEC_ROLE_LED_NODE, gpios);
static const struct gpio_dt_spec coletec_role_led =
	GPIO_DT_SPEC_GET(COLETEC_ROLE_LED_NODE, gpios);

static void print_device_id(const uint8_t *device_id, size_t length)
{
	printk("Nordic device ID:");
	for (size_t i = 0U; i < length; ++i) {
		printk("%s%02X", i == 0U ? " " : ":", device_id[i]);
	}
	printk("\n");
}

static enum device_role configured_role(void)
{
	return IS_ENABLED(CONFIG_DWM_UWB_ROLE_RECEPTEC) ?
		DEVICE_ROLE_RECEPTEC : DEVICE_ROLE_COLETEC;
}

static void configure_role_indicator(enum device_role role)
{
	const struct gpio_dt_spec *active_led;
	const char *active_color;
	int status;

	if (!gpio_is_ready_dt(&receptec_role_led)) {
		printk("RECEPTEC red role LED GPIO is not ready\n");
		return;
	}
	status = gpio_pin_configure_dt(&receptec_role_led,
				       GPIO_OUTPUT_INACTIVE);
	if (status < 0) {
		printk("Failed to configure RECEPTEC red role LED: %d\n",
		       status);
		return;
	}

	if (!gpio_is_ready_dt(&coletec_role_led)) {
		printk("COLETEC blue role LED GPIO is not ready\n");
		return;
	}
	status = gpio_pin_configure_dt(&coletec_role_led,
				       GPIO_OUTPUT_INACTIVE);
	if (status < 0) {
		printk("Failed to configure COLETEC blue role LED: %d\n",
		       status);
		return;
	}

	if (role == DEVICE_ROLE_RECEPTEC) {
		active_led = &receptec_role_led;
		active_color = "red";
	} else {
		active_led = &coletec_role_led;
		active_color = "blue";
	}

	status = gpio_pin_set_dt(active_led, 1);
	if (status < 0) {
		printk("Failed to light %s role LED: %d\n", active_color,
		       status);
		return;
	}
	printk("Role indicator: %s LED on\n", active_color);
}

int main(void)
{
	uint8_t device_id[DEVICE_ID_SIZE];
	enum device_role role;
	ssize_t length;

	length = hwinfo_get_device_id(device_id, sizeof(device_id));
	if (length < 0) {
		printk("Failed to read Nordic device ID: %d\n", (int)length);
		return (int)length;
	}

	if ((size_t)length != sizeof(device_id)) {
		printk("Unexpected Nordic device ID length: %d (expected %u)\n",
		       (int)length, (unsigned int)sizeof(device_id));
		return -1;
	}

	printk("Atletec DWM UWB bench\n");
	printk("Board: %s\n", CONFIG_BOARD_TARGET);
	printk("Build: %s contracts: %s\n", DWM_BENCH_GIT_REV,
	       DWM_BENCH_CONTRACTS_REV);
	print_device_id(device_id, (size_t)length);

	role = configured_role();
	printk("Role: %s\n",
	       role == DEVICE_ROLE_RECEPTEC ? "RECEPTEC" : "COLETEC");
	configure_role_indicator(role);

	if (dw3110_radio_init() < 0) {
		printk("DW3110 bring-up failed\n");
		return -1;
	}

	return dw3110_radio_start_protocol(role == DEVICE_ROLE_RECEPTEC);
}
