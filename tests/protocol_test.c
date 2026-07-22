#include <receptec/protocol.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t receptec_eui[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
};
static const uint8_t coletec_eui[RECEPTEC_PROTOCOL_EUI64_SIZE] = {
	0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40,
};

static struct receptec_frame frame(enum receptec_packet_type type,
				   const uint8_t *payload, size_t length)
{
	struct receptec_frame result = {
		.sequence = 0xA5U,
		.type = type,
		.payload = payload,
		.payload_length = length,
	};

	memcpy(result.destination, coletec_eui, sizeof(coletec_eui));
	memcpy(result.source, receptec_eui, sizeof(receptec_eui));
	return result;
}

static void test_sync_vector(void)
{
	static const uint8_t expected[] = {
		0x41, 0xEC, 0xA5,
		0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40,
		0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
		0x01, 0x00, 0x00,
	};
	uint8_t dt[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE];
	uint8_t encoded[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	struct receptec_frame decoded;
	struct receptec_frame sync;
	size_t length = 0U;

	receptec_protocol_encode_dt_sampling(0U, dt);
	sync = frame(RECEPTEC_PACKET_SYNC, dt, sizeof(dt));
	assert(receptec_protocol_encode(encoded, sizeof(encoded), &sync,
					&length) == RECEPTEC_PROTOCOL_OK);
	assert(length == sizeof(expected));
	assert(memcmp(encoded, expected, sizeof(expected)) == 0);
	assert(receptec_protocol_decode(encoded, length, &decoded) ==
	       RECEPTEC_PROTOCOL_OK);
	assert(decoded.type == RECEPTEC_PACKET_SYNC);
	assert(decoded.sequence == 0xA5U);
	assert(receptec_protocol_decode_dt_sampling(decoded.payload) == 0U);
}

static void test_maximum_data(void)
{
	uint8_t payload[RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE];
	uint8_t encoded[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	struct receptec_frame decoded;
	struct receptec_frame data;
	size_t length = 0U;

	for (size_t i = 0U; i < sizeof(payload); ++i) {
		payload[i] = (uint8_t)i;
	}
	data = frame(RECEPTEC_PACKET_DATA, payload, sizeof(payload));
	assert(receptec_protocol_encode(encoded, sizeof(encoded), &data,
					&length) == RECEPTEC_PROTOCOL_OK);
	assert(length == RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE);
	assert(receptec_protocol_decode(encoded, length, &decoded) ==
	       RECEPTEC_PROTOCOL_OK);
	assert(decoded.payload_length == sizeof(payload));
	assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}

static void test_rejections(void)
{
	uint8_t dt[] = { 0U, 0U };
	uint8_t encoded[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	struct receptec_frame decoded;
	struct receptec_frame sync = frame(RECEPTEC_PACKET_SYNC, dt, sizeof(dt));
	size_t length = 0U;

	assert(receptec_protocol_encode(encoded, sizeof(encoded), &sync,
					&length) == RECEPTEC_PROTOCOL_OK);
	encoded[0] = 0U;
	assert(receptec_protocol_decode(encoded, length, &decoded) ==
	       RECEPTEC_PROTOCOL_ERR_FCF);
	encoded[0] = RECEPTEC_PROTOCOL_FCF_LSB;
	encoded[19] = 0x11U;
	assert(receptec_protocol_decode(encoded, length, &decoded) ==
	       RECEPTEC_PROTOCOL_ERR_RESERVED);
	encoded[19] = 0x06U;
	assert(receptec_protocol_decode(encoded, length, &decoded) ==
	       RECEPTEC_PROTOCOL_ERR_TYPE);
	assert(receptec_protocol_decode(encoded, 19U, &decoded) ==
	       RECEPTEC_PROTOCOL_ERR_LENGTH);
}

int main(void)
{
	test_sync_vector();
	test_maximum_data();
	test_rejections();
	puts("receptec protocol tests passed");
	return 0;
}
