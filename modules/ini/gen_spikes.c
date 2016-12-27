#include "base/module.h"
#ifdef HAVE_PTHREADS
#include "ext/c11threads_posix.h"
#endif

#include <math.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>

#include "main.h"
#include "dynapse_common.h"
#include <libcaer/events/packetContainer.h>
#include <libcaer/events/spike.h>

#define STIM_POISSON 	1
#define STIM_REGULAR 	2
#define STIM_GAUSSIAN 	3
#define STIM_PATTERNA   4
#define STIM_PATTERNB   5
#define STIM_PATTERNC   6

bool caerGenSpikeInit(caerModuleData moduleData);
void caerGenSpikeExit(caerModuleData moduleData);
int spikeGenThread(void *spikeGenState);
void spiketrainReg(void *spikeGenState);
void spiketrainPat(void *spikeGenState, uint32_t spikePattern[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE]);
void SetCam(void *spikeGenState);
void ClearCam(void *spikeGenState);
void ClearAllCam(void *spikeGenState);
void WriteCam(void *spikeGenState, uint32_t preNeuronAddr,
		uint32_t postNeuronAddr, uint32_t camId, int16_t synapseType);
void ResetBiases(void *spikeGenState);
void setBiasBits(void *spikeGenState, uint32_t chipId, uint32_t coreId, const char *biasName_t,
		uint8_t coarseValue, uint16_t fineValue, const char *lowHigh, const char *npBias);

struct timespec tstart = { 0, 0 }, tend = { 0, 0 };
static int CamSeted = 0; //static bool CamSeted = false;
static int CamCleared = 0; //static bool CamCleared = false;
static int CamAllCleared = 0;
static int BiasesLoaded = 0;

bool caerGenSpikeInit(caerModuleData moduleData) {

	caerInputDynapseState state = moduleData->moduleState;

	sshsNode deviceConfigNodeMain = sshsGetRelativeNode(moduleData->moduleNode,
			chipIDToName(DYNAPSE_CHIP_DYNAPSE, true));

	sshsNode spikeNode = sshsGetRelativeNode(deviceConfigNodeMain, "spikeGen/");

	sshsNodePutBoolIfAbsent(spikeNode, "doStim", false); //false

	sshsNodePutIntIfAbsent(spikeNode, "stim_type", U8T(STIM_REGULAR)); //STIM_REGULAR
	atomic_store(&state->genSpikeState.stim_type,
			sshsNodeGetInt(spikeNode, "stim_type"));

	sshsNodePutIntIfAbsent(spikeNode, "stim_avr", 3);
	atomic_store(&state->genSpikeState.stim_avr,
			sshsNodeGetInt(spikeNode, "stim_avr"));

	sshsNodePutIntIfAbsent(spikeNode, "stim_std", 1);
	atomic_store(&state->genSpikeState.stim_std,
			sshsNodeGetInt(spikeNode, "stim_std"));

	sshsNodePutIntIfAbsent(spikeNode, "stim_duration", 10); //10
	atomic_store(&state->genSpikeState.stim_duration,
			sshsNodeGetInt(spikeNode, "stim_duration"));

	sshsNodePutBoolIfAbsent(spikeNode, "repeat", false); //false
	atomic_store(&state->genSpikeState.repeat,
			sshsNodeGetBool(spikeNode, "repeat"));

	sshsNodePutBoolIfAbsent(spikeNode, "setCam", false); //1 //false
	atomic_store(&state->genSpikeState.setCam,
			sshsNodeGetBool(spikeNode, "setCam"));

	sshsNodePutBoolIfAbsent(spikeNode, "clearCam", false); //1 //false
	atomic_store(&state->genSpikeState.clearCam,
			sshsNodeGetBool(spikeNode, "clearCam"));

	sshsNodePutBoolIfAbsent(spikeNode, "clearAllCam", false); //1 //false
	atomic_store(&state->genSpikeState.clearAllCam,
			sshsNodeGetBool(spikeNode, "clearAllCam"));

	sshsNodePutBoolIfAbsent(spikeNode, "loadDefaultBiases", false); //1 //false
	atomic_store(&state->genSpikeState.loadDefaultBiases,
			sshsNodeGetBool(spikeNode, "loadDefaultBiases"));

	atomic_store(&state->genSpikeState.started, false); //false
	atomic_store(&state->genSpikeState.done, true);

	// Start separate stimulation thread.
	atomic_store(&state->genSpikeState.running, true);

	if (thrd_create(&state->genSpikeState.spikeGenThread, &spikeGenThread,
			state) != thrd_success) {
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString,
				"SpikeGen: Failed to start thread.");
		return (NULL);
	}

	/*address*/
	sshsNodePutBoolIfAbsent(spikeNode, "sx", false);
	atomic_store(&state->genSpikeState.sx, sshsNodeGetBool(spikeNode, "sx"));

	sshsNodePutBoolIfAbsent(spikeNode, "sy", false);
	atomic_store(&state->genSpikeState.sy, sshsNodeGetBool(spikeNode, "sy"));

	sshsNodePutIntIfAbsent(spikeNode, "core_d", 0);
	atomic_store(&state->genSpikeState.core_d,
			sshsNodeGetInt(spikeNode, "core_d"));

	sshsNodePutIntIfAbsent(spikeNode, "core_s", 0);
	atomic_store(&state->genSpikeState.core_s,
			sshsNodeGetInt(spikeNode, "core_s"));

	sshsNodePutIntIfAbsent(spikeNode, "address", 1);
	atomic_store(&state->genSpikeState.address,
			sshsNodeGetInt(spikeNode, "address"));

	sshsNodePutIntIfAbsent(spikeNode, "dx", 0);
	atomic_store(&state->genSpikeState.dx, sshsNodeGetInt(spikeNode, "dx"));

	sshsNodePutIntIfAbsent(spikeNode, "dy", 0);
	atomic_store(&state->genSpikeState.dy, sshsNodeGetInt(spikeNode, "dy"));

	sshsNodePutIntIfAbsent(spikeNode, "chip_id", DYNAPSE_CONFIG_DYNAPSE_U0); //4
	atomic_store(&state->genSpikeState.chip_id,
			sshsNodeGetInt(spikeNode, "chip_id"));

	return (true);
}

