#ifndef DYNAPSE_COMMON_H_
#define DYNAPSE_COMMON_H_

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"

#include <limits.h>

#ifdef HAVE_PTHREADS
#include "ext/c11threads_posix.h"
#endif

#include <math.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <libcaer/devices/dynapse.h>


/**
 * Input spike event data structure definition.
 * This contains destination core, source core, dest address,
 * sign of x, sign of y, delta x and delta y, as well as chip id.
 */
struct input_spike_event {
	uint8_t dest_core;
	uint8_t dest_addr;
	uint8_t source_core;
	uint8_t sx;
	uint8_t dx;
	uint8_t sy;
	uint8_t dy;
	uint8_t chipid;
};
typedef struct input_spike_event *spike_event;

struct gen_spike_state {
	atomic_bool doStim;
	uint8_t stimType;
	atomic_int_fast32_t avr;				// Hertz [1/s]
	atomic_int_fast32_t std;				//
	float duration;
	bool repeat;
	spike_event inp;
	float loopTime;
	thrd_t spikeGenThread;
	atomic_bool running;
};

struct caer_input_dynapse_state {
	caerDeviceHandle deviceState;
	struct gen_spike_state genSpikeState;
};

typedef struct caer_input_dynapse_state *caerInputDynapseState;


bool caerInputDYNAPSEInit(caerModuleData moduleData, uint16_t deviceType);
void caerInputDYNAPSEExit(caerModuleData moduleData);
void caerInputDYNAPSERun(caerModuleData moduleData, size_t argsNumber, va_list args);

#endif /* DYNAPSE_COMMON_H_ */
