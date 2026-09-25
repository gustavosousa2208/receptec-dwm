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

int main(void)
{
	test_sync_vector();
	test_response_and_rejection();
	test_ack_matching_rejects_stale_sequence();
	return 0;
}
