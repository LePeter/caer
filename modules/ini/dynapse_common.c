#include "dynapse_common.h"

static uint32_t convertBias(const char *biasName, const char* lowhi,
		const char*cl, const char*sex, uint8_t enal, uint8_t fineValue,
		uint8_t coarseValue, uint8_t special);
static uint32_t generateCoarseFineBiasParent(sshsNode biasNode,
		const char *biasName);
static void systemConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void usbConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void  usbConfigSend(sshsNode node, caerModuleData moduleData);
static void biasConfigListener(sshsNode node, void *userData,
		enum sshs_node_attribute_events event, const char *changeKey,
		enum sshs_node_attr_value_type changeType,
		union sshs_node_attr_value changeValue);

static inline const char *chipIDToName(int16_t chipID, bool withEndSlash) {
	switch (chipID) {
	case 64:
		return ((withEndSlash) ? ("DYNAPSEFX2/") : ("DYNAPSEFX2"));
		break;
	}

	return ((withEndSlash) ? ("Unknown/") : ("Unknown"));
}

static void mainloopDataNotifyIncrease(void *p) {
	caerMainloopData mainloopData = p;

	atomic_fetch_add_explicit(&mainloopData->dataAvailable, 1,
			memory_order_release);
}

static void mainloopDataNotifyDecrease(void *p) {
	caerMainloopData mainloopData = p;

	// No special memory order for decrease, because the acquire load to even start running
	// through a mainloop already synchronizes with the release store above.
	atomic_fetch_sub_explicit(&mainloopData->dataAvailable, 1,
			memory_order_relaxed);
}

static void moduleShutdownNotify(void *p) {
	sshsNode moduleNode = p;

	// Ensure parent also shuts down (on disconnected device for example).
	sshsNodePutBool(moduleNode, "running", false);
}

static void chipConfigListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);

	caerModuleData moduleData = userData;
	struct caer_dynapse_info devInfo = caerDynapseInfoGet(moduleData->moduleState);

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		if (changeType == SSHS_BYTE && caerStrEquals(changeKey, "MonNeu0")) {
			//caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DDYNAPS_CONFIG_CHIP_DIGITALMUX0,
		//		U32T(changeValue.ibyte));
		}

	}
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

static void usbConfigSend(sshsNode node, caerModuleData moduleData) {
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_NUMBER,
		U32T(sshsNodeGetInt(node, "BufferNumber")));
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_USB, CAER_HOST_CONFIG_USB_BUFFER_SIZE,
		U32T(sshsNodeGetInt(node, "BufferSize")));

	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_EARLY_PACKET_DELAY,
		U32T(sshsNodeGetShort(node, "EarlyPacketDelay")));
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_USB, DYNAPSE_CONFIG_USB_RUN, sshsNodeGetBool(node, "Run"));
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
			caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_USB,  DYNAPSE_CONFIG_USB_EARLY_PACKET_DELAY,
				U32T(changeValue.ishort));
		}
		else if (changeType == SSHS_BOOL && caerStrEquals(changeKey, "Run")) {
			caerDeviceConfigSet(moduleData->moduleState,  DYNAPSE_CONFIG_USB,  DYNAPSE_CONFIG_USB_RUN, changeValue.boolean);
		}
	}
}

static void createCoarseFineBiasSetting(sshsNode biasNode, const char *biasName,
		uint8_t coarseValue, uint8_t fineValue, const char *hlbias,
		const char *currentLevel, const char *sex, bool enabled) {
	// Add trailing slash to node name (required!).
	size_t biasNameLength = strlen(biasName);
	char biasNameFull[biasNameLength + 2];
	memcpy(biasNameFull, biasName, biasNameLength);
	biasNameFull[biasNameLength] = '/';
	biasNameFull[biasNameLength + 1] = '\0';

	// Create configuration node for this particular bias.
	sshsNode biasConfigNode = sshsGetRelativeNode(biasNode, biasNameFull);

	// Add bias settings.
	sshsNodePutShortIfAbsent(biasConfigNode, "coarseValue", I8T(coarseValue));
	sshsNodePutShortIfAbsent(biasConfigNode, "fineValue", I16T(fineValue));
	sshsNodePutStringIfAbsent(biasConfigNode, "BiasLowHi", hlbias);
	sshsNodePutStringIfAbsent(biasConfigNode, "currentLevel", currentLevel);
	sshsNodePutStringIfAbsent(biasConfigNode, "sex", sex);
	sshsNodePutBoolIfAbsent(biasConfigNode, "enabled", enabled);
	sshsNodePutBoolIfAbsent(biasConfigNode, "special", false);

}

static void biasConfigListener(sshsNode node, void *userData,
		enum sshs_node_attribute_events event, const char *changeKey,
		enum sshs_node_attr_value_type changeType,
		union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(changeKey);
	UNUSED_ARGUMENT(changeType);
	UNUSED_ARGUMENT(changeValue);

	caerModuleData moduleData = userData;
	struct caer_dynapse_info devInfo = caerDynapseInfoGet(
			moduleData->moduleState);

	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		const char *nodeName = sshsNodeGetName(node);

		if (caerStrEquals(nodeName, "C0_IF_BUF_P")) {

		}
	}

}

