#include <receptec/protocol.h>
#include <receptec/coletec_sync.h>

#include <assert.h>
#include <stdint.h>
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

static void test_dt_sampling_values(void)
{
	static const struct {
		uint16_t value;
		uint8_t encoded[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE];
	} vectors[] = {
		{ 0U, { 0x00U, 0x00U } },
		{ 5000U, { 0x88U, 0x13U } },
		{ 10000U, { 0x10U, 0x27U } },
		{ UINT16_MAX, { 0xFFU, 0xFFU } },
	};
	uint8_t encoded[RECEPTEC_PROTOCOL_DT_SAMPLING_SIZE];

	for (size_t i = 0U; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
		receptec_protocol_encode_dt_sampling(vectors[i].value, encoded);
		assert(memcmp(encoded, vectors[i].encoded, sizeof(encoded)) == 0);
		assert(receptec_protocol_decode_dt_sampling(encoded) ==
		       vectors[i].value);
	}
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

static void test_stop_frames(void)
{
	uint8_t encoded[RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE];
	struct receptec_frame decoded;
	struct receptec_frame stop = frame(RECEPTEC_PACKET_STOP, NULL, 0U);
	struct receptec_frame stop_ack = frame(RECEPTEC_PACKET_STOP_ACK, NULL, 0U);
	size_t length = 0U;

	assert(receptec_protocol_encode(encoded, sizeof(encoded), &stop,
					&length) == RECEPTEC_PROTOCOL_OK);
	assert(length == RECEPTEC_PROTOCOL_HEADER_SIZE);
	assert(receptec_protocol_decode(encoded, length, &decoded) ==
	       RECEPTEC_PROTOCOL_OK);
	assert(decoded.type == RECEPTEC_PACKET_STOP);
	assert(decoded.payload_length == 0U);

	assert(receptec_protocol_encode(encoded, sizeof(encoded), &stop_ack,
					&length) == RECEPTEC_PROTOCOL_OK);
	assert(length == RECEPTEC_PROTOCOL_HEADER_SIZE);
	assert(receptec_protocol_decode(encoded, length, &decoded) ==
	       RECEPTEC_PROTOCOL_OK);
	assert(decoded.type == RECEPTEC_PACKET_STOP_ACK);
	assert(decoded.payload_length == 0U);
}

static void test_coletec_synchronization_recovery(void)
{
	struct coletec_sync sync;

	coletec_sync_init(&sync);
	assert(sync.state == COLETEC_SYNC_INITIAL_RX);
	assert(coletec_sync_guard_us(&sync) == 500U);
	coletec_sync_initial_rx_expired(&sync);
	assert(sync.state == COLETEC_SYNC_DISCOVERY);
	assert(coletec_sync_discovery_off_ms(&sync) == 2000U);

	coletec_sync_accept_request(&sync);
	assert(sync.state == COLETEC_SYNC_SYNCHRONIZED);
	for (uint8_t miss = 1U; miss < COLETEC_MISSES_BEFORE_DISCOVERY;
	     ++miss) {
		coletec_sync_miss_slot(&sync);
		assert(sync.state == COLETEC_SYNC_SYNCHRONIZED);
		assert(sync.missed_slots == miss);
		assert(coletec_sync_guard_us(&sync) ==
		       500U + (uint16_t)miss * 100U);
	}
	coletec_sync_miss_slot(&sync);
	assert(sync.state == COLETEC_SYNC_DISCOVERY);
	assert(sync.missed_slots == 0U);
}

static void test_coletec_discovery_backoff(void)
{
	struct coletec_sync sync;

	coletec_sync_init(&sync);
	coletec_sync_initial_rx_expired(&sync);
	for (uint8_t multiplier = 1U;
	     multiplier < COLETEC_DISCOVERY_MAX_OFF_MULTIPLIER;
	     ++multiplier) {
		assert(sync.discovery_off_multiplier == multiplier);
		for (uint8_t cycle = 0U;
		     cycle < COLETEC_DISCOVERY_CYCLES_PER_STEP; ++cycle) {
			coletec_sync_discovery_on_expired(&sync);
		}
	}
	assert(sync.discovery_off_multiplier ==
	       COLETEC_DISCOVERY_MAX_OFF_MULTIPLIER);
	assert(coletec_sync_discovery_off_ms(&sync) == 10000U);
	for (uint8_t cycle = 0U; cycle < 6U; ++cycle) {
		coletec_sync_discovery_on_expired(&sync);
	}
	assert(sync.discovery_off_multiplier ==
	       COLETEC_DISCOVERY_MAX_OFF_MULTIPLIER);
	coletec_sync_accept_request(&sync);
	assert(sync.state == COLETEC_SYNC_SYNCHRONIZED);
	assert(sync.discovery_cycle_count == 0U);
	assert(sync.discovery_off_multiplier == 1U);
}

int main(void)
{
	test_sync_vector();
	test_dt_sampling_values();
	test_maximum_data();
	test_rejections();
	test_stop_frames();
	test_coletec_synchronization_recovery();
	test_coletec_discovery_backoff();
	puts("receptec protocol tests passed");
	return 0;
}
