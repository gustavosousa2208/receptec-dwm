#pragma once
#include <stddef.h>
#include <stdint.h>
#define CONFIG_BT_DEVICE_NAME "test"
#define BT_LE_AD_GENERAL 1
#define BT_LE_AD_NO_BREDR 2
#define BT_DATA_FLAGS 1
#define BT_DATA_UUID16_ALL 2
#define BT_DATA_NAME_COMPLETE 3
#define BT_LE_ADV_CONN_FAST_1 NULL
struct bt_data { int type; const void *data; size_t len; };
#define BT_DATA(t,d,l) {t,d,l}
extern int mock_enable_error;
static inline int bt_enable(void *cb) { (void)cb; return mock_enable_error; }
static inline const char *bt_get_name(void) { return CONFIG_BT_DEVICE_NAME; }
static inline int bt_le_adv_start(const void *p,const struct bt_data *d,size_t n,const struct bt_data *s,size_t m) { (void)p;(void)d;(void)n;(void)s;(void)m;return 0; }
