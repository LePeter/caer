#include "ext/portable_time.h"
#include <fcntl.h>
#include <time.h>

#define STIM_POISSON 	1
#define STIM_REGULAR 	2
#define STIM_GAUSSIAN 	3
#define STIM_PATTERNA   4
#define STIM_PATTERNB   5
#define STIM_PATTERNC   6
#define STIM_PATTERNA_SINGLE   7
#define STIM_PATTERNB_SINGLE   8
#define STIM_PATTERNC_SINGLE   9
#define STIM_PATTERND_SINGLE   10
#define STIM_ETF	11

bool caerGenSpikeInit(caerModuleData moduleData);
void caerGenSpikeExit(caerModuleData moduleData);
int spikeGenThread(void *spikeGenState);
void spiketrainETF(void *spikeGenState);
void spiketrainReg(void *spikeGenState);
void spiketrainPat(void *spikeGenState, uint32_t spikePattern[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE]);
void spiketrainPatSingle(void *spikeGenState, uint32_t sourceAddress);
void SetCam(void *spikeGenState);
void SetCamSingle(void *spikeGenState);
void ClearCam(void *spikeGenState);
void ClearAllCam(void *spikeGenState);
void ResetBiases(void *spikeGenState);

static struct timespec tstart = { 0, 0 }, tend = { 0, 0 };
static struct timespec tstart_etf = { 0, 0 }, tend_etf = { 0, 0 };
static int CamSeted = 0;
static int CamSetedSingle = 0;
static int CamCleared = 0;
static int CamAllCleared = 0;
static int BiasesLoaded = 0;
static int pattern_number = 4; //3 or 4

struct gen_spike_state {
	atomic_bool doStim;
	atomic_int_fast32_t stim_type;
	atomic_int_fast32_t stim_avr;				// Hertz [1/s]
	atomic_int_fast32_t stim_std;				//
	atomic_int_fast32_t stim_duration;
	atomic_bool repeat;
	atomic_bool teaching;
	atomic_bool sendTeachingStimuli;
	atomic_bool sendInhibitoryStimuli;
	atomic_bool setCam;
	atomic_bool setCamSingle;
	atomic_bool clearCam;
	atomic_bool clearAllCam;
	atomic_bool doStimPrimitiveBias;
	atomic_bool doStimPrimitiveCam;
	atomic_bool loadDefaultBiases;
	atomic_bool done;
	atomic_bool started;
	thrd_t spikeGenThread;
	atomic_bool running;
	/*address spike*/
	atomic_int_fast32_t core_d;
	atomic_int_fast32_t address;
	atomic_int_fast32_t core_s;
	atomic_int_fast32_t chip_id;
	atomic_int_fast32_t dx;
	atomic_int_fast32_t dy;
	atomic_bool sx;
	atomic_bool sy;
	/* ETF */
	// stimulation Thread ETF
	atomic_bool ETFstarted;
	atomic_bool ETFdone;
	atomic_int_fast32_t ETFchip_id;		// the chip that will be measured [0,4,8,12]
	atomic_int_fast32_t ETFduration;	// total stimulation duration
	atomic_int_fast32_t ETFphase_num;	// stimulation phase number
	atomic_bool ETFrepeat;
	int ETFstepnum;
};

// TODO: this should be private. gen_spikes.c should be in the main C file.
struct caer_input_dynapse_state {
	caerDeviceHandle deviceState;
	sshsNode eventSourceConfigNode;
	struct gen_spike_state genSpikeState;
};

typedef struct caer_input_dynapse_state *caerInputDynapseState;

bool caerGenSpikeInit(caerModuleData moduleData);
void caerGenSpikeExit(caerModuleData moduleData);

