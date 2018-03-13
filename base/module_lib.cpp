/*
 * module.c
 *
 *  Created on: Dec 14, 2013
 *      Author: chtekk
 */

#include "module.h"

#include <regex>
#include <thread>
#include <mutex>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

bool caerModuleSetSubSystemString(caerModuleData moduleData, const char *subSystemString) {
	// Allocate new memory for new string.
	size_t subSystemStringLenght = strlen(subSystemString);

	char *newSubSystemString = (char *) malloc(subSystemStringLenght + 1);
	if (newSubSystemString == nullptr) {
		// Failed to allocate memory. Log this and don't use the new string.
		caerLog(CAER_LOG_ERROR, moduleData->moduleSubSystemString,
			"Failed to allocate new sub-system string for module.");
		return (false);
	}

	// Copy new string into allocated memory.
	strncpy(newSubSystemString, subSystemString, subSystemStringLenght);
	newSubSystemString[subSystemStringLenght] = '\0';

	// Switch new string with old string and free old memory.
	free(moduleData->moduleSubSystemString);
	moduleData->moduleSubSystemString = newSubSystemString;

	return (true);
}

void caerModuleConfigUpdateReset(caerModuleData moduleData) {
	moduleData->configUpdate.store(0);
}

void caerModuleConfigDefaultListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);
	UNUSED_ARGUMENT(changeKey);
	UNUSED_ARGUMENT(changeType);
	UNUSED_ARGUMENT(changeValue);

	caerModuleData data = (caerModuleData) userData;

	// Simply set the config update flag to 1 on any attribute change.
	if (event == SSHS_ATTRIBUTE_MODIFIED) {
		data->configUpdate.store(1);
	}
}

void caerModuleLog(caerModuleData moduleData, enum caer_log_level logLevel, const char *format, ...) {
	va_list argumentList;
	va_start(argumentList, format);
	caerLogVAFull(caerLogFileDescriptorsGetFirst(), caerLogFileDescriptorsGetSecond(),
		moduleData->moduleLogLevel.load(std::memory_order_relaxed), logLevel, moduleData->moduleSubSystemString, format,
		argumentList);
	va_end(argumentList);
}
