/* SPDX-License-Identifier: Apache-2.0 */

#include "ble_range.h"

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define BLE_RANGE_SVC_VAL                                                      \
  BT_UUID_128_ENCODE(0x00003100, 0x4550, 0x5453, 0x4154, 0x4C4554454300ULL)
#define BLE_RANGE_CHR_VAL(_short)                                              \
  BT_UUID_128_ENCODE(0x00000000 | (_short), 0x4550, 0x5453, 0x4154,            \
                     0x4C4554454300ULL)
#define BLE_RANGE_SVC_UUID BT_UUID_DECLARE_128(BLE_RANGE_SVC_VAL)
#define BLE_RANGE_CHR_UUID(_pair)                                              \
  BT_UUID_DECLARE_128(BLE_RANGE_CHR_VAL(0x3101 + (_pair)))
#define BLE_RANGE_PAIR_COUNT 3U
#define BLE_RANGE_VALUE_LEN 5U /* status u8 + distance_mm u32 LE */

#define BLE_RANGE_METADATA_LEN 20U
#define BLE_RANGE_METADATA_UUID BT_UUID_DECLARE_128(BLE_RANGE_CHR_VAL(0x3104))

static struct k_spinlock range_lock;
static uint8_t range_values[BLE_RANGE_PAIR_COUNT][BLE_RANGE_VALUE_LEN] = {
    {1, 0xff, 0xff, 0xff, 0xff},
    {1, 0xff, 0xff, 0xff, 0xff},
    {1, 0xff, 0xff, 0xff, 0xff},
};
static uint8_t range_metadata[BLE_RANGE_PAIR_COUNT][BLE_RANGE_METADATA_LEN] = {
    {1, 0, RECEPTEC_BLE_RANGE_UNAVAILABLE, 1},
    {1, 1, RECEPTEC_BLE_RANGE_UNAVAILABLE, 1},
    {1, 2, RECEPTEC_BLE_RANGE_UNAVAILABLE, 1},
};
static int64_t range_updated_ms[BLE_RANGE_PAIR_COUNT];

static void metadata_snapshot(uint8_t pair, uint8_t *out, int64_t now) {
  memcpy(out, range_metadata[pair], BLE_RANGE_METADATA_LEN);
  uint64_t age = (uint64_t)(now - range_updated_ms[pair]);
  sys_put_le32(out[2] == RECEPTEC_BLE_RANGE_UNAVAILABLE
                   ? UINT32_MAX
                   : (uint32_t)MIN(age, UINT32_MAX),
               out + 16);
}

static ssize_t read_range(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          void *buf, uint16_t len, uint16_t offset) {
  uint8_t snapshot[BLE_RANGE_VALUE_LEN];
  k_spinlock_key_t key = k_spin_lock(&range_lock);
  memcpy(snapshot, attr->user_data, sizeof(snapshot));
  k_spin_unlock(&range_lock, key);
  return bt_gatt_attr_read(conn, attr, buf, len, offset, snapshot,
                           sizeof(snapshot));
}

static ssize_t read_metadata(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr, void *buf,
                             uint16_t len, uint16_t offset) {
  uint8_t snapshot[BLE_RANGE_PAIR_COUNT * BLE_RANGE_METADATA_LEN];
  k_spinlock_key_t key = k_spin_lock(&range_lock);
  int64_t now = k_uptime_get();
  for (uint8_t i = 0; i < BLE_RANGE_PAIR_COUNT; i++) {
    metadata_snapshot(i, snapshot + i * BLE_RANGE_METADATA_LEN, now);
  }
  k_spin_unlock(&range_lock, key);
  return bt_gatt_attr_read(conn, attr, buf, len, offset, snapshot,
                           sizeof(snapshot));
}

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
  ARG_UNUSED(attr);
  ARG_UNUSED(value);
}

BT_GATT_SERVICE_DEFINE(
    range_svc, BT_GATT_PRIMARY_SERVICE(BLE_RANGE_SVC_UUID),
    BT_GATT_CHARACTERISTIC(BLE_RANGE_CHR_UUID(0),
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, read_range, NULL,
                           &range_values[0]),
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BLE_RANGE_CHR_UUID(1),
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, read_range, NULL,
                           &range_values[1]),
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BLE_RANGE_CHR_UUID(2),
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, read_range, NULL,
                           &range_values[2]),
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BLE_RANGE_METADATA_UUID,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, read_metadata, NULL, NULL),
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/* Each characteristic contributes declaration, value and CCC attributes. */
#define RANGE_VALUE_ATTR_INDEX(pair) ((uint16_t)(2U + (pair) * 3U))
#define RANGE_METADATA_ATTR_INDEX 11U

