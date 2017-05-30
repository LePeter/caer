#include "config_server.h"
#include <atomic>
#include <thread>
#include <boost/asio.hpp>
#include "ext/threads_ext.h"
#include "ext/sshs/sshs.hpp"
#include <libcaercpp/libcaer.hpp>

namespace asio = boost::asio;
namespace asioIP = boost::asio::ip;
using asioTCP = boost::asio::ip::tcp;

#define CONFIG_SERVER_NAME "Config Server"

class ConfigServerConnection;

static void caerConfigServerHandleRequest(std::shared_ptr<ConfigServerConnection> client, uint8_t action, uint8_t type,
	const uint8_t *extra, size_t extraLength, const uint8_t *node, size_t nodeLength, const uint8_t *key,
	size_t keyLength, const uint8_t *value, size_t valueLength);

class ConfigServerConnection: public std::enable_shared_from_this<ConfigServerConnection> {
private:
	asioTCP::socket socket;
	uint8_t data[CAER_CONFIG_SERVER_BUFFER_SIZE];

public:
	ConfigServerConnection(asioTCP::socket s) :
			socket(std::move(s)) {
	}

	void start() {
		readHeader();
	}

	uint8_t *getData() {
		return (data);
	}

	void writeResponse(size_t dataLength) {
		auto self(shared_from_this());

		boost::asio::async_write(socket, boost::asio::buffer(data, dataLength),
			[this, self](boost::system::error_code ec, std::size_t /*length*/) {
				if (!ec) {
					readHeader();
				}
			});
	}

private:
	void readHeader() {
		auto self(shared_from_this());

		boost::asio::async_read(socket, boost::asio::buffer(data, CAER_CONFIG_SERVER_HEADER_SIZE),
			[this, self](boost::system::error_code ec, std::size_t /*length*/) {
				// TODO: HANDLE EOF, log closed connection
				if (!ec) {
					// If we have enough data, we start parsing the lengths.
					// The main header is 10 bytes.
					// Decode length header fields (all in little-endian).
					uint16_t extraLength = le16toh(*(uint16_t * )(data + 2));
					uint16_t nodeLength = le16toh(*(uint16_t * )(data + 4));
					uint16_t keyLength = le16toh(*(uint16_t * )(data + 6));
					uint16_t valueLength = le16toh(*(uint16_t * )(data + 8));

					// Total length to get for command.
					size_t readLength = (size_t) (extraLength + nodeLength + keyLength + valueLength);

					readData(readLength);
				}
			});
	}

	void readData(size_t dataLength) {
		auto self(shared_from_this());

		boost::asio::async_read(socket, boost::asio::buffer(data + CAER_CONFIG_SERVER_HEADER_SIZE, dataLength),
			[this, self](boost::system::error_code ec, std::size_t /*length*/) {
				// TODO: HANDLE EOF, log closed connection
				if (!ec) {
					// Decode command header fields.
					uint8_t action = data[0];
					uint8_t type = data[1];

					// Decode length header fields (all in little-endian).
					uint16_t extraLength = le16toh(*(uint16_t * )(data + 2));
					uint16_t nodeLength = le16toh(*(uint16_t * )(data + 4));
					uint16_t keyLength = le16toh(*(uint16_t * )(data + 6));
					uint16_t valueLength = le16toh(*(uint16_t * )(data + 8));

					// Now we have everything. The header fields are already
					// fully decoded: handle request (and send back data eventually).
					caerConfigServerHandleRequest(self, action, type, data + CAER_CONFIG_SERVER_HEADER_SIZE,
						extraLength, data + CAER_CONFIG_SERVER_HEADER_SIZE + extraLength, nodeLength,
						data + CAER_CONFIG_SERVER_HEADER_SIZE + extraLength + nodeLength, keyLength,
						data + CAER_CONFIG_SERVER_HEADER_SIZE + extraLength + nodeLength + keyLength,
						valueLength);
				}
			});
	}
};

