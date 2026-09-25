/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_DW3110_RADIO_H_
#define RECEPTEC_DW3110_RADIO_H_

#include <stdbool.h>
#include <stdint.h>

#define DW3110_RADIO_PEER_COUNT 4U

enum dw3110_radio_operation {
	DW3110_RADIO_OPERATION_NONE,
	DW3110_RADIO_OPERATION_DATA_REQ,
	DW3110_RADIO_OPERATION_TEST_SYNC,
	DW3110_RADIO_OPERATION_TEST_STOP,
	DW3110_RADIO_OPERATION_TEST_WRONG_EUI,
	DW3110_RADIO_OPERATION_TEST_OUT_OF_SLOT,
	DW3110_RADIO_OPERATION_TEST_SILENCE,
	DW3110_RADIO_OPERATION_TEST_REJECTION,
	DW3110_RADIO_OPERATION_TEST_SEQUENCE_WRAP,
	DW3110_RADIO_OPERATION_TEST_WINDOW,
};

enum dw3110_radio_test {
	DW3110_RADIO_TEST_SYNC,
	DW3110_RADIO_TEST_STOP,
	DW3110_RADIO_TEST_WRONG_EUI,
	DW3110_RADIO_TEST_OUT_OF_SLOT,
	DW3110_RADIO_TEST_SILENCE,
	DW3110_RADIO_TEST_REJECTION,
	DW3110_RADIO_TEST_SEQUENCE_WRAP,
	DW3110_RADIO_TEST_WINDOW,
};

enum dw3110_radio_rejection_case {
	DW3110_RADIO_REJECT_WRONG_SOURCE,
	DW3110_RADIO_REJECT_WRONG_TYPE,
	DW3110_RADIO_REJECT_RESERVED_BITS,
	DW3110_RADIO_REJECT_SHORT_PAYLOAD,
	DW3110_RADIO_REJECT_DT_SAMPLING,
	DW3110_RADIO_REJECT_CASE_COUNT,
};

enum dw3110_radio_window_case {
	DW3110_RADIO_WINDOW_BEFORE,
	DW3110_RADIO_WINDOW_INSIDE,
	DW3110_RADIO_WINDOW_END,
	DW3110_RADIO_WINDOW_AFTER,
	DW3110_RADIO_WINDOW_CASE_COUNT,
};

struct dw3110_radio_control_status {
	bool receptec_role;
	bool protocol_running;
	bool slotted_running;
	bool periodic_sync_running;
	bool operation_pending;
	uint8_t periodic_sync_peer_slot;
	enum dw3110_radio_operation operation;
	uint32_t cycle;
};

struct dw3110_radio_peer_info {
	const char *name;
	const uint8_t *eui;
	bool expected_present;
};

int dw3110_radio_init(void);
int dw3110_radio_start_protocol(bool is_receptec);
int dw3110_radio_enable_coletec(void);
int dw3110_radio_set_slotted(bool enable);
int dw3110_radio_set_periodic_sync(bool enable, uint8_t peer_slot);
int dw3110_radio_request_data(uint8_t peer_slot);
int dw3110_radio_run_test(enum dw3110_radio_test test, uint8_t peer_slot, uint32_t argument);
int dw3110_radio_abort_test(void);
int dw3110_radio_get_control_status(struct dw3110_radio_control_status *status);
int dw3110_radio_get_peer_info(uint8_t peer_slot, struct dw3110_radio_peer_info *info);

#endif /* RECEPTEC_DW3110_RADIO_H_ */