void receptec_ble_range_publish_extended(uint8_t pair_index, uint8_t source,
                                         uint8_t status, uint64_t round_id,
                                         uint32_t distance_mm) {
  uint8_t encoded[BLE_RANGE_VALUE_LEN];
  uint8_t metadata[BLE_RANGE_METADATA_LEN] = {1, pair_index, source, status};
  if (pair_index >= BLE_RANGE_PAIR_COUNT ||
      source > RECEPTEC_BLE_RANGE_UNAVAILABLE) {
    return;
  }
  if (status != 0U || source == RECEPTEC_BLE_RANGE_UNAVAILABLE) {
    distance_mm = UINT32_MAX;
  }
  if (source == RECEPTEC_BLE_RANGE_UNAVAILABLE && status == 0U) {
    status = 1U;
    metadata[3] = status;
  }
  encoded[0] = status;
  sys_put_le32(distance_mm, encoded + 1);
  sys_put_le64(round_id, metadata + 4);
  sys_put_le32(distance_mm, metadata + 12);
  sys_put_le32(source == RECEPTEC_BLE_RANGE_UNAVAILABLE ? UINT32_MAX : 0,
               metadata + 16);
  k_spinlock_key_t key = k_spin_lock(&range_lock);
  memcpy(range_values[pair_index], encoded, sizeof(encoded));
  memcpy(range_metadata[pair_index], metadata, sizeof(metadata));
  range_updated_ms[pair_index] = k_uptime_get();
  k_spin_unlock(&range_lock, key);
  bt_gatt_notify(NULL, &range_svc.attrs[RANGE_VALUE_ATTR_INDEX(pair_index)],
                 encoded, sizeof(encoded));
  bt_gatt_notify(NULL, &range_svc.attrs[RANGE_METADATA_ATTR_INDEX], metadata,
                 sizeof(metadata));
}

void receptec_ble_range_publish(uint8_t pair_index, uint8_t status,
                                uint32_t distance_mm) {
  receptec_ble_range_publish_extended(pair_index, RECEPTEC_BLE_RANGE_MEASURED,
                                      status, 0, distance_mm);
}

static void ble_connected(struct bt_conn *conn, uint8_t err) {
  if (err == 0U) {
    printk("BLE range: central connected\n");
  } else {
    printk("BLE range: connection failed: %u\n", err);
  }
  ARG_UNUSED(conn);
}

static int ble_range_adv_start(void);

static void ble_disconnected(struct bt_conn *conn, uint8_t reason) {
  int ret;

  printk("BLE range: central disconnected (reason %u), re-advertising\n",
         reason);
  ret = ble_range_adv_start();
  if (ret < 0) {
    printk("BLE range: re-advertise failed: %d\n", ret);
  }
  ARG_UNUSED(conn);
}

BT_CONN_CB_DEFINE(ble_range_conn_cb) = {
    .connected = ble_connected,
    .disconnected = ble_disconnected,
};

static const uint8_t adv_flags[] = {
    BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR,
};
/* 00003100-4550-5453-4154-4c4554454300, air order (LSB first). */
static const uint8_t adv_uuid[] = {
    0x00, 0x43, 0x45, 0x45, 0x54, 0x4C, 0x54, 0x41,
    0x53, 0x54, 0x50, 0x45, 0x00, 0x31, 0x00, 0x00,
};
static const struct bt_data adv_data[] = {
    BT_DATA(BT_DATA_FLAGS, adv_flags, sizeof(adv_flags)),
    BT_DATA(BT_DATA_UUID128_ALL, adv_uuid, sizeof(adv_uuid)),
};
/* Device name rides in the scan response: flags + UUID128 + name do not fit
 * the 31-byte legacy advertisement together (3+18+11=32).
 */
static const struct bt_data scan_rsp[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
};

static int ble_range_adv_start(void) {
  return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data, ARRAY_SIZE(adv_data),
                         scan_rsp, ARRAY_SIZE(scan_rsp));
}

static void ble_thread_entry(void *unused_a, void *unused_b, void *unused_c) {
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
  ret = ble_range_adv_start();
  if (ret < 0) {
    printk("BLE range: advertise failed (%d); UWB unaffected\n", ret);
    return;
  }
  printk("BLE range: advertising UWB range service\n");
}

void receptec_ble_range_init(void) {
  static struct k_thread ble_thread;
  static K_THREAD_STACK_DEFINE(ble_stack, 2048);

  k_thread_create(&ble_thread, ble_stack, K_THREAD_STACK_SIZEOF(ble_stack),
                  ble_thread_entry, NULL, NULL, NULL, K_PRIO_COOP(7), 0,
                  K_NO_WAIT);
  k_thread_name_set(&ble_thread, "ble_range");
}
