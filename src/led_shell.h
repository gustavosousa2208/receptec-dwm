/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ATLETEC_LED_SHELL_H_
#define ATLETEC_LED_SHELL_H_

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>

bool led_shell_is_overridden(const struct gpio_dt_spec *gpio);

#endif /* ATLETEC_LED_SHELL_H_ */
