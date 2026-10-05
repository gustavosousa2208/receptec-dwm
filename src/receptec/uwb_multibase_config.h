/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RECEPTEC_UWB_MULTIBASE_CONFIG_H_
#define RECEPTEC_UWB_MULTIBASE_CONFIG_H_

#include <stdint.h>

#include <receptec/uwb_protocol.h>

enum receptec_uwb_pair_action {
	RECEPTEC_UWB_PAIR_SYNC = 0,
	RECEPTEC_UWB_PAIR_DATA_REQ = 1,
	RECEPTEC_UWB_PAIR_RANGE = 2,
};

struct receptec_uwb_pair_operation {
	uint8_t target_eui[RECEPTEC_UWB_EUI64_SIZE];
	enum receptec_uwb_pair_action action;
	uint8_t range_peer_eui[RECEPTEC_UWB_EUI64_SIZE];
};

struct receptec_uwb_multibase_config {
	uint8_t base_eui[RECEPTEC_UWB_EUI64_SIZE];
	uint8_t tag_eui[RECEPTEC_UWB_EUI64_SIZE];
	enum receptec_uwb_report_mode report_mode;
	uint32_t period_ms;
	struct receptec_uwb_pair_operation pair[3];
};

#endif
