#include "davis_common.h"

static void createDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo);
static void sendDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo);
static void moduleShutdownNotify(void *p);
static void biasConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void chipConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void chipConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void muxConfigSend(sshsNode node, caerModuleData moduleData);
static void muxConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void dvsConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void apsConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void apsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void imuConfigSend(sshsNode node, caerModuleData moduleData);
static void imuConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void extInputConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo);
static void extInputConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void usbConfigSend(sshsNode node, caerModuleData moduleData);
static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void systemConfigSend(sshsNode node, caerModuleData moduleData);
static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void logLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void createVDACBiasSetting(sshsNode biasNode, const char *biasName, uint8_t voltageValue, uint8_t currentValue);
static uint16_t generateVDACBiasParent(sshsNode biasNode, const char *biasName);
static uint16_t generateVDACBias(sshsNode biasNode);
static void createCoarseFineBiasSetting(sshsNode biasNode, const char *biasName, uint8_t coarseValue, uint8_t fineValue,
bool enabled, const char *sex, const char *type);
static uint16_t generateCoarseFineBiasParent(sshsNode biasNode, const char *biasName);
static uint16_t generateCoarseFineBias(sshsNode biasNode);
static void createShiftedSourceBiasSetting(sshsNode biasNode, const char *biasName, uint8_t refValue, uint8_t regValue,
	const char *operatingMode, const char *voltageLevel);
static uint16_t generateShiftedSourceBiasParent(sshsNode biasNode, const char *biasName);
static uint16_t generateShiftedSourceBias(sshsNode biasNode);

static inline const char *chipIDToName(int16_t chipID, bool withEndSlash) {
	switch (chipID) {
		case 0:
			return ((withEndSlash) ? ("DAVIS240A/") : ("DAVIS240A"));
			break;

		case 1:
			return ((withEndSlash) ? ("DAVIS240B/") : ("DAVIS240B"));
			break;

		case 2:
			return ((withEndSlash) ? ("DAVIS240C/") : ("DAVIS240C"));
			break;

		case 3:
			return ((withEndSlash) ? ("DAVIS128/") : ("DAVIS128"));
			break;

		case 4:
			return ((withEndSlash) ? ("DAVIS346A/") : ("DAVIS346A"));
			break;

		case 5:
			return ((withEndSlash) ? ("DAVIS346B/") : ("DAVIS346B"));
			break;

		case 6:
			return ((withEndSlash) ? ("DAVIS640/") : ("DAVIS640"));
			break;

		case 7:
			return ((withEndSlash) ? ("DAVISHet640/") : ("DAVISHet640"));
			break;

		case 8:
			return ((withEndSlash) ? ("DAVIS208/") : ("DAVIS208"));
			break;

		case 9:
			return ((withEndSlash) ? ("DAVIS346Cbsi/") : ("DAVIS346Cbsi"));
			break;
	}

	return ((withEndSlash) ? ("Unknown/") : ("Unknown"));
}

