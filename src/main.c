#include <zephyr/sys/printk.h>

#include <receptec/coletec_radio.h>

int main(void)
{
	printk("Board: %s\n", CONFIG_BOARD_TARGET);
	if (coletec_radio_init() < 0) {
		printk("DW3110 bring-up failed\n");
		return -1;
	}

	return coletec_radio_start();
}