class ConfigServer {
private:
	asio::io_service ioService;
	asioTCP::acceptor acceptor;
	asioTCP::socket socket;
	std::thread ioThread;

public:
	ConfigServer(const asioIP::address &listenAddress, unsigned short listenPort) :
			acceptor(ioService, asioTCP::endpoint(listenAddress, listenPort)),
			socket(ioService) {
		acceptStart();

		threadStart();
	}

	void stop() {
		threadStop();
	}

private:
	void acceptStart() {
		acceptor.async_accept(socket, [this](const boost::system::error_code &error) {
			if (error) {
				libcaer::log::log(libcaer::log::logLevel::ERROR, CONFIG_SERVER_NAME,
					"Failed to accept new config server connection. Error: %s.", error.message().c_str());
			}
			else {
				std::make_shared<ConfigServerConnection>(std::move(socket))->start();
			}

			acceptStart();
		});
	}

	void threadStart() {
		ioThread = std::thread([this]() {
			// Set thread name.
			thrd_set_name("ConfigServer");

			// Run IO service.
			while (!ioService.stopped()) {
				ioService.run();
			}
		});
	}

	void threadStop() {
		ioService.stop();

		ioThread.join();
	}
};

static std::unique_ptr<ConfigServer> cfg;

void caerConfigServerStart(void) {
	// Get the right configuration node first.
	sshsNode serverNode = sshsGetNode(sshsGetGlobal(), "/caer/server/");

	// Ensure default values are present.
	sshsNodeCreate(serverNode, "ipAddress", "127.0.0.1", 7, 15, SSHS_FLAGS_NORMAL,
		"IPv4 address to listen on for configuration server connections.");
	sshsNodeCreate(serverNode, "portNumber", 4040, 1, UINT16_MAX, SSHS_FLAGS_NORMAL,
		"Port to listen on for configuration server connections.");

	// Start the thread.
	try {
		cfg = std::make_unique<ConfigServer>(
			asioIP::address::from_string(sshsNodeGetStdString(serverNode, "ipAddress")),
			sshsNodeGetInt(serverNode, "portNumber"));
	}
	catch (const std::system_error &ex) {
		// Failed to create thread.
		caerLog(CAER_LOG_EMERGENCY, CONFIG_SERVER_NAME, "Failed to create thread. Error: %s.", ex.what());
		exit(EXIT_FAILURE);
	}

	// Successfully started thread.
	caerLog(CAER_LOG_DEBUG, CONFIG_SERVER_NAME, "Thread created successfully.");
}

void caerConfigServerStop(void) {
	try {
		cfg->stop();
	}
	catch (const std::system_error &ex) {
		// Failed to join thread.
		caerLog(CAER_LOG_EMERGENCY, CONFIG_SERVER_NAME, "Failed to terminate thread. Error: %s.", ex.what());
		exit(EXIT_FAILURE);
	}

	// Successfully joined thread.
	caerLog(CAER_LOG_DEBUG, CONFIG_SERVER_NAME, "Thread terminated successfully.");
}

// The response from the server follows a simplified version of the request
// protocol. A byte for ACTION, a byte for TYPE, 2 bytes for MSG_LEN and then
// up to 4092 bytes of MSG, for a maximum total of 4096 bytes again.
// MSG must be NUL terminated, and the NUL byte shall be part of the length.
static inline void setMsgLen(uint8_t *buf, uint16_t msgLen) {
	*((uint16_t *) (buf + 2)) = htole16(msgLen);
}

static inline void caerConfigSendError(std::shared_ptr<ConfigServerConnection> client, const char *errorMsg) {
	size_t errorMsgLength = strlen(errorMsg);
	size_t responseLength = 4 + errorMsgLength + 1; // +1 for terminating NUL byte.

	uint8_t *response = client->getData();

	response[0] = CAER_CONFIG_ERROR;
	response[1] = SSHS_STRING;
	setMsgLen(response, (uint16_t) (errorMsgLength + 1));
	memcpy(response + 4, errorMsg, errorMsgLength);
	response[4 + errorMsgLength] = '\0';

	client->writeResponse(responseLength);

	caerLog(CAER_LOG_DEBUG, "Config Server", "Sent back error message '%s' to client.", errorMsg);
}

