#ifndef PSHELL_STRING_UTILS_H
#define PSHELL_STRING_UTILS_H

#include <stddef.h>
#include <stdbool.h>

/**
 * String utility functions for pshell
 * All functions are POSIX-compliant with no external dependencies
 */

/**
 * Trim whitespace from both ends of a string (in-place)
 * @param str String to trim
 * @return Pointer to trimmed string (same as input)
 */
char* str_trim(char *str);

/**
 * Trim whitespace from start of string (in-place)
 * @param str String to trim
 * @return Pointer to trimmed string (same as input)
 */
char* str_ltrim(char *str);

/**
 * Trim whitespace from end of string (in-place)
 * @param str String to trim
 * @return Pointer to trimmed string (same as input)
 */
char* str_rtrim(char *str);

/**
 * Safe string copy with null termination guarantee
 * @param dst Destination buffer
 * @param src Source string
 * @param size Size of destination buffer
 * @return Number of characters copied (excluding null terminator)
 */
size_t str_copy(char *dst, const char *src, size_t size);

/**
 * Check if string starts with prefix
 * @param str String to check
 * @param prefix Prefix to look for
 * @return true if str starts with prefix
 */
bool str_starts_with(const char *str, const char *prefix);

/**
 * Check if string ends with suffix
 * @param str String to check
 * @param suffix Suffix to look for
 * @return true if str ends with suffix
 */
bool str_ends_with(const char *str, const char *suffix);

/**
 * Split string by delimiter (modifies input string)
 * @param str String to split (will be modified)
 * @param delim Delimiter character
 * @param tokens Array to store token pointers
 * @param max_tokens Maximum number of tokens
 * @return Number of tokens found
 */
int str_split(char *str, char delim, char **tokens, int max_tokens);

/**
 * Join strings with delimiter
 * @param tokens Array of strings to join
 * @param count Number of strings
 * @param delim Delimiter string
 * @param buf Output buffer
 * @param size Size of output buffer
 * @return Number of characters written
 */
size_t str_join(const char **tokens, int count, const char *delim, 
                char *buf, size_t size);

/**
 * Case-insensitive string comparison
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int str_casecmp(const char *s1, const char *s2);

/**
 * Case-insensitive string comparison (n characters)
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int str_ncasecmp(const char *s1, const char *s2, size_t n);

/**
 * Duplicate string (caller must free)
 * @param str String to duplicate
 * @return Newly allocated copy or NULL on error
 */
char* str_dup(const char *str);

/**
 * Duplicate string with length limit (caller must free)
 * @param str String to duplicate
 * @param n Maximum characters to copy
 * @return Newly allocated copy or NULL on error
 */
char* str_ndup(const char *str, size_t n);

/**
 * Replace all occurrences of character in string (in-place)
 * @param str String to modify
 * @param old_char Character to replace
 * @param new_char Replacement character
 * @return Number of replacements made
 */
int str_replace_char(char *str, char old_char, char new_char);

/**
 * Convert string to lowercase (in-place)
 * @param str String to convert
 * @return Pointer to converted string (same as input)
 */
char* str_tolower(char *str);

/**
 * Convert string to uppercase (in-place)
 * @param str String to convert
 * @return Pointer to converted string (same as input)
 */
char* str_toupper(char *str);

/**
 * Check if string contains only whitespace
 * @param str String to check
 * @return true if string is empty or contains only whitespace
 */
bool str_is_blank(const char *str);

/**
 * Format string with variable arguments (safe snprintf wrapper)
 * @param buf Output buffer
 * @param size Size of output buffer
 * @param fmt Format string
 * @param ... Variable arguments
 * @return Number of characters written (excluding null terminator)
 */
int str_format(char *buf, size_t size, const char *fmt, ...);

#endif /* PSHELL_STRING_UTILS_H */
