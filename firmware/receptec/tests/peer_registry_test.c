#include "uwb_peer_registry.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

static const uint8_t local_eui[RECEPTEC_UWB_EUI64_SIZE] = {
	0x41, 0x16, 0xA8, 0x06, 0x82, 0xF3, 0xC6, 0x3E,
};

static const uint8_t peer0[RECEPTEC_UWB_EUI64_SIZE] = {
	0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x40,
};

static const uint8_t peer1[RECEPTEC_UWB_EUI64_SIZE] = {
	0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x41,
};

static const uint8_t peer2[RECEPTEC_UWB_EUI64_SIZE] = {
	0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0x42,
};

static void test_register_get_unregister(void)
{
	struct receptec_uwb_peer_registry registry;
	uint8_t result[RECEPTEC_UWB_EUI64_SIZE];
	uint8_t sequence;

	assert(receptec_uwb_peer_registry_init(&registry, local_eui) == 0);
	assert(receptec_uwb_peer_registry_register(&registry, 0U, peer0) == 0);
	assert(receptec_uwb_peer_registry_register(&registry, 1U, peer1) == 0);
	assert(receptec_uwb_peer_registry_next_sequence(&registry, 0U,
						       &sequence) == 0);
	assert(sequence == 1U);
	assert(receptec_uwb_peer_registry_get(&registry, 0U, result) == 0);
	assert(memcmp(result, peer0, sizeof(result)) == 0);
	assert(receptec_uwb_peer_registry_get(&registry, 1U, result) == 0);
	assert(memcmp(result, peer1, sizeof(result)) == 0);
	assert(receptec_uwb_peer_registry_unregister(&registry, 0U) == 0);
	assert(receptec_uwb_peer_registry_get(&registry, 0U, result) == -ENOENT);
	assert(receptec_uwb_peer_registry_unregister(&registry, 0U) == -ENOENT);
	assert(receptec_uwb_peer_registry_register(&registry, 2U, peer0) == 0);
	assert(receptec_uwb_peer_registry_next_sequence(&registry, 2U,
						       &sequence) == 0);
	assert(sequence == 2U);
	assert(receptec_uwb_peer_registry_get(&registry, 2U, result) == 0);
	assert(memcmp(result, peer0, sizeof(result)) == 0);
	assert(receptec_uwb_peer_registry_unregister(&registry, 2U) == 0);
	assert(receptec_uwb_peer_registry_register(&registry, 0U, peer0) == 0);
	assert(receptec_uwb_peer_registry_next_sequence(&registry, 0U,
						       &sequence) == 0);
	assert(sequence == 3U);
	assert(receptec_uwb_peer_registry_register(&registry, 2U, peer2) == 0);
	assert(receptec_uwb_peer_registry_get(&registry, 2U, result) == 0);
	assert(memcmp(result, peer2, sizeof(result)) == 0);
	assert(receptec_uwb_peer_registry_unregister(&registry, 0U) == 0);
	assert(receptec_uwb_peer_registry_unregister(&registry, 2U) == 0);
	/* Reusing the old slot for another identity must not erase peer0 history. */
	assert(receptec_uwb_peer_registry_register(&registry, 0U, peer2) == 0);
	assert(receptec_uwb_peer_registry_register(&registry, 3U, peer0) == 0);
	assert(receptec_uwb_peer_registry_next_sequence(&registry, 3U,
						       &sequence) == 0);
	assert(sequence == 4U);
}

static void test_rejections_preserve_registry(void)
{
	struct receptec_uwb_peer_registry registry;
	uint8_t result[RECEPTEC_UWB_EUI64_SIZE];

	assert(receptec_uwb_peer_registry_init(&registry, local_eui) == 0);
	assert(receptec_uwb_peer_registry_register(&registry, 0U, peer0) == 0);
	assert(receptec_uwb_peer_registry_register(&registry, 1U, peer0) ==
	       -EADDRINUSE);
	assert(receptec_uwb_peer_registry_register(&registry, 0U, peer2) ==
	       -EEXIST);
	assert(receptec_uwb_peer_registry_register(&registry, 1U, local_eui) ==
	       -EADDRINUSE);
	assert(receptec_uwb_peer_registry_register(
		       &registry, RECEPTEC_UWB_MAX_PEERS, peer2) == -EINVAL);
	assert(receptec_uwb_peer_registry_register(&registry, 1U, NULL) ==
	       -EINVAL);
	assert(receptec_uwb_peer_registry_unregister(
		       &registry, RECEPTEC_UWB_MAX_PEERS) == -EINVAL);
	assert(receptec_uwb_peer_registry_get(&registry, 0U, result) == 0);
	assert(memcmp(result, peer0, sizeof(result)) == 0);
	assert(receptec_uwb_peer_registry_get(&registry, 1U, result) == -ENOENT);
}

