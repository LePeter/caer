#ifndef SSHS_H_
#define SSHS_H_

#ifdef __cplusplus

#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cinttypes>

#else

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

// Support symbol export on Windows GCC/Clang.
#if (defined(_WIN32) || defined(__WIN32__) || defined(WIN32)) && (defined(__GNUC__) || defined(__clang__))
#define CAER_SYMBOL_EXPORT __attribute__ ((__dllexport__))
#else
#define CAER_SYMBOL_EXPORT
#endif

// SSHS node
typedef struct sshs_node *sshsNode;

enum sshs_node_attr_value_type {
	SSHS_UNKNOWN = -1,
	SSHS_BOOL = 0,
	SSHS_BYTE = 1,
	SSHS_SHORT = 2,
	SSHS_INT = 3,
	SSHS_LONG = 4,
	SSHS_FLOAT = 5,
	SSHS_DOUBLE = 6,
	SSHS_STRING = 7,
};

union sshs_node_attr_value {
	bool boolean;
	int8_t ibyte;
	int16_t ishort;
	int32_t iint;
	int64_t ilong;
	float ffloat;
	double ddouble;
	char *string;
};

union sshs_node_attr_range {
	double d;
	int64_t i;
};

enum sshs_node_attr_flags {
	SSHS_FLAGS_NORMAL = 0,
	SSHS_FLAGS_READ_ONLY = 1,
	SSHS_FLAGS_NOTIFY_ONLY = 2,
	SSHS_FLAGS_FORCE_DEFAULT_VALUE = 4,
	SSHS_FLAGS_READ_ONLY_FORCE_DEFAULT_VALUE = SSHS_FLAGS_READ_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE,
	SSHS_FLAGS_NOTIFY_ONLY_FORCE_DEFAULT_VALUE = SSHS_FLAGS_NOTIFY_ONLY | SSHS_FLAGS_FORCE_DEFAULT_VALUE,
};

enum sshs_node_node_events {
	SSHS_CHILD_NODE_ADDED = 0,
};

enum sshs_node_attribute_events {
	SSHS_ATTRIBUTE_ADDED = 0,
	SSHS_ATTRIBUTE_MODIFIED = 1,
	SSHS_ATTRIBUTE_REMOVED = 2,
};

const char *sshsNodeGetName(sshsNode node) CAER_SYMBOL_EXPORT;
const char *sshsNodeGetPath(sshsNode node) CAER_SYMBOL_EXPORT;
sshsNode sshsNodeGetParent(sshsNode node) CAER_SYMBOL_EXPORT;
sshsNode *sshsNodeGetChildren(sshsNode node, size_t *numChildren) CAER_SYMBOL_EXPORT; // Walk all children.
void sshsNodeAddNodeListener(sshsNode node, void *userData,
	void (*node_changed)(sshsNode node, void *userData, enum sshs_node_node_events event, sshsNode changeNode))
		CAER_SYMBOL_EXPORT;
void sshsNodeRemoveNodeListener(sshsNode node, void *userData,
	void (*node_changed)(sshsNode node, void *userData, enum sshs_node_node_events event, sshsNode changeNode))
		CAER_SYMBOL_EXPORT;
void sshsNodeRemoveAllNodeListeners(sshsNode node) CAER_SYMBOL_EXPORT;
void sshsNodeAddAttributeListener(sshsNode node, void *userData,
	void (*attribute_changed)(sshsNode node, void *userData, enum sshs_node_attribute_events event,
		const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue))
			CAER_SYMBOL_EXPORT;
void sshsNodeRemoveAttributeListener(sshsNode node, void *userData,
	void (*attribute_changed)(sshsNode node, void *userData, enum sshs_node_attribute_events event,
		const char *changeKey, enum sshs_node_attr_value_type changeType, union sshs_node_attr_value changeValue))
			CAER_SYMBOL_EXPORT;
