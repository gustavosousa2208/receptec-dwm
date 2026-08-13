#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/printk.h>

#include <receptec/dw3110_radio.h>

#define DEVICE_ID_SIZE 8U
#define RECEPTEC_ROLE_LED_NODE DT_ALIAS(led3)

enum device_role {
	DEVICE_ROLE_COLETEC,
	DEVICE_ROLE_RECEPTEC,
};

/* This board (S/N 760216755, hwinfo 40:FA:8C:15:D5:02:5D:31) is dedicated to
 * running this repo's own RECEPTEC role as a reference implementation, to
 * compare against Firmware-Atletec's RECEPTEC (flashed on the other DWM3001C,
 * S/N 760216737) under identical conditions against the production COLETEC.
 * Do not run both RECEPTEC boards at the same time -- same on-air identity. */
static const uint8_t receptec_device_id[DEVICE_ID_SIZE] = {
	0x40, 0xFA, 0x8C, 0x15, 0xD5, 0x02, 0x5D, 0x31,
};

static const struct gpio_dt_spec receptec_role_led =
	GPIO_DT_SPEC_GET(RECEPTEC_ROLE_LED_NODE, gpios);

static void print_device_id(const uint8_t *device_id, size_t length)
{
	printk("Nordic device ID:");
	for (size_t i = 0U; i < length; ++i) {
		printk("%s%02X", i == 0U ? " " : ":", device_id[i]);
	}
	printk("\n");
}

static enum device_role get_device_role(const uint8_t *device_id)
{
	if (IS_ENABLED(CONFIG_RECEPTEC_FORCE_RECEPTEC_ROLE)) {
		printk("Role forced to RECEPTEC by Kconfig\n");
		return DEVICE_ROLE_RECEPTEC;
	}

	for (size_t i = 0U; i < sizeof(receptec_device_id); ++i) {
		if (device_id[i] != receptec_device_id[i]) {
			return DEVICE_ROLE_COLETEC;
		}
	}

	return DEVICE_ROLE_RECEPTEC;
}

static void configure_role_indicator(enum device_role role)
{
	int status;

	if (!gpio_is_ready_dt(&receptec_role_led)) {
		printk("RECEPTEC blue role LED GPIO is not ready\n");
		return;
	}

	status = gpio_pin_configure_dt(&receptec_role_led,
				       GPIO_OUTPUT_INACTIVE);
	if (status < 0) {
		printk("Failed to configure RECEPTEC blue role LED: %d\n",
		       status);
		return;
	}

	if (role == DEVICE_ROLE_RECEPTEC) {
		status = gpio_pin_set_dt(&receptec_role_led, 1);
		if (status < 0) {
			printk("Failed to light RECEPTEC blue role LED: %d\n",
			       status);
			return;
		}
		printk("Role indicator: blue LED on\n");
	}
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

	printk("Board: %s\n", CONFIG_BOARD_TARGET);
	print_device_id(device_id, (size_t)length);

	role = get_device_role(device_id);
	printk("Role: %s\n",
	       role == DEVICE_ROLE_RECEPTEC ? "RECEPTEC" : "COLETEC");
	configure_role_indicator(role);

	if (dw3110_radio_init() < 0) {
		printk("DW3110 bring-up failed\n");
		return -1;
	}

	return dw3110_radio_start_protocol(role == DEVICE_ROLE_RECEPTEC);
}
