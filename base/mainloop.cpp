#include "mainloop.h"
#include <csignal>
#include <climits>
#include <unistd.h>

#include <string>
#include <regex>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/range/join.hpp>
#include <boost/format.hpp>

// If Boost version recent enough, use their portable DLL loading support.
// Else use dlopen() on POSIX systems.
#if defined(BOOST_VERSION) && (BOOST_VERSION / 100000) >= 1 && (BOOST_VERSION / 100 % 1000) >= 61
#define BOOST_HAS_DLL_LOAD 1
#else
#define BOOST_HAS_DLL_LOAD 0
#endif

#if BOOST_HAS_DLL_LOAD
#include <boost/dll.hpp>
using ModuleLibrary = boost::dll::shared_library;
#else
#include <dlfcn.h>
using ModuleLibrary = void *;
#endif

#include <libcaercpp/libcaer.hpp>
using namespace libcaer::log;

struct OrderedInput {
	int16_t typeId;
	int16_t afterModuleId;
	bool copyNeeded;

	OrderedInput(int16_t t, int16_t a) :
			typeId(t),
			afterModuleId(a),
			copyNeeded(false) {
	}

	// Comparison operators.
	bool operator==(const OrderedInput &rhs) const noexcept {
		return (typeId == rhs.typeId);
	}

	bool operator!=(const OrderedInput &rhs) const noexcept {
		return (typeId != rhs.typeId);
	}

	bool operator<(const OrderedInput &rhs) const noexcept {
		return (typeId < rhs.typeId);
	}

	bool operator>(const OrderedInput &rhs) const noexcept {
		return (typeId > rhs.typeId);
	}

	bool operator<=(const OrderedInput &rhs) const noexcept {
		return (typeId <= rhs.typeId);
	}

	bool operator>=(const OrderedInput &rhs) const noexcept {
		return (typeId >= rhs.typeId);
	}
};

struct ModuleInfo {
	// Module identification.
	int16_t id;
	const std::string name;
	// SSHS configuration node.
	sshsNode configNode;
	// Parsed moduleInput configuration.
	std::unordered_map<int16_t, std::vector<OrderedInput>> inputDefinition;
	// Connectivity graph (I/O).
	std::vector<std::pair<ssize_t, ssize_t>> inputs;
	std::unordered_map<int16_t, ssize_t> outputs;
	// Loadable module support.
	const std::string library;
	ModuleLibrary libraryHandle;
	caerModuleInfo libraryInfo;

	ModuleInfo() :
			id(-1),
			name(),
			configNode(nullptr),
			library(),
			libraryHandle(),
			libraryInfo(nullptr) {
	}

	ModuleInfo(int16_t i, const std::string &n, sshsNode c, const std::string &l) :
			id(i),
			name(n),
			configNode(c),
			library(l),
			libraryHandle(),
			libraryInfo(nullptr) {
	}
};

struct DependencyNode;

struct DependencyLink {
	int16_t id;
	std::shared_ptr<DependencyNode> next;

	DependencyLink(int16_t i) :
			id(i) {
	}

	// Comparison operators.
	bool operator==(const DependencyLink &rhs) const noexcept {
		return (id == rhs.id);
	}

	bool operator!=(const DependencyLink &rhs) const noexcept {
		return (id != rhs.id);
	}

	bool operator<(const DependencyLink &rhs) const noexcept {
		return (id < rhs.id);
	}

	bool operator>(const DependencyLink &rhs) const noexcept {
		return (id > rhs.id);
	}

	bool operator<=(const DependencyLink &rhs) const noexcept {
		return (id <= rhs.id);
	}

	bool operator>=(const DependencyLink &rhs) const noexcept {
		return (id >= rhs.id);
	}
};

struct DependencyNode {
	size_t depth;
	int16_t parentId;
	DependencyNode *parentLink;
	std::vector<DependencyLink> links;

	DependencyNode(size_t d, int16_t pId, DependencyNode *pLink) :
			depth(d),
			parentId(pId),
			parentLink(pLink) {
	}
};

struct ActiveStreams {
	int16_t sourceId;
	int16_t typeId;
	bool isProcessor;
	std::vector<int16_t> users;
	std::shared_ptr<DependencyNode> dependencies;

	ActiveStreams(int16_t s, int16_t t) :
			sourceId(s),
			typeId(t),
			isProcessor(false) {
	}

	// Comparison operators.
	bool operator==(const ActiveStreams &rhs) const noexcept {
		return (sourceId == rhs.sourceId && typeId == rhs.typeId);
	}

	bool operator!=(const ActiveStreams &rhs) const noexcept {
		return (sourceId != rhs.sourceId || typeId != rhs.typeId);
	}

	bool operator<(const ActiveStreams &rhs) const noexcept {
		return (sourceId < rhs.sourceId || (sourceId == rhs.sourceId && typeId < rhs.typeId));
	}

	bool operator>(const ActiveStreams &rhs) const noexcept {
		return (sourceId > rhs.sourceId || (sourceId == rhs.sourceId && typeId > rhs.typeId));
	}

	bool operator<=(const ActiveStreams &rhs) const noexcept {
		return (sourceId < rhs.sourceId || (sourceId == rhs.sourceId && typeId <= rhs.typeId));
	}

	bool operator>=(const ActiveStreams &rhs) const noexcept {
		return (sourceId > rhs.sourceId || (sourceId == rhs.sourceId && typeId >= rhs.typeId));
	}
};

static struct {
	sshsNode configNode;
	atomic_bool systemRunning;
	atomic_bool running;
	atomic_uint_fast32_t dataAvailable;
	size_t copyCount;
	std::unordered_map<int16_t, ModuleInfo> modules;
	std::vector<ActiveStreams> streams;
	std::vector<std::reference_wrapper<ModuleInfo>> globalExecution;
	std::vector<caerEventPacketHeader> eventPackets;
} glMainloopData;

static std::vector<boost::filesystem::path> modulePaths;

static int caerMainloopRunner(void);
static void caerMainloopSignalHandler(int signal);
static void caerMainloopSystemRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);
static void caerMainloopRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue);

template<class T>
static void vectorSortUnique(std::vector<T> &vec) {
	std::sort(vec.begin(), vec.end());
	vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}

template<class T>
static bool vectorDetectDuplicates(std::vector<T> &vec) {
	// Detect duplicates.
	size_t sizeBefore = vec.size();

	vectorSortUnique(vec);

	size_t sizeAfter = vec.size();

	// If size changed, duplicates must have been removed, so they existed
	// in the first place!
	if (sizeAfter != sizeBefore) {
		return (true);
	}

	return (false);
}

