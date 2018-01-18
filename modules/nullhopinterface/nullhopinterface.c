/* NullHop Zynq Interface cAER module
 *  Author: federico.corradi@inilabs.com
 */

//const char * caerNullHopWrapper(uint16_t moduleID, int * hist_packet, bool * haveimg, int * result);

#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "wrapper.h"
#include <sys/types.h>
#include <sys/wait.h>

#include <libcaer/events/frame.h>
#include <libcaer/events/point1d.h>

struct nullhopwrapper_state {
	double detThreshold;
	struct MyClass* cpp_class; //pointer to cpp_class_object
};

typedef struct nullhopwrapper_state *nullhopwrapperState;

static bool caerNullHopWrapperInit(caerModuleData moduleData);
static void caerNullHopWrapperRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out);
static void caerNullHopWrapperExit(caerModuleData moduleData);

static struct caer_module_functions caerNullHopWrapperFunctions = {
		.moduleInit = &caerNullHopWrapperInit, .moduleRun =
				&caerNullHopWrapperRun, .moduleConfig =
		NULL, .moduleExit = &caerNullHopWrapperExit };


static const struct caer_event_stream_in moduleInputs[] = {
    { .type = FRAME_EVENT, .number = 1, .readOnly = true }
};

static const struct caer_event_stream_out moduleOutputs[] = {
    { .type = POINT1D_EVENT }
};

static const struct caer_module_info moduleInfo = {
	.version = 1, .name = "Nullhop Interface",
	.description = "NullHop interface",
	.type = CAER_MODULE_OUTPUT,
	.memSize = sizeof(struct nullhopwrapper_state),
	.functions = &caerNullHopWrapperFunctions,
	.inputStreams = moduleInputs,
	.inputStreamsSize = CAER_EVENT_STREAM_IN_SIZE(moduleInputs),
	.outputStreams = moduleOutputs,
	.outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(moduleOutputs)
};

// init

caerModuleInfo caerModuleGetInfo(void) {
    return (&moduleInfo);
}

static bool caerNullHopWrapperInit(caerModuleData moduleData) {

	nullhopwrapperState state = moduleData->moduleState;
	sshsNodeCreateDouble(moduleData->moduleNode, "detThreshold", 0.5, 0.1, 1, SSHS_FLAGS_NORMAL, "Detection Threshold");

	state->detThreshold = sshsNodeGetDouble(moduleData->moduleNode,
			"detThreshold");

	//Initializing nullhop network..
	state->cpp_class = newzs_driver("/nets/roshamboNet.nhp");

	return (true);
}

static void caerNullHopWrapperExit(caerModuleData moduleData) {
	nullhopwrapperState state = moduleData->moduleState;

	//zs_driverMonitor_closeThread(state->cpp_class); // join
	//deleteMyClass(state->cpp_class); //free memory block
}

static void caerNullHopWrapperRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {

	caerFrameEventPacketConst frameIn =
			(caerFrameEventPacketConst) caerEventPacketContainerFindEventPacketByTypeConst(in, FRAME_EVENT);


	//int * imagestreamer_hists = va_arg(args, int*);
	//bool * haveimg = va_arg(args, bool*);
	//int * result = va_arg(args, int*);

	if (frameIn == NULL) {
		return;
	}

	nullhopwrapperState state = moduleData->moduleState;

	//update module state
	state->detThreshold = sshsNodeGetDouble(moduleData->moduleNode,
			"detThreshold");

	int res = zs_driver_classify_image(state->cpp_class, frameIn);

	// get output and save it into the output stream packet
	*out = caerEventPacketContainerAllocate(1);
	if(*out == NULL){
		return;
	}
	caerPoint1DEventPacket solution = caerPoint1DEventPacketAllocate(1, moduleData->moduleID,
		caerEventPacketHeaderGetEventTSOverflow(&frameIn->packetHeader));

	// get first event empty allocated
	caerPoint1DEvent point = caerPoint1DEventPacketGetEvent(solution, 0);
	// now assign class to point
 	caerPoint1DEventSetX(point, res);	
	// set timestamp, of first frame in
	caerFrameEvent this_frame = caerFrameEventPacketGetEvent(frameIn, 0);
	caerPoint1DEventSetTimestamp(point, caerFrameEventGetTimestamp(this_frame) );
	// validate
	caerPoint1DEventValidate(point, solution);
	// not set eventpacket
	caerEventPacketContainerSetEventPacket(*out, 0, (caerEventPacketHeader) solution);

}