static void spikeConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	//caerModuleData moduleData = userData;
	caerInputDynapseState state = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "doStim")) { // && caerStrEquals(changeKey, "doStimBias")
			atomic_store(&state->genSpikeState.doStim, changeValue.boolean);
		//atomic_load(&state->genSpikeState.doStim);
			if (changeValue.boolean) {
				//caerModuleLog(CAER_LOG_NOTICE, "spikeGen", "stimulation started.");
				atomic_store(&state->genSpikeState.done, false); // we just started
				atomic_store(&state->genSpikeState.started, true);
			}
			else {
				//caerModuleLog(CAER_LOG_NOTICE, "spikeGen", "stimulation ended.");
				atomic_store(&state->genSpikeState.started, false);
				atomic_store(&state->genSpikeState.done, true);
			}
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_type")) {
			atomic_store(&state->genSpikeState.stim_type, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_avr")) {
			atomic_store(&state->genSpikeState.stim_avr, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_std")) {
			atomic_store(&state->genSpikeState.stim_std, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "stim_duration")) {
			atomic_store(&state->genSpikeState.stim_duration, changeValue.iint);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "repeat")) {
			atomic_store(&state->genSpikeState.repeat, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "teaching")) {
			atomic_store(&state->genSpikeState.teaching, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sendTeachingStimuli")) {
			atomic_store(&state->genSpikeState.sendTeachingStimuli, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sendInhibitoryStimuli")) {
			atomic_store(&state->genSpikeState.sendInhibitoryStimuli, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "setCam")) {
			atomic_store(&state->genSpikeState.setCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "setCamSingle")) {
			atomic_store(&state->genSpikeState.setCamSingle, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "clearCam")) {
			atomic_store(&state->genSpikeState.clearCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "clearAllCam")) {
			atomic_store(&state->genSpikeState.clearAllCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "doStimPrimitiveBias")) {
			atomic_store(&state->genSpikeState.doStimPrimitiveBias, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "doStimPrimitiveCam")) {
			atomic_store(&state->genSpikeState.doStimPrimitiveCam, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "loadDefaultBiases")) {
			atomic_store(&state->genSpikeState.loadDefaultBiases, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
			atomic_store(&state->genSpikeState.running, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sx")) {
			atomic_store(&state->genSpikeState.sx, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "sy")) {
			atomic_store(&state->genSpikeState.sy, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "dx")) {
			atomic_store(&state->genSpikeState.dx, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "dy")) {
			atomic_store(&state->genSpikeState.dy, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "core_d")) {
			atomic_store(&state->genSpikeState.core_d, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "core_s")) {
			atomic_store(&state->genSpikeState.core_s, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "address")) {
			atomic_store(&state->genSpikeState.address, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "chip_id")) {
			atomic_store(&state->genSpikeState.chip_id, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFphase_num")) {
			atomic_store(&state->genSpikeState.ETFphase_num, changeValue.iint);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ETFstarted")) {
			atomic_store(&state->genSpikeState.ETFstarted, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ETFdone")) {
			atomic_store(&state->genSpikeState.ETFdone, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFchip_id")) {
			atomic_store(&state->genSpikeState.ETFchip_id, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFduration")) {
			atomic_store(&state->genSpikeState.ETFduration, changeValue.iint);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "ETFphase_num")) {
			atomic_store(&state->genSpikeState.ETFphase_num, changeValue.iint);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ETFrepeat")) {
			atomic_store(&state->genSpikeState.ETFrepeat, changeValue.boolean);
		}
	}

}

bool caerGenSpikeInit(caerModuleData moduleData) {

	caerInputDynapseState state = moduleData->moduleState;

	sshsNode deviceConfigNodeMain = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CHIP_DYNAPSE, true));

	sshsNode spikeNode = sshsGetRelativeNode(deviceConfigNodeMain, "spikeGen/");

	sshsNodeCreateBool(spikeNode, "doStim", false, SSHS_FLAGS_NORMAL, "Enable stimulation.");
	atomic_store(&state->genSpikeState.doStim, sshsNodeGetBool(spikeNode, "doStim"));

	// TODO: fix range limits.
	sshsNodeCreateInt(spikeNode, "stim_type", U8T(STIM_REGULAR), 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.stim_type, sshsNodeGetInt(spikeNode, "stim_type"));

	sshsNodeCreateInt(spikeNode, "stim_avr", 3, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.stim_avr, sshsNodeGetInt(spikeNode, "stim_avr"));

	sshsNodeCreateInt(spikeNode, "stim_std", 1, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.stim_std, sshsNodeGetInt(spikeNode, "stim_std"));

	sshsNodeCreateInt(spikeNode, "stim_duration", 10, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.stim_duration, sshsNodeGetInt(spikeNode, "stim_duration"));

	sshsNodeCreateBool(spikeNode, "repeat", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.repeat, sshsNodeGetBool(spikeNode, "repeat"));

	sshsNodeCreateBool(spikeNode, "teaching", true, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.teaching, sshsNodeGetBool(spikeNode, "teaching"));

	sshsNodeCreateBool(spikeNode, "sendTeachingStimuli", true, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.sendTeachingStimuli, sshsNodeGetBool(spikeNode, "sendTeachingStimuli"));

	sshsNodeCreateBool(spikeNode, "sendInhibitoryStimuli", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.sendInhibitoryStimuli, sshsNodeGetBool(spikeNode, "sendInhibitoryStimuli"));

	sshsNodeCreateBool(spikeNode, "setCam", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.setCam, sshsNodeGetBool(spikeNode, "setCam"));

	sshsNodeCreateBool(spikeNode, "setCamSingle", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.setCamSingle, sshsNodeGetBool(spikeNode, "setCamSingle"));

	sshsNodeCreateBool(spikeNode, "clearCam", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.clearCam, sshsNodeGetBool(spikeNode, "clearCam"));

	sshsNodeCreateBool(spikeNode, "clearAllCam", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.clearAllCam, sshsNodeGetBool(spikeNode, "clearAllCam"));

	sshsNodeCreateBool(spikeNode, "doStimPrimitiveBias", true, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.doStimPrimitiveBias, sshsNodeGetBool(spikeNode, "doStimPrimitiveBias"));

	sshsNodeCreateBool(spikeNode, "doStimPrimitiveCam", true, SSHS_FLAGS_NORMAL, "TODO."); //false
	atomic_store(&state->genSpikeState.doStimPrimitiveCam, sshsNodeGetBool(spikeNode, "doStimPrimitiveCam"));

	sshsNodeCreateBool(spikeNode, "loadDefaultBiases", false, SSHS_FLAGS_NORMAL, "TODO."); //1 //false
	atomic_store(&state->genSpikeState.loadDefaultBiases, sshsNodeGetBool(spikeNode, "loadDefaultBiases"));

	atomic_store(&state->genSpikeState.started, false);
	atomic_store(&state->genSpikeState.done, true);

	atomic_store(&state->genSpikeState.ETFstarted, false);
	atomic_store(&state->genSpikeState.ETFdone, false);
	atomic_store(&state->genSpikeState.ETFchip_id, 0);
	atomic_store(&state->genSpikeState.ETFduration, 30);
	atomic_store(&state->genSpikeState.ETFphase_num, 0);
	atomic_store(&state->genSpikeState.ETFrepeat, true);

	state->genSpikeState.ETFstepnum = 6;	//internal

	// init status
	sshsNodeCreateBool(spikeNode, "loadDefaultBiases", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.loadDefaultBiases, sshsNodeGetBool(spikeNode, "loadDefaultBiases"));

	// Start separate stimulation thread.
	atomic_store(&state->genSpikeState.running, true);

	if (thrd_create(&state->genSpikeState.spikeGenThread, &spikeGenThread, state) != thrd_success) {
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "spikeGenThread: Failed to start thread.");
		return (NULL);
	}

	/*address*/
	sshsNodeCreateBool(spikeNode, "sx", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.sx, sshsNodeGetBool(spikeNode, "sx"));

	sshsNodeCreateBool(spikeNode, "sy", false, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.sy, sshsNodeGetBool(spikeNode, "sy"));

	sshsNodeCreateInt(spikeNode, "core_d", 0, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.core_d, sshsNodeGetInt(spikeNode, "core_d"));

	sshsNodeCreateInt(spikeNode, "core_s", 0, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.core_s, sshsNodeGetInt(spikeNode, "core_s"));

	sshsNodeCreateInt(spikeNode, "address", 1, 0, INT32_MAX, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.address, sshsNodeGetInt(spikeNode, "address"));

	sshsNodeCreateInt(spikeNode, "dx", 0, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.dx, sshsNodeGetInt(spikeNode, "dx"));

	sshsNodeCreateInt(spikeNode, "dy", 0, 0, 1024, SSHS_FLAGS_NORMAL, "TODO.");
	atomic_store(&state->genSpikeState.dy, sshsNodeGetInt(spikeNode, "dy"));

	sshsNodeCreateInt(spikeNode, "chip_id", DYNAPSE_CONFIG_DYNAPSE_U0, 0, 3, SSHS_FLAGS_NORMAL, "TODO."); //4
	atomic_store(&state->genSpikeState.chip_id, sshsNodeGetInt(spikeNode, "chip_id"));

	return (true);
}

void caerGenSpikeExit(caerModuleData moduleData) {

	caerLog(CAER_LOG_DEBUG, moduleData->moduleSubSystemString, "SpikeGenThread: init exit.");

	caerInputDynapseState state = moduleData->moduleState;

	// Shut down stimulation thread and wait on it to finish.
	atomic_store_explicit(&state->genSpikeState.doStim, false, memory_order_release);
	atomic_store_explicit(&state->genSpikeState.running, false, memory_order_release);

	//make sure that doStim is off
	sshsNode deviceConfigNodeMain = sshsGetRelativeNode(moduleData->moduleNode,
		chipIDToName(DYNAPSE_CHIP_DYNAPSE, true));
	sshsNode spikeGenNode = sshsGetRelativeNode(deviceConfigNodeMain, "spikeGen/");
	sshsNodePutBool(spikeGenNode, "doStim", false);
	sshsNodePutBool(spikeGenNode, "doStimPrimitiveBias", false);
	sshsNodePutBool(spikeGenNode, "doStimPrimitiveCam", false);

	if ((errno = thrd_join(state->genSpikeState.spikeGenThread, NULL)) != thrd_success) {
		// This should never happen!
		caerLog(CAER_LOG_CRITICAL, moduleData->moduleSubSystemString,
			"SpikeGen: Failed to join rendering thread. Error: %d.", errno);
	}

	caerLog(CAER_LOG_DEBUG, moduleData->moduleSubSystemString, "SpikeGenThread: Exited successfully.");

}

void spiketrainETF(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return;
	}

	caerInputDynapseState state = spikeGenState;

	struct timespec tim;
	tim.tv_sec = 0;
	float measureMinTime = (float) atomic_load(&state->genSpikeState.ETFduration);
	int inFreqs[6] = { 30, 50, 70, 90, 100, 120 };
	int nSteps = 6;
	state->genSpikeState.ETFstepnum = nSteps;
	double stepDur = (double) measureMinTime / (double) nSteps;

	int this_step = 0;

	struct timespec ss, dd;
	portable_clock_gettime_monotonic(&ss);

	if (!atomic_load(&state->genSpikeState.ETFstarted)) {
		LABELSTART: portable_clock_gettime_monotonic(&tstart_etf);
	}

	portable_clock_gettime_monotonic(&tend_etf);

	//check frequency phase and change accordingly
	double current_time = (double) ((double) tend_etf.tv_sec + 1.0e-9 * tend_etf.tv_nsec - (double) tstart_etf.tv_sec
		+ 1.0e-9 * tstart_etf.tv_nsec);
	this_step = 1;
	double chek = round((double) current_time / (double) stepDur);
	if (chek < INT32_MAX && chek > INT32_MIN) {
		this_step = (int32_t) chek;
	}

	atomic_store(&state->genSpikeState.ETFphase_num, this_step);
	if (this_step >= 0 && this_step < nSteps) {
		if (inFreqs[this_step] > 0) {
			tim.tv_nsec = 1000000000L / inFreqs[this_step];	// select frequency
		}
		else {
			tim.tv_nsec = 999999999L;
		}
	}
	else {
		tim.tv_nsec = 999999999L; // default value
	}
	if (atomic_load(&state->genSpikeState.ETFduration) <= current_time) {
		if (atomic_load(&state->genSpikeState.ETFstarted)) {
			//caerLog(CAER_LOG_NOTICE, __func__, "ETF stimulation finished.");
		}
		atomic_store(&state->genSpikeState.ETFdone, true);
		atomic_store(&state->genSpikeState.ETFstarted, false);
		if (atomic_load(&state->genSpikeState.ETFrepeat)) {
			//caerLog(CAER_LOG_NOTICE, __func__, "ETF stimulation re-started.");
			atomic_store(&state->genSpikeState.ETFstarted, true);
			atomic_store(&state->genSpikeState.ETFdone, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->genSpikeState.ETFdone)) {
		uint32_t bits_chipU0[1];

		bits_chipU0[0] = 0xf | 0 << 16 | 0 << 17 | 1 << 13 | 0 << 18 | 5 << 20 | 0 << 4 | 0 << 6 | 0 << 7 | 0 << 9;

		caerDeviceConfigSet(state->deviceState, DYNAPSE_CONFIG_CHIP,
		DYNAPSE_CONFIG_CHIP_ID, (uint32_t) atomic_load(&state->genSpikeState.ETFchip_id));

		// send data with libusb host transfer in packet
		if (!caerDynapseSendDataToUSB(state->deviceState, bits_chipU0, 1)) {
			caerLog(CAER_LOG_ERROR, __func__, "USB transfer failed");
		}

		portable_clock_gettime_monotonic(&dd);
		tim.tv_nsec = tim.tv_nsec - (dd.tv_nsec - ss.tv_nsec);
		/* now do the nano sleep */
		nanosleep(&tim, NULL);
	}

}

int spikeGenThread(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return (thrd_error);
	}

	caerInputDynapseState state = spikeGenState;

	thrd_set_name("SpikeGenThread");

	while (atomic_load_explicit(&state->genSpikeState.running, // the loop
		memory_order_acquire)) {
		if (!atomic_load(&state->genSpikeState.doStim)) {
			struct timespec noStimSleep = { .tv_sec = 0, .tv_nsec = 1000000 };
			thrd_sleep(&noStimSleep, NULL);
			continue;
		}

		if (state->genSpikeState.setCam == true && CamSeted == 0) {
			SetCam(state);
			CamSeted = 1;
		}
		else if (state->genSpikeState.setCam == false && CamSeted == 1) {
			CamSeted = 0;
		}

		if (state->genSpikeState.setCamSingle == true && CamSetedSingle == 0) {
			SetCamSingle(state);
			CamSetedSingle = 1;
		}
		else if (state->genSpikeState.setCamSingle == false && CamSetedSingle == 1) {
			CamSetedSingle = 0;
		}
		if (state->genSpikeState.clearCam == true && CamCleared == 0) {
			ClearCam(state);
			CamCleared = 1;
		}
		else if (state->genSpikeState.clearCam == false && CamCleared == 1) {
			CamCleared = 0;
		}
		if (state->genSpikeState.clearAllCam == true && CamAllCleared == 0) {
			ClearAllCam(state);
			CamAllCleared = 1;
		}
		else if (state->genSpikeState.clearAllCam == false && CamAllCleared == 1) {
			CamAllCleared = 0;
		}
		if (state->genSpikeState.loadDefaultBiases == true && BiasesLoaded == 0) {
			ResetBiases(state);
			BiasesLoaded = 1;
		}
		else if (state->genSpikeState.loadDefaultBiases == false && BiasesLoaded == 1) {
			BiasesLoaded = 0;
		}

		/* generate spikes*/

		if (state->genSpikeState.stim_type == STIM_REGULAR) {
			spiketrainReg(state);
		}
		else if (state->genSpikeState.stim_type == STIM_POISSON) {
			// TODO
		}
		else if (state->genSpikeState.stim_type == STIM_GAUSSIAN) {
			// TODO
		}
		else if (state->genSpikeState.stim_type == STIM_PATTERNA) {
			// generate pattern A
			uint32_t spikePatternA[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
			int64_t rowId, colId;
			int cx, cy, r;
			cx = 16;
			cy = 16;
			r = 14;
			for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++)
				for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++)
					spikePatternA[rowId][colId] = 0;
			for (rowId = cx - r; rowId <= cx + r; rowId++)
				for (colId = cy - r; colId <= cy + r; colId++)
					if (((cx - rowId) * (cx - rowId) + (cy - colId) * (cy - colId) <= r * r + sqrt(r))
						&& ((cx - rowId) * (cx - rowId) + (cy - colId) * (cy - colId) >= r * r - r))
						spikePatternA[rowId][colId] = 1;
			spiketrainPat(state, spikePatternA);
		}
		else if (state->genSpikeState.stim_type == STIM_PATTERNB) {
			//generate pattern B
			uint32_t spikePatternB[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
			int64_t rowId, colId;
			int64_t num = DYNAPSE_CONFIG_CAMCOL;
			for (rowId = -num; rowId < num; rowId++) {
				for (colId = -num; colId < num; colId++) {
					if (abs((int) rowId) + abs((int) colId) == num) // Change this condition >= <=
						spikePatternB[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 1;
					else
						spikePatternB[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 0;
				}
			}
			spiketrainPat(state, spikePatternB);
		}
		else if (state->genSpikeState.stim_type == STIM_PATTERNC) {
			//generate pattern C
			uint32_t spikePatternC[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
			int64_t rowId, colId;
			int64_t num = DYNAPSE_CONFIG_CAMCOL;
			for (rowId = -num; rowId < num; rowId++) {
				for (colId = -num; colId < num; colId++) {
					if (abs((int) rowId) == abs((int) colId)) // Change this condition
						spikePatternC[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 1;
					else
						spikePatternC[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 0;
				}
			}
			spiketrainPat(state, spikePatternC);
		}
		else if (state->genSpikeState.stim_type == STIM_PATTERNA_SINGLE) {
			// generate pattern A
			uint32_t sourceAddress = 1;
			spiketrainPatSingle(state, sourceAddress);
		}
		else if (state->genSpikeState.stim_type == STIM_PATTERNB_SINGLE) {
			//generate pattern B
			uint32_t sourceAddress = 2;
			spiketrainPatSingle(state, sourceAddress);
		}
		else if (state->genSpikeState.stim_type == STIM_PATTERNC_SINGLE) {
			//generate pattern C
			uint32_t sourceAddress = 3;
			spiketrainPatSingle(state, sourceAddress);
		}
		else if (state->genSpikeState.stim_type == STIM_PATTERND_SINGLE) {
			//generate pattern D
			uint32_t sourceAddress = 4;
			spiketrainPatSingle(state, sourceAddress);
		}
		else if (state->genSpikeState.stim_type == STIM_ETF) {
			spiketrainETF(state);
		}

	}

	return (thrd_success);
}

void spiketrainReg(void *spikeGenState) {

	if (spikeGenState == NULL) {
		return;
	}

	caerInputDynapseState state = spikeGenState;

	struct timespec tim;
	tim.tv_sec = 0;

	if (atomic_load(&state->genSpikeState.stim_avr) > 0) {
		tim.tv_nsec = 1000000000L / atomic_load(&state->genSpikeState.stim_avr);
	}
	else {
		tim.tv_nsec = 999999999L;
	}

	uint32_t value = (uint32_t) atomic_load(&state->genSpikeState.core_d) | 0 << 16 | 0 << 17 | 1 << 13
		| (uint32_t) atomic_load(&state->genSpikeState.core_s) << 18
		| (uint32_t) atomic_load(&state->genSpikeState.address) << 20
		| (uint32_t) atomic_load(&state->genSpikeState.dx) << 4 | (uint32_t) atomic_load(&state->genSpikeState.sx) << 6
		| (uint32_t) atomic_load(&state->genSpikeState.dy) << 7 | (uint32_t) atomic_load(&state->genSpikeState.sy) << 9;

	if (!atomic_load(&state->genSpikeState.started)) {
		LABELSTART: portable_clock_gettime_monotonic(&tstart);
	}

	portable_clock_gettime_monotonic(&tend);

	if (atomic_load(&state->genSpikeState.stim_duration)
		<= ((double) tend.tv_sec + 1.0e-9 * tend.tv_nsec) - ((double) tstart.tv_sec + 1.0e-9 * tstart.tv_nsec)) {
		if (atomic_load(&state->genSpikeState.started)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation finished.");
		}
		atomic_store(&state->genSpikeState.done, true);
		atomic_store(&state->genSpikeState.started, false);
		if (atomic_load(&state->genSpikeState.repeat)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation re-started.");
			atomic_store(&state->genSpikeState.started, true);
			atomic_store(&state->genSpikeState.done, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->genSpikeState.done)) {

		/* remove time it takes to send, to better match the target freq */
		struct timespec ss, dd;
		portable_clock_gettime_monotonic(&ss);
		/* */
		caerDeviceConfigSet((caerDeviceHandle) state->deviceState,
		DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, (uint32_t) atomic_load(&state->genSpikeState.chip_id)); //usb_handle
		caerDeviceConfigSet((caerDeviceHandle) state->deviceState,
		DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, (uint32_t) value); //usb_handle
		/* */
		portable_clock_gettime_monotonic(&dd);
		tim.tv_nsec = tim.tv_nsec - (dd.tv_nsec - ss.tv_nsec);
		/* now do the nano sleep */
		nanosleep(&tim, NULL);

	}

}

void spiketrainPat(void *spikeGenState, uint32_t spikePattern[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE]) { //generate and send 32*32 input stimuli

	if (spikeGenState == NULL) {
		return;
	}

	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;

	struct timespec tim;
	tim.tv_sec = 0;

	if (atomic_load(&state->genSpikeState.stim_avr) > 0) {
		tim.tv_nsec = 1000000000L / atomic_load(&state->genSpikeState.stim_avr);
	}
	else {
		tim.tv_nsec = 999999999L;
	}

	//generate chip command for stimulating
	uint32_t value, valueSent;
	uint32_t value2DArray[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
	int64_t rowId, colId;
	for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++) {
		for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++) {
			if (spikePattern[rowId][colId] == 1)
				value = 0xf | 0 << 16 | 0 << 17 | 1 << 13
					| (uint32_t) (((rowId / DYNAPSE_CONFIG_NEUROW) << 1) | (uint32_t) (colId / DYNAPSE_CONFIG_NEUCOL))
						<< 18
					| (uint32_t) (((rowId % DYNAPSE_CONFIG_NEUROW) << 4) | (uint32_t) (colId % DYNAPSE_CONFIG_NEUCOL))
						<< 20 | (uint32_t) atomic_load(&state->genSpikeState.dx) << 4
					| (uint32_t) atomic_load(&state->genSpikeState.sx) << 6
					| (uint32_t) atomic_load(&state->genSpikeState.dy) << 7
					| (uint32_t) atomic_load(&state->genSpikeState.sy) << 9;
			else {
				value = 0;
			}
			value2DArray[rowId][colId] = value;
		}
	}

	if (!atomic_load(&state->genSpikeState.started)) {
		LABELSTART: portable_clock_gettime_monotonic(&tstart);
	}

	portable_clock_gettime_monotonic(&tend);

	if (atomic_load(&state->genSpikeState.stim_duration)
		<= ((double) tend.tv_sec + 1.0e-9 * (double) tend.tv_nsec)
			- ((double) tstart.tv_sec + 1.0e-9 * (double) tstart.tv_nsec)) {
		if (atomic_load(&state->genSpikeState.started)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation finished.");
		}
		atomic_store(&state->genSpikeState.done, true);
		atomic_store(&state->genSpikeState.started, false);
		if (atomic_load(&state->genSpikeState.repeat)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation re-started.");
			atomic_store(&state->genSpikeState.started, true);
			atomic_store(&state->genSpikeState.done, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->genSpikeState.done)) {

		/* remove time it takes to send, to better match the target freq */
		struct timespec ss, dd;
		portable_clock_gettime_monotonic(&ss);
		/* */

		// send spikes
		caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
		DYNAPSE_CONFIG_CHIP_ID, (uint32_t) atomic_load(&state->genSpikeState.chip_id));
		//send the spike
		for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++)
			for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++) {
				valueSent = value2DArray[rowId][colId];
				if (valueSent != 0 && ((valueSent >> 18) & 0x3ff) != 0) {
					caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
					DYNAPSE_CONFIG_CHIP_CONTENT, valueSent);
					//caerLog(CAER_LOG_NOTICE, "spikeGen", "sending spikes %d ", valueSent);
				}
			}

		/* */
		portable_clock_gettime_monotonic(&dd);
		tim.tv_nsec = tim.tv_nsec - (dd.tv_nsec - ss.tv_nsec);
		/* */
		nanosleep(&tim, NULL);
	}

}

void spiketrainPatSingle(void *spikeGenState, uint32_t sourceAddress) {
	if (spikeGenState == NULL) {
		return;
	}

	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;

	struct timespec tim;
	tim.tv_sec = 0;

	if (atomic_load(&state->genSpikeState.stim_avr) > 0) {
		tim.tv_nsec = 1000000000L / atomic_load(&state->genSpikeState.stim_avr);
	}
	else {
		tim.tv_nsec = 999999999L;
	}

	// generate chip command for stimulating
	uint32_t valueSent, valueSentTeaching, valueSentTeachingControl, valueSentInhibitory, valueSentInhibitoryControl;
	uint32_t source_address;
	valueSent = 0xf | 0 << 16 | 0 << 17 | 1 << 13 | (uint32_t) (sourceAddress & 0xff) << 20
		| (uint32_t) ((sourceAddress & 0x300) >> 8) << 18 | (uint32_t) atomic_load(&state->genSpikeState.dx) << 4
		| (uint32_t) atomic_load(&state->genSpikeState.sx) << 6 | (uint32_t) atomic_load(&state->genSpikeState.dy) << 7
		| (uint32_t) atomic_load(&state->genSpikeState.sy) << 9;

	source_address = 0;
	if (pattern_number == 3) {
		if ((sourceAddress & 0xff) == 1) {
			source_address = 0;
		}
		else if ((sourceAddress & 0xff) == 2) {
			source_address = 4;
		}
		else if ((sourceAddress & 0xff) == 3) {
			source_address = 8;
		}
	}
	else if (pattern_number == 4) {
		if ((sourceAddress & 0xff) == 1) {
			source_address = 0;
		}
		else if ((sourceAddress & 0xff) == 2) {
			source_address = 4;
		}
		else if ((sourceAddress & 0xff) == 3) {
			source_address = 8;
		}
		else if ((sourceAddress & 0xff) == 4) {
			source_address = 12;
		}
	}

	valueSentTeaching = 0x8 | 0 << 16 | 0 << 17 | 1 << 13 | (uint32_t) source_address << 20 | 0x3 << 18
		| (uint32_t) atomic_load(&state->genSpikeState.dx) << 4 | (uint32_t) atomic_load(&state->genSpikeState.sx) << 6
		| (uint32_t) atomic_load(&state->genSpikeState.dy) << 7 | (uint32_t) atomic_load(&state->genSpikeState.sy) << 9; //((sourceAddress & 0x300) >> 8) << 18

	valueSentTeachingControl = 0xc | 0 << 16 | 0 << 17 | 1 << 13 | (uint32_t) source_address << 20 | 0x3 << 18
		| (uint32_t) atomic_load(&state->genSpikeState.dx) << 4 | (uint32_t) atomic_load(&state->genSpikeState.sx) << 6
		| 1 << 7 | 1 << 9;

	valueSentInhibitory = 0x8 | 0 << 16 | 0 << 17 | 1 << 13 | 3 << 20 | 0x3 << 18
		| (uint32_t) atomic_load(&state->genSpikeState.dx) << 4 | (uint32_t) atomic_load(&state->genSpikeState.sx) << 6
		| (uint32_t) atomic_load(&state->genSpikeState.dy) << 7 | (uint32_t) atomic_load(&state->genSpikeState.sy) << 9; //((sourceAddress & 0x300) >> 8) << 18

	valueSentInhibitoryControl = 0xc | 0 << 16 | 0 << 17 | 1 << 13 | 3 << 20 | 0x3 << 18
		| (uint32_t) atomic_load(&state->genSpikeState.dx) << 4 | (uint32_t) atomic_load(&state->genSpikeState.sx) << 6
		| 1 << 7 | 1 << 9;

	if (!atomic_load(&state->genSpikeState.started)) {
		LABELSTART: portable_clock_gettime_monotonic(&tstart);
	}

	portable_clock_gettime_monotonic(&tend);

	if (atomic_load(&state->genSpikeState.stim_duration)
		<= ((double) tend.tv_sec + 1.0e-9 * (double) tend.tv_nsec)
			- ((double) tstart.tv_sec + 1.0e-9 * (double) tstart.tv_nsec)) {
		if (atomic_load(&state->genSpikeState.started)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation finished.");
		}
		atomic_store(&state->genSpikeState.done, true);
		atomic_store(&state->genSpikeState.started, false);
		if (atomic_load(&state->genSpikeState.repeat)) {
			//caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation re-started.");
			atomic_store(&state->genSpikeState.started, true);
			atomic_store(&state->genSpikeState.done, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->genSpikeState.done)) {

		/* remove time it takes to send */
		struct timespec ss, dd;
		portable_clock_gettime_monotonic(&ss);
		/* */

		// send spikes
		if (atomic_load(&state->genSpikeState.doStimPrimitiveBias) == true
			&& atomic_load(&state->genSpikeState.doStimPrimitiveCam) == true) {
			caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
			DYNAPSE_CONFIG_CHIP_ID, (uint32_t) atomic_load(&state->genSpikeState.chip_id));
			//send the spike
			caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
			DYNAPSE_CONFIG_CHIP_CONTENT, valueSent);
			if (atomic_load(&state->genSpikeState.teaching) == true
				&& atomic_load(&state->genSpikeState.sendTeachingStimuli) == true) {
				caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_ID,
				DYNAPSE_CONFIG_DYNAPSE_U2);
				caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, valueSentTeaching);
				caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, valueSentTeachingControl);
			}
			if (atomic_load(&state->genSpikeState.sendInhibitoryStimuli) == true) { //atomic_load(&state->genSpikeState.teaching) == true &&
				caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_ID,
				DYNAPSE_CONFIG_DYNAPSE_U2);
				caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, valueSentInhibitory);
				caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, valueSentInhibitoryControl);
			}
		}

		/* remove time it took to send, to meet frequency */
		portable_clock_gettime_monotonic(&dd);
		tim.tv_nsec = tim.tv_nsec - (dd.tv_nsec - ss.tv_nsec);
		/* now do the nano sleep */
		nanosleep(&tim, NULL);

	}
}

