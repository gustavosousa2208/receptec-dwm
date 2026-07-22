/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_DW3110_PORT_H_
#define RECEPTEC_DW3110_PORT_H_

#include <stdbool.h>

#include <deca_interface.h>

int dw3110_port_init(void);
int dw3110_port_reset(void);
int dw3110_port_irq_enable(bool enable);
const struct dwt_spi_s *dw3110_port_spi(void);
void dw3110_port_wakeup(void);

#endif /* RECEPTEC_DW3110_PORT_H_ */
