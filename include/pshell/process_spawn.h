#ifndef PSHELL_PROCESS_SPAWN_H
#define PSHELL_PROCESS_SPAWN_H

#include "pshell/pty_posix.h"
#include <sys/types.h>
#include <stdbool.h>

/**
 * Process spawning with PTY support
 */

/**
 * Process credentials for privilege separation
 */
typedef struct {
    uid_t uid;           /* User ID (0 = use current) */
    gid_t gid;           /* Group ID (0 = use current) */
    const char *username; /* Username (optional, for logging) */
} process_credentials_t;

/**
 * Parse command string into argv array
 * @param command Command string
 * @param argv Output argv array
 * @param max_args Maximum number of arguments
 * @return Number of arguments parsed
 */
int process_parse_command(const char *command, char **argv, int max_args);

/**
 * Free argv array allocated by process_parse_command
 * @param argv Array to free
 * @param count Number of elements
 */
void process_free_argv(char **argv, int count);

/**
 * Spawn process with PTY (with privilege separation)
 * @param pty PTY to use
 * @param command Command to execute
 * @param env Environment variables (NULL = inherit)
 * @param creds Credentials for privilege separation (NULL = no separation)
 * @return Process ID or -1 on error
 */
pid_t process_spawn_with_creds(pty_t *pty, const char *command, char **env, const process_credentials_t *creds);

/**
 * Spawn process with PTY (legacy, no privilege separation)
 * @param pty PTY to use
 * @param command Command to execute
 * @param env Environment variables (NULL = inherit)
 * @return Process ID or -1 on error
 */
pid_t process_spawn(pty_t *pty, const char *command, char **env);

/**
 * Spawn process with argv array
 * @param pty PTY to use
 * @param argv Argument array (NULL-terminated)
 * @param env Environment variables (NULL = inherit)
 * @return Process ID or -1 on error
 */
pid_t process_spawn_argv(pty_t *pty, char **argv, char **env);

/**
 * Spawn process with argv array and credentials
 * @param pty PTY to use
 * @param argv Argument array (NULL-terminated)
 * @param env Environment variables (NULL = inherit)
 * @param creds Credentials for privilege separation (NULL = no separation)
 * @return Process ID or -1 on error
 */
pid_t process_spawn_argv_with_creds(pty_t *pty, char **argv, char **env, const process_credentials_t *creds);

/**
 * Wait for process to exit
 * @param pid Process ID
 * @param status Output status (optional)
 * @param timeout_ms Timeout in milliseconds (-1 = blocking)
 * @return true if process exited
 */
bool process_wait(pid_t pid, int *status, int timeout_ms);

/**
 * Check if process is running
 * @param pid Process ID
 * @return true if running
 */
bool process_is_running(pid_t pid);

/**
 * Send signal to process
 * @param pid Process ID
 * @param sig Signal number
 * @return true on success
 */
bool process_kill(pid_t pid, int sig);

#endif /* PSHELL_PROCESS_SPAWN_H */