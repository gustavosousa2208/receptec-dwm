#pragma once
#include <stdint.h>
static inline void sys_put_le32(uint32_t x,uint8_t *p) { for(int i=0;i<4;i++) p[i]=(uint8_t)(x>>(8*i)); }
static inline void sys_put_le64(uint64_t x,uint8_t *p) { for(int i=0;i<8;i++) p[i]=(uint8_t)(x>>(8*i)); }
static inline uint32_t sys_get_le32(const uint8_t *p) { uint32_t x=0; for(int i=0;i<4;i++) x|=(uint32_t)p[i]<<(8*i);return x; }
static inline uint64_t sys_get_le64(const uint8_t *p) { uint64_t x=0; for(int i=0;i<8;i++) x|=(uint64_t)p[i]<<(8*i);return x; }