void caerGenSpikeExit(caerModuleData moduleData) {
	caerInputDynapseState state = moduleData->moduleState;

	// Shut down stimulation thread and wait on it to finish.
	atomic_store(&state->genSpikeState.running, false);

	if ((errno = thrd_join(state->genSpikeState.spikeGenThread, NULL))
			!= thrd_success) {
		// This should never happen!
		caerLog(CAER_LOG_CRITICAL, moduleData->moduleSubSystemString,
				"SpikeGen: Failed to join rendering thread. Error: %d.", errno);
	}

}

int spikeGenThread(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return (thrd_error);
	}

	caerInputDynapseState state = spikeGenState;

	thrd_set_name("SpikeGenThread");

	while (atomic_load_explicit(&state->genSpikeState.running, // the loop
			memory_order_relaxed)) {

		if (state->genSpikeState.setCam == true && CamSeted == 0) {
			SetCam(state);
			CamSeted = 1;
		} else if (state->genSpikeState.setCam == false && CamSeted == 1) {
			CamSeted = 0;
		}
		if (state->genSpikeState.clearCam == true && CamCleared == 0) {
			ClearCam(state);
			CamCleared = 1;
		} else if (state->genSpikeState.clearCam == false && CamCleared == 1) {
			CamCleared = 0;
		}
		if (state->genSpikeState.clearAllCam == true && CamAllCleared == 0) {
			ClearAllCam(state);
			CamAllCleared = 1;
		} else if (state->genSpikeState.clearAllCam == false
				&& CamAllCleared == 1) {
			CamAllCleared = 0;
		}
		if (state->genSpikeState.loadDefaultBiases == true && BiasesLoaded == 0) {
			ResetBiases(spikeGenState);
			BiasesLoaded = 1;
		} else if (state->genSpikeState.loadDefaultBiases == false
				&& BiasesLoaded == 1) {
			BiasesLoaded = 0;
		}

		/* generate spikes*/

		if (state->genSpikeState.stim_type == STIM_REGULAR) {
			spiketrainReg(state);
		} else if (state->genSpikeState.stim_type == STIM_POISSON) {

		} else if (state->genSpikeState.stim_type == STIM_GAUSSIAN) {

		} else if (state->genSpikeState.stim_type == STIM_PATTERNA) {
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
					if (((cx - rowId) * (cx - rowId)
							+ (cy - colId) * (cy - colId) <= r * r + sqrt(r))
							&& ((cx - rowId) * (cx - rowId)
									+ (cy - colId) * (cy - colId) >= r * r - r))
						spikePatternA[rowId][colId] = 1;
			spiketrainPat(state, spikePatternA);
		} else if (state->genSpikeState.stim_type == STIM_PATTERNB) {
			//generate pattern B
			uint32_t spikePatternB[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
			int64_t rowId, colId;
			int64_t num = DYNAPSE_CONFIG_CAMNUM;
			for (rowId = -num; rowId < num; rowId++) {
				for (colId = -num; colId < num; colId++) {
					if (abs((int) rowId) + abs((int) colId) == num) // Change this condition >= <=
						spikePatternB[rowId + DYNAPSE_CONFIG_CAMNUM][colId + DYNAPSE_CONFIG_CAMNUM] = 1;
					else
						spikePatternB[rowId + DYNAPSE_CONFIG_CAMNUM][colId + DYNAPSE_CONFIG_CAMNUM] = 0;
				}
			}
			spiketrainPat(state, spikePatternB);
		} else if (state->genSpikeState.stim_type == STIM_PATTERNC) {
			//generate pattern C
			uint32_t spikePatternC[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
			int64_t rowId, colId;
			int64_t num = DYNAPSE_CONFIG_CAMNUM;
			for (rowId = -num; rowId < num; rowId++) {
				for (colId = -num; colId < num; colId++) {
					if (abs((int) rowId) == abs((int) colId)) // Change this condition
						spikePatternC[rowId + DYNAPSE_CONFIG_CAMNUM][colId + DYNAPSE_CONFIG_CAMNUM] = 1;
					else
						spikePatternC[rowId + DYNAPSE_CONFIG_CAMNUM][colId + DYNAPSE_CONFIG_CAMNUM] = 0;
				}
			}
			spiketrainPat(state, spikePatternC);
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
	} else {
		tim.tv_nsec = 1000000000L;
	}

	uint32_t value = atomic_load(&state->genSpikeState.core_d) | 0 << 16
			| 0 << 17 | 1 << 13 |
			atomic_load(&state->genSpikeState.core_s) << 18 |
			atomic_load(&state->genSpikeState.address) << 20 |
			atomic_load(&state->genSpikeState.dx) << 4 |
			atomic_load(&state->genSpikeState.sx) << 6 |
			atomic_load(&state->genSpikeState.dy) << 7 |
			atomic_load(&state->genSpikeState.sy) << 9;

	if (!atomic_load(&state->genSpikeState.started)) {
		LABELSTART: clock_gettime(CLOCK_MONOTONIC, &tstart);
	}

	clock_gettime(CLOCK_MONOTONIC, &tend);

	if (atomic_load(&state->genSpikeState.stim_duration)
			<= ((double) tend.tv_sec + 1.0e-9 * tend.tv_nsec)
					- ((double) tstart.tv_sec + 1.0e-9 * tstart.tv_nsec)) {
		if (atomic_load(&state->genSpikeState.started)) {
			caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation finished.\n");
		}
		atomic_store(&state->genSpikeState.done, true);
		atomic_store(&state->genSpikeState.started, false);
		if (atomic_load(&state->genSpikeState.repeat)) {
			caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation re-started.\n");
			atomic_store(&state->genSpikeState.started, true);
			atomic_store(&state->genSpikeState.done, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->genSpikeState.done)) {
		nanosleep(&tim, NULL);
		// send spikes
		caerDeviceConfigSet((caerDeviceHandle) state->deviceState,
				DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
				atomic_load(&state->genSpikeState.chip_id));  //usb_handle
		/*send the spike*/
		caerDeviceConfigSet((caerDeviceHandle) state->deviceState,
				DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, value); //usb_handle
		caerLog(CAER_LOG_NOTICE, "spikeGen", "sending spikes %d \n", value);

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
	} else {
		tim.tv_nsec = 1000000000L;
	}

	//generate chip command for stimulating
	uint32_t value, valueSent;
	uint32_t value2DArray[DYNAPSE_CONFIG_XCHIPSIZE][DYNAPSE_CONFIG_YCHIPSIZE];
	int64_t rowId, colId;
	for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++)
		for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++) {
			if (spikePattern[rowId][colId] == 1)
				value = 0xf | 0 << 16 | 0 << 17 | 1 << 13
						| (((rowId / DYNAPSE_CONFIG_NEUROW) << 1) | (colId / DYNAPSE_CONFIG_NEUCOL)) << 18
						| (((rowId % DYNAPSE_CONFIG_NEUROW) << 4) | (colId % DYNAPSE_CONFIG_NEUCOL)) << 20 |
						atomic_load(&state->genSpikeState.dx) << 4 |
						atomic_load(&state->genSpikeState.sx) << 6 |
						atomic_load(&state->genSpikeState.dy) << 7 |
						atomic_load(&state->genSpikeState.sy) << 9;
			else
				value = 0;
			value2DArray[rowId][colId] = value;
		}

	if (!atomic_load(&state->genSpikeState.started)) {
		LABELSTART: clock_gettime(CLOCK_MONOTONIC, &tstart);
	}

	clock_gettime(CLOCK_MONOTONIC, &tend);

	if (atomic_load(&state->genSpikeState.stim_duration)
			<= ((double) tend.tv_sec + 1.0e-9 * tend.tv_nsec)
					- ((double) tstart.tv_sec + 1.0e-9 * tstart.tv_nsec)) {
		if (atomic_load(&state->genSpikeState.started)) {
			caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation finished.\n");
		}
		atomic_store(&state->genSpikeState.done, true);
		atomic_store(&state->genSpikeState.started, false);
		if (atomic_load(&state->genSpikeState.repeat)) {
			caerLog(CAER_LOG_NOTICE, "spikeGen", "stimulation re-started.\n");
			atomic_store(&state->genSpikeState.started, true);
			atomic_store(&state->genSpikeState.done, false);
			goto LABELSTART;
		}
	}

	if (!atomic_load(&state->genSpikeState.done)) {
		nanosleep(&tim, NULL);
		// send spikes
		caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_ID,
				atomic_load(&state->genSpikeState.chip_id));
		//send the spike
		for (rowId = 0; rowId < DYNAPSE_CONFIG_XCHIPSIZE; rowId++)
			for (colId = 0; colId < DYNAPSE_CONFIG_YCHIPSIZE; colId++) {
				valueSent = value2DArray[rowId][colId];
				if (valueSent != 0 && ((valueSent >> 18) & 0x3ff) != 0) {
					caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
							DYNAPSE_CONFIG_CHIP_CONTENT, valueSent);
				}
			}
		caerLog(CAER_LOG_NOTICE, "spikeGen", "sending spikes %d \n", value);
	}

}

