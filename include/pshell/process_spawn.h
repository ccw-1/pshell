#ifndef PSHELL_PROCESS_SPAWN_H
#define PSHELL_PROCESS_SPAWN_H

#include "pshell/pty_posix.h"
#include <sys/types.h>
#include <stdbool.h>

/**
 * Process spawning for pshell
 * Handles fork/exec in PTY environment
 */

/**
 * Spawn a process in a PTY
 * @param pty PTY to spawn process in
 * @param command Command to execute (will be parsed)
 * @param env Environment variables (NULL-terminated array, NULL for inherit)
 * @return Child PID or -1 on error
 */
pid_t process_spawn(pty_t *pty, const char *command, char **env);

/**
 * Spawn a process with explicit argv
 * @param pty PTY to spawn process in
 * @param argv Argument array (NULL-terminated)
 * @param env Environment variables (NULL-terminated array, NULL for inherit)
 * @return Child PID or -1 on error
 */
pid_t process_spawn_argv(pty_t *pty, char **argv, char **env);

/**
 * Wait for process to exit
 * @param pid Process ID
 * @param status Output status code
 * @param timeout_ms Timeout in milliseconds (-1 for blocking)
 * @return true if process exited, false on timeout
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

/**
 * Parse command string into argv array
 * @param command Command string
 * @param argv Output array (caller must free)
 * @param max_args Maximum arguments
 * @return Number of arguments parsed
 */
int process_parse_command(const char *command, char **argv, int max_args);

/**
 * Free argv array
 * @param argv Array to free
 * @param count Number of elements
 */
void process_free_argv(char **argv, int count);

#endif /* PSHELL_PROCESS_SPAWN_H */
