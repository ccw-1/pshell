#ifndef PSHELL_PTY_POSIX_H
#define PSHELL_PTY_POSIX_H

#include <sys/types.h>
#include <termios.h>
#include <stdbool.h>

/**
 * POSIX PTY operations for pshell
 * Portable pseudo-terminal handling
 */

typedef struct {
    int master_fd;           /* Master PTY file descriptor */
    int slave_fd;            /* Slave PTY file descriptor */
    char slave_name[256];    /* Slave PTY device name */
    struct termios orig_termios; /* Original terminal settings */
    pid_t child_pid;         /* Child process PID */
    int rows;                /* Terminal rows */
    int cols;                /* Terminal columns */
} pty_t;

/**
 * Create a new PTY pair
 * @param rows Terminal rows
 * @param cols Terminal columns
 * @return PTY structure or NULL on error
 */
pty_t* pty_create(int rows, int cols);

/**
 * Destroy PTY and close file descriptors
 * @param pty PTY to destroy
 */
void pty_destroy(pty_t *pty);

/**
 * Resize PTY terminal
 * @param pty PTY
 * @param rows New rows
 * @param cols New columns
 * @return true on success
 */
bool pty_resize(pty_t *pty, int rows, int cols);

/**
 * Get master file descriptor
 * @param pty PTY
 * @return Master FD or -1
 */
int pty_get_master_fd(const pty_t *pty);

/**
 * Get slave file descriptor
 * @param pty PTY
 * @return Slave FD or -1
 */
int pty_get_slave_fd(const pty_t *pty);

/**
 * Get slave device name
 * @param pty PTY
 * @return Slave device name or NULL
 */
const char* pty_get_slave_name(const pty_t *pty);

/**
 * Set PTY to raw mode (no echo, no line buffering)
 * @param pty PTY
 * @return true on success
 */
bool pty_set_raw_mode(pty_t *pty);

/**
 * Restore original terminal settings
 * @param pty PTY
 * @return true on success
 */
bool pty_restore_mode(pty_t *pty);

/**
 * Read from PTY master
 * @param pty PTY
 * @param buf Buffer to read into
 * @param size Buffer size
 * @return Number of bytes read or -1 on error
 */
ssize_t pty_read(pty_t *pty, void *buf, size_t size);

/**
 * Write to PTY master
 * @param pty PTY
 * @param buf Buffer to write from
 * @param size Number of bytes to write
 * @return Number of bytes written or -1 on error
 */
ssize_t pty_write(pty_t *pty, const void *buf, size_t size);

/**
 * Check if PTY has data available to read
 * @param pty PTY
 * @param timeout_ms Timeout in milliseconds (-1 for blocking)
 * @return true if data available
 */
bool pty_has_data(pty_t *pty, int timeout_ms);

/**
 * Set child process PID
 * @param pty PTY
 * @param pid Child PID
 */
void pty_set_child_pid(pty_t *pty, pid_t pid);

/**
 * Get child process PID
 * @param pty PTY
 * @return Child PID or -1
 */
pid_t pty_get_child_pid(const pty_t *pty);

#endif /* PSHELL_PTY_POSIX_H */