void SetCam(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return;
	}
	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			atomic_load(&state->genSpikeState.chip_id)); //0
	uint32_t neuronId;
	caerLog(CAER_LOG_NOTICE, "\nSpikeGen", "Started programming cam..");
	for (neuronId = 0;
			neuronId < DYNAPSE_CONFIG_XCHIPSIZE * DYNAPSE_CONFIG_YCHIPSIZE;
			neuronId++) {
		WriteCam(state, neuronId, neuronId, 0, 3);
	}
	caerLog(CAER_LOG_NOTICE, "\nSpikeGen", "CAM programmed successfully.");
}

void ClearCam(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return;
	}
	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			atomic_load(&state->genSpikeState.chip_id)); //0
	uint32_t neuronId;
	caerLog(CAER_LOG_NOTICE, "\nSpikeGen", "Started clearing cam..");
	for (neuronId = 0; neuronId < DYNAPSE_CONFIG_NUMNEURONS; neuronId++) {
		WriteCam(state, 0, neuronId, 0, 0);
	}
	caerLog(CAER_LOG_NOTICE, "\nSpikeGen", "CAM cleared successfully.");
}

void ClearAllCam(void *spikeGenState) {
	if (spikeGenState == NULL) {
		return;
	}
	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			atomic_load(&state->genSpikeState.chip_id)); //0
	uint32_t neuronId, camId;
	caerLog(CAER_LOG_NOTICE, "\nSpikeGen", "Started clearing cam..");
	for (neuronId = 0; neuronId < DYNAPSE_CONFIG_NUMNEURONS; neuronId++) {
		for (camId = 0; camId < DYNAPSE_X4BOARD_NEUX; camId++) {
			WriteCam(state, 0, neuronId, camId, 0);
		}
	}
	caerLog(CAER_LOG_NOTICE, "\nSpikeGen", "CAM cleared successfully.");
}