void sshsNodeRemoveAllAttributeListeners(sshsNode node) CAER_SYMBOL_EXPORT;
void sshsNodeCreateAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value defaultValue, union sshs_node_attr_range minValue, union sshs_node_attr_range maxValue,
	enum sshs_node_attr_flags flags) CAER_SYMBOL_EXPORT;
void sshsNodeRemoveAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type) CAER_SYMBOL_EXPORT;
void sshsNodeRemoveAllAttributes(sshsNode node) CAER_SYMBOL_EXPORT;
bool sshsNodeAttributeExists(sshsNode node, const char *key, enum sshs_node_attr_value_type type) CAER_SYMBOL_EXPORT;
bool sshsNodePutAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type,
	union sshs_node_attr_value value) CAER_SYMBOL_EXPORT;
union sshs_node_attr_value sshsNodeGetAttribute(sshsNode node, const char *key, enum sshs_node_attr_value_type type)
	CAER_SYMBOL_EXPORT;
void sshsNodeCreateBool(sshsNode node, const char *key, bool defaultValue, enum sshs_node_attr_flags flags)
	CAER_SYMBOL_EXPORT;
void sshsNodePutBool(sshsNode node, const char *key, bool value) CAER_SYMBOL_EXPORT;
bool sshsNodeGetBool(sshsNode node, const char *key) CAER_SYMBOL_EXPORT;
void sshsNodeCreateByte(sshsNode node, const char *key, int8_t defaultValue, int8_t minValue, int8_t maxValue,
	enum sshs_node_attr_flags flags) CAER_SYMBOL_EXPORT;
void sshsNodePutByte(sshsNode node, const char *key, int8_t value) CAER_SYMBOL_EXPORT;
int8_t sshsNodeGetByte(sshsNode node, const char *key) CAER_SYMBOL_EXPORT;
void sshsNodeCreateShort(sshsNode node, const char *key, int16_t defaultValue, int16_t minValue, int16_t maxValue,
	enum sshs_node_attr_flags flags) CAER_SYMBOL_EXPORT;
void sshsNodePutShort(sshsNode node, const char *key, int16_t value) CAER_SYMBOL_EXPORT;
int16_t sshsNodeGetShort(sshsNode node, const char *key) CAER_SYMBOL_EXPORT;
void sshsNodeCreateInt(sshsNode node, const char *key, int32_t defaultValue, int32_t minValue, int32_t maxValue,
	enum sshs_node_attr_flags flags) CAER_SYMBOL_EXPORT;
void sshsNodePutInt(sshsNode node, const char *key, int32_t value) CAER_SYMBOL_EXPORT;
int32_t sshsNodeGetInt(sshsNode node, const char *key) CAER_SYMBOL_EXPORT;
void sshsNodeCreateLong(sshsNode node, const char *key, int64_t defaultValue, int64_t minValue, int64_t maxValue,
	enum sshs_node_attr_flags flags) CAER_SYMBOL_EXPORT;
void sshsNodePutLong(sshsNode node, const char *key, int64_t value) CAER_SYMBOL_EXPORT;
int64_t sshsNodeGetLong(sshsNode node, const char *key) CAER_SYMBOL_EXPORT;
void sshsNodeCreateFloat(sshsNode node, const char *key, float defaultValue, float minValue, float maxValue,
	enum sshs_node_attr_flags flags) CAER_SYMBOL_EXPORT;
void sshsNodePutFloat(sshsNode node, const char *key, float value) CAER_SYMBOL_EXPORT;
float sshsNodeGetFloat(sshsNode node, const char *key) CAER_SYMBOL_EXPORT;
void sshsNodeCreateDouble(sshsNode node, const char *key, double defaultValue, double minValue, double maxValue,
	enum sshs_node_attr_flags flags) CAER_SYMBOL_EXPORT;
void sshsNodePutDouble(sshsNode node, const char *key, double value) CAER_SYMBOL_EXPORT;
double sshsNodeGetDouble(sshsNode node, const char *key) CAER_SYMBOL_EXPORT;
void sshsNodeCreateString(sshsNode node, const char *key, const char *defaultValue, size_t minLength, size_t maxLength,
	enum sshs_node_attr_flags flags) CAER_SYMBOL_EXPORT;
