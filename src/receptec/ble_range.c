/* SPDX-License-Identifier: Apache-2.0 */

#include "ble_range.h"
#include <receptec/uwb_protocol.h>

#include <errno.h>
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
    {RECEPTEC_UWB_RANGE_NO_RESULT, 0xff, 0xff, 0xff, 0xff},
    {RECEPTEC_UWB_RANGE_NO_RESULT, 0xff, 0xff, 0xff, 0xff},
    {RECEPTEC_UWB_RANGE_NO_RESULT, 0xff, 0xff, 0xff, 0xff},
};
static uint8_t range_metadata[BLE_RANGE_PAIR_COUNT][BLE_RANGE_METADATA_LEN] = {
    {1, 0, RECEPTEC_BLE_RANGE_UNAVAILABLE, RECEPTEC_UWB_RANGE_NO_RESULT},
    {1, 1, RECEPTEC_BLE_RANGE_UNAVAILABLE, RECEPTEC_UWB_RANGE_NO_RESULT},
    {1, 2, RECEPTEC_BLE_RANGE_UNAVAILABLE, RECEPTEC_UWB_RANGE_NO_RESULT},
};
static int64_t range_updated_ms[BLE_RANGE_PAIR_COUNT];
struct range_notification {
  uint8_t value[BLE_RANGE_VALUE_LEN];
  uint8_t metadata[BLE_RANGE_METADATA_LEN];
  bool pending;
};
static struct range_notification range_pending[BLE_RANGE_PAIR_COUNT];
static struct receptec_ble_range_stats range_stats;
static bool ble_ready;


static void metadata_snapshot(uint8_t pair, uint8_t *out, int64_t now) {
  memcpy(out, range_metadata[pair], BLE_RANGE_METADATA_LEN);
  if (out[2] == RECEPTEC_BLE_RANGE_UNAVAILABLE) sys_put_le32(UINT32_MAX, out + 12);
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

/* Zephyr's ATT allocator uses K_NO_WAIT specifically in the SYSTEM workqueue.
 * A private worker would still use K_FOREVER and can stall on BLE buffers.
 * Process at most one snapshot per pair, never retry failed notifications. */
static void notify_work_handler(struct k_work *work) {
  ARG_UNUSED(work);
  struct range_notification batch[BLE_RANGE_PAIR_COUNT];
  k_spinlock_key_t key = k_spin_lock(&range_lock);
  for (uint8_t pair = 0; pair < BLE_RANGE_PAIR_COUNT; pair++) {
    batch[pair] = range_pending[pair];
    range_pending[pair].pending = false;
  }
  k_spin_unlock(&range_lock, key);
  for (uint8_t pair = 0; pair < BLE_RANGE_PAIR_COUNT; pair++) {
    if (!batch[pair].pending) continue;
    /* Preserve contract notification age: zero at publication, not RF age. */
    int legacy_ret = bt_gatt_notify(NULL, &range_svc.attrs[RANGE_VALUE_ATTR_INDEX(pair)],
                                   batch[pair].value, BLE_RANGE_VALUE_LEN);
    int ret = bt_gatt_notify(NULL, &range_svc.attrs[RANGE_METADATA_ATTR_INDEX],
                             batch[pair].metadata, BLE_RANGE_METADATA_LEN);
    key = k_spin_lock(&range_lock);
    if (legacy_ret < 0 && legacy_ret != -ENOTCONN) range_stats.legacy_errors[pair]++;
    range_stats.last_notify_error[pair] = ret;
    if (ret == 0) range_stats.notify_accepted[pair]++;
    else if (ret == -ENOTCONN) range_stats.no_subscriber[pair]++;
    else range_stats.notify_errors[pair]++;
    k_spin_unlock(&range_lock, key);
  }
}
static K_WORK_DEFINE(notify_work, notify_work_handler);

void receptec_ble_range_stats_get(struct receptec_ble_range_stats *out) {
  k_spinlock_key_t key = k_spin_lock(&range_lock);
  *out = range_stats;
  k_spin_unlock(&range_lock, key);
}

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
  int64_t now = k_uptime_get();
  range_updated_ms[pair_index] = now;
  range_stats.published[pair_index]++;
  bool submit = ble_ready;
  if (submit) {
    struct range_notification *pending = &range_pending[pair_index];
    if (pending->pending) range_stats.coalesced[pair_index]++;
    memcpy(pending->value, encoded, sizeof(encoded));
    memcpy(pending->metadata, metadata, sizeof(metadata));
    pending->pending = true;
  } else range_stats.not_ready[pair_index]++;
  k_spin_unlock(&range_lock, key);
  if (submit && k_work_submit(&notify_work) < 0) {
    key = k_spin_lock(&range_lock);
    range_stats.submit_errors++;
    k_spin_unlock(&range_lock, key);
  }
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
  printk("BLE range: central disconnected (reason %u)\n", reason);
  ARG_UNUSED(conn);
}

/* A disconnected callback still holds the connection object. With MAX_CONN=1,
 * restarting there can fail with ENOMEM and leave the dashboard undiscoverable. */
static void ble_recycled(void) {
  int ret = ble_range_adv_start();
  if (ret < 0 && ret != -EALREADY) {
    printk("BLE range: re-advertise failed: %d\n", ret);
  }
}

BT_CONN_CB_DEFINE(ble_range_conn_cb) = {
    .connected = ble_connected,
    .disconnected = ble_disconnected,
    .recycled = ble_recycled,
};

static const uint8_t adv_flags[] = {
    BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR,
};
/*
 * 16-bit alias of the range service plus the complete name: 3+4+11 = 18
 * bytes, so the human-readable name the browser picker shows fits the
 * 31-byte legacy advertisement together with the service hint. The full
 * 128-bit UUIDs are used for service/characteristic lookup after connect.
 */
static const uint8_t adv_uuid[] = {
    0x00, 0x31,
};
static const struct bt_data adv_data[] = {
    BT_DATA(BT_DATA_FLAGS, adv_flags, sizeof(adv_flags)),
    BT_DATA(BT_DATA_UUID16_ALL, adv_uuid, sizeof(adv_uuid)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
};
/* Scan response intentionally carries only flags. */
static const struct bt_data scan_rsp[] = {
    BT_DATA(BT_DATA_FLAGS, adv_flags, sizeof(adv_flags)),
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
  k_spinlock_key_t key = k_spin_lock(&range_lock);
  ble_ready = true;
  k_spin_unlock(&range_lock, key);
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
