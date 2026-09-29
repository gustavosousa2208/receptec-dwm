#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>

#include <receptec/dw3110_radio.h>

#define DEVICE_ID_SIZE 8U
#define ROLE_LED_NODE DT_ALIAS(role_led)

static const struct gpio_dt_spec role_led =
	GPIO_DT_SPEC_GET(ROLE_LED_NODE, gpios);

static void print_device_id(const uint8_t *device_id, size_t length)
{
	printk("Nordic device ID:");
	for (size_t i = 0U; i < length; ++i) {
		printk("%s%02X", i == 0U ? " " : ":", device_id[i]);
	}
	printk("\n");
}

static void configure_role_indicator(void)
{
	int status;

	if (!gpio_is_ready_dt(&role_led)) {
		printk("Role indicator GPIO is not ready\n");
		return;
	}
	status = gpio_pin_configure_dt(&role_led,
				       GPIO_OUTPUT_INACTIVE);
	if (status < 0) {
		printk("Failed to configure role indicator: %d\n",
		       status);
		return;
	}

	status = gpio_pin_set_dt(&role_led, 1);
	if (status < 0) {
		printk("Failed to light role indicator: %d\n", status);
		return;
	}
	printk("Role indicator: blue LED on\n");
}

int main(void)
{
	uint8_t device_id[DEVICE_ID_SIZE];
	ssize_t length;
	bool receptec_role;

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

	receptec_role = IS_ENABLED(CONFIG_BOARD_RECEPTEC_QORVO);
	printk("Atletec DWM UWB bench\n");
	printk("Board: %s\n", CONFIG_BOARD_TARGET);
	printk("Build: %s contracts: %s\n", DWM_BENCH_GIT_REV,
	       DWM_BENCH_CONTRACTS_REV);
	print_device_id(device_id, (size_t)length);
	if (receptec_role) {
		printk("Role: RECEPTEC\n");
	} else {
		printk("Role: fake-COLETEC-%u surrogate (on-air COLETEC-%u)\n",
		       CONFIG_DWM_COLETEC_INSTANCE,
		       CONFIG_DWM_COLETEC_INSTANCE);
	}
	configure_role_indicator();

	if (dw3110_radio_init() < 0) {
		printk("DW3110 bring-up failed\n");
		return -1;
	}

	return dw3110_radio_start_protocol(receptec_role);
}
