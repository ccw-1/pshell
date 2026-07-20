#include "pshell/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>

/* Logger state */
static struct {
    FILE *log_file;
    bool use_syslog;
    log_level_t level;
    bool initialized;
} logger_state = {
    .log_file = NULL,
    .use_syslog = false,
    .level = LOG_LEVEL_INFO,
    .initialized = false
};

static pthread_mutex_t logger_mutex;
static pthread_once_t logger_mutex_once = PTHREAD_ONCE_INIT;

static void init_logger_mutex(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&logger_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

/* Log level strings */
static const char* level_strings[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

/* Syslog priority mapping */
static const int syslog_priorities[] = {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERR,
    LOG_CRIT
};

bool logger_init(const char *log_file, bool use_syslog, log_level_t level) {
    pthread_once(&logger_mutex_once, init_logger_mutex);
    pthread_mutex_lock(&logger_mutex);
    
    if (logger_state.initialized) {
        pthread_mutex_unlock(&logger_mutex);
        return true;
    }
    
    logger_state.level = level;
    logger_state.use_syslog = use_syslog;
    
    if (log_file) {
        fprintf(stderr, "DEBUG: About to fopen(%s, \"a\")\n", log_file);
        fflush(stderr);
        logger_state.log_file = fopen(log_file, "a");
        fprintf(stderr, "DEBUG: fopen returned %p, errno=%d\n", (void*)logger_state.log_file, errno);
        fflush(stderr);
        if (!logger_state.log_file) {
            fprintf(stderr, "DEBUG: fopen failed: %s\n", strerror(errno));
            fflush(stderr);
            pthread_mutex_unlock(&logger_mutex);
            return false;
        }
        // Set line buffering
        setvbuf(logger_state.log_file, NULL, _IOLBF, 0);
        fprintf(stderr, "DEBUG: Logger file opened successfully\n");
        fflush(stderr);
    }
    
    if (use_syslog) {
        openlog("pshell", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    }
    
    logger_state.initialized = true;
    
    pthread_mutex_unlock(&logger_mutex);
    return true;
}

void logger_shutdown(void) {
    pthread_mutex_lock(&logger_mutex);
    
    if (!logger_state.initialized) {
        pthread_mutex_unlock(&logger_mutex);
        return;
    }
    
    if (logger_state.log_file) {
        fclose(logger_state.log_file);
        logger_state.log_file = NULL;
    }
    
    if (logger_state.use_syslog) {
        closelog();
    }
    
    logger_state.initialized = false;
    
    pthread_mutex_unlock(&logger_mutex);
}

void logger_set_level(log_level_t level) {
    pthread_mutex_lock(&logger_mutex);
    logger_state.level = level;
    pthread_mutex_unlock(&logger_mutex);
}

log_level_t logger_get_level(void) {
    pthread_mutex_lock(&logger_mutex);
    log_level_t level = logger_state.level;
    pthread_mutex_unlock(&logger_mutex);
    return level;
}

void logger_vlog(log_level_t level, const char *fmt, va_list args) {
    pthread_once(&logger_mutex_once, init_logger_mutex);
    pthread_mutex_lock(&logger_mutex);
    
    // Check if we should log this level
    if (level < logger_state.level) {
        pthread_mutex_unlock(&logger_mutex);
        return;
    }
    
    // Get timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Format message
    char message[4096];
    vsnprintf(message, sizeof(message), fmt, args);
    
    // Log to file
    if (logger_state.log_file) {
        fprintf(logger_state.log_file, "[%s] [%s] [%d] %s\n",
                timestamp,
                level_strings[level],
                getpid(),
                message);
        fflush(logger_state.log_file);
    }
    
    // Log to syslog
    if (logger_state.use_syslog) {
        syslog(syslog_priorities[level], "%s", message);
    }
    
    // Also log to stderr for errors and fatal
    if (level >= LOG_LEVEL_ERROR) {
        fprintf(stderr, "[%s] [%s] %s\n",
                timestamp,
                level_strings[level],
                message);
    }
    
    pthread_mutex_unlock(&logger_mutex);
}

void logger_log(log_level_t level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logger_vlog(level, fmt, args);
    va_end(args);
}

const char* logger_level_to_string(log_level_t level) {
    if (level < 0 || level > LOG_LEVEL_FATAL) {
        return "UNKNOWN";
    }
    return level_strings[level];
}

log_level_t logger_string_to_level(const char *str) {
    if (!str) return LOG_LEVEL_INFO;
    
    for (int i = 0; i <= LOG_LEVEL_FATAL; i++) {
        if (strcasecmp(str, level_strings[i]) == 0) {
            return (log_level_t)i;
        }
    }
    
    return LOG_LEVEL_INFO;
}
