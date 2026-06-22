#include <receptec/protocol.h>
#include <receptec/timing.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t destination[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01
};
static const uint8_t source[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
};

static struct receptec_frame make_frame(enum receptec_packet_type type,
					const uint8_t *payload,
					size_t payload_length)
{
	struct receptec_frame frame = {
		.sequence = 0xA5,
		.type = type,
		.payload = payload,
		.payload_length = payload_length,
	};

	memcpy(frame.destination, destination, sizeof(destination));
	memcpy(frame.source, source, sizeof(source));
	return frame;
}

static void test_known_data_req(void)
{
	uint8_t dt[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE];
	uint8_t encoded[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	struct receptec_frame decoded;
	struct receptec_frame frame;
	size_t encoded_length = 0U;

	receptec_protocol_encode_dt_sampling(5000U, dt);
	assert(dt[0] == 0x88U);
	assert(dt[1] == 0x13U);
	assert(receptec_protocol_decode_dt_sampling(dt) == 5000U);

	frame = make_frame(RECEPTEC_PACKET_DATA_REQ, dt, sizeof(dt));
	assert(receptec_protocol_encode(encoded, sizeof(encoded), &frame,
					&encoded_length) == RECEPTEC_PROTOCOL_OK);
	assert(encoded_length == 22U);
	assert(encoded[0] == 0x41U && encoded[1] == 0xECU);
	assert(encoded[2] == 0xA5U);
	assert(memcmp(&encoded[3], destination, sizeof(destination)) == 0);
	assert(memcmp(&encoded[11], source, sizeof(source)) == 0);
	assert(encoded[19] == RECEPTEC_PACKET_DATA_REQ);
	assert(encoded[20] == 0x88U && encoded[21] == 0x13U);

	assert(receptec_protocol_decode(encoded, encoded_length, &decoded) ==
	       RECEPTEC_PROTOCOL_OK);
	assert(decoded.sequence == frame.sequence);
	assert(decoded.type == frame.type);
	assert(decoded.payload_length == sizeof(dt));
	assert(memcmp(decoded.payload, dt, sizeof(dt)) == 0);
}

static void test_maximum_data(void)
{
	uint8_t payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
	uint8_t encoded[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	struct receptec_frame decoded;
	struct receptec_frame frame;
	size_t encoded_length = 0U;
	size_t i;

	for (i = 0U; i < sizeof(payload); ++i) {
		payload[i] = (uint8_t)i;
	}

	frame = make_frame(RECEPTEC_PACKET_DATA, payload, sizeof(payload));
	assert(receptec_protocol_encode(encoded, sizeof(encoded), &frame,
					&encoded_length) == RECEPTEC_PROTOCOL_OK);
	assert(encoded_length == RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE);
	assert(receptec_protocol_decode(encoded, encoded_length, &decoded) ==
	       RECEPTEC_PROTOCOL_OK);
	assert(decoded.payload_length == RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE);
	assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}

static void test_empty_packets_and_response_mapping(void)
{
	const enum receptec_packet_type empty_types[] = {
		RECEPTEC_PACKET_ACK,
		RECEPTEC_PACKET_STOP,
		RECEPTEC_PACKET_STOP_ACK,
	};
	uint8_t encoded[RECEPTEC_PROTOCOL_HEADER_SIZE];
	struct receptec_frame decoded;
	enum receptec_packet_type response;
	size_t encoded_length;
	size_t i;

	for (i = 0U; i < sizeof(empty_types) / sizeof(empty_types[0]); ++i) {
		struct receptec_frame frame = make_frame(empty_types[i], NULL, 0U);

		assert(receptec_protocol_encode(encoded, sizeof(encoded), &frame,
						&encoded_length) == RECEPTEC_PROTOCOL_OK);
		assert(encoded_length == RECEPTEC_PROTOCOL_HEADER_SIZE);
		assert(receptec_protocol_decode(encoded, encoded_length, &decoded) ==
		       RECEPTEC_PROTOCOL_OK);
	}

	assert(receptec_protocol_expected_response(RECEPTEC_PACKET_DATA_REQ,
						   &response) == RECEPTEC_PROTOCOL_OK);
	assert(response == RECEPTEC_PACKET_DATA);
	assert(receptec_protocol_expected_response(RECEPTEC_PACKET_SYNC,
						   &response) == RECEPTEC_PROTOCOL_OK);
	assert(response == RECEPTEC_PACKET_ACK);
	assert(receptec_protocol_expected_response(RECEPTEC_PACKET_STOP,
						   &response) == RECEPTEC_PROTOCOL_OK);
	assert(response == RECEPTEC_PACKET_STOP_ACK);
}

static void test_rejections(void)
{
	uint8_t encoded[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE] = { 0 };
	uint8_t dt[] = { 0, 0 };
	struct receptec_frame decoded;
	struct receptec_frame frame;
	size_t encoded_length;

	frame = make_frame(RECEPTEC_PACKET_DATA_REQ, dt, sizeof(dt));
	assert(receptec_protocol_encode(encoded, sizeof(encoded), &frame,
					&encoded_length) == RECEPTEC_PROTOCOL_OK);

	encoded[0] = 0;
	assert(receptec_protocol_decode(encoded, encoded_length, &decoded) ==
	       RECEPTEC_PROTOCOL_ERR_FCF);
	encoded[0] = RECEPTEC_PROTOCOL_FCF_LSB;

	encoded[19] = 0x10U | RECEPTEC_PACKET_DATA_REQ;
	assert(receptec_protocol_decode(encoded, encoded_length, &decoded) ==
	       RECEPTEC_PROTOCOL_ERR_RESERVED);
	encoded[19] = 0x06U;
	assert(receptec_protocol_decode(encoded, encoded_length, &decoded) ==
	       RECEPTEC_PROTOCOL_ERR_TYPE);

	assert(receptec_protocol_decode(encoded, 19U, &decoded) ==
	       RECEPTEC_PROTOCOL_ERR_LENGTH);

	frame = make_frame(RECEPTEC_PACKET_ACK, dt, sizeof(dt));
	assert(receptec_protocol_encode(encoded, sizeof(encoded), &frame,
					&encoded_length) == RECEPTEC_PROTOCOL_ERR_LENGTH);

	frame = make_frame(RECEPTEC_PACKET_DATA_REQ, NULL, sizeof(dt));
	assert(receptec_protocol_encode(encoded, sizeof(encoded), &frame,
					&encoded_length) == RECEPTEC_PROTOCOL_ERR_ARGUMENT);

	frame = make_frame(RECEPTEC_PACKET_STOP, NULL, 0U);
	assert(receptec_protocol_encode(encoded, 19U, &frame, &encoded_length) ==
	       RECEPTEC_PROTOCOL_ERR_BUFFER_TOO_SMALL);
}

static void test_timing_profile(void)
{
	struct receptec_timing_profile invalid;
	uint16_t dt_sampling_us;

	assert(receptec_timing_validate(&receptec_default_timing, 1U) ==
	       RECEPTEC_TIMING_OK);
	assert(receptec_timing_validate(&receptec_default_timing, 50U) ==
	       RECEPTEC_TIMING_OK);
	assert(receptec_timing_validate(&receptec_default_timing, 51U) ==
	       RECEPTEC_TIMING_ERR_CYCLE_BUDGET);

	assert(receptec_timing_dt_sampling(&receptec_default_timing, 0U,
					   &dt_sampling_us) == RECEPTEC_TIMING_OK);
	assert(dt_sampling_us == 0U);
	assert(receptec_timing_dt_sampling(&receptec_default_timing, 1U,
					   &dt_sampling_us) == RECEPTEC_TIMING_OK);
	assert(dt_sampling_us == 5000U);
	assert(receptec_timing_dt_sampling(&receptec_default_timing, 2U,
					   &dt_sampling_us) == RECEPTEC_TIMING_OK);
	assert(dt_sampling_us == 10000U);
	assert(receptec_timing_dt_sampling(&receptec_default_timing, 3U,
					   &dt_sampling_us) == RECEPTEC_TIMING_ERR_ATTEMPTS);

	assert(receptec_timing_expanded_guard(&receptec_default_timing, 0U) ==
	       500U);
	assert(receptec_timing_expanded_guard(&receptec_default_timing, 3U) ==
	       800U);
	assert(receptec_timing_expanded_guard(&receptec_default_timing, 20U) ==
	       1000U);

	invalid = receptec_default_timing;
	invalid.attempt_offset_us[2] = 18000U;
	assert(receptec_timing_validate(&invalid, 1U) ==
	       RECEPTEC_TIMING_ERR_SLOT_BUDGET);

	invalid = receptec_default_timing;
	invalid.attempt_offset_us[1] = invalid.attempt_offset_us[0];
	assert(receptec_timing_validate(&invalid, 1U) ==
	       RECEPTEC_TIMING_ERR_ATTEMPT_ORDER);

	invalid = receptec_default_timing;
	invalid.response_delay_us = invalid.response_timeout_us;
	assert(receptec_timing_validate(&invalid, 1U) ==
	       RECEPTEC_TIMING_ERR_RESPONSE_BUDGET);
}

int main(void)
{
	test_known_data_req();
	test_maximum_data();
	test_empty_packets_and_response_mapping();
	test_rejections();
	test_timing_profile();
	puts("receptec protocol tests passed");
	return 0;
}