createLowPowerConfiguration(caerModuleData moduleData,
		struct caer_dynapse_info *devInfo) {
// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode,
			chipIDToName(devInfo->chipID, true));

// Chip biases, defaults.
	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	createCoarseFineBiasSetting(biasNode, "C0_IF_BUF_P", 4, 80, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_RFR_N", 4, 3, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_NMDA_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_DC_P", 4, 30, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_TAU1_N", 0, 10, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_TAU2_N", 1, 100, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_THR_N", 4, 30, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_AHW_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_AHTAU_N", 0, 35, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_AHTHR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_CASC_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PULSE_PWLK_P", 4, 106, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PS_WEIGHT_INH_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PS_WEIGHT_INH_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PS_WEIGHT_EXC_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PS_WEIGHT_EXC_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPII_TAU_S_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPII_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPII_TAU_F_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPII_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPIE_TAU_S_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPIE_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPIE_TAU_F_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPIE_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_R2R_P", 3, 85, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_BUF_P", 4, 80, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_RFR_N", 4, 3, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_NMDA_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_DC_P", 4, 30, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_TAU1_N", 0, 10, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_TAU2_N", 1, 100, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_THR_N", 5, 255, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_AHW_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_AHTAU_N", 0, 35, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_AHTHR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_CASC_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PULSE_PWLK_P", 4, 106, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PS_WEIGHT_INH_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PS_WEIGHT_INH_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PS_WEIGHT_EXC_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PS_WEIGHT_EXC_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPII_TAU_S_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPII_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPII_TAU_F_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPII_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPIE_TAU_S_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPIE_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPIE_TAU_F_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPIE_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_R2R_P", 3, 85, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_BUF_P", 4, 80, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_RFR_N", 4, 3, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_NMDA_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_DC_P", 4, 30, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_TAU1_N", 0, 10, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_TAU2_N", 1, 100, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_THR_N", 4, 30, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_AHW_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_AHTAU_N", 0, 35, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_AHTHR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_CASC_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PULSE_PWLK_P", 4, 106, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PS_WEIGHT_INH_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PS_WEIGHT_INH_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PS_WEIGHT_EXC_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PS_WEIGHT_EXC_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPII_TAU_S_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPII_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPII_TAU_F_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPII_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPIE_TAU_S_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPIE_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPIE_TAU_F_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPIE_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_R2R_P", 3, 85, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_BUF_P", 4, 80, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_RFR_N", 4, 3, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_NMDA_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_DC_P", 4, 30, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_TAU1_N", 0, 10, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_TAU2_N", 1, 100, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_THR_N", 5, 255, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_AHW_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_AHTAU_N", 0, 35, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_AHTHR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_CASC_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PULSE_PWLK_P", 4, 106, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PS_WEIGHT_INH_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PS_WEIGHT_INH_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PS_WEIGHT_EXC_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PS_WEIGHT_EXC_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPII_TAU_S_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPII_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPII_TAU_F_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPII_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPIE_TAU_S_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPIE_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPIE_TAU_F_P", 0, 40, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPIE_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_R2R_P", 3, 85, "HighBias",
			"Normal", "PBias", true);

	/*special biases*/
	createCoarseFineBiasSetting(biasNode, "U_Buffer", 6, 85, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "U_SSP", 0, 7, "HighBias", "Normal",
			"PBias", true);
	createCoarseFineBiasSetting(biasNode, "U_SSN", 0, 15, "HighBias", "Normal",
			"PBias", true);

	createCoarseFineBiasSetting(biasNode, "D_Buffer", 6, 80, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "D_SSP", 0, 7, "HighBias", "Normal",
			"PBias", true);
	createCoarseFineBiasSetting(biasNode, "D_SSN", 0, 15, "HighBias", "Normal",
			"PBias", true);

}

static void createDefaultConfiguration(caerModuleData moduleData,
		struct caer_dynapse_info *devInfo) {
// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode,
			chipIDToName(devInfo->chipID, true));