void WriteCam(void *spikeGenState, uint32_t preNeuronAddr,
		uint32_t postNeuronAddr, uint32_t camId, int16_t synapseType) {
	if (spikeGenState == NULL) {
		return;
	}
	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;

	uint32_t bits;
	uint32_t ei = (synapseType & 0x2) >> 1;
	uint32_t fs = synapseType & 0x1;
	uint32_t address = preNeuronAddr & 0xff;
	uint32_t source_core = (preNeuronAddr & 0x300) >> 8;
	uint32_t coreId = (postNeuronAddr & 0x300) >> 8;
	uint32_t neuron_row = (postNeuronAddr & 0xf0) >> 4;
	uint32_t synapse_row = camId;
	uint32_t row = neuron_row << 6 | synapse_row;
	uint32_t column = postNeuronAddr & 0xf;
	bits = ei << 29 | fs << 28 | address << 20 | source_core << 18 | 1 << 17
			| coreId << 15 | row << 5 | column;
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
			DYNAPSE_CONFIG_CHIP_CONTENT, bits);
}

void ResetBiases(void *spikeGenState) {

	if (spikeGenState == NULL) {
		return;
	}
	caerInputDynapseState state = spikeGenState;
	caerDeviceHandle usb_handle = (caerDeviceHandle) state->deviceState;

	uint32_t chipId_t, chipId, coreId;

	for (chipId_t = 0; chipId_t < 1; chipId_t++) {

		if (chipId_t == 0)
			chipId = DYNAPSE_CONFIG_DYNAPSE_U0;
		else if (chipId_t == 1)
			chipId = DYNAPSE_CONFIG_DYNAPSE_U2;
		else if (chipId_t == 2)
			chipId = DYNAPSE_CONFIG_DYNAPSE_U1;
		else if (chipId_t == 3)
			chipId = DYNAPSE_CONFIG_DYNAPSE_U3;

		caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chipId);

		for (coreId = 0; coreId < 4; coreId++) {
			if (chipId == 0) {
				if (coreId == 0) {
					setBiasBits(spikeGenState, chipId, coreId, "IF_AHTAU_N", 7, 35, "LowBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_AHTHR_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_AHW_P", 7, 0, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_BUF_P", 3, 80, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_CASC_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_DC_P", 7, 0, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_NMDA_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_RFR_N", 5, 255, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_TAU1_N", 4, 200, "LowBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_TAU2_N", 6, 15, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_THR_N", 2, 40, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPIE_TAU_F_P", 6, 200, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPIE_TAU_S_P", 7, 40, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPIE_THR_F_P", 0, 220, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPIE_THR_S_P", 7, 0, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPII_TAU_F_P", 7, 40, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPII_TAU_S_P", 7, 40, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPII_THR_F_P", 7, 40, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPII_THR_S_P", 7, 40, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "PS_WEIGHT_EXC_F_N", 0, 76, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "PULSE_PWLK_P", 3, 50, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "R2R_P", 4, 85, "HighBias", "PBias");
				} else {
					setBiasBits(spikeGenState, chipId, coreId, "IF_AHTAU_N", 7, 35, "LowBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_AHTHR_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_AHW_P", 7, 0, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_BUF_P", 3, 80, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_CASC_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_DC_P", 7, 0, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_NMDA_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_RFR_N", 5, 255, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_TAU1_N", 4, 200, "LowBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_TAU2_N", 6, 15, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "IF_THR_N", 2, 40, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPIE_TAU_F_P", 6, 105, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPIE_TAU_S_P", 7, 40, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPIE_THR_F_P", 0, 220, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPIE_THR_S_P", 7, 0, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPII_TAU_F_P", 7, 40, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPII_TAU_S_P", 7, 40, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPII_THR_F_P", 7, 40, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "NPDPII_THR_S_P", 7, 40, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "PS_WEIGHT_EXC_F_N", 0, 76, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "PS_WEIGHT_EXC_S_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "PS_WEIGHT_INH_F_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "PS_WEIGHT_INH_S_N", 7, 0, "HighBias", "NBias");
					setBiasBits(spikeGenState, chipId, coreId, "PULSE_PWLK_P", 3, 50, "HighBias", "PBias");
					setBiasBits(spikeGenState, chipId, coreId, "R2R_P", 4, 85, "HighBias", "PBias");
				}
			}
		}
	}
}

void setBiasBits(void *spikeGenState, uint32_t chipId, uint32_t coreId, const char *biasName_t,
		uint8_t coarseValue, uint16_t fineValue, const char *lowHigh, const char *npBias) {

	if (spikeGenState == NULL) {
		return;
	}
	caerInputDynapseState stateSource = caerMainloopGetSourceState(U16T(1));
	struct caer_dynapse_info dynapse_info = caerDynapseInfoGet(stateSource->deviceState);

	caerDeviceHandle usb_handle = stateSource->deviceState;

    size_t biasNameLength = strlen(biasName_t);
    char biasName[biasNameLength+3];

	biasName[0] = 'C';
	if (coreId == 0)
		biasName[1] = '0';
	else if (coreId == 1)
		biasName[1] = '1';
	else if (coreId == 2)
		biasName[1] = '2';
	else if (coreId == 3)
		biasName[1] = '3';
	biasName[2] = '_';

	uint32_t i;
	for(i = 0; i < biasNameLength + 3; i++) {
		biasName[3+i] = biasName_t[i];
	}

	uint32_t bits = generatesBitsCoarseFineBiasSetting(caerMainloopGetSourceNode(U16T(1)), &dynapse_info,
			biasName, coarseValue, fineValue, lowHigh, "Normal", npBias, true, chipId);

	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, bits);
}