void caerMainloopRun(void) {
	// Install signal handler for global shutdown.
#if defined(OS_WINDOWS)
	if (signal(SIGTERM, &caerMainloopSignalHandler) == SIG_ERR) {
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	if (signal(SIGINT, &caerMainloopSignalHandler) == SIG_ERR) {
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGINT. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	if (signal(SIGBREAK, &caerMainloopSignalHandler) == SIG_ERR) {
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGBREAK. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	// Disable closing of the console window where cAER is executing.
	// While we do catch the signal (SIGBREAK) that such an action generates, it seems
	// we can't reliably shut down within the hard time window that Windows enforces when
	// pressing the close button (X in top right corner usually). This seems to be just
	// 5 seconds, and we can't guarantee full shutdown (USB, file writing, etc.) in all
	// cases within that time period (multiple cameras, modules etc. make this worse).
	// So we just disable that and force the user to CTRL+C, which works fine.
	HWND consoleWindow = GetConsoleWindow();
	if (consoleWindow != nullptr) {
		HMENU systemMenu = GetSystemMenu(consoleWindow, false);
		EnableMenuItem(systemMenu, SC_CLOSE, MF_GRAYED);
	}
#else
	struct sigaction shutdown;

	shutdown.sa_handler = &caerMainloopSignalHandler;
	shutdown.sa_flags = 0;
	sigemptyset(&shutdown.sa_mask);
	sigaddset(&shutdown.sa_mask, SIGTERM);
	sigaddset(&shutdown.sa_mask, SIGINT);

	if (sigaction(SIGTERM, &shutdown, nullptr) == -1) {
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	if (sigaction(SIGINT, &shutdown, nullptr) == -1) {
		log(logLevel::EMERGENCY, "Mainloop", "Failed to set signal handler for SIGINT. Error: %d.", errno);
		exit(EXIT_FAILURE);
	}

	// Ignore SIGPIPE.
	signal(SIGPIPE, SIG_IGN);
#endif

	// Search for available modules. Will be loaded as needed later.
	// Initialize with default search directory.
	sshsNode moduleSearchNode = sshsGetNode(sshsGetGlobal(), "/caer/modules/");

	boost::filesystem::path moduleSearchDir = boost::filesystem::current_path();
	moduleSearchDir.append("modules/");

	sshsNodeCreateString(moduleSearchNode, "moduleSearchPath", moduleSearchDir.generic_string().c_str(), 2, PATH_MAX,
		SSHS_FLAGS_NORMAL);

	// Now get actual search directory.
	char *moduleSearchPathC = sshsNodeGetString(moduleSearchNode, "moduleSearchPath");
	const std::string moduleSearchPath = moduleSearchPathC;
	free(moduleSearchPathC);

	const std::regex moduleRegex("\\w+\\.(so|dll)");

	std::for_each(boost::filesystem::recursive_directory_iterator(moduleSearchPath),
		boost::filesystem::recursive_directory_iterator(),
		[&moduleRegex](const boost::filesystem::directory_entry &e) {
			if (boost::filesystem::exists(e.path()) && boost::filesystem::is_regular_file(e.path()) && std::regex_match(e.path().filename().string(), moduleRegex)) {
				modulePaths.push_back(e.path());
			}
		});

	// Sort and unique.
	vectorSortUnique(modulePaths);

	// No modules, cannot start!
	if (modulePaths.empty()) {
		log(logLevel::CRITICAL, "Mainloop", "Failed to find any modules on path '%s'.", moduleSearchPath.c_str());
		return;
	}

	// No data at start-up.
	glMainloopData.dataAvailable.store(0);

	// System running control, separate to allow mainloop stop/start.
	glMainloopData.systemRunning.store(true);

	sshsNode systemNode = sshsGetNode(sshsGetGlobal(), "/caer/");
	sshsNodeCreateBool(systemNode, "running", true, SSHS_FLAGS_NORMAL);
	sshsNodeAddAttributeListener(systemNode, nullptr, &caerMainloopSystemRunningListener);

	// Mainloop running control.
	glMainloopData.running.store(true);

	glMainloopData.configNode = sshsGetNode(sshsGetGlobal(), "/");
	sshsNodeCreateBool(glMainloopData.configNode, "running", true, SSHS_FLAGS_NORMAL);
	sshsNodeAddAttributeListener(glMainloopData.configNode, nullptr, &caerMainloopRunningListener);

	while (glMainloopData.systemRunning.load()) {
		if (!glMainloopData.running.load()) {
			std::this_thread::sleep_for(std::chrono::seconds(1));
			continue;
		}

		int result = caerMainloopRunner();

		// On failure, make sure to disable mainloop, user will have to fix it.
		if (result == EXIT_FAILURE) {
			sshsNodePutBool(glMainloopData.configNode, "running", false);

			log(logLevel::CRITICAL, "Mainloop",
				"Failed to start mainloop, please fix the configuration and try again!");
		}
	}
}

static void checkInputOutputStreamDefinitions(caerModuleInfo info) {
	if (info->type == CAER_MODULE_INPUT) {
		if (info->inputStreams != nullptr || info->inputStreamsSize != 0 || info->outputStreams == nullptr
			|| info->outputStreamsSize == 0) {
			throw std::domain_error("Wrong I/O event stream definitions for type INPUT.");
		}
	}
	else if (info->type == CAER_MODULE_OUTPUT) {
		if (info->inputStreams == nullptr || info->inputStreamsSize == 0 || info->outputStreams != nullptr
			|| info->outputStreamsSize != 0) {
			throw std::domain_error("Wrong I/O event stream definitions for type OUTPUT.");
		}

		// Also ensure that all input streams of an output module are marked read-only.
		bool readOnlyError = false;

		for (size_t i = 0; i < info->inputStreamsSize; i++) {
			if (!info->inputStreams[i].readOnly) {
				readOnlyError = true;
				break;
			}
		}

		if (readOnlyError) {
			throw std::domain_error("Input event streams not marked read-only for type OUTPUT.");
		}
	}
	else {
		// CAER_MODULE_PROCESSOR
		if (info->inputStreams == nullptr || info->inputStreamsSize == 0) {
			throw std::domain_error("Wrong I/O event stream definitions for type PROCESSOR.");
		}

		// If no output streams are defined, then at least one input event
		// stream must not be readOnly, so that there is modified data to output.
		if (info->outputStreams == nullptr || info->outputStreamsSize == 0) {
			bool readOnlyError = true;

			for (size_t i = 0; i < info->inputStreamsSize; i++) {
				if (!info->inputStreams[i].readOnly) {
					readOnlyError = false;
					break;
				}
			}

			if (readOnlyError) {
				throw std::domain_error(
					"No output streams and all input streams are marked read-only for type PROCESSOR.");
			}
		}
	}
}

/**
 * Type must be either -1 or well defined (0-INT16_MAX).
 * Number must be either -1 or well defined (1-INT16_MAX). Zero not allowed.
 * The event stream array must be ordered by ascending type ID.
 * For each type, only one definition can exist.
 * If type is -1 (any), then number must also be -1; having a defined
 * number in this case makes no sense (N of any type???), a special exception
 * is made for the number 1 (1 of any type) with inputs, which can be useful.
 * Also this must then be the only definition.
 * If number is -1, then either the type is also -1 and this is the
 * only event stream definition (same as rule above), OR the type is well
 * defined and this is the only event stream definition for that type.
 */
static void checkInputStreamDefinitions(caerEventStreamIn inputStreams, size_t inputStreamsSize) {
	for (size_t i = 0; i < inputStreamsSize; i++) {
		// Check type range.
		if (inputStreams[i].type < -1) {
			throw std::domain_error("Input stream has invalid type value.");
		}

		// Check number range.
		if (inputStreams[i].number < -1 || inputStreams[i].number == 0) {
			throw std::domain_error("Input stream has invalid number value.");
		}

		// Check sorted array and only one definition per type; the two
		// requirements together mean strict monotonicity for types.
		if (i > 0 && inputStreams[i - 1].type >= inputStreams[i].type) {
			throw std::domain_error("Input stream has invalid order of declaration or duplicates.");
		}

		// Check that any type is always together with any number or 1, and the
		// only definition present in that case.
		if (inputStreams[i].type == -1
			&& ((inputStreams[i].number != -1 && inputStreams[i].number != 1) || inputStreamsSize != 1)) {
			throw std::domain_error("Input stream has invalid any declaration.");
		}
	}
}

/**
 * Type must be either -1 or well defined (0-INT16_MAX).
 * The event stream array must be ordered by ascending type ID.
 * For each type, only one definition can exist.
 * If type is -1 (any), then this must then be the only definition.
 */
static void checkOutputStreamDefinitions(caerEventStreamOut outputStreams, size_t outputStreamsSize) {
	// If type is any, must be the only definition.
	if (outputStreamsSize == 1 && outputStreams[0].type == -1) {
		return;
	}

	for (size_t i = 0; i < outputStreamsSize; i++) {
		// Check type range.
		if (outputStreams[i].type < 0) {
			throw std::domain_error("Output stream has invalid type value.");
		}

		// Check sorted array and only one definition per type; the two
		// requirements together mean strict monotonicity for types.
		if (i > 0 && outputStreams[i - 1].type >= outputStreams[i].type) {
			throw std::domain_error("Output stream has invalid order of declaration or duplicates.");
		}
	}
}

/**
 * Check for the presence of the 'moduleInput' and 'moduleOutput' configuration
 * parameters, depending on the type of module and its requirements.
 */
static void checkModuleInputOutput(caerModuleInfo info, sshsNode configNode) {
	if (info->type == CAER_MODULE_INPUT) {
		// moduleInput must not exist for INPUT modules.
		if (sshsNodeAttributeExists(configNode, "moduleInput", SSHS_STRING)) {
			throw std::domain_error("INPUT type cannot have a 'moduleInput' attribute.");
		}
	}
	else {
		// CAER_MODULE_OUTPUT / CAER_MODULE_PROCESSOR
		// moduleInput must exist for OUTPUT and PROCESSOR modules.
		if (!sshsNodeAttributeExists(configNode, "moduleInput", SSHS_STRING)) {
			throw std::domain_error("OUTPUT/PROCESSOR types must have a 'moduleInput' attribute.");
		}
	}

	if (info->type == CAER_MODULE_OUTPUT) {
		// moduleOutput must not exist for OUTPUT modules.
		if (sshsNodeAttributeExists(configNode, "moduleOutput", SSHS_STRING)) {
			throw std::domain_error("OUTPUT type cannot have a 'moduleOutput' attribute.");
		}
	}
	else {
		// CAER_MODULE_INPUT / CAER_MODULE_PROCESSOR
		// moduleOutput must exist for INPUT and PROCESSOR modules, only
		// if their outputs are undefined (-1).
		if (info->outputStreams != nullptr && info->outputStreamsSize == 1 && info->outputStreams[0].type == -1
			&& !sshsNodeAttributeExists(configNode, "moduleOutput", SSHS_STRING)) {
			throw std::domain_error(
				"INPUT/PROCESSOR types with ANY_TYPE definition must have a 'moduleOutput' attribute.");
		}
	}
}

static std::vector<int16_t> parseTypeIDString(const std::string &types) {
	// Empty string, cannot be!
	if (types.empty()) {
		throw std::invalid_argument("Empty Type ID string.");
	}

	std::vector<int16_t> results;

	// Extract all type IDs from comma-separated string.
	std::stringstream typesStream(types);
	std::string typeString;

	while (std::getline(typesStream, typeString, ',')) {
		int type = std::stoi(typeString);

		// Check type ID value.
		if (type < 0 || type > INT16_MAX) {
			throw std::out_of_range("Type ID negative or too big.");
		}

		// Add extracted Type IDs to the result vector.
		results.push_back(static_cast<int16_t>(type));
	}

	// Ensure that something was extracted.
	if (results.empty()) {
		throw std::length_error("Empty extracted Type ID vector.");
	}

	// Detect duplicates, which are not allowed.
	if (vectorDetectDuplicates(results)) {
		throw std::invalid_argument("Duplicate Type ID found.");
	}

	return (results);
}

static std::vector<OrderedInput> parseAugmentedTypeIDString(const std::string &types) {
	// Empty string, cannot be!
	if (types.empty()) {
		throw std::invalid_argument("Empty Augmented Type ID string.");
	}

	std::vector<OrderedInput> results;

	// Extract all type IDs from comma-separated string.
	std::stringstream typesStream(types);
	std::string typeString;

	while (std::getline(typesStream, typeString, ',')) {
		size_t modifierPosition = 0;
		int type = std::stoi(typeString, &modifierPosition);

		// Check type ID value.
		if (type < 0 || type > INT16_MAX) {
			throw std::out_of_range("Type ID negative or too big.");
		}

		int afterModuleOrder = -1;

		if (modifierPosition != typeString.length() && typeString.at(modifierPosition) == 'a') {
			const std::string orderString = typeString.substr(modifierPosition + 1);

			afterModuleOrder = std::stoi(orderString);

			// Check module ID value.
			if (afterModuleOrder < 0 || afterModuleOrder > INT16_MAX) {
				throw std::out_of_range("Module ID negative or too big.");
			}

			// Check that the module ID actually exists in the system.
			if (!caerMainloopModuleExists(static_cast<int16_t>(afterModuleOrder))) {
				throw std::out_of_range("Unknown module ID found.");
			}

			// Verify that the module ID belongs to a PROCESSOR module,
			// as only those can ever modify event streams and thus impose
			// an ordering on it and modules using it.
			if (!caerMainloopModuleIsType(static_cast<int16_t>(afterModuleOrder), CAER_MODULE_PROCESSOR)) {
				throw std::out_of_range("Module ID doesn't belong to a PROCESSOR type modules.");
			}
		}

		// Add extracted Type IDs to the result vector.
		results.push_back(OrderedInput(static_cast<int16_t>(type), static_cast<int16_t>(afterModuleOrder)));
	}

	// Ensure that something was extracted.
	if (results.empty()) {
		throw std::length_error("Empty extracted Augmented Type ID vector.");
	}

	// Detect duplicates, which are not allowed.
	// This because having the same type from the same source multiple times, even
	// if from different after-module points, would violate the event stream
	// uniqueness requirement for inputs and outputs, which is needed because it
	// would be impossible to distinguish packets inside a module if that were not
	// the case. For example we thus disallow 1[2a3,2a4] because inside the module
	// we would then have two packets with stream (1, 2), and no way to understand
	// which one was after being filtered by module ID 3 and which after module ID 4.
	// Augmenting the whole system to support such things is currently outside the
	// scope of this project, as it adds significant complexity with little or no
	// known gain, at least for the current use cases.
	if (vectorDetectDuplicates(results)) {
		throw std::invalid_argument("Duplicate Type ID found.");
	}

	return (results);
}

/**
 * moduleInput strings have the following format: different input IDs are
 * separated by a white-space character, for each input ID the used input
 * types are listed inside square-brackets [] and separated by a comma.
 * For example: "1[1,2,3] 2[2] 4[1,2]" means the inputs are: types 1,2,3
 * from module 1, type 2 from module 2, and types 1,2 from module 4.
 */
static void parseModuleInput(const std::string &inputDefinition,
	std::unordered_map<int16_t, std::vector<OrderedInput>> &resultMap, int16_t currId, const std::string &moduleName) {
	// Empty string, cannot be!
	if (inputDefinition.empty()) {
		throw std::invalid_argument("Empty 'moduleInput' attribute.");
	}

	try {
		std::regex wsRegex("\\s+"); // Whitespace(s) Regex.
		auto iter = std::sregex_token_iterator(inputDefinition.begin(), inputDefinition.end(), wsRegex, -1);

		while (iter != std::sregex_token_iterator()) {
			std::regex typeRegex("(\\d+)\\[(\\w+(?:,\\w+)*)\\]"); // Single Input Definition Regex.
			std::smatch matches;
			std::regex_match(iter->first, iter->second, matches, typeRegex);

			// Did we find the expected matches?
			if (matches.size() != 3) {
				throw std::length_error("Malformed input definition.");
			}

			// Get referenced module ID first.
			const std::string idString = matches[1];
			int id = std::stoi(idString);

			// Check module ID value.
			if (id < 0 || id > INT16_MAX) {
				throw std::out_of_range("Referenced module ID negative or too big.");
			}

			int16_t mId = static_cast<int16_t>(id);

			// If this module ID already exists in the map, it means there are
			// multiple definitions for the same ID; this is not allowed!
			if (resultMap.count(mId) == 1) {
				throw std::out_of_range("Duplicate referenced module ID found.");
			}

			// Check that the referenced module ID actually exists in the system.
			if (!caerMainloopModuleExists(mId)) {
				throw std::out_of_range("Unknown referenced module ID found.");
			}

			// Then get the various type IDs for that module.
			const std::string typeString = matches[2];

			resultMap[mId] = parseAugmentedTypeIDString(typeString);

			// Verify that the resulting event streams (sourceId, typeId) are
			// correct and do in fact exist.
			for (const auto &o : resultMap[mId]) {
				const auto foundEventStream = std::find(glMainloopData.streams.begin(), glMainloopData.streams.end(),
					ActiveStreams(mId, o.typeId));

				if (foundEventStream == glMainloopData.streams.end()) {
					// Specified event stream doesn't exist!
					throw std::out_of_range("Unknown event stream.");
				}
				else {
					// Event stream exists and is used here, mark it as used by
					// adding the current module ID to its users.
					foundEventStream->users.push_back(currId);
				}
			}

			iter++;
		}

		// inputDefinition was not empty, but we didn't manage to parse anything.
		if (resultMap.empty()) {
			throw std::length_error("Empty extracted input definition vector.");
		}
	}
	catch (const std::logic_error &ex) {
		// Clean map of any partial results on failure.
		resultMap.clear();

		boost::format exMsg = boost::format("Module '%s': Invalid 'moduleInput' attribute: %s") % moduleName
			% ex.what();
		throw std::logic_error(exMsg.str());
	}
}

static void checkInputDefinitionAgainstEventStreamIn(
	std::unordered_map<int16_t, std::vector<OrderedInput>> &inputDefinition, caerEventStreamIn eventStreams,
	size_t eventStreamsSize, const std::string &moduleName) {
	// Use parsed moduleInput configuration to get per-type count.
	std::unordered_map<int, int> typeCount;

	for (const auto &in : inputDefinition) {
		for (const auto &typeAndOrder : in.second) {
			typeCount[typeAndOrder.typeId]++;
		}
	}

	// Any_Type/Any_Number means there just needs to be something.
	if (eventStreamsSize == 1 && eventStreams[0].type == -1 && eventStreams[0].number == -1) {
		if (typeCount.empty()) {
			boost::format exMsg = boost::format(
				"Module '%s': ANY_TYPE/ANY_NUMBER definition has no connected input streams.") % moduleName;
			throw std::domain_error(exMsg.str());
		}

		return; // We're good!
	}

	// Any_Type/1 means there must be exactly one type with count of 1.
	if (eventStreamsSize == 1 && eventStreams[0].type == -1 && eventStreams[0].number == 1) {
		if (typeCount.size() != 1 || typeCount.cbegin()->second != 1) {
			boost::format exMsg = boost::format(
				"Module '%s': ANY_TYPE/1 definition requires 1 connected input stream of some type.") % moduleName;
			throw std::domain_error(exMsg.str());
		}

		return; // We're good!
	}

	// All other cases involve possibly multiple definitions with a defined type.
	// Since EventStreamIn definitions are strictly monotonic in this case, we
	// first check that the number of definitions and counted types match.
	if (typeCount.size() != eventStreamsSize) {
		boost::format exMsg = boost::format(
			"Module '%s': DEFINED_TYPE definitions require as many connected different types as specified.")
			% moduleName;
		throw std::domain_error(exMsg.str());
	}

	for (size_t i = 0; i < eventStreamsSize; i++) {
		// Defined_Type/Any_Number means there must be 1 or more such types present.
		if (eventStreams[i].type >= 0 && eventStreams[i].number == -1) {
			if (typeCount[eventStreams[i].type] < 1) {
				boost::format exMsg =
					boost::format(
						"Module '%s': DEFINED_TYPE/ANY_NUMBER definition requires at least one connected input stream of that type.")
						% moduleName;
				throw std::domain_error(exMsg.str());
			}
		}

		// Defined_Type/Defined_Number means there must be exactly as many such types present.
		if (eventStreams[i].type >= 0 && eventStreams[i].number > 0) {
			if (typeCount[eventStreams[i].type] != eventStreams[i].number) {
				boost::format exMsg =
					boost::format(
						"Module '%s': DEFINED_TYPE/DEFINED_NUMBER definition requires exactly that many connected input streams of that type.")
						% moduleName;
				throw std::domain_error(exMsg.str());
			}
		}
	}
}

static void updateInputDefinitionCopyNeeded(std::unordered_map<int16_t, std::vector<OrderedInput>> &inputDefinition,
	caerEventStreamIn eventStreams, size_t eventStreamsSize) {
	for (size_t i = 0; i < eventStreamsSize; i++) {
		// By default all inputs are marked as copyNeeded = false (readOnly = true).
		// So if we see any that are not readOnly, we must updated copyNeeded now.
		if (!eventStreams[i].readOnly) {
			// ANY_TYPE/ANY_NUMBER or 1, is the only definition in that case, and
			// means not-readOnly applies to all inputs.
			if (eventStreams[i].type == -1) {
				for (auto &in : inputDefinition) {
					for (auto &order : in.second) {
						order.copyNeeded = true;
					}
				}
			}

			// Else we have a DEFINED_TYPE, so this applies only to that type.
			for (auto &in : inputDefinition) {
				for (auto &order : in.second) {
					if (order.typeId == eventStreams[i].type) {
						order.copyNeeded = true;
					}
				}
			}
		}
	}
}

/**
 * Input modules _must_ have all their outputs well defined, or it becomes impossible
 * to validate and build the follow-up chain of processors and outputs correctly.
 * Now, this may not always be the case, for example File Input modules don't know a-priori
 * what their outputs are going to be (so they're declared with type set to -1).
 * For those cases, we need additional information, which we get from the 'moduleOutput'
 * configuration parameter that is required to be set in this case. For other input modules,
 * where the outputs are well known, like devices, this must not be set.
 */
static void parseModuleOutput(const std::string &moduleOutput, std::unordered_map<int16_t, ssize_t> &outputs,
	const std::string &moduleName) {
	try {
		std::vector<int16_t> results = parseTypeIDString(moduleOutput);

		for (auto type : results) {
			outputs[type] = -1;
		}
	}
	catch (const std::logic_error &ex) {
		boost::format exMsg = boost::format("Module '%s': Invalid 'moduleOutput' attribute: %s") % moduleName
			% ex.what();
		throw std::logic_error(exMsg.str());
	}
}

static void parseEventStreamOutDefinition(caerEventStreamOut eventStreams, size_t eventStreamsSize,
	std::unordered_map<int16_t, ssize_t> &outputs) {
	for (size_t i = 0; i < eventStreamsSize; i++) {
		outputs[eventStreams[i].type] = -1;
	}
}

/**
 * An active event stream knows its origin (sourceId) and all of its users
 * (users vector). If the sourceId appears again inside the users vector
 * (possible for PROCESSORS that generate output data), there is a cycle.
 * Also if any of the users appear multiple times within the users vector,
 * there is a cycle. Cycles are not allowed and will result in an exception!
 */
static void checkForActiveStreamCycles(ActiveStreams &stream) {
	const auto foundSourceID = std::find(stream.users.begin(), stream.users.end(), stream.sourceId);

	if (foundSourceID != stream.users.end()) {
		// SourceId found inside users vector!
		throw std::domain_error(
			boost::str(
				boost::format("Found cycle back to Source ID in stream (%d, %d).") % stream.sourceId % stream.typeId));
	}

	// Detect duplicates, which are not allowed, as they signal a cycle.
	if (vectorDetectDuplicates(stream.users)) {
		throw std::domain_error(
			boost::str(boost::format("Found cycles in stream (%d, %d).") % stream.sourceId % stream.typeId));
	}
}

static std::vector<int16_t> getAllUsersForStreamAfterID(const ActiveStreams &stream, int16_t afterCheckId) {
	std::vector<int16_t> tmpOrder;

	for (auto id : stream.users) {
		for (const auto &order : glMainloopData.modules[id].inputDefinition[stream.sourceId]) {
			if (order.typeId == stream.typeId && order.afterModuleId == afterCheckId) {
				tmpOrder.push_back(id);
			}
		}
	}

	std::sort(tmpOrder.begin(), tmpOrder.end());

	return (tmpOrder);
}

static void orderActiveStreamDeps(const ActiveStreams &stream, std::shared_ptr<DependencyNode> &deps, int16_t checkId,
	size_t depth, DependencyNode *parentLink, int16_t parentId) {
	std::vector<int16_t> users = getAllUsersForStreamAfterID(stream, checkId);

	if (!users.empty()) {
		deps = std::make_shared<DependencyNode>(depth, parentId, parentLink);

		for (auto id : users) {
			DependencyLink dep(id);

			orderActiveStreamDeps(stream, dep.next, id, depth + 1, deps.get(), dep.id);

			deps->links.push_back(dep);
		}
	}
}

static void printDeps(std::shared_ptr<DependencyNode> deps) {
	if (deps == nullptr) {
		return;
	}

	for (const auto &d : deps->links) {
		for (size_t i = 0; i < deps->depth; i++) {
			std::cout << "    ";
		}
		std::cout << d.id << std::endl;
		if (d.next) {
			printDeps(d.next);
		}
	}
}

// Search ID must not be a dummy node (-1).
static std::pair<DependencyNode *, DependencyLink *> IDExistsInDependencyTree(DependencyNode *root, int16_t searchId,
	bool directionUp) {
	if (searchId == -1) {
		throw std::out_of_range("Cannot search for dummy nodes. "
			"This should never happen, please report this to the developers and attach your XML configuration file.");
	}

	if (root == nullptr) {
		return (std::make_pair(nullptr, nullptr));
	}

	// Check if any of the nodes here match the searched for ID.
	// If no match, search in all children if we're going down, else
	// go up the hierarchy from parent to parent.
	for (auto &depLink : root->links) {
		if (depLink.id == searchId) {
			return (std::make_pair(root, &depLink));
		}

		if (!directionUp) {
			// Direction of recursion is down (children). Multiple children
			// need to be searched, one per DependencyLink.
			auto found = IDExistsInDependencyTree(depLink.next.get(), searchId, false);
			if (found.first != nullptr) {
				return (found);
			}
		}
	}

	if (directionUp) {
		// Direction of recursion is up (parents). There is only one parent
		// node per DependencyNode, so this is outside the above loop.
		auto found = IDExistsInDependencyTree(root->parentLink, searchId, true);
		if (found.first != nullptr) {
			return (found);
		}
	}

	// Nothing found!
	return (std::make_pair(nullptr, nullptr));
}

// Dummy nodes (-1) are ignored.
static std::vector<int16_t> getAllChildIDs(const DependencyNode *depNode) {
	std::vector<int16_t> results;

	if (depNode == nullptr) {
		return (results); // Empty vector.
	}

	for (const auto &depLink : depNode->links) {
		// Add current ID. Only if not -1 (dummy node). Those are skipped.
		if (depLink.id != -1) {
			results.push_back(depLink.id);
		}

		// Recurse down.
		std::vector<int16_t> recResults = getAllChildIDs(depLink.next.get());

		// Append recursion result to end of current results.
		results.insert(results.end(), recResults.begin(), recResults.end());
	}

	// Sort results.
	std::sort(results.begin(), results.end());

	return (results);
}

static void updateDepth(DependencyNode *depNode, size_t addToDepth) {
	if (depNode == nullptr) {
		return;
	}

	depNode->depth += addToDepth;

	for (auto &depLink : depNode->links) {
		updateDepth(depLink.next.get(), addToDepth);
	}
}

static void mergeDependencyTrees(std::shared_ptr<DependencyNode> destRoot,
	const std::shared_ptr<const DependencyNode> srcRoot) {
	std::queue<const DependencyNode *> queue;

	// Initialize traversal queue with level 0 content, always has one element.
	queue.push(srcRoot.get());

	while (!queue.empty()) {
		// Take out first element from queue.
		const DependencyNode *srcNode = queue.front();
		queue.pop();

		for (const auto &srcLink : srcNode->links) {
			// Process element. First we check if this module ID already exists in
			// the merge destination tree.
			auto destNodeLink = IDExistsInDependencyTree(destRoot.get(), srcLink.id, false);

			if (destNodeLink.first != nullptr) {
				// It exists! To ensure the resulting tree after insertion is
				// good, we first search for any possible dependency cycles that
				// could arise between multiple event streams. To do so, we check
				// if any of the source link's children (modules that depend on
				// that particular module ID) exist in the destination tree as
				// any direct parent of that particular module ID.
				std::vector<int16_t> moduleIDsToCheck = getAllChildIDs(srcLink.next.get());

				for (auto modId : moduleIDsToCheck) {
					auto checkNodeLink = IDExistsInDependencyTree(destNodeLink.first->parentLink, modId, true);

					if (checkNodeLink.first != nullptr) {
						// Dependency cycle found!
						boost::format exMsg =
							boost::format(
								"Found dependency cycle involving multiple streams between modules '%s' (ID %d) and '%s' (ID %d).")
								% glMainloopData.modules[srcLink.id].name % srcLink.id
								% glMainloopData.modules[modId].name % modId;
						throw std::domain_error(exMsg.str());
					}
				}

				// Now we know there cannot be cycles. This is important so that
				// the possible modifications to the tree that may be done to keep
				// the dependencies satisfied cannot result in an invalid tree.
				// So the ID exists already, which means we have to ensure both its
				// previous as well as its new dependencies hold after this operation.
				// We do that by checking the source node's parent ID in the destination
				// tree (exists or root): if we find it in a level of the tree that
				// is higher than here, it means the dependency from the source tree
				// is still kept and we're done. If on the other hand we find it on
				// the same level or any lower one, we must move this node down by N
				// levels, so that it is in the level below where we found the parent
				// ID, and the dependency then holds again. The final order will be
				// BFS (level-based), so it's enough to make dependencies hold between
				// levels, it's not necessary to move nodes between branches; we just
				// need to add dummy nodes to lengthen the current branch. Dummy nodes
				// have only one link with ID of -1, so they can be skipped easily.
				if (srcNode->parentId == -1) {
					// If the source node is the root node (only node in source tree
					// with a parent ID of -1), then we're good, it has no dependencies
					// that need to be verified.
					continue;
				}

				auto destParentNodeLink = IDExistsInDependencyTree(destRoot.get(), srcNode->parentId, false);

				// Parent is on a higher level, we're good, dependency holds!
				if (destParentNodeLink.first->depth < destNodeLink.first->depth) {
					continue;
				}

				// Parent is on same level or below, must insert dummy nodes.
				size_t numDummyNodes = (destParentNodeLink.first->depth - destNodeLink.first->depth);
				size_t moveDepth = numDummyNodes + 1;
				size_t currDepth = destNodeLink.first->depth;

				// First dummy is in the current node itself, where we change ID to -1.
				destNodeLink.second->id = -1;
				std::shared_ptr<DependencyNode> oldNextNode = destNodeLink.second->next;
				std::shared_ptr<DependencyNode> currNextNode = std::make_shared<DependencyNode>(++currDepth, -1,
					destNodeLink.first);
				destNodeLink.second->next = currNextNode;

				// Then we add any further needed dummy-only nodes.
				while (numDummyNodes-- > 0) {
					DependencyLink dummyDepLink(-1);

					currNextNode = std::make_shared<DependencyNode>(++currDepth, -1, currNextNode.get());
					dummyDepLink.next = currNextNode;

					currNextNode->links.push_back(dummyDepLink);
				}

				// Now currNextNode points to an empty node, where we add the
				// original ID we wanted to move down.
				DependencyLink origDepLink(srcLink.id);
				origDepLink.next = oldNextNode;

				currNextNode->links.push_back(origDepLink);

				// All insertions done, now we need to make sure the rest of the
				// tree we just moved down is still good: the parentLink of the
				// next node down needs to be updated, the IDs are still fine,
				// and all the depths have to be augmented by N.
				if (oldNextNode != nullptr) {
					oldNextNode->parentLink = currNextNode.get();
				}

				updateDepth(oldNextNode.get(), moveDepth);
			}
			else {
				// If it doesn't exist, we want to add it to the parent as another
				// child. Due to us going down the tree breadth-first, we can be
				// sure the parent ID exists (as previous calls either discovered
				// it or added it), so we just search for it and add the child.
				// The only exception is the root node, which has no parent, and
				// gets added to the destination root node in this case (level 0).
				if (srcNode->parentLink == nullptr) {
					// Root node in src, doesn't exist in dest, add at top level.
					destRoot->links.push_back(DependencyLink(srcLink.id));

					std::sort(destRoot->links.begin(), destRoot->links.end());
				}
				else {
					// Normal node in src, doesn't exist in dest, find parent, which
					// must exist, and add to it.
					auto destParentNodeLink = IDExistsInDependencyTree(destRoot.get(), srcNode->parentId, false);

					// The parent's DependencyLink.next can be NULL the first time any child
					// is added to that particular ID.
					if (destParentNodeLink.second->next == nullptr) {
						destParentNodeLink.second->next = std::make_shared<DependencyNode>(
							destParentNodeLink.first->depth + 1, destParentNodeLink.second->id,
							destParentNodeLink.first);
					}

					destParentNodeLink.second->next->links.push_back(DependencyLink(srcLink.id));

					std::sort(destParentNodeLink.second->next->links.begin(),
						destParentNodeLink.second->next->links.end());
				}
			}
		}

		// Continue traversal.
		for (const auto &srcLink : srcNode->links) {
			if (srcLink.next != nullptr) {
				queue.push(srcLink.next.get());
			}
		}
	}
}

static void mergeActiveStreamDeps() {
	std::shared_ptr<DependencyNode> mergeResult = std::make_shared<DependencyNode>(0, -1, nullptr);

	for (const auto &st : glMainloopData.streams) {
		// Merge the current stream's dependency tree to the global tree.
		mergeDependencyTrees(mergeResult, st.dependencies);
	}

	printDeps(mergeResult);

	// Now generate the final traversal order over all modules by going
	// through the merged tree in BFS (level) order.
	std::vector<int16_t> finalModuleOrder;

	std::queue<const DependencyNode *> queue;

	// Initialize traversal queue with level 0 content, always has one element.
	queue.push(mergeResult.get());

	while (!queue.empty()) {
		// Take out first element from queue.
		const DependencyNode *node = queue.front();
		queue.pop();

		for (const auto &link : node->links) {
			// Ignore dummy nodes (-1).
			if (link.id != -1) {
				finalModuleOrder.push_back(link.id);
			}
		}

		// Continue traversal.
		for (const auto &link : node->links) {
			if (link.next != nullptr) {
				queue.push(link.next.get());
			}
		}
	}

	// TODO: the final traversal order shoult try to take into account data copies.
	// To do so, for each depth-level, module IDs should be ordered by how many
	// inputs with copyNeeded=true they have. If same number, simple integer sort.
	// This might be implemented efficiently with an std::multimap.

	// Publish result to global module execution order.
	for (auto id : finalModuleOrder) {
		glMainloopData.globalExecution.push_back(glMainloopData.modules[id]);
	}
}

static void updateStreamUsersWithGlobalExecutionOrder() {
	for (auto &stream : glMainloopData.streams) {
		// Reorder list of stream users to follow the same ordering as
		// the global execution order resulting from the merged dep-trees.
		std::unordered_set<int16_t> userSet;

		// First put all users into a set for fast existence tests.
		for (auto &user : stream.users) {
			userSet.insert(user);
		}

		// Then clear users vector.
		stream.users.clear();

		// And now repopulate it in the right order: iterate through the
		// whole global execution order, and if an ID exists in the local
		// set, push it to the users vector.
		for (const auto &globalMod : glMainloopData.globalExecution) {
			int16_t globalModID = globalMod.get().id;

			if (userSet.count(globalModID) == 1) {
				stream.users.push_back(globalModID);
			}
		}
	}
}

static void buildConnectivity() {
	struct ModuleSlot {
		int16_t typeId;
		int16_t afterModuleId;
		size_t index;

		ModuleSlot(int16_t t, int16_t a, size_t i) :
				typeId(t),
				afterModuleId(a),
				index(i) {
		}

		// Comparison operators (std::find() support).
		bool operator==(const ModuleSlot &rhs) const noexcept {
			return (typeId == rhs.typeId && afterModuleId == rhs.afterModuleId);
		}
	};

	std::unordered_map<int16_t, std::vector<ModuleSlot>> streamIndexes;

	size_t nextFreeSlot = 0;

	for (auto &m : glMainloopData.globalExecution) {
		// INPUT module or PROCESSOR with data output defined.
		if (m.get().libraryInfo->type == CAER_MODULE_INPUT
			|| (m.get().libraryInfo->type == CAER_MODULE_PROCESSOR && m.get().libraryInfo->outputStreams != NULL)) {
			for (auto &o : m.get().outputs) {
				if (caerMainloopStreamExists(m.get().id, o.first)) {
					// Update active outputs with a viable index.
					o.second = static_cast<ssize_t>(nextFreeSlot);

					// Put combination into indexes table.
					streamIndexes[m.get().id].push_back(ModuleSlot(o.first, -1, nextFreeSlot));

					// Increment next free index.
					nextFreeSlot++;
				}
			}
		}

		// PROCESSOR module or OUTPUT (both must have data input defined).
		if (m.get().libraryInfo->type == CAER_MODULE_PROCESSOR || m.get().libraryInfo->type == CAER_MODULE_OUTPUT) {
			for (const auto &inputDef : m.get().inputDefinition) {
				int16_t sourceId = inputDef.first;

				for (const auto &orderIn : inputDef.second) {
					if (orderIn.copyNeeded) {
						// Copy needed (in theory), to make sure we first check if
						// any other modules in this stream that come later on have
						// an input definition that requires exactly this data.
						// If yes, we must do the copy. Tables updated accordingly.
						const auto streamUsers = std::find(glMainloopData.streams.begin(), glMainloopData.streams.end(),
							ActiveStreams(sourceId, orderIn.typeId));

						if (streamUsers == glMainloopData.streams.end()) {
							boost::format exMsg =
								boost::format(
									"Cannot find valid active event stream for module '%s' (ID %d) on input definition [s: %d, t: %d, a: %d]. "
										"This should never happen, please report this to the developers and attach your XML configuration file.")
									% m.get().name % m.get().id % sourceId % orderIn.typeId % orderIn.afterModuleId;
							throw std::out_of_range(exMsg.str());
						}

						// Find current module ID in stream users and get iterator.
						auto currUser = std::find(streamUsers->users.begin(), streamUsers->users.end(), m.get().id);

						if (currUser == streamUsers->users.end()) {
							boost::format exMsg =
								boost::format(
									"Cannot find valid user in event stream for module '%s' (ID %d) on input definition [s: %d, t: %d, a: %d]. "
										"This should never happen, please report this to the developers and attach your XML configuration file.")
									% m.get().name % m.get().id % sourceId % orderIn.typeId % orderIn.afterModuleId;
							throw std::out_of_range(exMsg.str());
						}

						// Advance iterator to next position, since we want to check
						// all modules that come after this one in order.
						currUser++;

						// Now search in the remaining modules if any need the exact
						// same data (sourceId, typeId, afterModuleId) that the
						// current module does. If yes, it will have to be copied.
						const auto nextUser =
							std::find_if(currUser, streamUsers->users.end(),
								[sourceId, &orderIn](const int16_t userId) {
									const auto &nextUserInputDef = glMainloopData.modules[userId].inputDefinition[sourceId];

									return (std::find_if(nextUserInputDef.begin(), nextUserInputDef.end(),
											[&orderIn](const OrderedInput &nextUserOrderIn) {
												return (nextUserOrderIn.typeId == orderIn.typeId && nextUserOrderIn.afterModuleId == orderIn.afterModuleId);
											}) != nextUserInputDef.end());
								});

						// Get old slot from indexes.
						auto &indexes = streamIndexes[sourceId];

						const auto idx = std::find(indexes.begin(), indexes.end(),
							ModuleSlot(orderIn.typeId, orderIn.afterModuleId, 0));

						if (idx == indexes.end()) {
							boost::format exMsg =
								boost::format(
									"Cannot find valid index slot for module '%s' (ID %d) on input definition [s: %d, t: %d, a: %d]. "
										"This should never happen, please report this to the developers and attach your XML configuration file.")
									% m.get().name % m.get().id % sourceId % orderIn.typeId % orderIn.afterModuleId;
							throw std::out_of_range(exMsg.str());
						}

						if (nextUser == streamUsers->users.end()) {
							// Nobody else needs this data, use it directly.
							// Update active inputs with a viable index.
							m.get().inputs.push_back(std::make_pair(idx->index, -1));

							// Put combination into indexes table.
							indexes.push_back(ModuleSlot(orderIn.typeId, m.get().id, idx->index));
						}
						else {
							// Others need this data, copy it.
							// Update active inputs with a viable index, use the
							// next free one and set copyFrom index to the old one.
							m.get().inputs.push_back(std::make_pair(nextFreeSlot, idx->index));

							// Put combination into indexes table.
							indexes.push_back(ModuleSlot(orderIn.typeId, m.get().id, nextFreeSlot));

							// Increment next free index.
							nextFreeSlot++;

							// Globally count number of data copies needed in a run.
							glMainloopData.copyCount++;
						}
					}
					else {
						// Copy not needed, just use index from indexes table.
						const auto &indexes = streamIndexes[sourceId];

						const auto idx = std::find(indexes.begin(), indexes.end(),
							ModuleSlot(orderIn.typeId, orderIn.afterModuleId, 0));

						if (idx == indexes.end()) {
							boost::format exMsg =
								boost::format(
									"Cannot find valid index slot for module '%s' (ID %d) on input definition [s: %d, t: %d, a: %d]. "
										"This should never happen, please report this to the developers and attach your XML configuration file.")
									% m.get().name % m.get().id % sourceId % orderIn.typeId % orderIn.afterModuleId;
							throw std::out_of_range(exMsg.str());
						}

						// Update active inputs with a viable index.
						m.get().inputs.push_back(std::make_pair(idx->index, -1));
					}

					std::sort(m.get().inputs.begin(), m.get().inputs.end());
				}
			}
		}
	}
}

// Small helper to unload libraries on error.
static void unloadLibrary(ModuleLibrary &moduleLibrary) {
#if BOOST_HAS_DLL_LOAD
	moduleLibrary.unload();
#else
	dlclose(moduleLibrary);
#endif
}

static void cleanupGlobals() {
	for (auto &m : glMainloopData.modules) {
		m.second.libraryInfo = nullptr;
		unloadLibrary(m.second.libraryHandle);
	}

	glMainloopData.modules.clear();
	glMainloopData.streams.clear();
	glMainloopData.globalExecution.clear();
}

static int caerMainloopRunner(void) {
	// At this point configuration is already loaded, so let's see if everything
	// we need to build and run a mainloop is really there.
	// Each node in the root / is a module, with a short-name as node-name,
	// an ID (16-bit integer, "moduleId") as attribute, and the module's library
	// (string, "moduleLibrary") as attribute.
	size_t modulesSize = 0;
	sshsNode *modules = sshsNodeGetChildren(glMainloopData.configNode, &modulesSize);
	if (modules == nullptr || modulesSize == 0) {
		// Empty configuration.
		log(logLevel::ERROR, "Mainloop", "No modules configuration found.");
		return (EXIT_FAILURE);
	}

	for (size_t i = 0; i < modulesSize; i++) {
		sshsNode module = modules[i];
		const std::string moduleName = sshsNodeGetName(module);

		if (moduleName == "caer") {
			// Skip system configuration, not a module.
			continue;
		}

		if (!sshsNodeAttributeExists(module, "moduleId", SSHS_SHORT)
			|| !sshsNodeAttributeExists(module, "moduleLibrary", SSHS_STRING)) {
			// Missing required attributes, notify and skip.
			log(logLevel::ERROR, "Mainloop",
				"Module '%s': Configuration is missing core attributes 'moduleId' and/or 'moduleLibrary'.",
				moduleName.c_str());
			continue;
		}

		int16_t moduleId = sshsNodeGetShort(module, "moduleId");

		char *moduleLibraryC = sshsNodeGetString(module, "moduleLibrary");
		const std::string moduleLibrary = moduleLibraryC;
		free(moduleLibraryC);

		ModuleInfo info = ModuleInfo(moduleId, moduleName, module, moduleLibrary);

		// Put data into an unordered map that holds all valid modules.
		// This also ensure the numerical ID is unique!
		auto result = glMainloopData.modules.insert(std::make_pair(info.id, info));
		if (!result.second) {
			// Failed insertion, key (ID) already exists!
			log(logLevel::ERROR, "Mainloop", "Module '%s': Module with ID %d already exists.", moduleName.c_str(),
				info.id);
			continue;
		}
	}

	// Free temporary configuration nodes array.
	free(modules);

	// At this point we have a map with all the valid modules and their info.
	// If that map is empty, there was nothing valid present.
	if (glMainloopData.modules.empty()) {
		log(logLevel::ERROR, "Mainloop", "No valid modules configuration found.");
		return (EXIT_FAILURE);
	}
	else {
		log(logLevel::NOTICE, "Mainloop", "%d modules found.", glMainloopData.modules.size());
	}

	// Let's load the module libraries and get their internal info.
	for (auto &m : glMainloopData.modules) {
		// For each module, we search if a path exists to load it from.
		// If yes, we do so. The various OS's shared library load mechanisms
		// will keep track of reference count if same module is loaded
		// multiple times.
		boost::filesystem::path modulePath;

		for (const auto &p : modulePaths) {
			if (m.second.library == p.stem().string()) {
				// Found a module with same name!
				modulePath = p;
			}
		}

		if (modulePath.empty()) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': No module library '%s' found.", m.second.name.c_str(),
				m.second.library.c_str());
			continue;
		}

		log(logLevel::NOTICE, "Mainloop", "Module '%s': Loading module library '%s'.", m.second.name.c_str(),
			modulePath.c_str());

#if BOOST_HAS_DLL_LOAD
		ModuleLibrary moduleLibrary;
		try {
			moduleLibrary.load(modulePath.c_str(), boost::dll::load_mode::rtld_now);
		}
		catch (const std::exception &ex) {
			// Failed to load shared library!
			log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to load library '%s', error: '%s'.",
				m.second.name.c_str(), modulePath.c_str(), ex.what());
			continue;
		}

		caerModuleInfo (*getInfo)(void);
		try {
			getInfo = moduleLibrary.get<caerModuleInfo(void)>("caerModuleGetInfo");
		}
		catch (const std::exception &ex) {
			// Failed to find symbol in shared library!
			log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to find symbol in library '%s', error: '%s'.",
				m.second.name.c_str(), modulePath.c_str(), ex.what());
			unloadLibrary(moduleLibrary);
			continue;
		}
#else
		void *moduleLibrary = dlopen(modulePath.c_str(), RTLD_NOW);
		if (moduleLibrary == nullptr) {
			// Failed to load shared library!
			log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to load library '%s', error: '%s'.",
				m.second.name.c_str(), modulePath.c_str(), dlerror());
			continue;
		}

		caerModuleInfo (*getInfo)(void) = (caerModuleInfo (*)(void)) dlsym(moduleLibrary, "caerModuleGetInfo");
		if (getInfo == nullptr) {
			// Failed to find symbol in shared library!
			log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to find symbol in library '%s', error: '%s'.",
				m.second.name.c_str(), modulePath.c_str(), dlerror());
			unloadLibrary(moduleLibrary);
			continue;
		}
