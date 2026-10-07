/* SPDX-License-Identifier: Apache-2.0 */

#ifndef BLE_OTA_H
#define BLE_OTA_H

/* OTA pilot (atletec-64a Fase 2): BLE peripheral carrying only the mcumgr
 * SMP service (auto-registered by MCUMGR_TRANSPORT_BT). No custom GATT
 * service here; UWB ranging is unaffected by design (separate radio).
 * Compiled in only when CONFIG_BT is enabled (see ble_ota.c). */
void atletec_ble_ota_init(void);

#endif /* BLE_OTA_H */