// Chip biases, defaults.
	sshsNode biasNode = sshsGetRelativeNode(deviceConfigNode, "bias/");

	createCoarseFineBiasSetting(biasNode, "C0_IF_BUF_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_RFR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_NMDA_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_DC_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_TAU1_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_TAU2_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_THR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_AHW_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_AHTAU_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_AHTHR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_IF_CASC_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PULSE_PWLK_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PS_WEIGHT_INH_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PS_WEIGHT_INH_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PS_WEIGHT_EXC_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_PS_WEIGHT_EXC_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPII_TAU_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPII_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPII_TAU_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPII_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPIE_TAU_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPIE_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPIE_TAU_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_NPDPIE_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C0_R2R_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_BUF_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_RFR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_NMDA_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_DC_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_TAU1_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_TAU2_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_THR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_AHW_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_AHTAU_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_AHTHR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_IF_CASC_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PULSE_PWLK_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PS_WEIGHT_INH_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PS_WEIGHT_INH_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PS_WEIGHT_EXC_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_PS_WEIGHT_EXC_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPII_TAU_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPII_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPII_TAU_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPII_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPIE_TAU_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPIE_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPIE_TAU_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_NPDPIE_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C1_R2R_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_BUF_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_RFR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_NMDA_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_DC_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_TAU1_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_TAU2_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_THR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_AHW_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_AHTAU_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_AHTHR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_IF_CASC_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PULSE_PWLK_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PS_WEIGHT_INH_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PS_WEIGHT_INH_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PS_WEIGHT_EXC_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_PS_WEIGHT_EXC_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPII_TAU_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPII_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPII_TAU_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPII_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPIE_TAU_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPIE_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPIE_TAU_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_NPDPIE_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C2_R2R_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_BUF_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_RFR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_NMDA_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_DC_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_TAU1_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_TAU2_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_THR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_AHW_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_AHTAU_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_AHTHR_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_IF_CASC_N", 0, 0, "HighBias",
			"Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PULSE_PWLK_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PS_WEIGHT_INH_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PS_WEIGHT_INH_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PS_WEIGHT_EXC_S_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_PS_WEIGHT_EXC_F_N", 0, 0,
			"HighBias", "Normal", "NBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPII_TAU_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPII_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPII_TAU_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPII_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPIE_TAU_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPIE_THR_S_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPIE_TAU_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_NPDPIE_THR_F_P", 0, 0, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "C3_R2R_P", 0, 0, "HighBias",
			"Normal", "PBias", true);

	/*special biases*/
	createCoarseFineBiasSetting(biasNode, "U_Buffer", 1, 80, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "U_SSP", 0, 7, "HighBias", "Normal",
			"PBias", true);
	createCoarseFineBiasSetting(biasNode, "U_SSN", 0, 15, "HighBias", "Normal",
			"PBias", true);

	createCoarseFineBiasSetting(biasNode, "D_Buffer", 1, 80, "HighBias",
			"Normal", "PBias", true);
	createCoarseFineBiasSetting(biasNode, "D_SSP", 0, 7, "HighBias", "Normal",
			"PBias", true);
	createCoarseFineBiasSetting(biasNode, "D_SSN", 0, 15, "HighBias", "Normal",
			"PBias", true);

}

