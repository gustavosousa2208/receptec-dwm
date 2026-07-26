/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_DW3110_PORT_H_
#define RECEPTEC_DW3110_PORT_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include <deca_interface.h>

int dw3110_port_init(void);
int dw3110_port_reset(void);
int dw3110_port_irq_enable(bool enable);
void dw3110_port_irq_clear(void);
int dw3110_port_wait_irq(k_timeout_t timeout);
uint32_t dw3110_port_last_irq_cycles(void);
const struct dwt_spi_s *dw3110_port_spi(void);
void dw3110_port_wakeup(void);

#endif /* RECEPTEC_DW3110_PORT_H_ */
