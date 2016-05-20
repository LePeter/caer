#ifndef BUFFERS_H_
#define BUFFERS_H_

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "nets.h"

struct simple_buffer {
	/// Size of data currently inside buffer, in bytes.
	size_t bufferUsedSize;
	/// Size of buffer, in bytes.
	size_t bufferSize;
	/// Buffer for R/W to file descriptor (buffered I/O).
	uint8_t buffer[];
};

typedef struct simple_buffer *simpleBuffer;

static inline bool simpleBufferWrite(int fd, simpleBuffer buffer) {
	return (writeUntilDone(fd, buffer->buffer, buffer->bufferUsedSize));
}

static inline bool simpleBufferRead(int fd, simpleBuffer buffer) {
	// Try to fill whole buffer.
	buffer->bufferUsedSize = buffer->bufferSize;

	return (readUntilDone(fd, buffer->buffer, buffer->bufferUsedSize));
}

#endif /* BUFFERS_H_ */