#endif

		caerModuleInfo info = (*getInfo)();
		if (info == nullptr) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': Failed to get info from library '%s', error: '%s'.",
				m.second.name.c_str(), modulePath.c_str(), dlerror());
			unloadLibrary(moduleLibrary);
			continue;
		}

		try {
			// Check that the modules respect the basic I/O definition requirements.
			checkInputOutputStreamDefinitions(info);

			// Check I/O event stream definitions for correctness.
			if (info->inputStreams != nullptr) {
				checkInputStreamDefinitions(info->inputStreams, info->inputStreamsSize);
			}

			if (info->outputStreams != nullptr) {
				checkOutputStreamDefinitions(info->outputStreams, info->outputStreamsSize);
			}

			checkModuleInputOutput(info, m.second.configNode);
		}
		catch (const std::logic_error &ex) {
			log(logLevel::ERROR, "Mainloop", "Module '%s': %s", m.second.name.c_str(), ex.what());
			unloadLibrary(moduleLibrary);
			continue;
		}

		m.second.libraryHandle = moduleLibrary;
		m.second.libraryInfo = info;
	}

	// If any modules failed to load, exit program now. We didn't do that before, so that we
	// could run through all modules and check them all in one go.
	for (const auto &m : glMainloopData.modules) {
		if (m.second.libraryInfo == nullptr) {
			// Clean up generated data on failure.
			cleanupGlobals();

			log(logLevel::ERROR, "Mainloop", "Errors in module library loading.");

			return (EXIT_FAILURE);
		}
	}

	std::vector<std::reference_wrapper<ModuleInfo>> inputModules;
	std::vector<std::reference_wrapper<ModuleInfo>> outputModules;
	std::vector<std::reference_wrapper<ModuleInfo>> processorModules;

	// Now we must parse, validate and create the connectivity map between modules.
	// First we sort the modules into their three possible categories.
	for (auto &m : glMainloopData.modules) {
		if (m.second.libraryInfo->type == CAER_MODULE_INPUT) {
			inputModules.push_back(m.second);
		}
		else if (m.second.libraryInfo->type == CAER_MODULE_OUTPUT) {
			outputModules.push_back(m.second);
		}
		else {
			processorModules.push_back(m.second);
		}
	}

	// Simple sanity check: at least 1 input and 1 output module must exist
	// to have a minimal, working system.
	if (inputModules.size() < 1 || outputModules.size() < 1) {
		// Clean up generated data on failure.
		cleanupGlobals();

		log(logLevel::ERROR, "Mainloop", "No input or output modules defined.");

		return (EXIT_FAILURE);
	}

	try {
		// Then we parse all the 'moduleOutput' configurations for certain INPUT
		// and PROCESSOR modules that have an ANY type declaration. If the types
		// are instead well defined, we parse the event stream definition directly.
		// We do this first so we can build up the map of all possible active event
		// streams, which we then can use for checking 'moduleInput' for correctness.
		for (const auto &m : boost::join(inputModules, processorModules)) {
			caerModuleInfo info = m.get().libraryInfo;

			if (info->outputStreams != nullptr) {
				// ANY type declaration.
				if (info->outputStreamsSize == 1 && info->outputStreams[0].type == -1) {
					char *moduleOutput = sshsNodeGetString(m.get().configNode, "moduleOutput");
					const std::string outputDefinition = moduleOutput;
					free(moduleOutput);

					parseModuleOutput(outputDefinition, m.get().outputs, m.get().name);
				}
				else {
					parseEventStreamOutDefinition(info->outputStreams, info->outputStreamsSize, m.get().outputs);
				}

				// Now add discovered outputs to possible active streams.
				for (const auto &o : m.get().outputs) {
					ActiveStreams st = ActiveStreams(m.get().id, o.first);

					// Store if stream originates from a PROCESSOR (default from INPUT).
					if (info->type == CAER_MODULE_PROCESSOR) {
						st.isProcessor = true;
					}

					glMainloopData.streams.push_back(st);
				}
			}
		}

		// Then we parse all the 'moduleInput' configurations for OUTPUT and
		// PROCESSOR modules, which we can now verify against possible streams.
		for (const auto &m : boost::join(outputModules, processorModules)) {
			char *moduleInput = sshsNodeGetString(m.get().configNode, "moduleInput");
			const std::string inputDefinition = moduleInput;
			free(moduleInput);

			parseModuleInput(inputDefinition, m.get().inputDefinition, m.get().id, m.get().name);

			checkInputDefinitionAgainstEventStreamIn(m.get().inputDefinition, m.get().libraryInfo->inputStreams,
				m.get().libraryInfo->inputStreamsSize, m.get().name);

			updateInputDefinitionCopyNeeded(m.get().inputDefinition, m.get().libraryInfo->inputStreams,
				m.get().libraryInfo->inputStreamsSize);
		}

		// At this point we can prune all event streams that are not marked active,
		// since this means nobody is referring to them.
		glMainloopData.streams.erase(
			std::remove_if(glMainloopData.streams.begin(), glMainloopData.streams.end(),
				[](const ActiveStreams &st) {return (st.users.empty());}), glMainloopData.streams.end());

		// If all event streams of an INPUT module are dropped, the module itself
		// is unconnected and useless, and that is a user configuration error.
		for (const auto &m : inputModules) {
			int16_t id = m.get().id;

			const auto iter = std::find_if(glMainloopData.streams.begin(), glMainloopData.streams.end(),
				[id](const ActiveStreams &st) {return (st.sourceId == id);});

			// No stream found for source ID corresponding to this module's ID.
			if (iter == glMainloopData.streams.end()) {
				boost::format exMsg = boost::format(
					"Module '%s': INPUT module is not connected to anything and will not be used.") % m.get().name;
				throw std::domain_error(exMsg.str());
			}
		}

		// At this point we know that all active event stream do come from some
		// active input module. We also know all of its follow-up users. Now those
		// user can specify data dependencies on that event stream, by telling after
		// which module they want to tap the stream for themselves. The only check
		// done on that specification up till now is that the module ID is valid and
		// exists, but it could refer to a module that's completely unrelated with
		// this event stream, and as such cannot be a valid point to tap into it.
		// We detect this now, as we have all the users of a stream listed in it.
		for (const auto &st : glMainloopData.streams) {
			for (auto id : st.users) {
				for (const auto &order : glMainloopData.modules[id].inputDefinition[st.sourceId]) {
					if (order.typeId == st.typeId && order.afterModuleId != -1) {
						// For each corresponding afterModuleId (that is not -1
						// which refers to original source ID and is always valid),
						// we check if we can find that ID inside of the stream's
						// users. If yes, then that's a valid tap point and we're
						// good; if no, this is a user configuration error.
						const auto iter = std::find_if(st.users.begin(), st.users.end(),
							[&order](int16_t moduleId) {return (order.afterModuleId == moduleId);});

						if (iter == st.users.end()) {
							boost::format exMsg =
								boost::format(
									"Module '%s': found invalid afterModuleID declaration of '%d' for stream (%d, %d); referenced module is not part of stream.")
									% glMainloopData.modules[id].name % order.afterModuleId % st.sourceId % st.typeId;
							throw std::domain_error(exMsg.str());
						}

						// Now we do a second check: the module is part of the stream,
						// which means it does indeed take in such data itself. But it
						// only makes sense to use as it as afterModuleID if that data
						// got modified by this module, if nothing is modified, then
						// other modules should refer to whatever prior module is
						// actually changing or generating data!
						for (const auto &orderAfter : glMainloopData.modules[order.afterModuleId].inputDefinition[st
							.sourceId]) {
							if (orderAfter.typeId == order.typeId && !orderAfter.copyNeeded) {
								boost::format exMsg =
									boost::format(
										"Module '%s': found invalid afterModuleID declaration of '%d' for stream (%d, %d); referenced module does not modify this event stream.")
										% glMainloopData.modules[id].name % order.afterModuleId % st.sourceId
										% st.typeId;
								throw std::domain_error(exMsg.str());
							}
						}
					}
				}
			}
		}

		// Detect cycles inside an active event stream.
		for (auto &st : glMainloopData.streams) {
			checkForActiveStreamCycles(st);
		}

		// Order event stream users according to the configuration.
		// Add single root node/link manually here, before recursion.
		for (auto &st : glMainloopData.streams) {
			st.dependencies = std::make_shared<DependencyNode>(0, -1, nullptr);

			DependencyLink depRoot(st.sourceId);

			orderActiveStreamDeps(st, depRoot.next, -1, 1, st.dependencies.get(), depRoot.id);

			st.dependencies->links.push_back(depRoot);
		}

		// Now merge all streams and their users into one global order over
		// all modules. If this cannot be resolved, wrong connections or a
		// cycle involving multiple streams are present.
		mergeActiveStreamDeps();

		// Reorder stream.users to follow global execution order.
		updateStreamUsersWithGlobalExecutionOrder();

		// There's multiple ways now to build the full connectivity graph once we
		// have all the starting points. Since we do have a global execution order
		// (see above), we can just visit the modules in that order and build
		// all the input and output connections.
		// TODO: detect processors that serve no purpose, ie. no output or unused
		// output, as well as no further users of modified inputs.
		buildConnectivity();
	}
	catch (const std::exception &ex) {
		// Cleanup modules and streams on exit.
		cleanupGlobals();

		log(logLevel::ERROR, "Mainloop", ex.what());

		return (EXIT_FAILURE);
	}

	// Debug output.
	std::cout << "Global order: ";
	for (const auto &m : glMainloopData.globalExecution) {
		std::cout << m.get().id << ", ";
	}
	std::cout << std::endl;

	for (const auto &st : glMainloopData.streams) {
		std::cout << "(" << st.sourceId << ", " << st.typeId << ") - IS_PROC: " << st.isProcessor << " - ";
		for (auto mid : st.users) {
			std::cout << mid << ", ";
		}
		std::cout << std::endl;
		printDeps(st.dependencies);
	}

	std::cout << "Global copy count: " << glMainloopData.copyCount << std::endl;

	for (const auto &m : glMainloopData.globalExecution) {
		std::cout << m.get().id << "-MOD: " << m.get().libraryInfo->type << " - " << m.get().name << std::endl;

		for (const auto &i : m.get().inputs) {
			std::cout << " --> " << i.first << " - " << i.second << " - POS_IN" << std::endl;
		}

		for (const auto &o : m.get().outputs) {
			std::cout << " --> " << o.first << " - " << o.second << " - POS_OUT" << std::endl;
		}
	}

