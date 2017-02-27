/*
 * activityindicator.c
 *
 *  Created on: Feb 2017
 *      Author: Tianyu
 */

#include "activityindicator.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "ext/buffers.h"
#include "math.h"

typedef enum {Verylow, low, median, high} activityLevel;

struct AI_state {
	int64_t LastUpdateTime;
	int measuringTime ;
	int activeThreshold;
	int low;
	int median;
	int high;
	long activeNum;
	activityLevel areaActivity;
	simple2DBufferLong spikeCountMap;
	simple2DBufferInt activeCountMap;
};

typedef struct AI_state *AIState;

int64_t maxLastTime = 0;

static bool caerActivityIndicatorInit(caerModuleData moduleData);
static void caerActivityIndicatorRun(caerModuleData moduleData, size_t argsNumber, va_list args);
static void caerActivityIndicatorConfig(caerModuleData moduleData);
static void caerActivityIndicatorExit(caerModuleData moduleData);
static void caerActivityIndicatorReset(caerModuleData moduleData, uint16_t resetCallSourceID);
static bool allocateSpikeCountMap(AIState state, int16_t sourceID);
static bool allocateActiveCountMap(AIState state, int16_t sourceID);

static struct caer_module_functions caerActivityIndicatorFunctions = { .moduleInit = &caerActivityIndicatorInit, .moduleRun = &caerActivityIndicatorRun, .moduleConfig = &caerActivityIndicatorConfig, .moduleExit = &caerActivityIndicatorExit, .moduleReset = &caerActivityIndicatorReset };

AResults caerActivityIndicator(uint16_t moduleID, caerPolarityEventPacket polarity) {

	AResults number = malloc(sizeof(struct activity_results));
	caerMainloopFreeAfterLoop(&free, number);
	number->activityValue = -1;
	strcpy(number->stringValue,"");

	caerModuleData moduleData = caerMainloopFindModule(moduleID, "ActivityIndicator", CAER_MODULE_PROCESSOR);
	if (moduleData == NULL) {
		return(number);
	}
	caerModuleSM(&caerActivityIndicatorFunctions, moduleData, sizeof(struct AI_state), 2, polarity, number);

	return(number);
}

static bool caerActivityIndicatorInit(caerModuleData moduleData) {
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "measuringTime", 10000000);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "activeThreshold", 10);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "low", 100);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "median", 500);
	sshsNodePutIntIfAbsent(moduleData->moduleNode, "high", 1000);

	AIState state = moduleData->moduleState;

	state->measuringTime = sshsNodeGetInt(moduleData->moduleNode, "measuringTime");
	state->activeThreshold = sshsNodeGetInt(moduleData->moduleNode, "activeThreshold");
	state->low = sshsNodeGetInt(moduleData->moduleNode, "low");
	state->median = sshsNodeGetInt(moduleData->moduleNode, "median");
	state->high = sshsNodeGetInt(moduleData->moduleNode, "high");

	state->LastUpdateTime = 0;
	state->activeNum = 0;

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	// Nothing that can fail here.
	return (true);
}
static void caerActivityIndicatorRun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	caerPolarityEventPacket polarity = va_arg(args, caerPolarityEventPacket);
	AResults results = va_arg(args, AResults);

	//Only process packets with content.
	if (polarity == NULL) {
		return;
	}

	AIState state = moduleData->moduleState;

	int16_t sourceID = caerEventPacketHeaderGetEventSource(&polarity->packetHeader);
	sshsNode sourceInfoNodeCA = caerMainloopGetSourceInfo((uint16_t)sourceID);
	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
	if (!sshsNodeAttributeExists(sourceInfoNode, "dataSizeX", SSHS_SHORT)) {
		sshsNodePutShort(sourceInfoNode, "dataSizeX", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeX"));
		sshsNodePutShort(sourceInfoNode, "dataSizeY", sshsNodeGetShort(sourceInfoNodeCA, "dvsSizeY"));
	}
	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");

	// If the map is not allocated yet, do it.
	if (state->spikeCountMap == NULL) {
		if (!allocateSpikeCountMap(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
			// Failed to allocate memory, nothing to do.
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for spikeCountMap.");
			return;
		}
	}
	if (state->activeCountMap == NULL) {
		if (!allocateActiveCountMap(state, caerEventPacketHeaderGetEventSource(&polarity->packetHeader))) {
			// Failed to allocate memory, nothing to do.
			caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString, "Failed to allocate memory for activeCountMap.");
			return;
		}
	}

	// reset last time of one packet
	maxLastTime = 0;

	//Iterate over events
	CAER_POLARITY_ITERATOR_VALID_START(polarity)

	// Get values on which to operate.
	int64_t ts = caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity);
	uint16_t x = caerPolarityEventGetX(caerPolarityIteratorElement);
	uint16_t y = caerPolarityEventGetY(caerPolarityIteratorElement);

	if ((caerPolarityIteratorElement == NULL)){
		continue;
	}
	if ((x > sizeX) || (y > sizeY)) {
		continue;
	}
	if (maxLastTime < caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity)){
		maxLastTime = caerPolarityEventGetTimestamp64(caerPolarityIteratorElement, polarity);
	}

	state->spikeCountMap->buffer2d[x][y] += 1;

	CAER_POLARITY_ITERATOR_VALID_END

	// check time to update status
	if ((maxLastTime - state->LastUpdateTime) > state->measuringTime){
		// reset active number
		state->activeNum = 0;
		//checkThreshold and updateActivity
		for (size_t x = 0; x < sizeX; x++) {
			for (size_t y = 0; y < sizeY; y++) {
				if (state->spikeCountMap->buffer2d[x][y] > state->activeThreshold) {
					state->activeCountMap->buffer2d[x][y] = 1;
					state->activeNum++;
				}
				else{
					state->activeCountMap->buffer2d[x][y] = 0;
				}
				//reset spike count map
				state->spikeCountMap->buffer2d[x][y] = 0;
			}
		}
		state->LastUpdateTime = maxLastTime;

		if (state->activeNum < state->low){
			state->areaActivity = Verylow;
			//caerLog(CAER_LOG_NOTICE, __func__, "   Very low, activeNum: %d", state->activeNum);
			strcpy(results->stringValue, "Very low");
		}
		else if (state->activeNum < median){
			state->areaActivity = low;
			//caerLog(CAER_LOG_NOTICE, __func__, "  Low, activeNum: %d", state->activeNum);
			strcpy(results->stringValue, "Low");
		}
		else if (state->activeNum < high){
			state->areaActivity = median;
			//caerLog(CAER_LOG_NOTICE, __func__, "   Median, activeNum: %d", state->activeNum);
			strcpy(results->stringValue, "Median");
		}
		else {
			state->areaActivity = high;
			//caerLog(CAER_LOG_NOTICE, __func__, "   High, activeNum: %d", state->activeNum);
			strcpy(results->stringValue, "High");
		}
		//printf("real number %d\n", state->activeNum);
		results->activityValue = state->activeNum;
	}
}

