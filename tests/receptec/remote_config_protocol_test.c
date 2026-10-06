#include <receptec/uwb_protocol.h>
#include <receptec/uwb_remote_config.h>

#include <assert.h>
#include <string.h>

static struct receptec_uwb_frame config_frame(uint8_t *payload, size_t length)
{
	struct receptec_uwb_frame frame = {
		.sequence = 0x42,
		.destination = {1, 2, 3, 4, 5, 6, 7, 8},
		.source = {8, 7, 6, 5, 4, 3, 2, 1},
		.type = RECEPTEC_UWB_CONFIG_REQUEST,
		.payload = payload,
		.payload_length = length,
	};
	return frame;
}

static void test_wire_vectors(void)
{
	uint8_t payload[] = {1, 1, 2, 3, 0x78, 0x56, 0x34, 0x12, 0, 0, 2, 0xAA, 0xBB};
	const uint8_t expected[] = {
		0x41, 0xEC, 0x42, 1, 2, 3, 4, 5, 6, 7, 8,
		8, 7, 6, 5, 4, 3, 2, 1, 0x0B,
		1, 1, 2, 3, 0x78, 0x56, 0x34, 0x12, 0, 0, 2, 0xAA, 0xBB
	};
	uint8_t wire[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	struct receptec_uwb_frame frame = config_frame(payload, sizeof(payload));
	struct receptec_uwb_frame decoded;
	size_t length;

	assert(receptec_uwb_encode(wire, sizeof(wire), &frame, &length) == RECEPTEC_UWB_OK);
	assert(length == sizeof(expected) && memcmp(wire, expected, length) == 0);
	assert(receptec_uwb_decode(wire, length, &decoded) == RECEPTEC_UWB_OK);
	assert(decoded.type == RECEPTEC_UWB_CONFIG_REQUEST);
	assert(decoded.payload_length == sizeof(payload));
	assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
	/* Status is opaque at codec level; handler interprets signed little-endian. */
	frame.type = RECEPTEC_UWB_CONFIG_RESPONSE;
	payload[8] = 0xF3;
	payload[9] = 0xFF;
	assert(receptec_uwb_encode(wire, sizeof(wire), &frame, &length) == RECEPTEC_UWB_OK);
	assert(wire[19] == 0x0C);
	assert(receptec_uwb_decode(wire, length, &decoded) == RECEPTEC_UWB_OK);
	assert(decoded.type == RECEPTEC_UWB_CONFIG_RESPONSE);
	assert(decoded.payload[8] == 0xF3 && decoded.payload[9] == 0xFF);
}

static void test_payload_boundaries(void)
{
	uint8_t payload[UWB_REMOTE_CONFIG_HEADER_SIZE + UWB_REMOTE_CONFIG_MAX_DATA + 1] = {1};
	uint8_t wire[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
	struct receptec_uwb_frame frame = config_frame(payload, UWB_REMOTE_CONFIG_HEADER_SIZE);
	struct receptec_uwb_frame decoded;
	size_t length;

	for (unsigned op = 0; op <= UWB_REMOTE_CONFIG_REBOOT; op++) {
		payload[1] = (uint8_t)op;
		for (unsigned count = 0; count <= UWB_REMOTE_CONFIG_MAX_DATA; count++) {
			payload[10] = (uint8_t)count;
			frame.payload_length = UWB_REMOTE_CONFIG_HEADER_SIZE + count;
			assert(receptec_uwb_encode(wire, sizeof(wire), &frame, &length) == RECEPTEC_UWB_OK);
			assert(receptec_uwb_decode(wire, length, &decoded) == RECEPTEC_UWB_OK);
			assert(receptec_uwb_decode(wire, length - 1, &decoded) == RECEPTEC_UWB_ERR_LENGTH);
			wire[length] = 0;
			assert(receptec_uwb_decode(wire, length + 1, &decoded) == RECEPTEC_UWB_ERR_LENGTH);
		}
	}
	payload[10] = UWB_REMOTE_CONFIG_MAX_DATA + 1;
	frame.payload_length = sizeof(payload);
	assert(receptec_uwb_encode(wire, sizeof(wire), &frame, &length) == RECEPTEC_UWB_ERR_LENGTH);
	payload[10] = 0;
	frame.payload_length = UWB_REMOTE_CONFIG_HEADER_SIZE;
	payload[0] = UWB_REMOTE_CONFIG_SCHEMA + 1;
	assert(receptec_uwb_encode(wire, sizeof(wire), &frame, &length) == RECEPTEC_UWB_ERR_LENGTH);
	payload[0] = UWB_REMOTE_CONFIG_SCHEMA;
	payload[1] = UWB_REMOTE_CONFIG_REBOOT + 1;
	assert(receptec_uwb_encode(wire, sizeof(wire), &frame, &length) == RECEPTEC_UWB_ERR_LENGTH);
	payload[1] = 0;
	assert(receptec_uwb_encode(wire, sizeof(wire), &frame, &length) == RECEPTEC_UWB_OK);
	wire[19] = 0x0D;
	assert(receptec_uwb_decode(wire, length, &decoded) == RECEPTEC_UWB_ERR_TYPE);
	frame.type = RECEPTEC_UWB_CONFIG_RESPONSE;
	frame.payload_length = UWB_REMOTE_CONFIG_HEADER_SIZE - 1;
	assert(receptec_uwb_encode(wire, sizeof(wire), &frame, &length) == RECEPTEC_UWB_ERR_LENGTH);
}

int main(void)
{
	test_wire_vectors();
	test_payload_boundaries();
	return 0;
}
