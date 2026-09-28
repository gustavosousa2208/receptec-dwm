/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_TEST_MOCK_ZEPHYR_SETTINGS_H_
#define RECEPTEC_TEST_MOCK_ZEPHYR_SETTINGS_H_

#include <stddef.h>
#include <sys/types.h>

int settings_subsys_init(void);
ssize_t settings_get_val_len(const char *key);
ssize_t settings_load_one(const char *key, void *value, size_t value_size);
int settings_save_one(const char *key, const void *value, size_t value_size);
int settings_delete(const char *key);

#endif
