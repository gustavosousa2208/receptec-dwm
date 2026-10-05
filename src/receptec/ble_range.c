/* SPDX-License-Identifier: Apache-2.0 */

#include "ble_range.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define BLE_RANGE_SVC_UUID 0x3110
#define BLE_RANGE_CHR_BASE 0x3111
#define BLE_RANGE_PAIR_COUNT 3U
#define BLE_RANGE_VALUE_LEN 5U /* status u8 + distance_mm u32 LE */

/*
 * Latest result per pair. Written by the UWB range thread, read by the BT
 * stack on notify; a torn 5-byte copy only ever affects one notification.
 */
static uint8_t range_values[BLE_RANGE_PAIR_COUNT][BLE_RANGE_VALUE_LEN];

static ssize_t read_range(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 attr->user_data, BLE_RANGE_VALUE_LEN);
}

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(value);
}

BT_GATT_SERVICE_DEFINE(range_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_16(BLE_RANGE_SVC_UUID)),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(BLE_RANGE_CHR_BASE),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_range, NULL,
			       &range_values[0]),
	BT_GATT_CCC(ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(BLE_RANGE_CHR_BASE + 1U),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_range, NULL,
			       &range_values[1]),
	BT_GATT_CCC(ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(BLE_RANGE_CHR_BASE + 2U),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_range, NULL,
			       &range_values[2]),
	BT_GATT_CCC(ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* Attribute indexes of the three characteristic values in range_svc.attrs. */
#define RANGE_VALUE_ATTR_INDEX(pair) ((uint16_t)(2U + (pair) * 2U))

void receptec_ble_range_publish(uint8_t pair_index, uint8_t status,
				uint32_t distance_mm)
{
	uint8_t encoded[BLE_RANGE_VALUE_LEN];

	if (pair_index >= BLE_RANGE_PAIR_COUNT) {
		return;
	}
	encoded[0] = status;
	encoded[1] = (uint8_t)(distance_mm & 0xFFU);
	encoded[2] = (uint8_t)((distance_mm >> 8) & 0xFFU);
	encoded[3] = (uint8_t)((distance_mm >> 16) & 0xFFU);
	encoded[4] = (uint8_t)((distance_mm >> 24) & 0xFFU);
	memcpy(range_values[pair_index], encoded, sizeof(encoded));
	bt_gatt_notify(NULL, &range_svc.attrs[RANGE_VALUE_ATTR_INDEX(pair_index)],
		       encoded, sizeof(encoded));
}

static void ble_connected(struct bt_conn *conn, uint8_t err)
{
	if (err == 0U) {
		printk("BLE range: central connected\n");
	} else {
		printk("BLE range: connection failed: %u\n", err);
	}
	ARG_UNUSED(conn);
}

static void ble_disconnected(struct bt_conn *conn, uint8_t reason)
{
	int ret;

	printk("BLE range: central disconnected (reason %u), re-advertising\n",
	       reason);
	ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, NULL, 0, NULL, 0);
	if (ret < 0) {
		printk("BLE range: re-advertise failed: %d\n", ret);
	}
	ARG_UNUSED(conn);
}

BT_CONN_CB_DEFINE(ble_range_conn_cb) = {
	.connected = ble_connected,
	.disconnected = ble_disconnected,
};

static void ble_thread_entry(void *unused_a, void *unused_b, void *unused_c)
{
	static const uint8_t adv_flags[] = {
		BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR,
	};
	static const uint8_t adv_uuid[] = {
		(uint8_t)(BLE_RANGE_SVC_UUID & 0xFFU),
		(uint8_t)((BLE_RANGE_SVC_UUID >> 8) & 0xFFU),
	};
	static const struct bt_data adv_data[] = {
		BT_DATA(BT_DATA_FLAGS, adv_flags, sizeof(adv_flags)),
		BT_DATA(BT_DATA_UUID16_ALL, adv_uuid, sizeof(adv_uuid)),
		BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
			sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
	};
	int ret;

	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	ret = bt_enable(NULL);
	if (ret < 0) {
		printk("BLE range: not available (%d); UWB unaffected\n", ret);
		return;
	}
	printk("BLE range: advertising as '%s'\n", bt_get_name());
	ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data,
			      ARRAY_SIZE(adv_data), NULL, 0);
	if (ret < 0) {
		printk("BLE range: advertise failed (%d); UWB unaffected\n",
		       ret);
		return;
	}
	printk("BLE range: advertising UWB range service\n");
}

void receptec_ble_range_init(void)
{
	static struct k_thread ble_thread;
	static K_THREAD_STACK_DEFINE(ble_stack, 2048);

	k_thread_create(&ble_thread, ble_stack,
			K_THREAD_STACK_SIZEOF(ble_stack), ble_thread_entry,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&ble_thread, "ble_range");
}
