#pragma once
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include "conn.h"
struct bt_gatt_attr { ssize_t (*read)(struct bt_conn *,const struct bt_gatt_attr *,void *,uint16_t,uint16_t); void *user_data; void (*ccc)(const struct bt_gatt_attr *,uint16_t); };
#define BT_GATT_CHRC_READ 1
#define BT_GATT_CHRC_NOTIFY 2
#define BT_GATT_PERM_READ 1
#define BT_GATT_PERM_WRITE 2
#define BT_GATT_PRIMARY_SERVICE(uuid) {NULL,NULL,NULL}
#define BT_GATT_CHARACTERISTIC(uuid,props,perm,rd,wr,data) {NULL,NULL,NULL}, {rd,data,NULL}
#define BT_GATT_CCC(fn,perm) {NULL,NULL,fn}
#define BT_GATT_SERVICE_DEFINE(name,...) static struct bt_gatt_attr name##_attrs[] = {__VA_ARGS__}; static struct { struct bt_gatt_attr *attrs; } name = {name##_attrs}
static inline ssize_t bt_gatt_attr_read(struct bt_conn *c,const struct bt_gatt_attr *a,void *buf,uint16_t len,uint16_t off,const void *value,uint16_t size) { (void)c;(void)a;if(off>size)return -1;size_t n=size-off;if(n>len)n=len;memcpy(buf,(const uint8_t *)value+off,n);return (ssize_t)n; }
int bt_gatt_notify(struct bt_conn *,const struct bt_gatt_attr *,const void *,uint16_t);
