/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_TEST_MOCK_ZEPHYR_SYS_BYTEORDER_H_
#define RECEPTEC_TEST_MOCK_ZEPHYR_SYS_BYTEORDER_H_

#include <stdint.h>

static inline void sys_put_le32(uint32_t val, uint8_t dst[4])
{
	dst[0] = (uint8_t)(val & 0xFFU);
	dst[1] = (uint8_t)((val >> 8) & 0xFFU);
	dst[2] = (uint8_t)((val >> 16) & 0xFFU);
	dst[3] = (uint8_t)((val >> 24) & 0xFFU);
}

static inline uint32_t sys_get_le32(const uint8_t src[4])
{
	return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8) |
	       ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

#endif
