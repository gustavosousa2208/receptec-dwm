#include <stdio.h>

#include <receptec/protocol.h>
#include <receptec/timing.h>

int main(void)
{
	int status = receptec_timing_validate(&receptec_default_timing, 1U);

	if (status != RECEPTEC_TIMING_OK) {
		printf("RECEPTEC timing configuration invalid: %d\n", status);
		return status;
	}

	printf("RECEPTEC protocol foundation ready on %s\n", CONFIG_BOARD_TARGET);
	printf("logical frame max=%u, DATA payload max=%u, slot=%u us\n",
	       (unsigned int)RECEPTEC_PROTOCOL_MAX_LOGICAL_SIZE,
	       (unsigned int)RECEPTEC_PROTOCOL_MAX_PAYLOAD_SIZE,
	       (unsigned int)receptec_default_timing.slot_duration_us);
	printf("DW3110 driver integration pending\n");

	return 0;
}