bool caerInputDAVISInit(caerModuleData moduleData, uint16_t deviceType) {
	caerModuleLog(moduleData, CAER_LOG_DEBUG, "Initializing module ...");

	// USB port/bus/SN settings/restrictions.
	// These can be used to force connection to one specific device at startup.
	sshsNodeCreateShort(moduleData->moduleNode, "busNumber", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL);
	sshsNodeCreateShort(moduleData->moduleNode, "devAddress", 0, 0, INT16_MAX, SSHS_FLAGS_NORMAL);
	sshsNodeCreateString(moduleData->moduleNode, "serialNumber", "", 0, 8, SSHS_FLAGS_NORMAL);

	// Add auto-restart setting.
	sshsNodeCreateBool(moduleData->moduleNode, "autoRestart", true, SSHS_FLAGS_NORMAL);

	/// Start data acquisition, and correctly notify mainloop of new data and module of exceptional
	// shutdown cases (device pulled, ...).
	char *serialNumber = sshsNodeGetString(moduleData->moduleNode, "serialNumber");
	moduleData->moduleState = caerDeviceOpen(U16T(moduleData->moduleID), deviceType,
		U8T(sshsNodeGetShort(moduleData->moduleNode, "busNumber")),
		U8T(sshsNodeGetShort(moduleData->moduleNode, "devAddress")), serialNumber);
	free(serialNumber);

	if (moduleData->moduleState == NULL) {
		// Failed to open device.
		return (false);
	}

	// Initialize per-device log-level to module log-level.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL,
		atomic_load(&moduleData->moduleLogLevel));

	// Put global source information into SSHS.
	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");

	sshsNodeCreateLong(sourceInfoNode, "highestTimestamp", -1, -1, INT64_MAX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);

	sshsNodeCreateShort(sourceInfoNode, "logicVersion", devInfo.logicVersion, devInfo.logicVersion,
		devInfo.logicVersion, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateBool(sourceInfoNode, "deviceIsMaster", devInfo.deviceIsMaster,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateShort(sourceInfoNode, "chipID", devInfo.chipID, devInfo.chipID, devInfo.chipID,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);

	sshsNodeCreateShort(sourceInfoNode, "dvsSizeX", devInfo.dvsSizeX, devInfo.dvsSizeX, devInfo.dvsSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateShort(sourceInfoNode, "dvsSizeY", devInfo.dvsSizeY, devInfo.dvsSizeY, devInfo.dvsSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateBool(sourceInfoNode, "dvsHasPixelFilter", devInfo.dvsHasPixelFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateBool(sourceInfoNode, "dvsHasBackgroundActivityFilter", devInfo.dvsHasBackgroundActivityFilter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateBool(sourceInfoNode, "dvsHasTestEventGenerator", devInfo.dvsHasTestEventGenerator,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);

	sshsNodeCreateShort(sourceInfoNode, "apsSizeX", devInfo.apsSizeX, devInfo.apsSizeX, devInfo.apsSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateShort(sourceInfoNode, "apsSizeY", devInfo.apsSizeY, devInfo.apsSizeY, devInfo.apsSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateByte(sourceInfoNode, "apsColorFilter", devInfo.apsColorFilter, devInfo.apsColorFilter,
		devInfo.apsColorFilter, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateBool(sourceInfoNode, "apsHasGlobalShutter", devInfo.apsHasGlobalShutter,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateBool(sourceInfoNode, "apsHasQuadROI", devInfo.apsHasQuadROI,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateBool(sourceInfoNode, "apsHasExternalADC", devInfo.apsHasExternalADC,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateBool(sourceInfoNode, "apsHasInternalADC", devInfo.apsHasInternalADC,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);

	sshsNodeCreateBool(sourceInfoNode, "extInputHasGenerator", devInfo.extInputHasGenerator,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateBool(sourceInfoNode, "extInputHasExtraDetectors", devInfo.extInputHasExtraDetectors,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);

	// Put source information for generic visualization, to be used to display and debug filter information.
	int16_t dataSizeX = (devInfo.dvsSizeX > devInfo.apsSizeX) ? (devInfo.dvsSizeX) : (devInfo.apsSizeX);
	int16_t dataSizeY = (devInfo.dvsSizeY > devInfo.apsSizeY) ? (devInfo.dvsSizeY) : (devInfo.apsSizeY);

	sshsNodeCreateShort(sourceInfoNode, "dataSizeX", dataSizeX, dataSizeX, dataSizeX,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
	sshsNodeCreateShort(sourceInfoNode, "dataSizeY", dataSizeY, dataSizeY, dataSizeY,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);

	// Generate source string for output modules.
	size_t sourceStringLength = (size_t) snprintf(NULL, 0, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID,
		chipIDToName(devInfo.chipID, false));

	char sourceString[sourceStringLength + 1];
	snprintf(sourceString, sourceStringLength + 1, "#Source %" PRIu16 ": %s\r\n", moduleData->moduleID,
		chipIDToName(devInfo.chipID, false));
	sourceString[sourceStringLength] = '\0';

	sshsNodeCreateString(sourceInfoNode, "sourceString", sourceString, sourceStringLength, sourceStringLength,
		SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);

	// Generate sub-system string for module.
	size_t subSystemStringLength = (size_t) snprintf(NULL, 0, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
		moduleData->moduleSubSystemString, devInfo.deviceSerialNumber, devInfo.deviceUSBBusNumber,
		devInfo.deviceUSBDeviceAddress);

	char subSystemString[subSystemStringLength + 1];
	snprintf(subSystemString, subSystemStringLength + 1, "%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
		moduleData->moduleSubSystemString, devInfo.deviceSerialNumber, devInfo.deviceUSBBusNumber,
		devInfo.deviceUSBDeviceAddress);
	subSystemString[subSystemStringLength] = '\0';

	caerModuleSetSubSystemString(moduleData, subSystemString);

	// Ensure good defaults for data acquisition settings.
	// No blocking behavior due to mainloop notification, and no auto-start of
	// all producers to ensure cAER settings are respected.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, false);
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_START_PRODUCERS, false);
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_STOP_PRODUCERS, true);

	// Create default settings and send them to the device.
	createDefaultConfiguration(moduleData, &devInfo);
	sendDefaultConfiguration(moduleData, &devInfo);

	// Start data acquisition.
	bool ret = caerDeviceDataStart(moduleData->moduleState, &caerMainloopDataNotifyIncrease,
		&caerMainloopDataNotifyDecrease,
		NULL, &moduleShutdownNotify, moduleData->moduleNode);

	if (!ret) {
		// Failed to start data acquisition, close device and exit.
		caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

		return (false);
	}

	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo.chipID, true));

	// Add config listeners last, to avoid having them dangling if Init doesn't succeed.
	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");
	sshsNodeAddAttributeListener(chipNode, moduleData, &chipConfigListener);

	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");
	sshsNodeAddAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");
	sshsNodeAddAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode apsNode = sshsGetRelativeNode(deviceConfigNode, "aps/");
	sshsNodeAddAttributeListener(apsNode, moduleData, &apsConfigListener);

	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");
	sshsNodeAddAttributeListener(imuNode, moduleData, &imuConfigListener);

	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");
	sshsNodeAddAttributeListener(extNode, moduleData, &extInputConfigListener);

	sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
	sshsNodeAddAttributeListener(usbNode, moduleData, &usbConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeAddAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	size_t biasNodesLength = 0;
	sshsNode *biasNodes = sshsNodeGetChildren(biasNode, &biasNodesLength);

	if (biasNodes != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Add listener for this particular bias.
			sshsNodeAddAttributeListener(biasNodes[i], moduleData, &biasConfigListener);
		}

		free(biasNodes);
	}

	sshsNodeAddAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	return (true);
}

void caerInputDAVISExit(caerModuleData moduleData) {
	// Device related configuration has its own sub-node.
	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo.chipID, true));

	// Remove listener, which can reference invalid memory in userData.
	sshsNodeRemoveAttributeListener(moduleData->moduleNode, moduleData, &logLevelListener);

	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");
	sshsNodeRemoveAttributeListener(chipNode, moduleData, &chipConfigListener);

	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");
	sshsNodeRemoveAttributeListener(muxNode, moduleData, &muxConfigListener);

	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");
	sshsNodeRemoveAttributeListener(dvsNode, moduleData, &dvsConfigListener);

	sshsNode apsNode = sshsGetRelativeNode(deviceConfigNode, "aps/");
	sshsNodeRemoveAttributeListener(apsNode, moduleData, &apsConfigListener);

	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");
	sshsNodeRemoveAttributeListener(imuNode, moduleData, &imuConfigListener);

	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");
	sshsNodeRemoveAttributeListener(extNode, moduleData, &extInputConfigListener);

	sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
	sshsNodeRemoveAttributeListener(usbNode, moduleData, &usbConfigListener);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");
	sshsNodeRemoveAttributeListener(sysNode, moduleData, &systemConfigListener);

	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	size_t biasNodesLength = 0;
	sshsNode *biasNodes = sshsNodeGetChildren(biasNode, &biasNodesLength);

	if (biasNodes != NULL) {
		for (size_t i = 0; i < biasNodesLength; i++) {
			// Remove listener for this particular bias.
			sshsNodeRemoveAttributeListener(biasNodes[i], moduleData, &biasConfigListener);
		}

		free(biasNodes);
	}

	caerDeviceDataStop(moduleData->moduleState);

	caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

	if (sshsNodeGetBool(moduleData->moduleNode, "autoRestart")) {
		// Prime input module again so that it will try to restart if new devices detected.
		sshsNodePutBool(moduleData->moduleNode, "running", true);
	}
}

void caerInputDAVISRun(caerModuleData moduleData, caerEventPacketContainer in, caerEventPacketContainer *out) {
	UNUSED_ARGUMENT(in);

	*out = caerDeviceDataGet(moduleData->moduleState);

	if (*out != NULL) {
		sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
		sshsNodeCreateLong(sourceInfoNode, "highestTimestamp", caerEventPacketContainerGetHighestEventTimestamp(*out),
			-1, INT64_MAX, SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);

		// Detect timestamp reset and call all reset functions for processors and outputs.
		caerEventPacketHeader special = caerEventPacketContainerGetEventPacket(*out, SPECIAL_EVENT);

		if ((special != NULL) && (caerEventPacketHeaderGetEventNumber(special) == 1)
			&& (caerSpecialEventPacketFindEventByType((caerSpecialEventPacket) special, TIMESTAMP_RESET) != NULL)) {
			caerMainloopResetProcessors(moduleData->moduleID);
			caerMainloopResetOutputs(moduleData->moduleID);

			// Update master/slave information.
			struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);
			sshsNodeCreateBool(sourceInfoNode, "deviceIsMaster", devInfo.deviceIsMaster,
				SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE);
		}
	}
}

static void createDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// First, always create all needed setting nodes, set their default values
	// and add their listeners.

	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo->chipID, true));

	// Chip biases, based on testing defaults.
	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	if (IS_DAVIS240(devInfo->chipID)) {
		createCoarseFineBiasSetting(biasNode, "DiffBn", 4, 39, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OnBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OffBn", 4, 0, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "ApsCasEpc", 5, 185, true, "N", "Cascode");
		createCoarseFineBiasSetting(biasNode, "DiffCasBnc", 5, 115, true, "N", "Cascode");
		createCoarseFineBiasSetting(biasNode, "ApsROSFBn", 6, 219, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "LocalBufBn", 5, 164, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PixInvBn", 5, 129, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrBp", 2, 58, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrSFBp", 1, 16, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "RefrBp", 4, 25, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPdBn", 6, 91, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "LcolTimeoutBn", 5, 49, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuXBp", 4, 80, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuYBp", 7, 152, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFThrBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFRefrBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PadFollBn", 7, 215, true, "N", "Normal"); // TODO: check if needed.
		createCoarseFineBiasSetting(biasNode, "ApsOverflowLevelBn", 6, 253, true, "N", "Normal");

		createCoarseFineBiasSetting(biasNode, "BiasBuffer", 6, 255, true, "N", "Normal");

		createShiftedSourceBiasSetting(biasNode, "SSP", 1, 33, "ShiftedSource", "SplitGate");
		createShiftedSourceBiasSetting(biasNode, "SSN", 1, 33, "ShiftedSource", "SplitGate");
	}

	if (IS_DAVIS128(devInfo->chipID) || IS_DAVIS208(devInfo->chipID) || IS_DAVIS346(devInfo->chipID)
	|| IS_DAVIS640(devInfo->chipID)) {
		// This is first so that it takes precedence over later settings for all other chips.
		if (IS_DAVIS640(devInfo->chipID)) {
			// Slow down pixels for big 640x480 array, to avoid overwhelming the AER bus.
			createCoarseFineBiasSetting(biasNode, "PrBp", 2, 3, true, "P", "Normal");
			createCoarseFineBiasSetting(biasNode, "PrSFBp", 1, 1, true, "P", "Normal");
			createCoarseFineBiasSetting(biasNode, "OnBn", 4, 150, true, "N", "Normal");
			createCoarseFineBiasSetting(biasNode, "OffBn", 1, 4, true, "N", "Normal");
		}

		createVDACBiasSetting(biasNode, "ApsOverflowLevel", 27, 6);
		createVDACBiasSetting(biasNode, "ApsCas", 21, 6);
		createVDACBiasSetting(biasNode, "AdcRefHigh", 30, 7);
		createVDACBiasSetting(biasNode, "AdcRefLow", 1, 7);

		if (IS_DAVIS346(devInfo->chipID) || IS_DAVIS640(devInfo->chipID)) {
			// Only DAVIS346 and 640 have ADC testing.
			createVDACBiasSetting(biasNode, "AdcTestVoltage", 21, 7);
		}

		if (IS_DAVIS208(devInfo->chipID)) {
			createVDACBiasSetting(biasNode, "ResetHighPass", 63, 7);
			createVDACBiasSetting(biasNode, "RefSS", 11, 5);

			createCoarseFineBiasSetting(biasNode, "RegBiasBp", 5, 20, true, "P", "Normal");
			createCoarseFineBiasSetting(biasNode, "RefSSBn", 5, 20, true, "N", "Normal");
		}

		createCoarseFineBiasSetting(biasNode, "LocalBufBn", 5, 164, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PadFollBn", 7, 215, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "DiffBn", 4, 39, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OnBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OffBn", 4, 1, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrBp", 2, 58, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrSFBp", 1, 16, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "PixInvBn", 5, 129, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "RefrBp", 4, 25, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ReadoutBufBp", 6, 20, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ApsROSFBn", 6, 219, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AdcCompBp", 5, 20, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ColSelLowBn", 0, 1, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "DACBufBp", 6, 60, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "LcolTimeoutBn", 5, 49, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPdBn", 6, 91, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuXBp", 4, 80, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuYBp", 7, 152, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFRefrBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFThrBn", 5, 255, true, "N", "Normal");

		createCoarseFineBiasSetting(biasNode, "BiasBuffer", 7, 255, true, "N", "Normal");

		createShiftedSourceBiasSetting(biasNode, "SSP", 1, 33, "ShiftedSource", "SplitGate");
		createShiftedSourceBiasSetting(biasNode, "SSN", 1, 33, "ShiftedSource", "SplitGate");
	}

	if (IS_DAVISRGB(devInfo->chipID)) {
		createVDACBiasSetting(biasNode, "ApsCas", 21, 4);
		createVDACBiasSetting(biasNode, "OVG1Lo", 63, 4);
		createVDACBiasSetting(biasNode, "OVG2Lo", 0, 0);
		createVDACBiasSetting(biasNode, "TX2OVG2Hi", 63, 0);
		createVDACBiasSetting(biasNode, "Gnd07", 13, 4);
		createVDACBiasSetting(biasNode, "AdcTestVoltage", 21, 0);
		createVDACBiasSetting(biasNode, "AdcRefHigh", 46, 7);
		createVDACBiasSetting(biasNode, "AdcRefLow", 3, 7);

		createCoarseFineBiasSetting(biasNode, "IFRefrBn", 5, 255, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "IFThrBn", 5, 255, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "LocalBufBn", 5, 164, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PadFollBn", 7, 209, false, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PixInvBn", 4, 164, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "DiffBn", 3, 75, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OnBn", 6, 95, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "OffBn", 2, 41, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrBp", 1, 88, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "PrSFBp", 1, 173, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "RefrBp", 2, 62, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ArrayBiasBufferBn", 6, 128, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "ArrayLogicBufferBn", 5, 255, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "FalltimeBn", 7, 41, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "RisetimeBp", 6, 162, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ReadoutBufBp", 6, 20, false, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "ApsROSFBn", 7, 82, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AdcCompBp", 4, 159, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "DACBufBp", 6, 194, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "LcolTimeoutBn", 5, 49, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPdBn", 6, 91, true, "N", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuXBp", 4, 80, true, "P", "Normal");
		createCoarseFineBiasSetting(biasNode, "AEPuYBp", 7, 152, true, "P", "Normal");

		createCoarseFineBiasSetting(biasNode, "BiasBuffer", 6, 251, true, "N", "Normal");

		createShiftedSourceBiasSetting(biasNode, "SSP", 1, 33, "TiedToRail", "SplitGate");
		createShiftedSourceBiasSetting(biasNode, "SSN", 2, 33, "ShiftedSource", "SplitGate");
	}

	// Chip configuration shift register.
	sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");

	sshsNodeCreateByte(chipNode, "DigitalMux0", 0, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(chipNode, "DigitalMux1", 0, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(chipNode, "DigitalMux2", 0, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(chipNode, "DigitalMux3", 0, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(chipNode, "AnalogMux0", 0, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(chipNode, "AnalogMux1", 0, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(chipNode, "AnalogMux2", 0, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(chipNode, "BiasMux0", 0, 0, 15, SSHS_FLAGS_NORMAL);

	sshsNodeCreateBool(chipNode, "ResetCalibNeuron", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(chipNode, "TypeNCalibNeuron", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(chipNode, "ResetTestPixel", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(chipNode, "AERnArow", false, SSHS_FLAGS_NORMAL); // Use nArow in the AER state machine.
	sshsNodeCreateBool(chipNode, "UseAOut", false, SSHS_FLAGS_NORMAL); // Enable analog pads for aMUX output (testing).

	// No GlobalShutter flag here, it's controlled by the APS module's GS flag, and libcaer
	// ensures that both the chip SR and the APS module flags are kept in sync.

	if (IS_DAVIS240A(devInfo->chipID) || IS_DAVIS240B(devInfo->chipID)) {
		sshsNodeCreateBool(chipNode, "SpecialPixelControl", false, SSHS_FLAGS_NORMAL);
	}

	if (IS_DAVIS128(devInfo->chipID) || IS_DAVIS208(devInfo->chipID) || IS_DAVIS346(devInfo->chipID)
	|| IS_DAVIS640(devInfo->chipID)|| IS_DAVISRGB(devInfo->chipID)) {
		// Select which gray counter to use with the internal ADC: '0' means the external gray counter is used, which
		// has to be supplied off-chip. '1' means the on-chip gray counter is used instead.
		sshsNodeCreateBool(chipNode, "SelectGrayCounter", 1, SSHS_FLAGS_NORMAL);
	}

	if (IS_DAVIS346(devInfo->chipID) || IS_DAVIS640(devInfo->chipID) || IS_DAVISRGB(devInfo->chipID)) {
		// Test ADC functionality: if true, the ADC takes its input voltage not from the pixel, but from the
		// VDAC 'AdcTestVoltage'. If false, the voltage comes from the pixels.
		sshsNodeCreateBool(chipNode, "TestADC", false, SSHS_FLAGS_NORMAL);
	}

	if (IS_DAVIS208(devInfo->chipID)) {
		sshsNodeCreateBool(chipNode, "SelectPreAmpAvg", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(chipNode, "SelectBiasRefSS", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(chipNode, "SelectSense", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(chipNode, "SelectPosFb", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(chipNode, "SelectHighPass", false, SSHS_FLAGS_NORMAL);
	}

	if (IS_DAVISRGB(devInfo->chipID)) {
		sshsNodeCreateBool(chipNode, "AdjustOVG1Lo", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(chipNode, "AdjustOVG2Lo", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(chipNode, "AdjustTX2OVG2Hi", false, SSHS_FLAGS_NORMAL);
	}

	// Subsystem 0: Multiplexer
	sshsNode muxNode = sshsGetRelativeNode(deviceConfigNode, "multiplexer/");

	sshsNodeCreateBool(muxNode, "Run", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(muxNode, "TimestampRun", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(muxNode, "TimestampReset", false, SSHS_FLAGS_NOTIFY_ONLY);
	sshsNodeCreateBool(muxNode, "ForceChipBiasEnable", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(muxNode, "DropDVSOnTransferStall", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(muxNode, "DropAPSOnTransferStall", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(muxNode, "DropIMUOnTransferStall", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(muxNode, "DropExtInputOnTransferStall", true, SSHS_FLAGS_NORMAL);

	// Subsystem 1: DVS AER
	sshsNode dvsNode = sshsGetRelativeNode(deviceConfigNode, "dvs/");

	sshsNodeCreateBool(dvsNode, "Run", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(dvsNode, "AckDelayRow", 4, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(dvsNode, "AckDelayColumn", 0, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(dvsNode, "AckExtensionRow", 1, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(dvsNode, "AckExtensionColumn", 0, 0, 15, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(dvsNode, "WaitOnTransferStall", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(dvsNode, "FilterRowOnlyEvents", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(dvsNode, "ExternalAERControl", false, SSHS_FLAGS_NORMAL);

	if (devInfo->dvsHasPixelFilter) {
		sshsNodeCreateShort(dvsNode, "FilterPixel0Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel0Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel1Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel1Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel2Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel2Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel3Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel3Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel4Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel4Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel5Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel5Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel6Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel6Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel7Row", devInfo->dvsSizeY, 0, devInfo->dvsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(dvsNode, "FilterPixel7Column", devInfo->dvsSizeX, 0, devInfo->dvsSizeX, SSHS_FLAGS_NORMAL);
	}

	if (devInfo->dvsHasBackgroundActivityFilter) {
		sshsNodeCreateBool(dvsNode, "FilterBackgroundActivity", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateInt(dvsNode, "FilterBackgroundActivityDeltaTime", 20000, 0, (0x01 << 16) - 1, SSHS_FLAGS_NORMAL); // in µs
	}

	if (devInfo->dvsHasTestEventGenerator) {
		sshsNodeCreateBool(dvsNode, "TestEventGeneratorEnable", false, SSHS_FLAGS_NORMAL);
	}

	// Subsystem 2: APS ADC
	sshsNode apsNode = sshsGetRelativeNode(deviceConfigNode, "aps/");

	// Only support GS on chips that have it available.
	if (devInfo->apsHasGlobalShutter) {
		sshsNodeCreateBool(apsNode, "GlobalShutter", true, SSHS_FLAGS_NORMAL);
	}

	sshsNodeCreateBool(apsNode, "Run", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(apsNode, "ResetRead", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(apsNode, "WaitOnTransferStall", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateShort(apsNode, "StartColumn0", 0, 0, devInfo->apsSizeX, SSHS_FLAGS_NORMAL);
	sshsNodeCreateShort(apsNode, "StartRow0", 0, 0, devInfo->apsSizeY, SSHS_FLAGS_NORMAL);
	sshsNodeCreateShort(apsNode, "EndColumn0", I16T(devInfo->apsSizeX - 1), 0, devInfo->apsSizeX, SSHS_FLAGS_NORMAL);
	sshsNodeCreateShort(apsNode, "EndRow0", I16T(devInfo->apsSizeY - 1), 0, devInfo->apsSizeY, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(apsNode, "Exposure", 4000, 0, (0x01 << 20) - 1, SSHS_FLAGS_NORMAL); // in µs
	sshsNodeCreateInt(apsNode, "FrameDelay", 1000, 0, (0x01 << 20) - 1, SSHS_FLAGS_NORMAL); // in µs
	sshsNodeCreateShort(apsNode, "RowSettle", (devInfo->adcClock / 3), 0, devInfo->adcClock, SSHS_FLAGS_NORMAL); // in cycles
	sshsNodeCreateBool(apsNode, "TakeSnapShot", false, SSHS_FLAGS_NOTIFY_ONLY);
	sshsNodeCreateBool(apsNode, "AutoExposure", false, SSHS_FLAGS_NORMAL);

	// Not supported on DAVIS RGB.
	if (!IS_DAVISRGB(devInfo->chipID)) {
		sshsNodeCreateShort(apsNode, "ResetSettle", (devInfo->adcClock / 3), 0, devInfo->adcClock, SSHS_FLAGS_NORMAL); // in cycles
		sshsNodeCreateShort(apsNode, "ColumnSettle", devInfo->adcClock, 0, I16T(devInfo->adcClock * 2),
			SSHS_FLAGS_NORMAL); // in cycles
		sshsNodeCreateShort(apsNode, "NullSettle", (devInfo->adcClock / 10), 0, devInfo->adcClock, SSHS_FLAGS_NORMAL); // in cycles
	}

	if (devInfo->apsHasQuadROI) {
		sshsNodeCreateShort(apsNode, "StartColumn1", devInfo->apsSizeX, 0, devInfo->apsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "StartRow1", devInfo->apsSizeY, 0, devInfo->apsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "EndColumn1", devInfo->apsSizeX, 0, devInfo->apsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "EndRow1", devInfo->apsSizeY, 0, devInfo->apsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "StartColumn2", devInfo->apsSizeX, 0, devInfo->apsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "StartRow2", devInfo->apsSizeY, 0, devInfo->apsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "EndColumn2", devInfo->apsSizeX, 0, devInfo->apsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "EndRow2", devInfo->apsSizeY, 0, devInfo->apsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "StartColumn3", devInfo->apsSizeX, 0, devInfo->apsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "StartRow3", devInfo->apsSizeY, 0, devInfo->apsSizeY, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "EndColumn3", devInfo->apsSizeX, 0, devInfo->apsSizeX, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "EndRow3", devInfo->apsSizeY, 0, devInfo->apsSizeY, SSHS_FLAGS_NORMAL);
	}

	if (devInfo->apsHasInternalADC) {
		sshsNodeCreateBool(apsNode, "UseInternalADC", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(apsNode, "SampleEnable", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateShort(apsNode, "SampleSettle", devInfo->adcClock, 0, I16T(devInfo->adcClock * 2),
			SSHS_FLAGS_NORMAL); // in cycles
		sshsNodeCreateShort(apsNode, "RampReset", (devInfo->adcClock / 3), 0, I16T(devInfo->adcClock * 2),
			SSHS_FLAGS_NORMAL); // in cycles
		sshsNodeCreateBool(apsNode, "RampShortReset", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(apsNode, "ADCTestMode", false, SSHS_FLAGS_NORMAL);
	}

	// DAVIS RGB has additional timing counters.
	if (IS_DAVISRGB(devInfo->chipID)) {
		sshsNodeCreateShort(apsNode, "TransferTime", 1500, 0, I16T(devInfo->adcClock * 2048), SSHS_FLAGS_NORMAL); // in cycles
		sshsNodeCreateShort(apsNode, "RSFDSettleTime", 1000, 0, I16T(devInfo->adcClock * 128), SSHS_FLAGS_NORMAL); // in cycles
		sshsNodeCreateShort(apsNode, "GSPDResetTime", 1000, 0, I16T(devInfo->adcClock * 128), SSHS_FLAGS_NORMAL); // in cycles
		sshsNodeCreateShort(apsNode, "GSResetFallTime", 1000, 0, I16T(devInfo->adcClock * 128), SSHS_FLAGS_NORMAL); // in cycles
		sshsNodeCreateShort(apsNode, "GSTXFallTime", 1000, 0, I16T(devInfo->adcClock * 128), SSHS_FLAGS_NORMAL); // in cycles
		sshsNodeCreateShort(apsNode, "GSFDResetTime", 1000, 0, I16T(devInfo->adcClock * 128), SSHS_FLAGS_NORMAL); // in cycles
	}

	// Subsystem 3: IMU
	sshsNode imuNode = sshsGetRelativeNode(deviceConfigNode, "imu/");

	sshsNodeCreateBool(imuNode, "Run", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(imuNode, "TempStandby", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(imuNode, "AccelXStandby", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(imuNode, "AccelYStandby", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(imuNode, "AccelZStandby", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(imuNode, "GyroXStandby", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(imuNode, "GyroYStandby", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(imuNode, "GyroZStandby", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(imuNode, "LowPowerCycle", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(imuNode, "LowPowerWakeupFrequency", 1, 0, 3, SSHS_FLAGS_NORMAL);
	sshsNodeCreateShort(imuNode, "SampleRateDivider", 0, 0, 255, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(imuNode, "DigitalLowPassFilter", 1, 0, 7, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(imuNode, "AccelFullScale", 1, 0, 3, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(imuNode, "GyroFullScale", 1, 0, 3, SSHS_FLAGS_NORMAL);

	// Subsystem 4: External Input
	sshsNode extNode = sshsGetRelativeNode(deviceConfigNode, "externalInput/");

	sshsNodeCreateBool(extNode, "RunDetector", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(extNode, "DetectRisingEdges", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(extNode, "DetectFallingEdges", false, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(extNode, "DetectPulses", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(extNode, "DetectPulsePolarity", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(extNode, "DetectPulseLength", devInfo->logicClock, 1, ((0x01 << 20) - 1) * devInfo->logicClock,
		SSHS_FLAGS_NORMAL);

	if (devInfo->extInputHasGenerator) {
		sshsNodeCreateBool(extNode, "RunGenerator", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "GenerateUseCustomSignal", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "GeneratePulsePolarity", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateInt(extNode, "GeneratePulseInterval", devInfo->logicClock, 1,
			((0x01 << 20) - 1) * devInfo->logicClock, SSHS_FLAGS_NORMAL);
		sshsNodeCreateInt(extNode, "GeneratePulseLength", devInfo->logicClock / 2, 1,
			((0x01 << 20) - 1) * devInfo->logicClock, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "GenerateInjectOnRisingEdge", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "GenerateInjectOnFallingEdge", false, SSHS_FLAGS_NORMAL);
	}

	if (devInfo->extInputHasExtraDetectors) {
		sshsNodeCreateBool(extNode, "RunDetector1", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "DetectRisingEdges1", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "DetectFallingEdges1", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "DetectPulses1", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "DetectPulsePolarity1", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateInt(extNode, "DetectPulseLength1", devInfo->logicClock, 1,
			((0x01 << 20) - 1) * devInfo->logicClock, SSHS_FLAGS_NORMAL);

		sshsNodeCreateBool(extNode, "RunDetector2", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "DetectRisingEdges2", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "DetectFallingEdges2", false, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "DetectPulses2", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateBool(extNode, "DetectPulsePolarity2", true, SSHS_FLAGS_NORMAL);
		sshsNodeCreateInt(extNode, "DetectPulseLength2", devInfo->logicClock, 1,
			((0x01 << 20) - 1) * devInfo->logicClock, SSHS_FLAGS_NORMAL);
	}

	// Subsystem 9: FX2/3 USB Configuration and USB buffer settings.
	sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
	sshsNodeCreateBool(usbNode, "Run", true, SSHS_FLAGS_NORMAL);
	sshsNodeCreateShort(usbNode, "EarlyPacketDelay", 8, 1, 8000, SSHS_FLAGS_NORMAL); // 125µs time-slices, so 1ms

	sshsNodeCreateInt(usbNode, "BufferNumber", 8, 2, 128, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(usbNode, "BufferSize", 8192, 512, 32768, SSHS_FLAGS_NORMAL);

	sshsNode sysNode = sshsGetRelativeNode(moduleData->moduleNode, "system/");

	// Packet settings (size (in events) and time interval (in µs)).
	sshsNodeCreateInt(sysNode, "PacketContainerMaxPacketSize", 8192, 1, 10 * 1024 * 1024, SSHS_FLAGS_NORMAL);
	sshsNodeCreateInt(sysNode, "PacketContainerInterval", 10000, 1, 120 * 1000 * 1000, SSHS_FLAGS_NORMAL);

	// Ring-buffer setting (only changes value on module init/shutdown cycles).
	sshsNodeCreateInt(sysNode, "DataExchangeBufferSize", 64, 8, 1024, SSHS_FLAGS_NORMAL);
}

static void sendDefaultConfiguration(caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo->chipID, true));

	// Send cAER configuration to libcaer and device.
	biasConfigSend(sshsGetRelativeNode(deviceConfigNode, "bias/"), moduleData, devInfo);
	chipConfigSend(sshsGetRelativeNode(deviceConfigNode, "chip/"), moduleData, devInfo);
	systemConfigSend(sshsGetRelativeNode(moduleData->moduleNode, "system/"), moduleData);
	usbConfigSend(sshsGetRelativeNode(deviceConfigNode, "usb/"), moduleData);
	muxConfigSend(sshsGetRelativeNode(deviceConfigNode, "multiplexer/"), moduleData);
	dvsConfigSend(sshsGetRelativeNode(deviceConfigNode, "dvs/"), moduleData, devInfo);
	apsConfigSend(sshsGetRelativeNode(deviceConfigNode, "aps/"), moduleData, devInfo);
	imuConfigSend(sshsGetRelativeNode(deviceConfigNode, "imu/"), moduleData);
	extInputConfigSend(sshsGetRelativeNode(deviceConfigNode, "externalInput/"), moduleData, devInfo);
}

static void moduleShutdownNotify(void *p) {
	sshsNode moduleNode = p;

	// Ensure parent also shuts down (on disconnected device for example).
	sshsNodePutBool(moduleNode, "running", false);
}

static void biasConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// All chips of a kind have the same bias address for the same bias!
	if (IS_DAVIS240(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_DIFFBN,
			generateCoarseFineBiasParent(node, "DiffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_ONBN,
			generateCoarseFineBiasParent(node, "OnBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_OFFBN,
			generateCoarseFineBiasParent(node, "OffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSCASEPC,
			generateCoarseFineBiasParent(node, "ApsCasEpc"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_DIFFCASBNC,
			generateCoarseFineBiasParent(node, "DiffCasBnc"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSROSFBN,
			generateCoarseFineBiasParent(node, "ApsROSFBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_LOCALBUFBN,
			generateCoarseFineBiasParent(node, "LocalBufBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PIXINVBN,
			generateCoarseFineBiasParent(node, "PixInvBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PRBP,
			generateCoarseFineBiasParent(node, "PrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PRSFBP,
			generateCoarseFineBiasParent(node, "PrSFBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_REFRBP,
			generateCoarseFineBiasParent(node, "RefrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPDBN,
			generateCoarseFineBiasParent(node, "AEPdBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_LCOLTIMEOUTBN,
			generateCoarseFineBiasParent(node, "LcolTimeoutBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPUXBP,
			generateCoarseFineBiasParent(node, "AEPuXBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPUYBP,
			generateCoarseFineBiasParent(node, "AEPuYBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_IFTHRBN,
			generateCoarseFineBiasParent(node, "IFThrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_IFREFRBN,
			generateCoarseFineBiasParent(node, "IFRefrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PADFOLLBN,
			generateCoarseFineBiasParent(node, "PadFollBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSOVERFLOWLEVELBN,
			generateCoarseFineBiasParent(node, "ApsOverflowLevelBn"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_BIASBUFFER,
			generateCoarseFineBiasParent(node, "BiasBuffer"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_SSP,
			generateShiftedSourceBiasParent(node, "SSP"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_SSN,
			generateShiftedSourceBiasParent(node, "SSN"));
	}

	if (IS_DAVIS128(devInfo->chipID) || IS_DAVIS208(devInfo->chipID) || IS_DAVIS346(devInfo->chipID)
	|| IS_DAVIS640(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSOVERFLOWLEVEL,
			generateVDACBiasParent(node, "ApsOverflowLevel"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSCAS,
			generateVDACBiasParent(node, "ApsCas"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCREFHIGH,
			generateVDACBiasParent(node, "AdcRefHigh"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCREFLOW,
			generateVDACBiasParent(node, "AdcRefLow"));

		if (IS_DAVIS346(devInfo->chipID) || IS_DAVIS640(devInfo->chipID)) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS346_CONFIG_BIAS_ADCTESTVOLTAGE,
				generateVDACBiasParent(node, "AdcTestVoltage"));
		}

		if (IS_DAVIS208(devInfo->chipID)) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_RESETHIGHPASS,
				generateVDACBiasParent(node, "ResetHighPass"));
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REFSS,
				generateVDACBiasParent(node, "RefSS"));

			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REGBIASBP,
				generateCoarseFineBiasParent(node, "RegBiasBp"));
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REFSSBN,
				generateCoarseFineBiasParent(node, "RefSSBn"));
		}

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_LOCALBUFBN,
			generateCoarseFineBiasParent(node, "LocalBufBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PADFOLLBN,
			generateCoarseFineBiasParent(node, "PadFollBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_DIFFBN,
			generateCoarseFineBiasParent(node, "DiffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ONBN,
			generateCoarseFineBiasParent(node, "OnBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_OFFBN,
			generateCoarseFineBiasParent(node, "OffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PIXINVBN,
			generateCoarseFineBiasParent(node, "PixInvBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PRBP,
			generateCoarseFineBiasParent(node, "PrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PRSFBP,
			generateCoarseFineBiasParent(node, "PrSFBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_REFRBP,
			generateCoarseFineBiasParent(node, "RefrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_READOUTBUFBP,
			generateCoarseFineBiasParent(node, "ReadoutBufBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSROSFBN,
			generateCoarseFineBiasParent(node, "ApsROSFBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCCOMPBP,
			generateCoarseFineBiasParent(node, "AdcCompBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_COLSELLOWBN,
			generateCoarseFineBiasParent(node, "ColSelLowBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_DACBUFBP,
			generateCoarseFineBiasParent(node, "DACBufBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_LCOLTIMEOUTBN,
			generateCoarseFineBiasParent(node, "LcolTimeoutBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPDBN,
			generateCoarseFineBiasParent(node, "AEPdBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPUXBP,
			generateCoarseFineBiasParent(node, "AEPuXBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPUYBP,
			generateCoarseFineBiasParent(node, "AEPuYBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_IFREFRBN,
			generateCoarseFineBiasParent(node, "IFRefrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_IFTHRBN,
			generateCoarseFineBiasParent(node, "IFThrBn"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_BIASBUFFER,
			generateCoarseFineBiasParent(node, "BiasBuffer"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_SSP,
			generateShiftedSourceBiasParent(node, "SSP"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_SSN,
			generateShiftedSourceBiasParent(node, "SSN"));
	}

	if (IS_DAVISRGB(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_APSCAS,
			generateVDACBiasParent(node, "ApsCas"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_OVG1LO,
			generateVDACBiasParent(node, "OVG1Lo"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_OVG2LO,
			generateVDACBiasParent(node, "OVG2Lo"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_TX2OVG2HI,
			generateVDACBiasParent(node, "TX2OVG2Hi"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_GND07,
			generateVDACBiasParent(node, "Gnd07"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ADCTESTVOLTAGE,
			generateVDACBiasParent(node, "AdcTestVoltage"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ADCREFHIGH,
			generateVDACBiasParent(node, "AdcRefHigh"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ADCREFLOW,
			generateVDACBiasParent(node, "AdcRefLow"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_IFREFRBN,
			generateCoarseFineBiasParent(node, "IFRefrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_IFTHRBN,
			generateCoarseFineBiasParent(node, "IFThrBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_LOCALBUFBN,
			generateCoarseFineBiasParent(node, "LocalBufBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_PADFOLLBN,
			generateCoarseFineBiasParent(node, "PadFollBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_PIXINVBN,
			generateCoarseFineBiasParent(node, "PixInvBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_DIFFBN,
			generateCoarseFineBiasParent(node, "DiffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ONBN,
			generateCoarseFineBiasParent(node, "OnBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_OFFBN,
			generateCoarseFineBiasParent(node, "OffBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_PRBP,
			generateCoarseFineBiasParent(node, "PrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_PRSFBP,
			generateCoarseFineBiasParent(node, "PrSFBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_REFRBP,
			generateCoarseFineBiasParent(node, "RefrBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ARRAYBIASBUFFERBN,
			generateCoarseFineBiasParent(node, "ArrayBiasBufferBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ARRAYLOGICBUFFERBN,
			generateCoarseFineBiasParent(node, "ArrayLogicBufferBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_FALLTIMEBN,
			generateCoarseFineBiasParent(node, "FalltimeBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_RISETIMEBP,
			generateCoarseFineBiasParent(node, "RisetimeBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_READOUTBUFBP,
			generateCoarseFineBiasParent(node, "ReadoutBufBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_APSROSFBN,
			generateCoarseFineBiasParent(node, "ApsROSFBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ADCCOMPBP,
			generateCoarseFineBiasParent(node, "AdcCompBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_DACBUFBP,
			generateCoarseFineBiasParent(node, "DACBufBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_LCOLTIMEOUTBN,
			generateCoarseFineBiasParent(node, "LcolTimeoutBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_AEPDBN,
			generateCoarseFineBiasParent(node, "AEPdBn"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_AEPUXBP,
			generateCoarseFineBiasParent(node, "AEPuXBp"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_AEPUYBP,
			generateCoarseFineBiasParent(node, "AEPuYBp"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_BIASBUFFER,
			generateCoarseFineBiasParent(node, "BiasBuffer"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_SSP,
			generateShiftedSourceBiasParent(node, "SSP"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_SSN,
			generateShiftedSourceBiasParent(node, "SSN"));
	}
}

static void biasConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(changeKey);
	UNUSED_ARGUMENT(changeType);
	UNUSED_ARGUMENT(changeValue);

	caerModuleData moduleData = userData;
	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		const char *nodeName = sshsNodeGetName(node);

		if (IS_DAVIS240(devInfo.chipID)) {
			if (caerStrEquals(nodeName, "DiffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_DIFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OnBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_ONBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_OFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsCasEpc")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSCASEPC,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DiffCasBnc")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_DIFFCASBNC,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsROSFBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSROSFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LocalBufBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_LOCALBUFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PixInvBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PIXINVBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrSFBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PRSFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "RefrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_REFRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPdBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPDBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LcolTimeoutBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_LCOLTIMEOUTBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuXBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPUXBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuYBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_AEPUYBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFThrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_IFTHRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFRefrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_IFREFRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PadFollBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_PADFOLLBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsOverflowLevelBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_APSOVERFLOWLEVELBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "BiasBuffer")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_BIASBUFFER,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "SSP")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_SSP,
					generateShiftedSourceBias(node));
			}
			else if (caerStrEquals(nodeName, "SSP")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS240_CONFIG_BIAS_SSN,
					generateShiftedSourceBias(node));
			}
		}

		if (IS_DAVIS128(devInfo.chipID) || IS_DAVIS208(devInfo.chipID) || IS_DAVIS346(devInfo.chipID)
		|| IS_DAVIS640(devInfo.chipID)) {
			if (caerStrEquals(nodeName, "ApsOverflowLevel")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSOVERFLOWLEVEL,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsCas")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSCAS,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcRefHigh")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCREFHIGH,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcRefLow")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCREFLOW,
					generateVDACBias(node));
			}
			else if ((IS_DAVIS346(devInfo.chipID) || IS_DAVIS640(devInfo.chipID))
				&& caerStrEquals(nodeName, "AdcTestVoltage")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS346_CONFIG_BIAS_ADCTESTVOLTAGE,
					generateVDACBias(node));
			}
			else if ((IS_DAVIS208(devInfo.chipID)) && caerStrEquals(nodeName, "ResetHighPass")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_RESETHIGHPASS,
					generateVDACBias(node));
			}
			else if ((IS_DAVIS208(devInfo.chipID)) && caerStrEquals(nodeName, "RefSS")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REFSS,
					generateVDACBias(node));
			}
			else if ((IS_DAVIS208(devInfo.chipID)) && caerStrEquals(nodeName, "RegBiasBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REGBIASBP,
					generateCoarseFineBias(node));
			}
			else if ((IS_DAVIS208(devInfo.chipID)) && caerStrEquals(nodeName, "RefSSBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS208_CONFIG_BIAS_REFSSBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LocalBufBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_LOCALBUFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PadFollBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PADFOLLBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DiffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_DIFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OnBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ONBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_OFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PixInvBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PIXINVBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrSFBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_PRSFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "RefrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_REFRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ReadoutBufBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_READOUTBUFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsROSFBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_APSROSFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcCompBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_ADCCOMPBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ColSelLowBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_COLSELLOWBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DACBufBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_DACBUFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LcolTimeoutBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_LCOLTIMEOUTBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPdBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPDBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuXBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPUXBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuYBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_AEPUYBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFRefrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_IFREFRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFThrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_IFTHRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "BiasBuffer")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_BIASBUFFER,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "SSP")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_SSP,
					generateShiftedSourceBias(node));
			}
			else if (caerStrEquals(nodeName, "SSN")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVIS128_CONFIG_BIAS_SSN,
					generateShiftedSourceBias(node));
			}
		}

		if (IS_DAVISRGB(devInfo.chipID)) {
			if (caerStrEquals(nodeName, "ApsCas")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_APSCAS,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "OVG1Lo")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_OVG1LO,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "OVG2Lo")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_OVG2LO,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "TX2OVG2Hi")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_TX2OVG2HI,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "Gnd07")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_GND07,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcTestVoltage")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ADCTESTVOLTAGE,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcRefHigh")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ADCREFHIGH,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcRefLow")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ADCREFLOW,
					generateVDACBias(node));
			}
			else if (caerStrEquals(nodeName, "IFRefrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_IFREFRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "IFThrBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_IFTHRBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LocalBufBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_LOCALBUFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PadFollBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_PADFOLLBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PixInvBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_PIXINVBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DiffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_DIFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OnBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ONBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "OffBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_OFFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_PRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "PrSFBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_PRSFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "RefrBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_REFRBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ArrayBiasBufferBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ARRAYBIASBUFFERBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ArrayLogicBufferBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ARRAYLOGICBUFFERBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "FalltimeBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_FALLTIMEBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "RisetimeBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_RISETIMEBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ReadoutBufBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_READOUTBUFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "ApsROSFBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_APSROSFBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AdcCompBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_ADCCOMPBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "DACBufBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_DACBUFBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "LcolTimeoutBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_LCOLTIMEOUTBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPdBn")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_AEPDBN,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuXBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_AEPUXBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "AEPuYBp")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_AEPUYBP,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "BiasBuffer")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_BIASBUFFER,
					generateCoarseFineBias(node));
			}
			else if (caerStrEquals(nodeName, "SSP")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_SSP,
					generateShiftedSourceBias(node));
			}
			else if (caerStrEquals(nodeName, "SSN")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_BIAS, DAVISRGB_CONFIG_BIAS_SSN,
					generateShiftedSourceBias(node));
			}
		}
	}
}

static void chipConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	// All chips have the same parameter address for the same setting!
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX0,
		U32T(sshsNodeGetByte(node, "DigitalMux0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX1,
		U32T(sshsNodeGetByte(node, "DigitalMux1")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX2,
		U32T(sshsNodeGetByte(node, "DigitalMux2")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX3,
		U32T(sshsNodeGetByte(node, "DigitalMux3")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX0,
		U32T(sshsNodeGetByte(node, "AnalogMux0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX1,
		U32T(sshsNodeGetByte(node, "AnalogMux1")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX2,
		U32T(sshsNodeGetByte(node, "AnalogMux2")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_BIASMUX0,
		U32T(sshsNodeGetByte(node, "BiasMux0")));

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_RESETCALIBNEURON,
		sshsNodeGetBool(node, "ResetCalibNeuron"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_TYPENCALIBNEURON,
		sshsNodeGetBool(node, "TypeNCalibNeuron"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_RESETTESTPIXEL,
		sshsNodeGetBool(node, "ResetTestPixel"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_AERNAROW,
		sshsNodeGetBool(node, "AERnArow"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_USEAOUT,
		sshsNodeGetBool(node, "UseAOut"));

	if (IS_DAVIS240A(devInfo->chipID) || IS_DAVIS240B(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS240_CONFIG_CHIP_SPECIALPIXELCONTROL,
			sshsNodeGetBool(node, "SpecialPixelControl"));
	}

	if (IS_DAVIS128(devInfo->chipID) || IS_DAVIS208(devInfo->chipID) || IS_DAVIS346(devInfo->chipID)
	|| IS_DAVIS640(devInfo->chipID) || IS_DAVISRGB(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_SELECTGRAYCOUNTER,
			sshsNodeGetBool(node, "SelectGrayCounter"));
	}

	if (IS_DAVIS346(devInfo->chipID) || IS_DAVIS640(devInfo->chipID) || IS_DAVISRGB(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS346_CONFIG_CHIP_TESTADC,
			sshsNodeGetBool(node, "TestADC"));
	}

	if (IS_DAVIS208(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTPREAMPAVG,
			sshsNodeGetBool(node, "SelectPreAmpAvg"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTBIASREFSS,
			sshsNodeGetBool(node, "SelectBiasRefSS"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTSENSE,
			sshsNodeGetBool(node, "SelectSense"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTPOSFB,
			sshsNodeGetBool(node, "SelectPosFb"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTHIGHPASS,
			sshsNodeGetBool(node, "SelectHighPass"));
	}

	if (IS_DAVISRGB(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVISRGB_CONFIG_CHIP_ADJUSTOVG1LO,
			sshsNodeGetBool(node, "AdjustOVG1Lo"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVISRGB_CONFIG_CHIP_ADJUSTOVG2LO,
			sshsNodeGetBool(node, "AdjustOVG2Lo"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVISRGB_CONFIG_CHIP_ADJUSTTX2OVG2HI,
			sshsNodeGetBool(node, "AdjustTX2OVG2Hi"));
	}
}

static void chipConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;
	struct caer_davis_info devInfo = caerDavisInfoGet(moduleData->moduleState);

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "DigitalMux0")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX0,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "DigitalMux1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX1,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "DigitalMux2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX2,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "DigitalMux3")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_DIGITALMUX3,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "AnalogMux0")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX0,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "AnalogMux1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX1,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "AnalogMux2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_ANALOGMUX2,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "BiasMux0")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_BIASMUX0,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ResetCalibNeuron")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_RESETCALIBNEURON,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TypeNCalibNeuron")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_TYPENCALIBNEURON,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ResetTestPixel")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_RESETTESTPIXEL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AERnArow")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_AERNAROW,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "UseAOut")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_USEAOUT,
				changeValue.boolean);
		}
		else if ((IS_DAVIS240A(devInfo.chipID) || IS_DAVIS240B(devInfo.chipID)) && changeType == SSHS_BOOL
			&& caerStrEquals(changeKey, "SpecialPixelControl")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS240_CONFIG_CHIP_SPECIALPIXELCONTROL,
				changeValue.boolean);
		}
		else if ((IS_DAVIS128(devInfo.chipID) || IS_DAVIS208(devInfo.chipID) || IS_DAVIS346(devInfo.chipID)
			|| IS_DAVIS640(devInfo.chipID) || IS_DAVISRGB(devInfo.chipID)) && changeType == SSHS_BOOL
			&& caerStrEquals(changeKey, "SelectGrayCounter")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS128_CONFIG_CHIP_SELECTGRAYCOUNTER,
				changeValue.boolean);
		}
		else if ((IS_DAVIS346(devInfo.chipID) || IS_DAVIS640(devInfo.chipID) || IS_DAVISRGB(devInfo.chipID))
			&& changeType == SSHS_BOOL && caerStrEquals(changeKey, "TestADC")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS346_CONFIG_CHIP_TESTADC,
				changeValue.boolean);
		}

		if (IS_DAVIS208(devInfo.chipID)) {
			if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectPreAmpAvg")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTPREAMPAVG,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectBiasRefSS")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTBIASREFSS,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectSense")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTSENSE,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectPosFb")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTPOSFB,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SelectHighPass")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVIS208_CONFIG_CHIP_SELECTHIGHPASS,
					changeValue.boolean);
			}
		}

		if (IS_DAVISRGB(devInfo.chipID)) {
			if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AdjustOVG1Lo")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVISRGB_CONFIG_CHIP_ADJUSTOVG1LO,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AdjustOVG2Lo")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVISRGB_CONFIG_CHIP_ADJUSTOVG2LO,
					changeValue.boolean);
			}
			else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AdjustTX2OVG2Hi")) {
				caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_CHIP, DAVISRGB_CONFIG_CHIP_ADJUSTTX2OVG2HI,
					changeValue.boolean);
			}
		}
	}
}

static void muxConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RESET,
		sshsNodeGetBool(node, "TimestampReset"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE,
		sshsNodeGetBool(node, "ForceChipBiasEnable"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_DVS_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "DropDVSOnTransferStall"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_APS_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "DropAPSOnTransferStall"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_IMU_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "DropIMUOnTransferStall"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_EXTINPUT_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "DropExtInputOnTransferStall"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RUN,
		sshsNodeGetBool(node, "TimestampRun"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN, sshsNodeGetBool(node, "Run"));
}

static void muxConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampReset")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RESET,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ForceChipBiasEnable")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DropDVSOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_DVS_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DropAPSOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_APS_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DropIMUOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_DROP_IMU_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DropExtInputOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX,
			DAVIS_CONFIG_MUX_DROP_EXTINPUT_ON_TRANSFER_STALL, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TimestampRun")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_TIMESTAMP_RUN,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_MUX, DAVIS_CONFIG_MUX_RUN, changeValue.boolean);
		}
	}
}

static void dvsConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_ACK_DELAY_ROW,
		U32T(sshsNodeGetByte(node, "AckDelayRow")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_ACK_DELAY_COLUMN,
		U32T(sshsNodeGetByte(node, "AckDelayColumn")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_ACK_EXTENSION_ROW,
		U32T(sshsNodeGetByte(node, "AckExtensionRow")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_ACK_EXTENSION_COLUMN,
		U32T(sshsNodeGetByte(node, "AckExtensionColumn")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_WAIT_ON_TRANSFER_STALL,
		U32T(sshsNodeGetBool(node, "WaitOnTransferStall")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROW_ONLY_EVENTS,
		U32T(sshsNodeGetBool(node, "FilterRowOnlyEvents")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_EXTERNAL_AER_CONTROL,
		U32T(sshsNodeGetBool(node, "ExternalAERControl")));

	if (devInfo->dvsHasPixelFilter) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_0_ROW,
			U32T(sshsNodeGetShort(node, "FilterPixel0Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_0_COLUMN,
			U32T(sshsNodeGetShort(node, "FilterPixel0Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_1_ROW,
			U32T(sshsNodeGetShort(node, "FilterPixel1Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_1_COLUMN,
			U32T(sshsNodeGetShort(node, "FilterPixel1Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_2_ROW,
			U32T(sshsNodeGetShort(node, "FilterPixel2Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_2_COLUMN,
			U32T(sshsNodeGetShort(node, "FilterPixel2Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_3_ROW,
			U32T(sshsNodeGetShort(node, "FilterPixel3Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_3_COLUMN,
			U32T(sshsNodeGetShort(node, "FilterPixel3Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_4_ROW,
			U32T(sshsNodeGetShort(node, "FilterPixel4Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_4_COLUMN,
			U32T(sshsNodeGetShort(node, "FilterPixel4Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_5_ROW,
			U32T(sshsNodeGetShort(node, "FilterPixel5Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_5_COLUMN,
			U32T(sshsNodeGetShort(node, "FilterPixel5Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_6_ROW,
			U32T(sshsNodeGetShort(node, "FilterPixel6Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_6_COLUMN,
			U32T(sshsNodeGetShort(node, "FilterPixel6Column")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_7_ROW,
			U32T(sshsNodeGetShort(node, "FilterPixel7Row")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_7_COLUMN,
			U32T(sshsNodeGetShort(node, "FilterPixel7Column")));
	}

	if (devInfo->dvsHasBackgroundActivityFilter) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY,
			sshsNodeGetBool(node, "FilterBackgroundActivity"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS,
		DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY_DELTAT,
			U32T(sshsNodeGetInt(node, "FilterBackgroundActivityDeltaTime")));
	}

	if (devInfo->dvsHasTestEventGenerator) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_TEST_EVENT_GENERATOR_ENABLE,
			sshsNodeGetBool(node, "TestEventGeneratorEnable"));
	}

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_RUN, sshsNodeGetBool(node, "Run"));
}

static void dvsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "AckDelayRow")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_ACK_DELAY_ROW,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "AckDelayColumn")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_ACK_DELAY_COLUMN,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "AckExtensionRow")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_ACK_EXTENSION_ROW,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "AckExtensionColumn")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_ACK_EXTENSION_COLUMN,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "WaitOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_WAIT_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterRowOnlyEvents")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_ROW_ONLY_EVENTS,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ExternalAERControl")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_EXTERNAL_AER_CONTROL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel0Row")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_0_ROW,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel0Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_0_COLUMN,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel1Row")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_1_ROW,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel1Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_1_COLUMN,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel2Row")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_2_ROW,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel2Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_2_COLUMN,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel3Row")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_3_ROW,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel3Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_3_COLUMN,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel4Row")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_4_ROW,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel4Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_4_COLUMN,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel5Row")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_5_ROW,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel5Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_5_COLUMN,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel6Row")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_6_ROW,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel6Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_6_COLUMN,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel7Row")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_7_ROW,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "FilterPixel7Column")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_PIXEL_7_COLUMN,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "FilterBackgroundActivity")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY,
				changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FilterBackgroundActivityDeltaTime")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS,
			DAVIS_CONFIG_DVS_FILTER_BACKGROUND_ACTIVITY_DELTAT, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TestEventGeneratorEnable")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_TEST_EVENT_GENERATOR_ENABLE,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_DVS, DAVIS_CONFIG_DVS_RUN, changeValue.boolean);
		}
	}
}

static void apsConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	if (devInfo->apsHasGlobalShutter) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_GLOBAL_SHUTTER,
			sshsNodeGetBool(node, "GlobalShutter"));
	}

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RESET_READ,
		sshsNodeGetBool(node, "ResetRead"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_WAIT_ON_TRANSFER_STALL,
		sshsNodeGetBool(node, "WaitOnTransferStall"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_0,
		U32T(sshsNodeGetShort(node, "StartColumn0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_0,
		U32T(sshsNodeGetShort(node, "StartRow0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_0,
		U32T(sshsNodeGetShort(node, "EndColumn0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_0,
		U32T(sshsNodeGetShort(node, "EndRow0")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_EXPOSURE,
		U32T(sshsNodeGetInt(node, "Exposure")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_FRAME_DELAY,
		U32T(sshsNodeGetInt(node, "FrameDelay")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_ROW_SETTLE,
		U32T(sshsNodeGetShort(node, "RowSettle")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_AUTOEXPOSURE,
		sshsNodeGetBool(node, "AutoExposure"));

	// Not supported on DAVIS RGB.
	if (!IS_DAVISRGB(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RESET_SETTLE,
			U32T(sshsNodeGetShort(node, "ResetSettle")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_COLUMN_SETTLE,
			U32T(sshsNodeGetShort(node, "ColumnSettle")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_NULL_SETTLE,
			U32T(sshsNodeGetShort(node, "NullSettle")));
	}

	if (devInfo->apsHasQuadROI) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_1,
			U32T(sshsNodeGetShort(node, "StartColumn1")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_1,
			U32T(sshsNodeGetShort(node, "StartRow1")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_1,
			U32T(sshsNodeGetShort(node, "EndColumn1")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_1,
			U32T(sshsNodeGetShort(node, "EndRow1")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_2,
			U32T(sshsNodeGetShort(node, "StartColumn2")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_2,
			U32T(sshsNodeGetShort(node, "StartRow2")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_2,
			U32T(sshsNodeGetShort(node, "EndColumn2")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_2,
			U32T(sshsNodeGetShort(node, "EndRow2")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_3,
			U32T(sshsNodeGetShort(node, "StartColumn3")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_3,
			U32T(sshsNodeGetShort(node, "StartRow3")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_3,
			U32T(sshsNodeGetShort(node, "EndColumn3")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_3,
			U32T(sshsNodeGetShort(node, "EndRow3")));
	}

	if (devInfo->apsHasInternalADC) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_USE_INTERNAL_ADC,
			sshsNodeGetBool(node, "UseInternalADC"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_SAMPLE_ENABLE,
			sshsNodeGetBool(node, "SampleEnable"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_SAMPLE_SETTLE,
			U32T(sshsNodeGetShort(node, "SampleSettle")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RAMP_RESET,
			U32T(sshsNodeGetShort(node, "RampReset")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RAMP_SHORT_RESET,
			sshsNodeGetBool(node, "RampShortReset"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_ADC_TEST_MODE,
			sshsNodeGetBool(node, "ADCTestMode"));
	}

	// DAVIS RGB extra timing support.
	if (IS_DAVISRGB(devInfo->chipID)) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_TRANSFER,
			U32T(sshsNodeGetShort(node, "TransferTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_RSFDSETTLE,
			U32T(sshsNodeGetShort(node, "RSFDSettleTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_GSPDRESET,
			U32T(sshsNodeGetShort(node, "GSPDResetTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_GSRESETFALL,
			U32T(sshsNodeGetShort(node, "GSResetFallTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_GSTXFALL,
			U32T(sshsNodeGetShort(node, "GSTXFallTime")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_GSFDRESET,
			U32T(sshsNodeGetShort(node, "GSFDResetTime")));
	}

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RUN, sshsNodeGetBool(node, "Run"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_SNAPSHOT,
		sshsNodeGetBool(node, "TakeSnapShot"));
}

static void apsConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GlobalShutter")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_GLOBAL_SHUTTER,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ResetRead")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RESET_READ,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "WaitOnTransferStall")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_WAIT_ON_TRANSFER_STALL,
				changeValue.boolean);
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "StartColumn0")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_0,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "StartRow0")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_0,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EndColumn0")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_0,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EndRow0")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_0,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "Exposure")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_EXPOSURE,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "FrameDelay")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_FRAME_DELAY,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "ResetSettle")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RESET_SETTLE,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "ColumnSettle")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_COLUMN_SETTLE,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "RowSettle")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_ROW_SETTLE,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "NullSettle")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_NULL_SETTLE,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "StartColumn1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_1,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "StartRow1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_1,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EndColumn1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_1,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EndRow1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_1,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "StartColumn2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_2,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "StartRow2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_2,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EndColumn2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_2,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EndRow2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_2,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "StartColumn3")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_COLUMN_3,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "StartRow3")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_START_ROW_3,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EndColumn3")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_COLUMN_3,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EndRow3")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_END_ROW_3,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "UseInternalADC")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_USE_INTERNAL_ADC,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "SampleEnable")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_SAMPLE_ENABLE,
				changeValue.boolean);
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "SampleSettle")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_SAMPLE_SETTLE,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "RampReset")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RAMP_RESET,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RampShortReset")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RAMP_SHORT_RESET,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "ADCTestMode")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_ADC_TEST_MODE,
				changeValue.boolean);
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "TransferTime")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_TRANSFER,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "RSFDSettleTime")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_RSFDSETTLE,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "GSPDResetTime")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_GSPDRESET,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "GSResetFallTime")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_GSRESETFALL,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "GSTXFallTime")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_GSTXFALL,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "GSFDResetTime")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVISRGB_CONFIG_APS_GSFDRESET,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_RUN, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TakeSnapShot")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_SNAPSHOT,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "AutoExposure")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_APS, DAVIS_CONFIG_APS_AUTOEXPOSURE,
				changeValue.boolean);
		}
	}
}

static void imuConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_TEMP_STANDBY,
		sshsNodeGetBool(node, "TempStandby"));

	uint8_t accelStandby = 0;
	accelStandby |= U8T(sshsNodeGetBool(node, "AccelXStandby") << 2);
	accelStandby |= U8T(sshsNodeGetBool(node, "AccelYStandby") << 1);
	accelStandby |= U8T(sshsNodeGetBool(node, "AccelZStandby") << 0);
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_ACCEL_STANDBY, accelStandby);

	uint8_t gyroStandby = 0;
	gyroStandby |= U8T(sshsNodeGetBool(node, "GyroXStandby") << 2);
	gyroStandby |= U8T(sshsNodeGetBool(node, "GyroYStandby") << 1);
	gyroStandby |= U8T(sshsNodeGetBool(node, "GyroZStandby") << 0);
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_GYRO_STANDBY, gyroStandby);

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_LP_CYCLE,
		sshsNodeGetBool(node, "LowPowerCycle"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_LP_WAKEUP,
		U32T(sshsNodeGetByte(node, "LowPowerWakeupFrequency")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_SAMPLE_RATE_DIVIDER,
		U32T(sshsNodeGetShort(node, "SampleRateDivider")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_DIGITAL_LOW_PASS_FILTER,
		U32T(sshsNodeGetByte(node, "DigitalLowPassFilter")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_ACCEL_FULL_SCALE,
		U32T(sshsNodeGetByte(node, "AccelFullScale")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_GYRO_FULL_SCALE,
		U32T(sshsNodeGetByte(node, "GyroFullScale")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN, sshsNodeGetBool(node, "Run"));
}

static void imuConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "TempStandby")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_TEMP_STANDBY,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL
			&& (caerStrEquals(changeKey, "AccelXStandby") || caerStrEquals(changeKey, "AccelYStandby")
				|| caerStrEquals(changeKey, "AccelZStandby"))) {
			uint8_t accelStandby = 0;
			accelStandby |= U8T(sshsNodeGetBool(node, "AccelXStandby") << 2);
			accelStandby |= U8T(sshsNodeGetBool(node, "AccelYStandby") << 1);
			accelStandby |= U8T(sshsNodeGetBool(node, "AccelZStandby") << 0);

			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_ACCEL_STANDBY,
				accelStandby);
		}
		else if (changeType == SSHS_BOOL
			&& (caerStrEquals(changeKey, "GyroXStandby") || caerStrEquals(changeKey, "GyroYStandby")
				|| caerStrEquals(changeKey, "GyroZStandby"))) {
			uint8_t gyroStandby = 0;
			gyroStandby |= U8T(sshsNodeGetBool(node, "GyroXStandby") << 2);
			gyroStandby |= U8T(sshsNodeGetBool(node, "GyroYStandby") << 1);
			gyroStandby |= U8T(sshsNodeGetBool(node, "GyroZStandby") << 0);

			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_GYRO_STANDBY, gyroStandby);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "LowPowerCycle")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_LP_CYCLE,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "LowPowerWakeupFrequency")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_LP_WAKEUP,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "SampleRateDivider")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_SAMPLE_RATE_DIVIDER,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "DigitalLowPassFilter")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_DIGITAL_LOW_PASS_FILTER,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "AccelFullScale")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_ACCEL_FULL_SCALE,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "GyroFullScale")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_GYRO_FULL_SCALE,
				U32T(changeValue.ibyte));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_IMU, DAVIS_CONFIG_IMU_RUN, changeValue.boolean);
		}
	}
}

static void extInputConfigSend(sshsNode node, caerModuleData moduleData, struct caer_davis_info *devInfo) {
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_RISING_EDGES,
		sshsNodeGetBool(node, "DetectRisingEdges"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_FALLING_EDGES,
		sshsNodeGetBool(node, "DetectFallingEdges"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSES,
		sshsNodeGetBool(node, "DetectPulses"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY,
		sshsNodeGetBool(node, "DetectPulsePolarity"));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH,
		U32T(sshsNodeGetInt(node, "DetectPulseLength")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR,
		sshsNodeGetBool(node, "RunDetector"));

	if (devInfo->extInputHasGenerator) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
		DAVIS_CONFIG_EXTINPUT_GENERATE_USE_CUSTOM_SIGNAL, sshsNodeGetBool(node, "GenerateUseCustomSignal"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
		DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_POLARITY, sshsNodeGetBool(node, "GeneratePulsePolarity"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
		DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_INTERVAL, U32T(sshsNodeGetInt(node, "GeneratePulseInterval")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_LENGTH,
			U32T(sshsNodeGetInt(node, "GeneratePulseLength")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
		DAVIS_CONFIG_EXTINPUT_GENERATE_INJECT_ON_RISING_EDGE, sshsNodeGetBool(node, "GenerateInjectOnRisingEdge"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
		DAVIS_CONFIG_EXTINPUT_GENERATE_INJECT_ON_FALLING_EDGE, sshsNodeGetBool(node, "GenerateInjectOnFallingEdge"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_GENERATOR,
			sshsNodeGetBool(node, "RunGenerator"));
	}

	if (devInfo->extInputHasExtraDetectors) {
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_RISING_EDGES1,
			sshsNodeGetBool(node, "DetectRisingEdges1"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_FALLING_EDGES1,
			sshsNodeGetBool(node, "DetectFallingEdges1"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSES1,
			sshsNodeGetBool(node, "DetectPulses1"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
		DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY1, sshsNodeGetBool(node, "DetectPulsePolarity1"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH1,
			U32T(sshsNodeGetInt(node, "DetectPulseLength1")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR1,
			sshsNodeGetBool(node, "RunDetector1"));

		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_RISING_EDGES2,
			sshsNodeGetBool(node, "DetectRisingEdges2"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_FALLING_EDGES2,
			sshsNodeGetBool(node, "DetectFallingEdges2"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSES2,
			sshsNodeGetBool(node, "DetectPulses2"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
		DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY2, sshsNodeGetBool(node, "DetectPulsePolarity2"));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH2,
			U32T(sshsNodeGetInt(node, "DetectPulseLength2")));
		caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR2,
			sshsNodeGetBool(node, "RunDetector2"));
	}
}

static void extInputConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectRisingEdges")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_RISING_EDGES, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectFallingEdges")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_FALLING_EDGES, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulses")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSES,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulsePolarity")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DetectPulseLength")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunDetector")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GenerateUseCustomSignal")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_USE_CUSTOM_SIGNAL, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GeneratePulsePolarity")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_POLARITY, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GeneratePulseInterval")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_INTERVAL, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "GeneratePulseLength")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_PULSE_LENGTH, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GenerateInjectOnRisingEdge")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_INJECT_ON_RISING_EDGE, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "GenerateInjectOnFallingEdge")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_GENERATE_INJECT_ON_FALLING_EDGE, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunGenerator")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_GENERATOR,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectRisingEdges1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_RISING_EDGES1, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectFallingEdges1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_FALLING_EDGES1, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulses1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSES1,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulsePolarity1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY1, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DetectPulseLength1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH1, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunDetector1")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR1,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectRisingEdge2s")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_RISING_EDGES2, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectFallingEdges2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_FALLING_EDGES2, changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulses2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_DETECT_PULSES2,
				changeValue.boolean);
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "DetectPulsePolarity2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_POLARITY2, changeValue.boolean);
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "DetectPulseLength2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT,
			DAVIS_CONFIG_EXTINPUT_DETECT_PULSE_LENGTH2, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "RunDetector2")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_EXTINPUT, DAVIS_CONFIG_EXTINPUT_RUN_DETECTOR2,
				changeValue.boolean);
		}
	}
}

static void usbConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER,
		U32T(sshsNodeGetInt(node, "BufferNumber")));
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE,
		U32T(sshsNodeGetInt(node, "BufferSize")));

	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_USB, DAVIS_CONFIG_USB_EARLY_PACKET_DELAY,
		U32T(sshsNodeGetShort(node, "EarlyPacketDelay")));
	caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_USB, DAVIS_CONFIG_USB_RUN, sshsNodeGetBool(node, "Run"));
}

static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferNumber")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "BufferSize")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE,
				U32T(changeValue.iint));
		}
		else if (changeType == SSHS_SHORT && caerStrEquals(changeKey, "EarlyPacketDelay")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_USB, DAVIS_CONFIG_USB_EARLY_PACKET_DELAY,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState, DAVIS_CONFIG_USB, DAVIS_CONFIG_USB_RUN, changeValue.boolean);
		}
	}
}

static void systemConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
	CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_PACKET_SIZE, U32T(sshsNodeGetInt(node, "PacketContainerMaxPacketSize")));
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
	CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_INTERVAL, U32T(sshsNodeGetInt(node, "PacketContainerInterval")));

	// Changes only take effect on module start!
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
	CAER_HOST_CONFIG_DATAEXCHANGE_BUFFER_SIZE, U32T(sshsNodeGetInt(node, "DataExchangeBufferSize")));
}

static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerMaxPacketSize")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
			CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_PACKET_SIZE, U32T(changeValue.iint));
		}
		else if (changeType == SSHS_INT && caerStrEquals(changeKey, "PacketContainerInterval")) {
			caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_PACKETS,
			CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_INTERVAL, U32T(changeValue.iint));
		}
	}
}

static void logLevelListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BYTE && caerStrEquals(changeKey, "logLevel")) {
		caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_LOG, CAER_HOST_CONFIG_LOG_LEVEL,
			U32T(changeValue.ibyte));
	}
}

static void createVDACBiasSetting(sshsNode biasNode, const char *biasName, uint8_t voltageValue, uint8_t currentValue) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	// Add bias settings.
	sshsNodeCreateByte(biasConfigNode, "voltageValue", I8T(voltageValue), 0, 63, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(biasConfigNode, "currentValue", I8T(currentValue), 0, 7, SSHS_FLAGS_NORMAL);
}

static uint16_t generateVDACBiasParent(sshsNode biasNode, const char *biasName) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Get bias configuration node.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	return (generateVDACBias(biasConfigNode));
}

static uint16_t generateVDACBias(sshsNode biasNode) {
	// Build up bias value from all its components.
	struct caer_bias_vdac biasValue = { .voltageValue = U8T(sshsNodeGetByte(biasNode, "voltageValue")), .currentValue =
		U8T(sshsNodeGetByte(biasNode, "currentValue")), };

	return (caerBiasVDACGenerate(biasValue));
}

static void createCoarseFineBiasSetting(sshsNode biasNode, const char *biasName, uint8_t coarseValue, uint8_t fineValue,
bool enabled, const char *sex, const char *type) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	// Add bias settings.
	sshsNodeCreateByte(biasConfigNode, "coarseValue", I8T(coarseValue), 0, 7, SSHS_FLAGS_NORMAL);
	sshsNodeCreateShort(biasConfigNode, "fineValue", I16T(fineValue), 0, 255, SSHS_FLAGS_NORMAL);
	sshsNodeCreateBool(biasConfigNode, "enabled", enabled, SSHS_FLAGS_NORMAL);
	sshsNodeCreateString(biasConfigNode, "sex", sex, 1, 1, SSHS_FLAGS_NORMAL);
	sshsNodeCreateString(biasConfigNode, "type", type, 6, 7, SSHS_FLAGS_NORMAL);
	sshsNodeCreateString(biasConfigNode, "currentLevel", "Normal", 3, 6, SSHS_FLAGS_NORMAL);
}

static uint16_t generateCoarseFineBiasParent(sshsNode biasNode, const char *biasName) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Get bias configuration node.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	return (generateCoarseFineBias(biasConfigNode));
}

static uint16_t generateCoarseFineBias(sshsNode biasNode) {
	// Build up bias value from all its components.
	char *sexString = sshsNodeGetString(biasNode, "sex");
	char *typeString = sshsNodeGetString(biasNode, "type");
	char *currentLevelString = sshsNodeGetString(biasNode, "currentLevel");

	struct caer_bias_coarsefine biasValue = { .coarseValue = U8T(sshsNodeGetByte(biasNode, "coarseValue")), .fineValue =
		U8T(sshsNodeGetShort(biasNode, "fineValue")), .enabled = sshsNodeGetBool(biasNode, "enabled"), .sexN =
		caerStrEquals(sexString, "N"), .typeNormal = caerStrEquals(typeString, "Normal"), .currentLevelNormal =
		caerStrEquals(currentLevelString, "Normal"), };

	// Free strings to avoid memory leaks.
	free(sexString);
	free(typeString);
	free(currentLevelString);

	return (caerBiasCoarseFineGenerate(biasValue));
}

static void createShiftedSourceBiasSetting(sshsNode biasNode, const char *biasName, uint8_t refValue, uint8_t regValue,
	const char *operatingMode, const char *voltageLevel) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	// Add bias settings.
	sshsNodeCreateByte(biasConfigNode, "refValue", I8T(refValue), 0, 63, SSHS_FLAGS_NORMAL);
	sshsNodeCreateByte(biasConfigNode, "regValue", I8T(regValue), 0, 63, SSHS_FLAGS_NORMAL);
	sshsNodeCreateString(biasConfigNode, "operatingMode", operatingMode, 3, 13, SSHS_FLAGS_NORMAL);
	sshsNodeCreateString(biasConfigNode, "voltageLevel", voltageLevel, 9, 11, SSHS_FLAGS_NORMAL);
}

static uint16_t generateShiftedSourceBiasParent(sshsNode biasNode, const char *biasName) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Get bias configuration node.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	return (generateShiftedSourceBias(biasConfigNode));
}

static uint16_t generateShiftedSourceBias(sshsNode biasNode) {
	// Build up bias value from all its components.
	char *operatingModeString = sshsNodeGetString(biasNode, "operatingMode");
	char *voltageLevelString = sshsNodeGetString(biasNode, "voltageLevel");

	struct caer_bias_shiftedsource biasValue = { .refValue = U8T(sshsNodeGetByte(biasNode, "refValue")), .regValue =
		U8T(sshsNodeGetByte(biasNode, "regValue")), .operatingMode =
		(caerStrEquals(operatingModeString, "HiZ")) ?
			(HI_Z) : ((caerStrEquals(operatingModeString, "TiedToRail")) ? (TIED_TO_RAIL) : (SHIFTED_SOURCE)),
		.voltageLevel =
			(caerStrEquals(voltageLevelString, "SingleDiode")) ?
				(SINGLE_DIODE) : ((caerStrEquals(voltageLevelString, "DoubleDiode")) ? (DOUBLE_DIODE) : (SPLIT_GATE)), };

	// Free strings to avoid memory leaks.
	free(operatingModeString);
	free(voltageLevelString);

	return (caerBiasShiftedSourceGenerate(biasValue));
}
