/* Exercise the production publisher with a deliberately stalled host worker. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/gatt.h>
int64_t mock_now;
int mock_enable_error;
static struct k_work *queued;
static unsigned calls;
static int notify_error;
static uint8_t sent[32][20];
static uint16_t sent_len[32];
static bool inject_publish;
#include "../../src/receptec/ble_range.c"
int k_work_submit(struct k_work *work) { queued=work;work->pending=true;return 1; }
int bt_gatt_notify(struct bt_conn *conn,const struct bt_gatt_attr *attr,const void *data,uint16_t len) {
  (void)conn;(void)attr;assert(calls<32);sent_len[calls]=len;memcpy(sent[calls++],data,len);
  if(inject_publish) { inject_publish=false;receptec_ble_range_publish_extended(0,0,0,999,9999); }
  return notify_error;
}
static void drain(void) { assert(queued);struct k_work *work=queued;queued=NULL;work->pending=false;work->handler(work); }
static unsigned metadata_index(unsigned from,uint8_t pair,uint64_t round) {
  for(unsigned i=from;i<calls;i++) if(sent_len[i]==20 && sent[i][1]==pair && sys_get_le64(sent[i]+4)==round)return i;
  assert(!"missing metadata notification");return 0;
}
int main(void) {
  struct receptec_ble_range_stats stats;
  mock_now=100;
  receptec_ble_range_publish_extended(0,0,0,1,100);
  assert(!queued && calls==0);
  receptec_ble_range_stats_get(&stats);assert(stats.not_ready[0]==1);
  receptec_ble_range_init();
  receptec_ble_range_publish_extended(0,0,0,2,200);
  receptec_ble_range_publish_extended(0,0,0,3,300);
  receptec_ble_range_publish_extended(1,0,0,3,400);
  assert(calls==0); /* Producer never enters GATT, even when the worker is stalled. */
  uint8_t readback[60];
  assert(read_metadata(NULL,&range_svc.attrs[11],readback,sizeof(readback),0)==60);
  assert(sys_get_le64(readback+4)==3 && sys_get_le32(readback+12)==300);
  mock_now=175;drain();
  unsigned i=metadata_index(0,0,3);assert(sys_get_le32(sent[i]+12)==300 && sys_get_le32(sent[i]+16)==0);
  (void)metadata_index(0,1,3);
  assert(calls==4); /* The obsolete round was coalesced, not queued indefinitely. */
  receptec_ble_range_stats_get(&stats);assert(stats.coalesced[0]>=1);
  unsigned before=calls;
  receptec_ble_range_publish_extended(0,0,0,4,444);
  inject_publish=true;drain();
  i=metadata_index(before,0,4);assert(sys_get_le32(sent[i]+12)==444);
  /* A producer invoked during notification cannot mutate the in-flight snapshot. */
  assert(queued);drain();(void)metadata_index(before,0,999);
  notify_error=-ENOMEM;before=calls;
  receptec_ble_range_publish_extended(2,1,0,5,950);
  assert(calls==before);drain();assert(!queued);
  receptec_ble_range_stats_get(&stats);assert(stats.notify_errors[2]==1 && stats.last_notify_error[2]==-ENOMEM);
  notify_error=-ENOTCONN;
  receptec_ble_range_publish_extended(2,1,0,6,950);drain();assert(!queued);
  receptec_ble_range_stats_get(&stats);assert(stats.no_subscriber[2]==1 && stats.notify_errors[2]==1);
  notify_error=0;
  receptec_ble_range_publish_extended(2,2,0,7,950);drain();
  i=metadata_index(0,2,7);assert(sent[i][3]!=0 && sys_get_le32(sent[i]+12)==UINT32_MAX && sys_get_le32(sent[i]+16)==UINT32_MAX);
  before=calls;receptec_ble_range_publish_extended(3,0,0,8,1);assert(calls==before && !queued);
  puts("BLE latest-value asynchronous publisher: PASS");return 0;
}
