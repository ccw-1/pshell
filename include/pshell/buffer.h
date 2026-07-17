#ifndef PSHELL_BUFFER_H
#define PSHELL_BUFFER_H

#include <stddef.h>
#include <sys/types.h>
#include <stdbool.h>

/**
 * Circular buffer implementation for pshell
 * Thread-safe, fixed-size circular buffer for efficient I/O buffering
 */

typedef struct {
    char *data;           /* Buffer data */
    size_t size;          /* Total buffer size */
    size_t head;          /* Write position */
    size_t tail;          /* Read position */
    size_t count;         /* Number of bytes in buffer */
    bool overflow;        /* Overflow flag */
} buffer_t;

/**
 * Create a new buffer
 * @param size Buffer size in bytes
 * @return Pointer to new buffer or NULL on error
 */
buffer_t* buffer_create(size_t size);

/**
 * Destroy buffer and free memory
 * @param buf Buffer to destroy
 */
void buffer_destroy(buffer_t *buf);

/**
 * Write data to buffer
 * @param buf Buffer
 * @param data Data to write
 * @param len Length of data
 * @return Number of bytes written
 */
size_t buffer_write(buffer_t *buf, const void *data, size_t len);

/**
 * Read data from buffer
 * @param buf Buffer
 * @param data Output buffer
 * @param len Maximum bytes to read
 * @return Number of bytes read
 */
size_t buffer_read(buffer_t *buf, void *data, size_t len);

/**
 * Peek at data without removing it
 * @param buf Buffer
 * @param data Output buffer
 * @param len Maximum bytes to peek
 * @return Number of bytes peeked
 */
size_t buffer_peek(buffer_t *buf, void *data, size_t len);

/**
 * Get number of bytes available to read
 * @param buf Buffer
 * @return Number of bytes available
 */
size_t buffer_available(const buffer_t *buf);

/**
 * Get number of bytes free for writing
 * @param buf Buffer
 * @return Number of bytes free
 */
size_t buffer_free(const buffer_t *buf);

/**
 * Check if buffer is empty
 * @param buf Buffer
 * @return true if empty
 */
bool buffer_is_empty(const buffer_t *buf);

/**
 * Check if buffer is full
 * @param buf Buffer
 * @return true if full
 */
bool buffer_is_full(const buffer_t *buf);

/**
 * Clear buffer (reset to empty)
 * @param buf Buffer
 */
void buffer_clear(buffer_t *buf);

/**
 * Get total buffer size
 * @param buf Buffer
 * @return Total size in bytes
 */
size_t buffer_size(const buffer_t *buf);

/**
 * Check if buffer has overflowed
 * @param buf Buffer
 * @return true if overflow occurred
 */
bool buffer_has_overflowed(const buffer_t *buf);

/**
 * Clear overflow flag
 * @param buf Buffer
 */
void buffer_clear_overflow(buffer_t *buf);

/**
 * Write a single byte
 * @param buf Buffer
 * @param byte Byte to write
 * @return true if successful
 */
bool buffer_write_byte(buffer_t *buf, unsigned char byte);

/**
 * Read a single byte
 * @param buf Buffer
 * @param byte Output byte
 * @return true if successful
 */
bool buffer_read_byte(buffer_t *buf, unsigned char *byte);

/**
 * Search for a byte in buffer
 * @param buf Buffer
 * @param byte Byte to search for
 * @param offset Offset to start search
 * @return Position of byte or -1 if not found
 */
ssize_t buffer_find_byte(const buffer_t *buf, unsigned char byte, size_t offset);

/**
 * Search for a pattern in buffer
 * @param buf Buffer
 * @param pattern Pattern to search for
 * @param pattern_len Length of pattern
 * @param offset Offset to start search
 * @return Position of pattern or -1 if not found
 */
ssize_t buffer_find_pattern(const buffer_t *buf, const void *pattern, 
                            size_t pattern_len, size_t offset);

#endif /* PSHELL_BUFFER_H */