static void test_full_table(void)
{
	struct receptec_uwb_peer_registry registry;
	size_t slot;

	assert(receptec_uwb_peer_registry_init(&registry, local_eui) == 0);
	for (slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		uint8_t eui[RECEPTEC_UWB_EUI64_SIZE] = {
			0x31, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA,
			(uint8_t)(0x50U + slot),
		};

		assert(receptec_uwb_peer_registry_register(&registry, slot, eui) == 0);
	}
	assert(receptec_uwb_peer_registry_register(&registry, 0U, peer2) ==
	       -EEXIST);
	assert(receptec_uwb_peer_registry_unregister(&registry, 2U) == 0);
	assert(receptec_uwb_peer_registry_register(&registry, 2U, peer2) == 0);
}

static void test_history_capacity_is_explicit(void)
{
	struct receptec_uwb_peer_registry registry;
	size_t i;
	uint8_t eui[RECEPTEC_UWB_EUI64_SIZE] = {
		0xA0, 0x5D, 0x02, 0xD5, 0x15, 0x8C, 0xFA, 0U,
	};

	assert(receptec_uwb_peer_registry_init(&registry, local_eui) == 0);
	for (i = 0U; i < RECEPTEC_UWB_PEER_HISTORY_CAPACITY; ++i) {
		eui[7] = (uint8_t)i;
		assert(receptec_uwb_peer_registry_register(&registry, 0U, eui) == 0);
		assert(receptec_uwb_peer_registry_unregister(&registry, 0U) == 0);
	}
	eui[0] = 0xA1U;
	eui[7] = 0U;
	assert(receptec_uwb_peer_registry_register(&registry, 0U, eui) ==
	       -ENOSPC);
}

static void test_set_next_sequence_restores_the_counter(void)
{
	struct receptec_uwb_peer_registry registry;
	uint8_t sequence;

	assert(receptec_uwb_peer_registry_init(&registry, local_eui) == 0);
	assert(receptec_uwb_peer_registry_set_next_sequence(&registry, 0U, 7U) == -ENOENT);
	assert(receptec_uwb_peer_registry_register(&registry, 0U, peer0) == 0);
	assert(receptec_uwb_peer_registry_next_sequence(&registry, 0U, &sequence) == 0);
	assert(sequence == 1U);
	/* the sequence_wrap test saves the next value, sends 254, 255 and 0, then restores it */
	assert(receptec_uwb_peer_registry_set_next_sequence(&registry, 0U, 254U) == 0);
	assert(receptec_uwb_peer_registry_next_sequence(&registry, 0U, &sequence) == 0);
	assert(sequence == 254U);
	assert(receptec_uwb_peer_registry_next_sequence(&registry, 0U, &sequence) == 0);
	assert(sequence == 255U);
	assert(receptec_uwb_peer_registry_next_sequence(&registry, 0U, &sequence) == 0);
	assert(sequence == 0U);
	assert(receptec_uwb_peer_registry_set_next_sequence(&registry, 0U, 2U) == 0);
	assert(receptec_uwb_peer_registry_next_sequence(&registry, 0U, &sequence) == 0);
	assert(sequence == 2U);
	assert(receptec_uwb_peer_registry_set_next_sequence(NULL, 0U, 1U) == -EINVAL);
	assert(receptec_uwb_peer_registry_set_next_sequence(&registry, RECEPTEC_UWB_MAX_PEERS, 1U) ==
	       -EINVAL);
}

int main(void)
{
	struct receptec_uwb_peer_registry registry;

	assert(receptec_uwb_peer_registry_init(NULL, local_eui) == -EINVAL);
	assert(receptec_uwb_peer_registry_init(&registry, NULL) == -EINVAL);
	test_register_get_unregister();
	test_rejections_preserve_registry();
	test_full_table();
	test_history_capacity_is_explicit();
	test_set_next_sequence_restores_the_counter();
	return 0;
}