void sshsNodePutString(sshsNode node, const char *key, const char *value) CAER_SYMBOL_EXPORT;
char *sshsNodeGetString(sshsNode node, const char *key) CAER_SYMBOL_EXPORT;
void sshsNodeExportNodeToXML(sshsNode node, int outFd, const char **filterKeys, size_t filterKeysLength)
	CAER_SYMBOL_EXPORT;
void sshsNodeExportSubTreeToXML(sshsNode node, int outFd, const char **filterKeys, size_t filterKeysLength,
	const char **filterNodes, size_t filterNodesLength) CAER_SYMBOL_EXPORT;
bool sshsNodeImportNodeFromXML(sshsNode node, int inFd, bool strict) CAER_SYMBOL_EXPORT;
bool sshsNodeImportSubTreeFromXML(sshsNode node, int inFd, bool strict) CAER_SYMBOL_EXPORT;
bool sshsNodeStringToAttributeConverter(sshsNode node, const char *key, const char *type, const char *value)
	CAER_SYMBOL_EXPORT;
const char **sshsNodeGetChildNames(sshsNode node, size_t *numNames) CAER_SYMBOL_EXPORT;
const char **sshsNodeGetAttributeKeys(sshsNode node, size_t *numKeys) CAER_SYMBOL_EXPORT;
enum sshs_node_attr_value_type *sshsNodeGetAttributeTypes(sshsNode node, const char *key, size_t *numTypes)
	CAER_SYMBOL_EXPORT;
union sshs_node_attr_range sshsNodeGetAttributeMinRange(sshsNode node, const char *key,
	enum sshs_node_attr_value_type type) CAER_SYMBOL_EXPORT;
union sshs_node_attr_range sshsNodeGetAttributeMaxRange(sshsNode node, const char *key,
	enum sshs_node_attr_value_type type) CAER_SYMBOL_EXPORT;
enum sshs_node_attr_flags sshsNodeGetAttributeFlags(sshsNode node, const char *key, enum sshs_node_attr_value_type type)
	CAER_SYMBOL_EXPORT;

// Helper functions
const char *sshsHelperTypeToStringConverter(enum sshs_node_attr_value_type type) CAER_SYMBOL_EXPORT;
enum sshs_node_attr_value_type sshsHelperStringToTypeConverter(const char *typeString) CAER_SYMBOL_EXPORT;
char *sshsHelperValueToStringConverter(enum sshs_node_attr_value_type type, union sshs_node_attr_value value)
	CAER_SYMBOL_EXPORT;
bool sshsHelperStringToValueConverter(enum sshs_node_attr_value_type type, const char *valueString,
	union sshs_node_attr_value *value) CAER_SYMBOL_EXPORT;

// SSHS
typedef struct sshs_struct *sshs;
typedef void (*sshsErrorLogCallback)(const char *msg);

sshs sshsGetGlobal(void) CAER_SYMBOL_EXPORT;
void sshsSetGlobalErrorLogCallback(sshsErrorLogCallback error_log_cb) CAER_SYMBOL_EXPORT;
sshs sshsNew(void) CAER_SYMBOL_EXPORT;
bool sshsExistsNode(sshs st, const char *nodePath) CAER_SYMBOL_EXPORT;
sshsNode sshsGetNode(sshs st, const char *nodePath) CAER_SYMBOL_EXPORT;
bool sshsExistsRelativeNode(sshsNode node, const char *nodePath) CAER_SYMBOL_EXPORT;
sshsNode sshsGetRelativeNode(sshsNode node, const char *nodePath) CAER_SYMBOL_EXPORT;
bool sshsBeginTransaction(sshs st, char *nodePaths[], size_t nodePathsLength) CAER_SYMBOL_EXPORT;
bool sshsEndTransaction(sshs st, char *nodePaths[], size_t nodePathsLength) CAER_SYMBOL_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* SSHS_H_ */