static uint32_t convertBias(const char *biasName, const char* lowhi,
		const char*cl, const char*sex, uint8_t enal, uint8_t fineValue,
		uint8_t coarseValue, uint8_t special) {

	int32_t confbits;
	int32_t addr = 0;
	int32_t inbits = 0;

	/*start names*/
	if (caerStrEquals(biasName, "C0_PULSE_PWLK_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PULSE_PWLK_P;
	}
	if (caerStrEquals(biasName, "C0_PS_WEIGHT_INH_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_INH_S_N;
	}
	if (caerStrEquals(biasName, "C0_PS_WEIGHT_INH_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_INH_F_N;
	}
	if (caerStrEquals(biasName, "C0_PS_WEIGHT_EXC_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_EXC_S_N;
	}
	if (caerStrEquals(biasName, "C0_PS_WEIGHT_EXC_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_EXC_F_N;
	}
	if (caerStrEquals(biasName, "C0_IF_RFR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_RFR_N;
	}
	if (caerStrEquals(biasName, "C0_IF_TAU1_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_TAU1_N;
	}
	if (caerStrEquals(biasName, "C0_IF_AHTAU_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_AHTAU_N;
	}
	if (caerStrEquals(biasName, "C0_IF_CASC_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_CASC_N;
	}
	if (caerStrEquals(biasName, "C0_IF_TAU2_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_TAU2_N;
	}
	if (caerStrEquals(biasName, "C0_IF_BUF_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_BUF_P;
	}
	if (caerStrEquals(biasName, "C0_IF_AHTHR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_AHTHR_N;
	}
	if (caerStrEquals(biasName, "C0_IF_THR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_THR_N;
	}
	if (caerStrEquals(biasName, "C0_NPDPIE_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPIE_THR_S_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPIE_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPIE_THR_F_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPII_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPII_THR_F_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPII_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPII_THR_S_P;
	}
	if (caerStrEquals(biasName, "C0_IF_NMDA_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_NMDA_N;
	}
	if (caerStrEquals(biasName, "C0_IF_DC_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_DC_P;
	}
	if (caerStrEquals(biasName, "C0_IF_AHW_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_IF_AHW_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPII_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPII_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPII_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPII_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPIE_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPIE_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C0_NPDPIE_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_NPDPIE_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C0_R2R_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C0_R2R_P;
	}

	if (caerStrEquals(biasName, "C1_PULSE_PWLK_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PULSE_PWLK_P;
	}
	if (caerStrEquals(biasName, "C1_PS_WEIGHT_INH_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_INH_S_N;
	}
	if (caerStrEquals(biasName, "C1_PS_WEIGHT_INH_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_INH_F_N;
	}
	if (caerStrEquals(biasName, "C1_PS_WEIGHT_EXC_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_EXC_S_N;
	}
	if (caerStrEquals(biasName, "C1_PS_WEIGHT_EXC_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_EXC_F_N;
	}
	if (caerStrEquals(biasName, "C1_IF_RFR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_RFR_N;
	}
	if (caerStrEquals(biasName, "C1_IF_TAU1_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_TAU1_N;
	}
	if (caerStrEquals(biasName, "C1_IF_AHTAU_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_AHTAU_N;
	}
	if (caerStrEquals(biasName, "C1_IF_CASC_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_CASC_N;
	}
	if (caerStrEquals(biasName, "C1_IF_TAU2_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_TAU2_N;
	}
	if (caerStrEquals(biasName, "C1_IF_BUF_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_BUF_P;
	}
	if (caerStrEquals(biasName, "C1_IF_AHTHR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_AHTHR_N;
	}
	if (caerStrEquals(biasName, "C1_IF_THR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_THR_N;
	}
	if (caerStrEquals(biasName, "C1_NPDPIE_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPIE_THR_S_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPIE_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPIE_THR_F_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPII_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPII_THR_F_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPII_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPII_THR_S_P;
	}
	if (caerStrEquals(biasName, "C1_IF_NMDA_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_NMDA_N;
	}
	if (caerStrEquals(biasName, "C1_IF_DC_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_DC_P;
	}
	if (caerStrEquals(biasName, "C1_IF_AHW_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_IF_AHW_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPII_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPII_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPII_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPII_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPIE_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPIE_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C1_NPDPIE_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_NPDPIE_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C1_R2R_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C1_R2R_P;
	}

	if (caerStrEquals(biasName, "C2_PULSE_PWLK_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PULSE_PWLK_P;
	}
	if (caerStrEquals(biasName, "C2_PS_WEIGHT_INH_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_INH_S_N;
	}
	if (caerStrEquals(biasName, "C2_PS_WEIGHT_INH_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_INH_F_N;
	}
	if (caerStrEquals(biasName, "C2_PS_WEIGHT_EXC_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_EXC_S_N;
	}
	if (caerStrEquals(biasName, "C2_PS_WEIGHT_EXC_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_EXC_F_N;
	}
	if (caerStrEquals(biasName, "C2_IF_RFR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_RFR_N;
	}
	if (caerStrEquals(biasName, "C2_IF_TAU1_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_TAU1_N;
	}
	if (caerStrEquals(biasName, "C2_IF_AHTAU_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_AHTAU_N;
	}
	if (caerStrEquals(biasName, "C2_IF_CASC_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_CASC_N;
	}
	if (caerStrEquals(biasName, "C2_IF_TAU2_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_TAU2_N;
	}
	if (caerStrEquals(biasName, "C2_IF_BUF_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_BUF_P;
	}
	if (caerStrEquals(biasName, "C2_IF_AHTHR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_AHTHR_N;
	}
	if (caerStrEquals(biasName, "C2_IF_THR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_THR_N;
	}
	if (caerStrEquals(biasName, "C2_NPDPIE_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPIE_THR_S_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPIE_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPIE_THR_F_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPII_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPII_THR_F_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPII_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPII_THR_S_P;
	}
	if (caerStrEquals(biasName, "C2_IF_NMDA_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_NMDA_N;
	}
	if (caerStrEquals(biasName, "C2_IF_DC_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_DC_P;
	}
	if (caerStrEquals(biasName, "C2_IF_AHW_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_IF_AHW_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPII_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPII_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPII_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPII_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPIE_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPIE_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C2_NPDPIE_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_NPDPIE_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C2_R2R_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C2_R2R_P;
	}

	if (caerStrEquals(biasName, "C3_PULSE_PWLK_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PULSE_PWLK_P;
	}
	if (caerStrEquals(biasName, "C3_PS_WEIGHT_INH_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_INH_S_N;
	}
	if (caerStrEquals(biasName, "C3_PS_WEIGHT_INH_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_INH_F_N;
	}
	if (caerStrEquals(biasName, "C3_PS_WEIGHT_EXC_S_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_EXC_S_N;
	}
	if (caerStrEquals(biasName, "C3_PS_WEIGHT_EXC_F_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_EXC_F_N;
	}
	if (caerStrEquals(biasName, "C3_IF_RFR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_RFR_N;
	}
	if (caerStrEquals(biasName, "C3_IF_TAU1_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_TAU1_N;
	}
	if (caerStrEquals(biasName, "C3_IF_AHTAU_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_AHTAU_N;
	}
	if (caerStrEquals(biasName, "C3_IF_CASC_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_CASC_N;
	}
	if (caerStrEquals(biasName, "C3_IF_TAU2_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_TAU2_N;
	}
	if (caerStrEquals(biasName, "C3_IF_BUF_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_BUF_P;
	}
	if (caerStrEquals(biasName, "C3_IF_AHTHR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_AHTHR_N;
	}
	if (caerStrEquals(biasName, "C3_IF_THR_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_THR_N;
	}
	if (caerStrEquals(biasName, "C3_NPDPIE_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPIE_THR_S_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPIE_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPIE_THR_F_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPII_THR_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPII_THR_F_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPII_THR_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPII_THR_S_P;
	}
	if (caerStrEquals(biasName, "C3_IF_NMDA_N")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_NMDA_N;
	}
	if (caerStrEquals(biasName, "C3_IF_DC_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_DC_P;
	}
	if (caerStrEquals(biasName, "C3_IF_AHW_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_IF_AHW_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPII_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPII_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPII_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPII_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPIE_TAU_F_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPIE_TAU_F_P;
	}
	if (caerStrEquals(biasName, "C3_NPDPIE_TAU_S_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_NPDPIE_TAU_S_P;
	}
	if (caerStrEquals(biasName, "C3_R2R_P")) {
		addr = DYNAPSE_CONFIG_BIAS_C3_R2R_P;
	}

	if (caerStrEquals(biasName, "U_BUFFER")) {
		addr = DYNAPSE_CONFIG_BIAS_U_BUFFER;
	}
	if (caerStrEquals(biasName, "U_SSP")) {
		addr = DYNAPSE_CONFIG_BIAS_U_SSP;
	}
	if (caerStrEquals(biasName, "U_SSN")) {
		addr = DYNAPSE_CONFIG_BIAS_U_SSN;
	}
	if (caerStrEquals(biasName, "D_BUFFER")) {
		addr = DYNAPSE_CONFIG_BIAS_D_BUFFER;
	}
	if (caerStrEquals(biasName, "D_SSP")) {
		addr = DYNAPSE_CONFIG_BIAS_D_SSP;
	}
	if (caerStrEquals(biasName, "D_SSN")) {
		addr = DYNAPSE_CONFIG_BIAS_D_SSN;
	}

	uint8_t lws, ssx, cls;
	if (caerStrEquals(lowhi, "HighBias")) {
		lws = 1;
	} else {
		lws = 0;
	}
	if (caerStrEquals(sex, "NBias")) {
		ssx = 1;
	} else {
		ssx = 0;
	}
	if (caerStrEquals(sex, "Normal")) {
		cls = 1;
	} else {
		cls = 0;
	}

	caerLog(CAER_LOG_DEBUG, "BIAS CONFIGURE ", " biasName %s --> ADDR %d\n",
			biasName, addr);

	/*end names*/

	confbits = lws << 3 | cls << 2 | ssx << 1 | enal;

	// snn and ssp
	if (addr == 51 || addr == 52 || addr == 115 || addr == 116) {
		inbits = addr << 18 | 1 << 16 | 63 << 10 | fineValue << 4 | confbits;
	} else {
		inbits = addr << 18 | 1 << 16 | special << 15 | coarseValue << 12
				| fineValue << 4 | confbits;
	}

	caerLog(CAER_LOG_DEBUG, "BIAS CONFIGURE ", "coarseFineBias.fineValue %d , "
			"coarseFineBias.currentLevel %d, "
			"coarseFineBias.coarseValue %d ,  "
			"coarseFineBias.special %d --> ADDR %d\n", fineValue, cls,
			coarseValue, special, addr);

	return inbits;

}

static void biasConfigSend(sshsNode node, caerModuleData moduleData,
		struct caer_dynapse_info *devInfo) {

	//  10100010111000000001101
	//1110100011111110000001101
	// get the number of childrens biases
	uint32_t value;
	size_t biasNodesLength = 0;
	sshsNode *biasNodes = sshsNodeGetChildren(node, &biasNodesLength);
	const char *nodeName = sshsNodeGetName(node);
	caerLog(CAER_LOG_DEBUG, moduleData->moduleSubSystemString,
			"BIAS LENGHT ... %d NAME %s\n", biasNodesLength, nodeName);

	// SEND DEFAULT BIASES TO ALL CHIPS in BOARD (0,3) only chip id 4 for now
	for (uint32_t this_chip = 4; this_chip < 5; this_chip++) {
		// Let's select this chip for configuration
		if(!caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_ID, this_chip)){
			caerLog(CAER_LOG_CRITICAL, moduleData->moduleSubSystemString, "Failed to configure chip bits");
		}

		// send configuration, one bias per time
		if (biasNodes != NULL) {
			for (size_t i = 0; i < biasNodesLength; i++) {
				//sshsNodeRemoveAttributeListener(biasNodes[i], moduleData, &biasConfigListener);
				const char *nodeName = sshsNodeGetName(biasNodes[i]);

				//value = generateCoarseFineBiasParent(biasNodes[i], nodeName);
				bool enal = sshsNodeGetBool(biasNodes[i], "enabled");
				bool special = sshsNodeGetBool(biasNodes[i], "special");
				uint8_t coarseValue = sshsNodeGetShort(biasNodes[i],
						"coarseValue");
				uint8_t fineValue = sshsNodeGetShort(biasNodes[i], "fineValue");
				const char * lowhi = sshsNodeGetString(biasNodes[i],
						"BiasLowHi");
				const char * cl = sshsNodeGetString(biasNodes[i],
						"currentLevel");
				const char * sex = sshsNodeGetString(biasNodes[i], "sex");

				// generates bits values
				uint32_t bits = convertBias(nodeName, lowhi, cl, sex, enal,
						fineValue, coarseValue, special);

				caerLog(CAER_LOG_DEBUG, moduleData->moduleSubSystemString,
						"Bias lenght ... %d name %s enabled %d special %d coarseValue %d bits %d\n",
						biasNodesLength, nodeName, enal, special, coarseValue,
						bits);

				// finally send configuration via USB
				caerDeviceConfigSet(moduleData->moduleState,
						DYNAPSE_CONFIG_CHIP,
						DYNAPSE_CONFIG_CHIP_CONTENT, bits);

				//free strings
				free(lowhi);
				free(cl);
				free(sex);

			}
			free(biasNodes);
		}

	}

}

static void sendDefaultConfiguration(caerModuleData moduleData,
		struct caer_dynapse_info *devInfo) {
	// Device related configuration has its own sub-node.
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(devInfo->chipID, true));

	// send default bias configuration
	biasConfigSend(sshsGetRelativeNode(deviceConfigNode, "bias/"), moduleData, devInfo);

}

bool caerInputDYNAPSEInit(caerModuleData moduleData, uint16_t deviceType) {

// USB port/bus/SN settings/restrictions.
// These can be used to force connection to one specific device at startup.
	sshsNodePutShortIfAbsent(moduleData->moduleNode, "busNumber", 0);
	sshsNodePutShortIfAbsent(moduleData->moduleNode, "devAddress", 0);
	sshsNodePutStringIfAbsent(moduleData->moduleNode, "serialNumber", "");

// Add auto-restart setting.
	sshsNodePutBoolIfAbsent(moduleData->moduleNode, "autoRestart", true);

/// Start data acquisition, and correctly notify mainloop of new data and module of exceptional
// shutdown cases (device pulled, ...).
	char *serialNumber = sshsNodeGetString(moduleData->moduleNode,
			"serialNumber");

	moduleData->moduleState = caerDeviceOpen(1, CAER_DEVICE_DYNAPSE, 0, 0,
	NULL);

	free(serialNumber);

	if (moduleData->moduleState == NULL) {
		// Failed to open device.
		return (false);
	}

// Let's take a look at the information we have on the device.
	struct caer_dynapse_info dynapse_info = caerDynapseInfoGet(
			moduleData->moduleState);

	caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,
			"%s --- ID: %d, Master: %d,  Logic: %d,  ChipID: %d.\n",
			dynapse_info.deviceString, dynapse_info.deviceID,
			dynapse_info.deviceIsMaster, dynapse_info.logicVersion,
			dynapse_info.chipID);

	sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode,
			"sourceInfo/");

	sshsNodePutLong(sourceInfoNode, "highestTimestamp", -1);
	sshsNodePutShort(sourceInfoNode, "logicVersion", dynapse_info.logicVersion);
	sshsNodePutBool(sourceInfoNode, "deviceIsMaster",
			dynapse_info.deviceIsMaster);
	sshsNodePutShort(sourceInfoNode, "deviceID", dynapse_info.deviceID);
	sshsNodePutShort(sourceInfoNode, "chipID", dynapse_info.chipID);

// Generate source string for output modules.
	size_t sourceStringLength = (size_t) snprintf(NULL, 0,
			"#Source %" PRIu16 ": %s\r\n", moduleData->moduleID,
			chipIDToName(dynapse_info.chipID, false));

	char sourceString[sourceStringLength + 1];
	snprintf(sourceString, sourceStringLength + 1,
			"#Source %" PRIu16 ": %s\r\n", moduleData->moduleID,
			chipIDToName(dynapse_info.chipID, false));
	sourceString[sourceStringLength] = '\0';

	sshsNodePutString(sourceInfoNode, "sourceString", sourceString);

// Generate sub-system string for module.
	size_t subSystemStringLength = (size_t) snprintf(NULL, 0,
			"%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
			moduleData->moduleSubSystemString, dynapse_info.deviceSerialNumber,
			dynapse_info.deviceUSBBusNumber,
			dynapse_info.deviceUSBDeviceAddress);

	char subSystemString[subSystemStringLength + 1];
	snprintf(subSystemString, subSystemStringLength + 1,
			"%s[SN %s, %" PRIu8 ":%" PRIu8 "]",
			moduleData->moduleSubSystemString, dynapse_info.deviceSerialNumber,
			dynapse_info.deviceUSBBusNumber,
			dynapse_info.deviceUSBDeviceAddress);
	subSystemString[subSystemStringLength] = '\0';

	caerModuleSetSubSystemString(moduleData, subSystemString);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE,
			CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
			DYNAPSE_CONFIG_CHIP_RUN, true);

	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN,
			true);
	// chip id is CONFCHIPID
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			4);
	caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX,
			DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);


	// Create default settings and send them to the device.
		//createDefaultConfiguration(moduleData, &dynapse_info);
		createLowPowerConfiguration(moduleData, &dynapse_info);
		//sendDefaultConfiguration(moduleData, &dynapse_info); // inactive

		//send file biases
	    FILE * fp;
	    char * line = NULL;
	    size_t len = 0;
	    ssize_t read;

		// send silent  biases
	    caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Send default biases from file ...\n");
	    fp = fopen("modules/ini/dynapse_null.txt", "r");
	    if (fp == NULL)
	        exit(EXIT_FAILURE);

	    while ((read = getline(&line, &len, fp)) != -1) {
	        //printf("Retrieved line of length %zu :\n", read);
	        //printf("%s", line);
	        uint32_t val = atoi(line);
	        caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
	        							DYNAPSE_CONFIG_CHIP_CONTENT, val);
	    }
	    caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Done.\n");

	    fclose(fp);


		// sram content
	    /*caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Send default sram content from file ...\n");
	    fp = fopen("modules/ini/sram_null.txt", "r");
	    if (fp == NULL)
	        exit(EXIT_FAILURE);

	    while ((read = getline(&line, &len, fp)) != -1) {
	        //printf("Retrieved line of length %zu :\n", read);
	        //printf("%s", line);
	        uint32_t val = atoi(line);
	        caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
	        							DYNAPSE_CONFIG_CHIP_CONTENT, val);
	    }
	    caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Done.\n");

	    fclose(fp);*/


	   /* caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Send default cam content from file ...\n");
	    fp = fopen("modules/ini/cam_null.txt", "r");
	    if (fp == NULL)
	        exit(EXIT_FAILURE);

	    while ((read = getline(&line, &len, fp)) != -1) {
	        //printf("Retrieved line of length %zu :\n", read);
	        //printf("%s", line);
	        uint32_t val = atoi(line);
	        caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
	        							DYNAPSE_CONFIG_CHIP_CONTENT, val);
	    }
	    caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Done.\n");

	    fclose(fp);*/

		// Clear SRAM
		uint32_t bits = 0;
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Clearing SRAM ...\n");
			for (uint8_t chip = 4; chip < 5; chip++) {
				caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Device number  %d...\n", chip);
				// chip number
				caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
								DYNAPSE_CONFIG_CHIP_ID, chip);

				// all cores
				for (uint8_t core = 0; core < 4; core++) {
					// all rows
					for (uint16_t row = 0; row < 1024; row++) {
							bits = row << 5 | core << 15 | 1 << 17 | 1 << 4;
							// finally send configuration via USB
							caerDeviceConfigSet(moduleData->moduleState,
									DYNAPSE_CONFIG_CHIP,
									DYNAPSE_CONFIG_CHIP_CONTENT, bits);
							caerLog(CAER_LOG_DEBUG, moduleData->moduleSubSystemString,"SRAM BITS %d\n", bits);
					}
				}
			}
			caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString," Done.\n");

		// Clear CAM
				caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Clearing CAM ...\n");
				for (uint8_t chip = 4; chip < 5; chip++) {
					caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Device number  %d...\n", chip);
					// chip number
					caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
									DYNAPSE_CONFIG_CHIP_ID, chip);

					// all cores
					for (uint8_t core = 0; core < 4; core++) {
						// all rows
						for (uint16_t row = 0; row < 1024; row++) {
							for (uint16_t columns = 0; columns < 16; columns++) {
								bits =  row << 5 | core << 15 | 1 << 17;
								// finally send configuration via USB
								caerDeviceConfigSet(moduleData->moduleState,
										DYNAPSE_CONFIG_CHIP,
										DYNAPSE_CONFIG_CHIP_CONTENT, bits);
								//caerLog(CAER_LOG_DEBUG, moduleData->moduleSubSystemString,"SRAM BITS %d\n", bits);
							}
						}
					}
				}
				caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString," Done.\n");



    // close config
    caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, false);

    //close aer communication
    caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, false);

    caerDeviceClose(&moduleData->moduleState);


	// Open the communication with Dynap-se, give it a device ID of 1, and don't care about USB bus or SN restrictions.
    moduleData->moduleState = caerDeviceOpen(1, CAER_DEVICE_DYNAPSE, 0, 0,
			NULL);
	if (moduleData->moduleState == NULL) {
		return (EXIT_FAILURE);
	}

	// Let's take a look at the information we have on the device.
	dynapse_info = caerDynapseInfoGet(moduleData->moduleState);

	caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,
			"%s --- ID: %d, Master: %d,  Logic: %d.\n",
			dynapse_info.deviceString, dynapse_info.deviceID,
			dynapse_info.deviceIsMaster, dynapse_info.logicVersion);

    caerDeviceConfigSet(moduleData->moduleState, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

    caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, true);

    caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, true);

    caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, 4);

    // force chip to be enable even if aer is off
    caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);


// now load low power biases

		// always work on code id 4
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
								DYNAPSE_CONFIG_CHIP_ID, 4);

		createLowPowerConfiguration(moduleData, &dynapse_info);
		//sendDefaultConfiguration(moduleData, &dynapse_info);

		// send low power biases
	    caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Send default low power biases from file ...\n");
	    fp = fopen("modules/ini/dynapse_lp.txt", "r");
	    if (fp == NULL)
	        exit(EXIT_FAILURE);

	    while ((read = getline(&line, &len, fp)) != -1) {
	        //printf("Retrieved line of length %zu :\n", read);
	        //printf("%s", line);
	        uint32_t val = atoi(line);
	        caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
	        							DYNAPSE_CONFIG_CHIP_CONTENT, val);
	    }
	    caerLog(CAER_LOG_NOTICE, moduleData->moduleSubSystemString,"Done.\n");

	    // free file related strings
	    fclose(fp);
	    if (line)
	        free(line);

		/*output one neuron per core, neuron id 0 chip 4*/
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
						DYNAPSE_CONFIG_CHIP_CONTENT, 2048);
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
						DYNAPSE_CONFIG_CHIP_CONTENT, 0);
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
							DYNAPSE_CONFIG_CHIP_CONTENT, 2304);
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
						DYNAPSE_CONFIG_CHIP_CONTENT, 256);
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
								DYNAPSE_CONFIG_CHIP_CONTENT, 2560);
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
						DYNAPSE_CONFIG_CHIP_CONTENT, 512);
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
									DYNAPSE_CONFIG_CHIP_CONTENT, 2816);
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
						DYNAPSE_CONFIG_CHIP_CONTENT, 768);


//send to FPGA neuron zero of core 3 as from coreID 15
		caerDeviceConfigSet(moduleData->moduleState, DYNAPSE_CONFIG_CHIP,
							DYNAPSE_CONFIG_CHIP_CONTENT, 977240176);

		// Start data acquisition.
		//bool ret = caerDeviceDataStart(moduleData->moduleState, &mainloopDataNotifyIncrease, &mainloopDataNotifyDecrease,
		//	caerMainloopGetReference(), &moduleShutdownNotify, moduleData->moduleNode);
		bool ret = caerDeviceDataStart(moduleData->moduleState, &mainloopDataNotifyIncrease, &mainloopDataNotifyDecrease,
					caerMainloopGetReference(), &moduleShutdownNotify, moduleData->moduleNode);

		if (!ret) {
			// Failed to start data acquisition, close device and exit.
			caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

			return (false);
		}

		// Device related configuration has its own sub-node.
		sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode, chipIDToName(dynapse_info.chipID, true));

		sshsNode chipNode = sshsGetRelativeNode(deviceConfigNode, "chip/");
		sshsNodeAddAttributeListener(chipNode, moduleData, &chipConfigListener);

		sshsNode sysNode = sshsGetRelativeNode(deviceConfigNode, "system/");
		sshsNodeAddAttributeListener(sysNode, moduleData, &systemConfigListener);

		sshsNode usbNode = sshsGetRelativeNode(deviceConfigNode, "usb/");
		sshsNodeAddAttributeListener(usbNode, moduleData, &usbConfigListener);

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

		return (true);



}

void caerInputDYNAPSEExit(caerModuleData moduleData) {
// Device related configuration has its own sub-node.
	struct caer_dynapse_info devInfo = caerDynapseInfoGet(
			moduleData->moduleState);
	sshsNode deviceConfigNode = sshsGetRelativeNode(moduleData->moduleNode,
			chipIDToName(devInfo.chipID, true));

	caerDeviceDataStop(moduleData->moduleState);

	caerDeviceClose((caerDeviceHandle *) &moduleData->moduleState);

	if (sshsNodeGetBool(moduleData->moduleNode, "autoRestart")) {
		// Prime input module again so that it will try to restart if new devices detected.
		sshsNodePutBool(moduleData->moduleNode, "running", true);
	}
}

void caerInputDYNAPSERun(caerModuleData moduleData, size_t argsNumber, va_list args) {
	UNUSED_ARGUMENT(argsNumber);

	// Interpret variable arguments (same as above in main function).
	caerEventPacketContainer *container = va_arg(args, caerEventPacketContainer *);

	*container = caerDeviceDataGet(moduleData->moduleState);

	if (*container != NULL) {
		caerMainloopFreeAfterLoop((void (*)(void *)) &caerEventPacketContainerFree, *container);

		sshsNode sourceInfoNode = sshsGetRelativeNode(moduleData->moduleNode, "sourceInfo/");
		sshsNodePutLong(sourceInfoNode, "highestTimestamp",
			caerEventPacketContainerGetHighestEventTimestamp(*container));

		// Detect timestamp reset and call all reset functions for processors and outputs.
		caerEventPacketHeader special = caerEventPacketContainerGetEventPacket(*container, SPECIAL_EVENT);

		if ((special != NULL) && (caerEventPacketHeaderGetEventNumber(special) == 1)
			&& (caerSpecialEventPacketFindEventByType((caerSpecialEventPacket) special, TIMESTAMP_RESET) != NULL)) {
			caerMainloopResetProcessors(moduleData->moduleID);
			caerMainloopResetOutputs(moduleData->moduleID);

			// Update master/slave information.
			struct caer_dynapse_info devInfo = caerDynapseInfoGet(moduleData->moduleState);
			sshsNodePutBool(sourceInfoNode, "deviceIsMaster", devInfo.deviceIsMaster);
		}
	}
}

