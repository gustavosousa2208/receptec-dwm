#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
struct k_spinlock { int unused; };
typedef int k_spinlock_key_t;
static inline int k_spin_lock(struct k_spinlock *p) { (void)p; return 0; }
static inline void k_spin_unlock(struct k_spinlock *p,int k) { (void)p;(void)k; }
extern int64_t mock_now;
static inline int64_t k_uptime_get(void) { return mock_now; }
struct k_work { void (*handler)(struct k_work *); bool pending; };
#define K_WORK_DEFINE(name, fn) struct k_work name = {fn, false}
int k_work_submit(struct k_work *work);
struct k_thread { int unused; };
#define K_THREAD_STACK_DEFINE(name,size) char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
#define K_PRIO_COOP(x) (x)
#define K_NO_WAIT 0
static inline void k_thread_create(struct k_thread *t,void *s,size_t n,void (*fn)(void *,void *,void *),void *a,void *b,void *c,int p,int o,int d) { (void)t;(void)s;(void)n;(void)p;(void)o;(void)d;fn(a,b,c); }
static inline void k_thread_name_set(struct k_thread *t,const char *name) { (void)t;(void)name; }