static inline void caerConfigSendResponse(std::shared_ptr<ConfigServerConnection> client, uint8_t action, uint8_t type,
	const uint8_t *msg, size_t msgLength) {
	size_t responseLength = 4 + msgLength;

	uint8_t *response = client->getData();

	response[0] = action;
	response[1] = type;
	setMsgLen(response, (uint16_t) msgLength);
	memcpy(response + 4, msg, msgLength);
	// Msg must already be NUL terminated!

	client->writeResponse(responseLength);

	caerLog(CAER_LOG_DEBUG, "Config Server",
		"Sent back message to client: action=%" PRIu8 ", type=%" PRIu8 ", msgLength=%zu.", action, type, msgLength);
}

static void caerConfigServerHandleRequest(std::shared_ptr<ConfigServerConnection> client, uint8_t action, uint8_t type,
	const uint8_t *extra, size_t extraLength, const uint8_t *node, size_t nodeLength, const uint8_t *key,
	size_t keyLength, const uint8_t *value, size_t valueLength) {
	UNUSED_ARGUMENT(extra);

	caerLog(CAER_LOG_DEBUG, "Config Server",
		"Handling request: action=%" PRIu8 ", type=%" PRIu8 ", extraLength=%zu, nodeLength=%zu, keyLength=%zu, valueLength=%zu.",
		action, type, extraLength, nodeLength, keyLength, valueLength);

	// Interpretation of data is up to each action individually.
	sshs configStore = sshsGetGlobal();

	switch (action) {
		case CAER_CONFIG_NODE_EXISTS: {
			// We only need the node name here. Type is not used (ignored)!
			bool result = sshsExistsNode(configStore, (const char *) node);

			// Send back result to client. Format is the same as incoming data.
			const uint8_t *sendResult = (const uint8_t *) ((result) ? ("true") : ("false"));
			size_t sendResultLength = (result) ? (5) : (6);
			caerConfigSendResponse(client, CAER_CONFIG_NODE_EXISTS, SSHS_BOOL, sendResult, sendResultLength);

			break;
		}

		case CAER_CONFIG_ATTR_EXISTS: {
			bool nodeExists = sshsExistsNode(configStore, (const char *) node);

			// Only allow operations on existing nodes, this is for remote
			// control, so we only manipulate what's already there!
			if (!nodeExists) {
				// Send back error message to client.
				caerConfigSendError(client, "Node doesn't exist. Operations are only allowed on existing data.");

				break;
			}

			// This cannot fail, since we know the node exists from above.
			sshsNode wantedNode = sshsGetNode(configStore, (const char *) node);

			// Check if attribute exists.
			bool result = sshsNodeAttributeExists(wantedNode, (const char *) key,
				(enum sshs_node_attr_value_type) type);

			// Send back result to client. Format is the same as incoming data.
			const uint8_t *sendResult = (const uint8_t *) ((result) ? ("true") : ("false"));
			size_t sendResultLength = (result) ? (5) : (6);
			caerConfigSendResponse(client, CAER_CONFIG_ATTR_EXISTS, SSHS_BOOL, sendResult, sendResultLength);

			break;
		}

		case CAER_CONFIG_GET: {
			bool nodeExists = sshsExistsNode(configStore, (const char *) node);

			// Only allow operations on existing nodes, this is for remote
			// control, so we only manipulate what's already there!
			if (!nodeExists) {
				// Send back error message to client.
				caerConfigSendError(client, "Node doesn't exist. Operations are only allowed on existing data.");

				break;
			}

			// This cannot fail, since we know the node exists from above.
			sshsNode wantedNode = sshsGetNode(configStore, (const char *) node);

			// Check if attribute exists. Only allow operations on existing attributes!
			bool attrExists = sshsNodeAttributeExists(wantedNode, (const char *) key,
				(enum sshs_node_attr_value_type) type);

			if (!attrExists) {
				// Send back error message to client.
				caerConfigSendError(client,
					"Attribute of given type doesn't exist. Operations are only allowed on existing data.");

				break;
			}

			union sshs_node_attr_value result = sshsNodeGetAttribute(wantedNode, (const char *) key,
				(enum sshs_node_attr_value_type) type);

			char *resultStr = sshsHelperValueToStringConverter((enum sshs_node_attr_value_type) type, result);

			if (resultStr == NULL) {
				// Send back error message to client.
				caerConfigSendError(client, "Failed to allocate memory for value string.");
			}
			else {
				caerConfigSendResponse(client, CAER_CONFIG_GET, type, (const uint8_t *) resultStr,
					strlen(resultStr) + 1);

				free(resultStr);
			}

			// If this is a string, we must remember to free the original result.str
			// too, since it will also be a copy of the string coming from SSHS.
			if (type == SSHS_STRING) {
				free(result.string);
			}

			break;
		}

		case CAER_CONFIG_PUT: {
			bool nodeExists = sshsExistsNode(configStore, (const char *) node);

			// Only allow operations on existing nodes, this is for remote
			// control, so we only manipulate what's already there!
			if (!nodeExists) {
				// Send back error message to client.
				caerConfigSendError(client, "Node doesn't exist. Operations are only allowed on existing data.");

				break;
			}

			// This cannot fail, since we know the node exists from above.
			sshsNode wantedNode = sshsGetNode(configStore, (const char *) node);

			// Check if attribute exists. Only allow operations on existing attributes!
			bool attrExists = sshsNodeAttributeExists(wantedNode, (const char *) key,
				(enum sshs_node_attr_value_type) type);

			if (!attrExists) {
				// Send back error message to client.
				caerConfigSendError(client,
					"Attribute of given type doesn't exist. Operations are only allowed on existing data.");

				break;
			}

			// Put given value into config node. Node, attr and type are already verified.
			const char *typeStr = sshsHelperTypeToStringConverter((enum sshs_node_attr_value_type) type);
			if (!sshsNodeStringToAttributeConverter(wantedNode, (const char *) key, typeStr, (const char *) value)) {
				// Send back error message to client.
				caerConfigSendError(client, "Impossible to convert value according to type.");

				break;
			}

			// Send back confirmation to the client.
			caerConfigSendResponse(client, CAER_CONFIG_PUT, SSHS_BOOL, (const uint8_t *) "true", 5);

			break;
		}

		case CAER_CONFIG_GET_CHILDREN: {
			bool nodeExists = sshsExistsNode(configStore, (const char *) node);

			// Only allow operations on existing nodes, this is for remote
			// control, so we only manipulate what's already there!
			if (!nodeExists) {
				// Send back error message to client.
				caerConfigSendError(client, "Node doesn't exist. Operations are only allowed on existing data.");

				break;
			}

			// This cannot fail, since we know the node exists from above.
			sshsNode wantedNode = sshsGetNode(configStore, (const char *) node);

			// Get the names of all the child nodes and return them.
			size_t numNames;
			const char **childNames = sshsNodeGetChildNames(wantedNode, &numNames);

			// No children at all, return empty.
			if (childNames == NULL) {
				// Send back error message to client.
				caerConfigSendError(client, "Node has no children.");

				break;
			}

			// We need to return a big string with all of the child names,
			// separated by a NUL character.
			size_t namesLength = 0;

			for (size_t i = 0; i < numNames; i++) {
				namesLength += strlen(childNames[i]) + 1; // +1 for terminating NUL byte.
			}

			// Allocate a buffer for the names and copy them over.
			char namesBuffer[namesLength];

			for (size_t i = 0, acc = 0; i < numNames; i++) {
				size_t len = strlen(childNames[i]) + 1;
				memcpy(namesBuffer + acc, childNames[i], len);
				acc += len;
			}

			free(childNames);

			caerConfigSendResponse(client, CAER_CONFIG_GET_CHILDREN, SSHS_STRING, (const uint8_t *) namesBuffer,
				namesLength);

			break;
		}

		case CAER_CONFIG_GET_ATTRIBUTES: {
			bool nodeExists = sshsExistsNode(configStore, (const char *) node);

			// Only allow operations on existing nodes, this is for remote
			// control, so we only manipulate what's already there!
			if (!nodeExists) {
				// Send back error message to client.
				caerConfigSendError(client, "Node doesn't exist. Operations are only allowed on existing data.");

				break;
			}

			// This cannot fail, since we know the node exists from above.
			sshsNode wantedNode = sshsGetNode(configStore, (const char *) node);

			// Get the keys of all the attributes and return them.
			size_t numKeys;
			const char **attrKeys = sshsNodeGetAttributeKeys(wantedNode, &numKeys);

			// No attributes at all, return empty.
			if (attrKeys == NULL) {
				// Send back error message to client.
				caerConfigSendError(client, "Node has no attributes.");

				break;
			}

			// We need to return a big string with all of the attribute keys,
			// separated by a NUL character.
			size_t keysLength = 0;

			for (size_t i = 0; i < numKeys; i++) {
				keysLength += strlen(attrKeys[i]) + 1; // +1 for terminating NUL byte.
			}

			// Allocate a buffer for the keys and copy them over.
			char keysBuffer[keysLength];

			for (size_t i = 0, acc = 0; i < numKeys; i++) {
				size_t len = strlen(attrKeys[i]) + 1;
				memcpy(keysBuffer + acc, attrKeys[i], len);
				acc += len;
			}

			free(attrKeys);

			caerConfigSendResponse(client, CAER_CONFIG_GET_ATTRIBUTES, SSHS_STRING, (const uint8_t *) keysBuffer,
				keysLength);

			break;
		}

		case CAER_CONFIG_GET_TYPES: {
			bool nodeExists = sshsExistsNode(configStore, (const char *) node);

			// Only allow operations on existing nodes, this is for remote
			// control, so we only manipulate what's already there!
			if (!nodeExists) {
				// Send back error message to client.
				caerConfigSendError(client, "Node doesn't exist. Operations are only allowed on existing data.");

				break;
			}

			// This cannot fail, since we know the node exists from above.
			sshsNode wantedNode = sshsGetNode(configStore, (const char *) node);

			// Check if any keys match the given one and return its types.
			size_t numTypes;
			enum sshs_node_attr_value_type *attrTypes = sshsNodeGetAttributeTypes(wantedNode, (const char *) key,
				&numTypes);

			// No attributes for specified key, return empty.
			if (attrTypes == NULL) {
				// Send back error message to client.
				caerConfigSendError(client, "Node has no attributes with specified key.");

				break;
			}

			// We need to return a big string with all of the attribute types,
			// separated by a NUL character.
			size_t typesLength = 0;

			for (size_t i = 0; i < numTypes; i++) {
				const char *typeString = sshsHelperTypeToStringConverter(attrTypes[i]);
				typesLength += strlen(typeString) + 1; // +1 for terminating NUL byte.
			}

			// Allocate a buffer for the types and copy them over.
			char typesBuffer[typesLength];

			for (size_t i = 0, acc = 0; i < numTypes; i++) {
				const char *typeString = sshsHelperTypeToStringConverter(attrTypes[i]);
				size_t len = strlen(typeString) + 1;
				memcpy(typesBuffer + acc, typeString, len);
				acc += len;
			}

			free(attrTypes);

			caerConfigSendResponse(client, CAER_CONFIG_GET_TYPES, SSHS_STRING, (const uint8_t *) typesBuffer,
				typesLength);

			break;
		}

		default:
			// Unknown action, send error back to client.
			caerConfigSendError(client, "Unknown action.");

			break;
	}
}
