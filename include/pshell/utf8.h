#ifndef PSHELL_UTF8_H
#define PSHELL_UTF8_H

#include <stdint.h>
#include <stddef.h>

/**
 * Minimal UTF-8 helpers for the screen buffer.
 */

/* Replacement character used for invalid input */
#define UTF8_REPLACEMENT 0xFFFDu

/* Longest UTF-8 encoding of one code point */
#define UTF8_MAX_BYTES 4

/**
 * Encode a code point as UTF-8.
 * Writes 1-4 bytes to out (not NUL terminated) and returns the byte count.
 * Invalid code points (surrogates, > U+10FFFF) are encoded as U+FFFD.
 */
size_t utf8_encode(uint32_t cp, char *out);

/**
 * Number of terminal columns a code point occupies:
 * 0 for combining/zero-width and control characters, 2 for East Asian wide
 * and emoji characters, 1 otherwise.
 */
int utf8_cp_width(uint32_t cp);

#endif /* PSHELL_UTF8_H */
