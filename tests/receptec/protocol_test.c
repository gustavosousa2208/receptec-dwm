#include <receptec/uwb_protocol.h>

#include <assert.h>
#include <string.h>

static void test_sync_vector(void)
{
	const uint8_t expected[] = {
		0x41, 0xEC, 0xA5,
		0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40,
		0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
		0x01, 0x00, 0x00,
	};
	uint8_t dt[RECEPTEC_UWB_DT_SAMPLING_SIZE];
	uint8_t output[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	struct receptec_uwb_frame decoded;
	struct receptec_uwb_frame sync = {
		.sequence = 0xA5U,
		.destination = { 0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40 },
		.source = { 0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E },
		.type = RECEPTEC_UWB_SYNC,
		.payload = dt,
		.payload_length = sizeof(dt),
	};
	size_t length = 0U;

	receptec_uwb_encode_dt_sampling(0U, dt);
	assert(receptec_uwb_encode(output, sizeof(output), &sync, &length) ==
	       RECEPTEC_UWB_OK);
	assert(length == sizeof(expected));
	assert(memcmp(output, expected, sizeof(expected)) == 0);
	assert(receptec_uwb_decode(output, length, &decoded) == RECEPTEC_UWB_OK);
	assert(decoded.type == RECEPTEC_UWB_SYNC);
	assert(receptec_uwb_decode_dt_sampling(decoded.payload) == 0U);
}

static void test_response_and_rejection(void)
{
	enum receptec_uwb_packet_type response;
	uint8_t short_frame[RECEPTEC_UWB_HEADER_SIZE - 1U] = { 0 };

	assert(receptec_uwb_expected_response(RECEPTEC_UWB_DATA_REQ, &response) ==
	       RECEPTEC_UWB_OK);
	assert(response == RECEPTEC_UWB_DATA);
	assert(receptec_uwb_expected_response(RECEPTEC_UWB_SYNC, &response) ==
	       RECEPTEC_UWB_OK);
	assert(response == RECEPTEC_UWB_ACK);
	assert(receptec_uwb_decode(short_frame, sizeof(short_frame), NULL) ==
	       RECEPTEC_UWB_ERR_ARGUMENT);
}

static void test_ack_matching_rejects_stale_sequence(void)
{
	static const uint8_t peer_eui[RECEPTEC_UWB_EUI64_SIZE] = {
		0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x41,
	};
	static const uint8_t local_eui[RECEPTEC_UWB_EUI64_SIZE] = {
		0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
	};
	static const uint8_t wrong_eui[RECEPTEC_UWB_EUI64_SIZE] = {
		0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40,
	};
	struct receptec_uwb_frame ack = {
		.sequence = 5U,
		.destination = { 0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E },
		.source = { 0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x41 },
		.type = RECEPTEC_UWB_ACK,
		.payload = NULL,
		.payload_length = 0U,
	};

	assert(receptec_uwb_ack_matches(&ack, peer_eui, local_eui, 5U));
	/* An ACK from the prior registration/exchange must not complete seq 5. */
	assert(!receptec_uwb_ack_matches(&ack, peer_eui, local_eui, 4U));
	assert(!receptec_uwb_ack_matches(&ack, wrong_eui, local_eui, 5U));
	assert(!receptec_uwb_ack_matches(&ack, peer_eui, wrong_eui, 5U));
	ack.type = RECEPTEC_UWB_DATA;
	assert(!receptec_uwb_ack_matches(&ack, peer_eui, local_eui, 5U));
	assert(!receptec_uwb_ack_matches(NULL, peer_eui, local_eui, 5U));
}

static void test_multibase_wire_and_distance(void)
{
	const uint8_t master[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	const uint8_t base[8] = { 8, 7, 6, 5, 4, 3, 2, 1 };
	uint8_t payload[RECEPTEC_UWB_MAX_PAYLOAD_SIZE];
	uint8_t frame_bytes[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	struct receptec_uwb_plan plan = {
		.phase = RECEPTEC_UWB_PLAN_ARM,
		.round_id = 0x1122334455667788ULL,
		.pair_index = 2U,
		.report_mode = RECEPTEC_UWB_REPORT_AGGREGATE,
	};
	struct receptec_uwb_plan decoded_plan;
	struct receptec_uwb_result result = {
		.round_id = 0x1122334455667788ULL,
		.report_mode = RECEPTEC_UWB_REPORT_AGGREGATE,
		.record_count = 3U,
	};
	struct receptec_uwb_result decoded_result;
	struct receptec_uwb_twr_response response = {
		.round_id = 11U, .pair_index = 2U,
	};
	struct receptec_uwb_twr_response decoded_response;
	struct receptec_uwb_twr_final final = {
		.round_id = 11U, .pair_index = 2U,
	};
	struct receptec_uwb_twr_final decoded_final;
	struct receptec_uwb_frame frame = {
		.sequence = 21U, .type = RECEPTEC_UWB_RANGE_PLAN,
	};
	struct receptec_uwb_frame decoded_frame;
	uint32_t millimeters = 0U;
	size_t length;

	memcpy(plan.initiator, master, 8U);
	memcpy(plan.responder, base, 8U);
	assert(receptec_uwb_plan_encode(payload, sizeof(payload), &plan) == RECEPTEC_UWB_OK);
	assert(payload[0] == 1U && payload[1] == 0U && payload[2] == 0x88U &&
	       payload[9] == 0x11U && payload[10] == 2U && payload[11] == 1U);
	assert(receptec_uwb_plan_decode(payload, 28U, &decoded_plan) ==
	       RECEPTEC_UWB_OK);
	assert(decoded_plan.round_id == plan.round_id);
	memcpy(frame.destination, base, 8U);
	memcpy(frame.source, master, 8U);
	frame.payload = payload;
	frame.payload_length = 28U;
	assert(receptec_uwb_encode(frame_bytes, sizeof(frame_bytes), &frame,
				   &length) == RECEPTEC_UWB_OK);
	assert(length == 48U);
	assert(receptec_uwb_decode(frame_bytes, length, &decoded_frame) ==
	       RECEPTEC_UWB_OK);
	frame_bytes[20U] = 2U;
	assert(receptec_uwb_decode(frame_bytes, length, &decoded_frame) ==
	       RECEPTEC_UWB_ERR_RESERVED);
	frame_bytes[20U] = 1U;

	for (uint8_t i = 0U; i < 3U; ++i) {
		result.records[i].pair_index = i;
		memcpy(result.records[i].initiator, master, 8U);
		memcpy(result.records[i].responder, base, 8U);
		result.records[i].status = RECEPTEC_UWB_RANGE_OK;
		result.records[i].distance_mm = 4692U + i;
		result.records[i].master_rx_time_ms = 12345U + i;
	}
	assert(receptec_uwb_result_encode(payload, sizeof(payload), &result,
				    &length) == RECEPTEC_UWB_OK);
	assert(length == 89U);
	assert(receptec_uwb_result_decode(payload, length, &decoded_result) ==
	       RECEPTEC_UWB_OK);
	assert(decoded_result.records[2].distance_mm == 4694U);
	assert(receptec_uwb_result_decode(payload, length - 1U,
					  &decoded_result) == RECEPTEC_UWB_ERR_LENGTH);
	result.records[2].pair_index = 1U;
	assert(receptec_uwb_result_encode(payload, sizeof(payload), &result,
				    &length) == RECEPTEC_UWB_ERR_RESERVED);

	final.poll_tx_ts = 0x100000U;
	response.poll_rx_ts = final.poll_tx_ts + 1000U;
	response.response_tx_ts =
		(response.poll_rx_ts + 100000U + 511U) & ~511ULL;
	final.response_rx_ts = response.response_tx_ts + 1000U;
	final.final_tx_ts =
		(final.response_rx_ts + 100000U + 511U) & ~511ULL;
	assert(receptec_uwb_twr_response_encode(payload, &response) ==
	       RECEPTEC_UWB_OK);
	assert(receptec_uwb_twr_response_decode(payload, 19U,
						  &decoded_response) == RECEPTEC_UWB_OK);
	assert(receptec_uwb_twr_final_encode(payload, &final) ==
	       RECEPTEC_UWB_OK);
	assert(receptec_uwb_twr_final_decode(payload, 24U, &decoded_final) ==
	       RECEPTEC_UWB_OK);
	assert(receptec_uwb_distance_mm(&decoded_response, &decoded_final,
					final.final_tx_ts + 1000U, &millimeters) ==
	       RECEPTEC_UWB_OK);
	assert(millimeters == 4692U);
	assert(receptec_uwb_distance_mm(&decoded_response, &decoded_final,
					final.final_tx_ts + 2000000000ULL,
					&millimeters) == RECEPTEC_UWB_ERR_RESERVED);
}

static void test_plan_v2_authorization(void)
{
	uint8_t payload[RECEPTEC_UWB_PLAN_V2_SIZE];
	struct receptec_uwb_plan plan = {.authorization_ms = 50,
		.phase = RECEPTEC_UWB_PLAN_ARM, .pair_index = 1,
		.round_id = (1ULL << 40), .report_mode = RECEPTEC_UWB_REPORT_AGGREGATE};
	struct receptec_uwb_plan decoded;
	assert(receptec_uwb_plan_encode(payload, sizeof(payload), &plan) == RECEPTEC_UWB_OK);
	assert(payload[0] == 2 && payload[28] == 50 && payload[29] == 0);
	assert(receptec_uwb_plan_encode(payload, 28, &plan) == RECEPTEC_UWB_ERR_LENGTH);
	assert(receptec_uwb_plan_decode(payload, sizeof(payload), &decoded) == RECEPTEC_UWB_OK);
	assert(decoded.authorization_ms == 50 && decoded.round_id == plan.round_id);
	assert(receptec_uwb_plan_decode(payload, 28, &decoded) != RECEPTEC_UWB_OK);
	payload[28] = 19;
	assert(receptec_uwb_plan_decode(payload, 30, &decoded) != RECEPTEC_UWB_OK);
	payload[28] = 201;
	assert(receptec_uwb_plan_decode(payload, 30, &decoded) != RECEPTEC_UWB_OK);
	plan.authorization_ms = 0;
	assert(receptec_uwb_plan_encode(payload, sizeof(payload), &plan) == RECEPTEC_UWB_OK);
	assert(receptec_uwb_plan_decode(payload, 28, &decoded) == RECEPTEC_UWB_OK);
	assert(decoded.authorization_ms == 0);
	assert(receptec_uwb_plan_decode(payload, 30, &decoded) != RECEPTEC_UWB_OK);
}

int main(void)
{
	test_plan_v2_authorization();
	test_sync_vector();
	test_response_and_rejection();
	test_ack_matching_rejects_stale_sequence();
	test_multibase_wire_and_distance();
	return 0;
}
