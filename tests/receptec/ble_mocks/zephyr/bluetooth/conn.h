#pragma once
#include <stdint.h>
struct bt_conn { int unused; };
struct bt_conn_cb { void (*connected)(struct bt_conn *,uint8_t); void (*disconnected)(struct bt_conn *,uint8_t); void (*recycled)(void); };
#define BT_CONN_CB_DEFINE(name) struct bt_conn_cb name