void SetCam(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return;
	}
	caerInputDynapseState state = spikeGenState;

	if (atomic_load(&state->genSpikeState.running) == false) {
		return;
	}

	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
		(uint32_t) atomic_load(&state->genSpikeState.chip_id)); //0
	uint32_t neuronId;
	caerLog(CAER_LOG_NOTICE, __func__, "Started programming cam..");
	for (neuronId = 0; neuronId < DYNAPSE_CONFIG_XCHIPSIZE * DYNAPSE_CONFIG_YCHIPSIZE; neuronId++) {
		caerDynapseWriteCam(state->deviceState, neuronId, neuronId, 0, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
	}
	caerLog(CAER_LOG_NOTICE, __func__, "CAM programmed successfully.");

}

void SetCamSingle(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return;
	}
	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
		(uint32_t) atomic_load(&state->genSpikeState.chip_id)); //0

	int64_t rowId, colId;
	int64_t num = DYNAPSE_CONFIG_CAMCOL;
	// generate pattern A
	uint32_t spikePatternA[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
	int cx, cy, r;
	cx = 16;
	cy = 16;
	r = 14;
	for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++)
		for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++)
			spikePatternA[rowId][colId] = 0;
	for (rowId = cx - r; rowId <= cx + r; rowId++)
		for (colId = cy - r; colId <= cy + r; colId++)
			if (((cx - rowId) * (cx - rowId) + (cy - colId) * (cy - colId) <= r * r + sqrt(r))
				&& ((cx - rowId) * (cx - rowId) + (cy - colId) * (cy - colId) >= r * r - r))
				spikePatternA[rowId][colId] = 1;

	uint32_t spikePatternB[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
	for (rowId = -num; rowId < num; rowId++) {
		for (colId = -num; colId < num; colId++) {
			if (abs((int) rowId) + abs((int) colId) == num) // Change this condition >= <=
				spikePatternB[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 1;
			else
				spikePatternB[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 0;
		}
	}

	uint32_t spikePatternC[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
	for (rowId = -num; rowId < num; rowId++) {
		for (colId = -num; colId < num; colId++) {
			if (abs((int) rowId) == abs((int) colId)) // Change this condition
				spikePatternC[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 1;
			else
				spikePatternC[rowId + DYNAPSE_CONFIG_CAMCOL][colId + DYNAPSE_CONFIG_CAMCOL] = 0;
		}
	}

	uint32_t neuronId;
	caerLog(CAER_LOG_NOTICE, __func__, "Started programming cam..");
	for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++) {
		for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++) {
			neuronId = (uint32_t) ((rowId & 0X10) >> 4) << 9 | (uint32_t) ((colId & 0X10) >> 4) << 8
				| (uint32_t) (rowId & 0xf) << 4 | (uint32_t) (colId & 0xf);
			if (spikePatternA[rowId][colId] == 1) {
				caerDynapseWriteCam(state->deviceState, 1, neuronId, 0, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
			}
			if (spikePatternB[rowId][colId] == 1) {
				//WriteCam(state, 2, neuronId, 1, 3);
				caerDynapseWriteCam(state->deviceState, 2, neuronId, 1, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
			}
			if (spikePatternC[rowId][colId] == 1) {
				//WriteCam(state, 3, neuronId, 2, 3);
				caerDynapseWriteCam(state->deviceState, 3, neuronId, 2, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
			}
		}
	}

	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
	DYNAPSE_CONFIG_DYNAPSE_U2); //4, the third chip
	neuronId = 3 << 8 | 0;
	caerDynapseWriteCam(state->deviceState, 1, neuronId, 61, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
	caerDynapseWriteCam(state->deviceState, 2, neuronId, 62, 1);
	caerDynapseWriteCam(state->deviceState, 3, neuronId, 63, 1);
	neuronId = 3 << 8 | 1;
	caerDynapseWriteCam(state->deviceState, 1, neuronId, 61, 1);
	caerDynapseWriteCam(state->deviceState, 2, neuronId, 62, DYNAPSE_CONFIG_CAMTYPE_F_EXC);
	caerDynapseWriteCam(state->deviceState, 3, neuronId, 63, 1);
	neuronId = 3 << 8 | 2;
	caerDynapseWriteCam(state->deviceState, 1, neuronId, 61, 1);
	caerDynapseWriteCam(state->deviceState, 2, neuronId, 62, 1);
	caerDynapseWriteCam(state->deviceState, 3, neuronId, 63, DYNAPSE_CONFIG_CAMTYPE_F_EXC);

	caerLog(CAER_LOG_NOTICE, "SpikeGen", "CAM programmed successfully.");

}

void ClearCam(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return;
	}
	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
		(uint32_t) atomic_load(&state->genSpikeState.chip_id)); //0
	uint32_t neuronId;
	caerLog(CAER_LOG_NOTICE, "SpikeGen", "Started clearing cam...");
	caerLog(CAER_LOG_NOTICE, "SpikeGen", "please wait...");
	for (neuronId = 0; neuronId < DYNAPSE_CONFIG_NUMNEURONS; neuronId++) {
		//WriteCam(state, 0, neuronId, 0, 0);
		caerDynapseWriteCam(state->deviceState, 0, neuronId, 0, 0);
	}
	caerLog(CAER_LOG_NOTICE, "SpikeGen", "Done, CAM cleared successfully.");
	atomic_store(&state->genSpikeState.clearCam, false);

}

void ClearAllCam(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return;
	}

	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;

	// Select chip-id to operate on.
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
		U32T(atomic_load(&state->genSpikeState.chip_id)));

	// Clear all CAMs on this chip.
	caerLog(CAER_LOG_NOTICE, "SpikeGen", "Started clearing CAM ...");
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CLEAR_CAM, 0, 0);
	caerLog(CAER_LOG_NOTICE, "SpikeGen", "CAM cleared successfully.");

	atomic_store(&state->genSpikeState.clearAllCam, false);
}

