#ifndef PSHELL_UUID_SIMPLE_H
#define PSHELL_UUID_SIMPLE_H

#include <stddef.h>
#include <stdbool.h>

/**
 * Simple UUID (v4) generator for pshell
 * No external dependencies, uses /dev/urandom for randomness
 * Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 */

#define UUID_STRING_LENGTH 37  /* 36 chars + null terminator */

/**
 * Generate a random UUID v4
 * @param buf Buffer to store UUID string (must be at least UUID_STRING_LENGTH bytes)
 * @return true on success, false on error
 */
bool uuid_generate(char *buf);

/**
 * Validate UUID string format
 * @param uuid UUID string to validate
 * @return true if valid UUID format
 */
bool uuid_is_valid(const char *uuid);

#endif /* PSHELL_UUID_SIMPLE_H */