//	// Enable memory recycling.
//	utarray_new(glMainloopData.memoryToFree, &ut_genericFree_icd);
//
//	// Store references to all active modules, separated by type.
//	utarray_new(glMainloopData.inputModules, &ut_ptr_icd);
//	utarray_new(glMainloopData.outputModules, &ut_ptr_icd);
//	utarray_new(glMainloopData.processorModules, &ut_ptr_icd);
//
//	// TODO: init modules.
//
//	// After each successful main-loop run, free the memory that was
//	// accumulated for things like packets, valid only during the run.
//	struct genericFree *memFree = NULL;
//	while ((memFree = (struct genericFree *) utarray_next(glMainloopData.memoryToFree, memFree)) != NULL) {
//		memFree->func(memFree->memPtr);
//	}
//	utarray_clear(glMainloopData.memoryToFree);
//
//	// Shutdown all modules.
//	for (caerModuleData m = glMainloopData.modules; m != NULL; m = m->hh.next) {
//		sshsNodePutBool(m->moduleNode, "running", false);
//	}
//
//	// Run through the loop one last time to correctly shutdown all the modules.
//	// TODO: exit modules.
//
//	// Do one last memory recycle run.
//	struct genericFree *memFree = NULL;
//	while ((memFree = (struct genericFree *) utarray_next(glMainloopData.memoryToFree, memFree)) != NULL) {
//		memFree->func(memFree->memPtr);
//	}
//
//	// Clear and free all allocated arrays.
//	utarray_free(glMainloopData.memoryToFree);
//
//	utarray_free(glMainloopData.inputModules);
//	utarray_free(glMainloopData.outputModules);
//	utarray_free(glMainloopData.processorModules);

	log(logLevel::INFO, "Mainloop", "Started successfully.");

	// If no data is available, sleep for a millisecond to avoid wasting resources.
	// Wait for someone to toggle the module shutdown flag OR for the loop
	// itself to signal termination.
	size_t sleepCount = 0;

	while (glMainloopData.running.load(std::memory_order_relaxed)) {
		// Run only if data available to consume, else sleep. But make a run
		// anyway each second, to detect new devices for example.
		if (glMainloopData.dataAvailable.load(std::memory_order_acquire) > 0 || sleepCount > 1000) {
			sleepCount = 0;

			// TODO: execute modules.
		}
		else {
			sleepCount++;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	// Cleanup modules and streams on exit.
	cleanupGlobals();

	log(logLevel::INFO, "Mainloop", "Terminated successfully.");

	return (EXIT_SUCCESS);
}

void caerMainloopDataNotifyIncrease(void *p) {
	UNUSED_ARGUMENT(p);

	glMainloopData.dataAvailable.fetch_add(1, std::memory_order_release);
}

void caerMainloopDataNotifyDecrease(void *p) {
	UNUSED_ARGUMENT(p);

	// No special memory order for decrease, because the acquire load to even start running
	// through a mainloop already synchronizes with the release store above.
	glMainloopData.dataAvailable.fetch_sub(1, std::memory_order_relaxed);
}

bool caerMainloopModuleExists(int16_t id) {
	return (glMainloopData.modules.count(id) == 1);
}

bool caerMainloopModuleIsType(int16_t id, enum caer_module_type type) {
	return (glMainloopData.modules.at(id).libraryInfo->type == type);
}

bool caerMainloopStreamExists(int16_t sourceId, int16_t typeId) {
	return (std::find(glMainloopData.streams.begin(), glMainloopData.streams.end(), ActiveStreams(sourceId, typeId))
		!= glMainloopData.streams.end());
}

// Only use this inside the mainloop-thread, not inside any other thread,
// like additional data acquisition threads or output threads.
void caerMainloopFreeAfterLoop(void (*func)(void *mem), void *memPtr) {

}

static inline caerModuleData findSourceModule(uint16_t sourceID) {

}

sshsNode caerMainloopGetSourceNode(uint16_t sourceID) {
	caerModuleData moduleData = findSourceModule(sourceID);
	if (moduleData == NULL) {
		return (NULL);
	}

	return (moduleData->moduleNode);
}

sshsNode caerMainloopGetSourceInfo(uint16_t sourceID) {
	sshsNode sourceNode = caerMainloopGetSourceNode(sourceID);
	if (sourceNode == NULL) {
		return (NULL);
	}

	// All sources have a sub-node in SSHS called 'sourceInfo/'.
	return (sshsGetRelativeNode(sourceNode, "sourceInfo/"));
}

void *caerMainloopGetSourceState(uint16_t sourceID) {
	caerModuleData moduleData = findSourceModule(sourceID);
	if (moduleData == NULL) {
		return (NULL);
	}

	return (moduleData->moduleState);
}

void caerMainloopResetInputs(uint16_t sourceID) {

}

void caerMainloopResetOutputs(uint16_t sourceID) {

}

void caerMainloopResetProcessors(uint16_t sourceID) {

}

static void caerMainloopSignalHandler(int signal) {
	UNUSED_ARGUMENT(signal);

	// Simply set all the running flags to false on SIGTERM and SIGINT (CTRL+C) for global shutdown.
	glMainloopData.systemRunning.store(false);
	glMainloopData.running.store(false);
}

static void caerMainloopSystemRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);
	UNUSED_ARGUMENT(userData);
	UNUSED_ARGUMENT(changeValue);

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
		glMainloopData.systemRunning.store(false);
		glMainloopData.running.store(false);
	}
}

static void caerMainloopRunningListener(sshsNode node, void *userData, enum sshs_node_attribute_events event,
	const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue) {
	UNUSED_ARGUMENT(node);
	UNUSED_ARGUMENT(userData);

	if (event == SSHS_ATTRIBUTE_MODIFIED && changeType == SSHS_BOOL && caerStrEquals(changeKey, "running")) {
		glMainloopData.running.store(changeValue.boolean);
	}
}
