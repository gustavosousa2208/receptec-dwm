/* SPDX-License-Identifier: Apache-2.0 */

#include "ble_ota.h"

#if defined(CONFIG_BT)

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS,
		      (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
};

static int ble_ota_adv_start(void)
{
	return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data,
			       ARRAY_SIZE(adv_data), NULL, 0);
}

static struct k_work_delayable adv_work;

static void adv_worker(struct k_work *work)
{
	int ret;

	ARG_UNUSED(work);
	ret = ble_ota_adv_start();
	if (ret < 0 && ret != -EALREADY) {
		printk("BLE OTA: re-advertise failed (%d)\n", ret);
	}
	/* Self-healing watchdog: BT does not restart advertising on its
	 * own, and a restart straight from the disconnect callback is
	 * not reliable. Re-check every 30 s. */
	k_work_reschedule(&adv_work, K_SECONDS(30));
}

static void ble_ota_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);
	k_work_init_delayable(&adv_work, adv_worker);
	k_work_reschedule(&adv_work, K_MSEC(500));
}

BT_CONN_CB_DEFINE(ble_ota_conn_callbacks) = {
	.disconnected = ble_ota_disconnected,
};

static void ble_ota_thread_entry(void *a, void *b, void *c)
{
	int ret;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	ret = bt_enable(NULL);
	if (ret < 0) {
		printk("BLE OTA: not available (%d); UWB unaffected\n", ret);
		return;
	}
	printk("BLE OTA: advertising as '%s'\n", bt_get_name());
	ret = ble_ota_adv_start();
	if (ret < 0) {
		printk("BLE OTA: advertise failed (%d); UWB unaffected\n",
		       ret);
		return;
	}
	printk("BLE OTA: SMP service up\n");
	k_work_init_delayable(&adv_work, adv_worker);
	k_work_reschedule(&adv_work, K_SECONDS(30));
}

void atletec_ble_ota_init(void)
{
	static struct k_thread ble_thread;
	static K_THREAD_STACK_DEFINE(ble_stack, 2048);

	k_thread_create(&ble_thread, ble_stack, K_THREAD_STACK_SIZEOF(ble_stack),
			ble_ota_thread_entry, NULL, NULL, NULL,
			K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&ble_thread, "ble_ota");
}

#else

void atletec_ble_ota_init(void)
{
}

#endif /* CONFIG_BT */
