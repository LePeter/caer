#include "main.h"
#include "base/mainloop.h"
#include "base/module.h"
#include "input_common.h"
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>

static bool caerInputFileInit(caerModuleData moduleData);

static const struct caer_module_functions InputFileFunctions = { .moduleInit = &caerInputFileInit, .moduleRun =
	&caerInputCommonRun, .moduleConfig = NULL, .moduleExit = &caerInputCommonExit };

static const struct caer_event_stream_out InputFileOutputs[] = { { .type = -1 } };

static const struct caer_module_info InputFileInfo = { .version = 1, .name = "FileInput", .type = CAER_MODULE_INPUT,
	.memSize = sizeof(struct input_common_state), .functions = &InputFileFunctions, .inputStreams = NULL,
	.inputStreamsSize = 0, .outputStreams = InputFileOutputs, .outputStreamsSize = CAER_EVENT_STREAM_OUT_SIZE(
		InputFileOutputs), };

caerModuleInfo caerModuleGetInfo(void) {
	return (&InputFileInfo);
}

static bool caerInputFileInit(caerModuleData moduleData) {
	sshsNodeCreateString(moduleData->moduleNode, "filePath", "", 0, PATH_MAX, SSHS_FLAGS_NORMAL);

	char *filePath = sshsNodeGetString(moduleData->moduleNode, "filePath");

	if (caerStrEquals(filePath, "")) {
		free(filePath);

		caerModuleLog(moduleData, CAER_LOG_ERROR, "No input file given, please specify the 'filePath' parameter.");
		return (false);
	}

	int fileFd = open(filePath, O_RDONLY);
	if (fileFd < 0) {
		caerModuleLog(moduleData, CAER_LOG_CRITICAL, "Could not open input file '%s' for reading. Error: %d.", filePath,
			errno);
		free(filePath);

		return (false);
	}

	caerModuleLog(moduleData, CAER_LOG_INFO, "Opened input file '%s' successfully for reading.", filePath);
	free(filePath);

	if (!caerInputCommonInit(moduleData, fileFd, false, false)) {
		close(fileFd);

		return (false);
	}

	return (true);
}