void ResetBiases(void *spikeGenState) {

	caerInputDynapseState state = spikeGenState;

	if (spikeGenState == NULL) {
		return;
	}
	caerLog(CAER_LOG_NOTICE, "loadDefaultBiases", "started...");

	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;
	uint8_t chipId = U8T(atomic_load(&state->genSpikeState.chip_id));
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, U32T(chipId));

	for (uint8_t coreId = 0; coreId < 4; coreId++) {
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTAU_N", 7, 34, false);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTAU_N", 7, 35, false);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTHR_N", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHTHR_N", 7, 1, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHW_P", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_AHW_P", 7, 1, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_BUF_P", 3, 79, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_BUF_P", 3, 80, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_CASC_N", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_CASC_N", 7, 1, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_DC_P", 5, 1, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_DC_P", 5, 2, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_NMDA_N", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_NMDA_N", 7, 1, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_RFR_N", 2, 179, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_RFR_N", 2, 180, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU1_N", 4, 224, false);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU1_N", 4, 225, false);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU2_N", 4, 224, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_TAU2_N", 4, 225, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_THR_N", 2, 179, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "IF_THR_N", 2, 180, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_F_P", 6, 149, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_F_P", 6, 150, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_S_P", 7, 39, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_TAU_S_P", 7, 40, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_F_P", 0, 199, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_F_P", 0, 200, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_S_P", 7, 1, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPIE_THR_S_P", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_F_P", 7, 39, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_F_P", 7, 40, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_S_P", 7, 39, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_TAU_S_P", 7, 40, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_F_P", 7, 39, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_F_P", 7, 40, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_S_P", 7, 39, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "NPDPII_THR_S_P", 7, 40, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_F_N", 0, 251, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_F_N", 0, 250, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_S_N", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_EXC_S_N", 7, 1, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_F_N", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_F_N", 7, 1, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_S_N", 7, 1, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PS_WEIGHT_INH_S_N", 7, 0, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PULSE_PWLK_P", 3, 49, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "PULSE_PWLK_P", 3, 50, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "R2R_P", 4, 84, true);
		caerDynapseSetBiasCore(state->eventSourceConfigNode, chipId, coreId, "R2R_P", 4, 85, true);
	}
}
