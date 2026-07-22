#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/printk.h>

#include <receptec/dw3110_radio.h>

#define DEVICE_ID_SIZE 8U

enum device_role {
	DEVICE_ROLE_COLETEC,
	DEVICE_ROLE_RECEPTEC,
};

static const uint8_t receptec_device_id[DEVICE_ID_SIZE] = {
	0x3E, 0xC6, 0xF3, 0x82, 0x06, 0xA8, 0x16, 0x41,
};

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
	for (size_t i = 0U; i < sizeof(receptec_device_id); ++i) {
		if (device_id[i] != receptec_device_id[i]) {
			return DEVICE_ROLE_COLETEC;
		}
	}

	return DEVICE_ROLE_RECEPTEC;
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

	if (dw3110_radio_init() < 0) {
		printk("DW3110 bring-up failed\n");
		return -1;
	}

	return dw3110_radio_run_protocol_test(role == DEVICE_ROLE_RECEPTEC);
}