static bool allocateSpikeCountMap(AIState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");

	state->spikeCountMap = simple2DBufferInitLong((size_t) sizeX, (size_t) sizeY);
	if (state->spikeCountMap == NULL) {
		return (false);
	}

	for (size_t x = 0; x < sizeX; x++) {
		for (size_t y = 0; y < sizeY; y++) {
			state->spikeCountMap->buffer2d[x][y] = 0; // init to zero
		}
	}

	return (true);
}

static bool allocateActiveCountMap(AIState state, int16_t sourceID) {
	// Get size information from source.
	sshsNode sourceInfoNode = caerMainloopGetSourceInfo(U16T(sourceID));
	if (sourceInfoNode == NULL) {
		// This should never happen, but we handle it gracefully.
		caerLog(CAER_LOG_ERROR, __func__, "Failed to get source info to allocate map.");
		return (false);
	}

	int16_t sizeX = sshsNodeGetShort(sourceInfoNode, "dataSizeX");
	int16_t sizeY = sshsNodeGetShort(sourceInfoNode, "dataSizeY");

	state->activeCountMap = simple2DBufferInitInt((size_t) sizeX, (size_t) sizeY);
	if (state->activeCountMap == NULL) {
		return (false);
	}

	for (size_t x = 0; x < sizeX; x++) {
		for (size_t y = 0; y < sizeY; y++) {
			state->activeCountMap->buffer2d[x][y] = 0; // init to zero
		}
	}

	return (true);
}

static void caerActivityIndicatorConfig(caerModuleData moduleData) {
	caerModuleConfigUpdateReset(moduleData);

	AIState state = moduleData->moduleState;

	state->measuringTime = sshsNodeGetInt(moduleData->moduleNode, "measuringTime");
	state->activeThreshold = sshsNodeGetInt(moduleData->moduleNode, "activeThreshold");
	state->low = sshsNodeGetInt(moduleData->moduleNode, "low");
	state->median = sshsNodeGetInt(moduleData->moduleNode, "median");
	state->high = sshsNodeGetInt(moduleData->moduleNode, "high");
}

static void caerActivityIndicatorExit(caerModuleData moduleData) {
	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &caerModuleConfigDefaultListener);

	AIState state = moduleData->moduleState;

	// Ensure maps are freed.
	simple2DBufferResetLong(state->spikeCountMap);
	simple2DBufferResetInt(state->activeCountMap);
}

static void caerActivityIndicatorReset(caerModuleData moduleData, uint16_t resetCallSourceID) {
	UNUSED_ARGUMENT(resetCallSourceID);

	AIState state = moduleData->moduleState;

	// Reset maps to all zeros (startup state).
	simple2DBufferResetLong(state->spikeCountMap);
	simple2DBufferResetInt(state->activeCountMap);
}
