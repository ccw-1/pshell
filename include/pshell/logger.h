#ifndef PSHELL_LOGGER_H
#define PSHELL_LOGGER_H

#include <stdarg.h>
#include <stdbool.h>

/**
 * Logging system for pshell
 * Supports both syslog and file logging
 * Thread-safe
 */

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_FATAL = 4
} log_level_t;

/**
 * Initialize logger
 * @param log_file Path to log file (NULL for syslog only)
 * @param use_syslog Enable syslog logging
 * @param level Minimum log level
 * @return true on success
 */
bool logger_init(const char *log_file, bool use_syslog, log_level_t level);

/**
 * Shutdown logger and close files
 */
void logger_shutdown(void);

/**
 * Set log level
 * @param level New log level
 */
void logger_set_level(log_level_t level);

/**
 * Get current log level
 * @return Current log level
 */
log_level_t logger_get_level(void);

/**
 * Log a message
 * @param level Log level
 * @param fmt Format string
 * @param ... Variable arguments
 */
void logger_log(log_level_t level, const char *fmt, ...);

/**
 * Log a message with variable arguments
 * @param level Log level
 * @param fmt Format string
 * @param args Variable arguments
 */
void logger_vlog(log_level_t level, const char *fmt, va_list args);

/**
 * Convenience macros for logging
 */
#define log_debug(...) logger_log(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...)  logger_log(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_warn(...)  logger_log(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_error(...) logger_log(LOG_LEVEL_ERROR, __VA_ARGS__)
#define log_fatal(...) logger_log(LOG_LEVEL_FATAL, __VA_ARGS__)

/**
 * Convert log level to string
 * @param level Log level
 * @return String representation
 */
const char* logger_level_to_string(log_level_t level);

/**
 * Parse log level from string
 * @param str String representation
 * @return Log level or LOG_LEVEL_INFO if invalid
 */
log_level_t logger_string_to_level(const char *str);

#endif /* PSHELL_LOGGER_H */
