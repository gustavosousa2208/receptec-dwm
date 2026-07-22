/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_DW3110_RADIO_H_
#define RECEPTEC_DW3110_RADIO_H_

#include <stdbool.h>

int dw3110_radio_init(void);
int dw3110_radio_run_sync_test(bool is_receptec);

#endif /* RECEPTEC_DW3110_RADIO_H_ */
