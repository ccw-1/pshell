#ifndef PSHELL_SESSION_H
#define PSHELL_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>
#include "io_multiplexer.h"

/**
 * Session management for pshell
 * Manages terminal sessions with PTY integration
 */

typedef struct session session_t;
typedef struct session_manager session_manager_t;
typedef unsigned long session_line_no_t;

/* Session state */
typedef enum {
    SESSION_STATE_STARTING,
    SESSION_STATE_ACTIVE,
    SESSION_STATE_IDLE,
    SESSION_STATE_DEAD
} session_state_t;

/* Session info structure */
typedef struct {
    char id[64];
    char command[256];
    int rows;
    int cols;
    session_state_t state;
    pid_t pid;
    time_t created;
    time_t last_activity;
} session_info_t;

/**
 * Create session manager
 * @param max_sessions Maximum number of concurrent sessions
 * @param mux I/O multiplexer for monitoring session PTY file descriptors
 * @return Manager instance or NULL on error
 */
session_manager_t* session_manager_create(int max_sessions, io_multiplexer_t *mux);

/**
 * Destroy session manager
 * @param mgr Manager to destroy
 */
void session_manager_destroy(session_manager_t *mgr);

/**
 * Create new session with privilege separation
 * @param mgr Session manager
 * @param command Command to execute
 * @param rows Terminal rows
 * @param cols Terminal columns
 * @param uid User ID (0 = use daemon's UID)
 * @param gid Group ID (0 = use daemon's GID)
 * @param username Username for logging (optional)
 * @param scrollback_lines Number of scrollback lines (0 = default 1000)
 * @param output_file Path to output log file (NULL = no logging)
 * @param rotate_count Number of rotation files (0 = no rotation)
 * @param rotate_lines Max lines per file before rotation (0 = no rotation)
 * @return Session ID or NULL on error
 */
const char* session_manager_create_session_as_user(session_manager_t *mgr,
                                                    const char *command,
                                                    int rows, int cols,
                                                    uid_t uid, gid_t gid,
                                                    const char *username,
                                                    int scrollback_lines,
                                                    const char *output_file,
                                                    int rotate_count,
                                                    int rotate_lines);

/**
 * Create new session (legacy, no privilege separation)
 * @param mgr Session manager
 * @param command Command to execute
 * @param rows Terminal rows
 * @param cols Terminal columns
 * @param scrollback_lines Number of scrollback lines (0 = default 1000)
 * @param output_file Path to output log file (NULL = no logging)
 * @param rotate_count Number of rotation files (0 = no rotation)
 * @param rotate_lines Max lines per file before rotation (0 = no rotation)
 * @return Session ID or NULL on error
 */
const char* session_manager_create_session(session_manager_t *mgr,
                                           const char *command,
                                           int rows, int cols,
                                           int scrollback_lines,
                                           const char *output_file,
                                           int rotate_count,
                                           int rotate_lines);

/**
 * Get session by ID
 * @param mgr Session manager
 * @param session_id Session ID
 * @return Session or NULL if not found
 */
session_t* session_manager_get(session_manager_t *mgr, const char *session_id);

/**
 * Destroy session
 * @param mgr Session manager
 * @param session_id Session ID
 * @return true on success
 */
bool session_manager_destroy_session(session_manager_t *mgr, const char *session_id);

/**
 * Cleanup dead sessions
 * @param mgr Session manager
 * @return Number of sessions cleaned up
 */
int session_manager_cleanup_dead_sessions(session_manager_t *mgr);

/**
 * List all sessions
 * @param mgr Session manager
 * @param infos Output array of session info
 * @param max_count Maximum number of sessions to return
 * @return Number of sessions returned
 */
int session_manager_list(session_manager_t *mgr, session_info_t *infos, int max_count);

/**
 * Get session count
 * @param mgr Session manager
 * @return Number of active sessions
 */
int session_manager_count(const session_manager_t *mgr);

/**
 * Cleanup dead sessions
 * @param mgr Session manager
 * @return Number of sessions cleaned up
 */
int session_manager_cleanup(session_manager_t *mgr);

/**
 * Get session file descriptor
 * @param session Session
 * @return PTY master file descriptor or -1 on error
 */
int session_get_fd(session_t *session);

/**
 * Get session info
 * @param session Session
 * @param info Output session info
 * @return true on success
 */
bool session_get_info(const session_t *session, session_info_t *info);

/**
 * Get session ID
 * @param session Session
 * @return Session ID
 */
const char* session_get_id(const session_t *session);

/**
 * Get session PTY master FD
 * @param session Session
 * @return PTY master FD or -1
 */
int session_get_pty_fd(const session_t *session);

/**
 * Send input to session
 * @param session Session
 * @param data Input data
 * @param len Data length
 * @return Number of bytes written or -1 on error
 */
ssize_t session_send_input(session_t *session, const char *data, size_t len);

/**
 * Read output from session
 * @param session Session
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes read or -1 on error
 */
ssize_t session_read_output(session_t *session, char *buffer, size_t size);

/**
 * Resize session terminal
 * @param session Session
 * @param rows New rows
 * @param cols New columns
 * @return true on success
 */
bool session_resize(session_t *session, int rows, int cols);

/**
 * Check if session is alive
 * @param session Session
 * @return true if alive
 */
bool session_is_alive(const session_t *session);

/**
 * Get session state
 * @param session Session
 * @return Session state
 */
session_state_t session_get_state(const session_t *session);

/**
 * Update session activity timestamp
 * @param session Session
 */
void session_update_activity(session_t *session);

/**
 * Get screen buffer text (for rendering)
 * @param session Session
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of characters written
 */
size_t session_get_screen_text(const session_t *session, char *buffer, size_t size);

/**
 * Get screen line
 * @param session Session
 * @param row Row number (0-based)
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of characters written
 */
size_t session_get_screen_line(const session_t *session, int row, char *buffer, size_t size);

/**
 * Get cursor position
 * @param session Session
 * @param row Output row (0-based)
 * @param col Output column (0-based)
 */

/**
 * Get available raw PTY output data size
 * @param session Session
 * @return Number of bytes available in raw buffer
 */
size_t session_get_raw_available(const session_t *session);

/**
 * Read raw PTY output from circular buffer (consumes data)
 * @param session Session
 * @param buffer Output buffer
 * @param size Maximum bytes to read
 * @return Number of bytes read
 */
size_t session_read_raw_output(session_t *session, char *buffer, size_t size);

/**
 * Peek at raw PTY output without consuming it
 * @param session Session
 * @param buffer Output buffer
 * @param size Maximum bytes to peek
 * @return Number of bytes peeked
 */
size_t session_peek_raw_output(const session_t *session, char *buffer, size_t size);

/**
 * Clear raw PTY output buffer
 * @param session Session
 */

/**
 * Start expect operation - allocate and activate expect buffer
 * @param session Session
 * @param skip_seeding If true, don't seed buffer from screen (incoming-only mode)
 * @return 0 on success, -1 on error
 */
int session_start_expect(session_t *session, bool skip_seeding);

/**
 * Check whether an expect operation is currently active on this session
 * (thread-safe)
 * @param session Session
 * @return true if an expect is in progress
 */
bool session_expect_active(session_t *session);

/**
 * Stop expect operation - deactivate expect buffer
 * @param session Session
 */
void session_stop_expect(session_t *session);

/**
 * Clear expect buffer contents
 * @param session Session
 */
void session_clear_expect_buffer(session_t *session);

/**
 * Get the current monotonic line counter for the session.
 * The counter starts at 1 and increments whenever a newline is read from the PTY.
 * @param session Session
 * @return Current session line number
 */
session_line_no_t session_get_current_line_no(const session_t *session);

/**
 * Read from expect buffer (non-destructive peek)
 * @param session Session
 * @param buffer Output buffer
 * @param size Maximum bytes to read
 * @param start_line Exclusive lower bound for incoming-only reads; only data from
 *        lines greater than this snapshot line is returned, plus partial data that
 *        arrives on the snapshot line after the snapshot byte offset.
 * @param start_offset Byte offset within start_line for partial-line incoming reads
 * @param next_line Output current line snapshot after the read (optional)
 * @param next_offset Output current byte offset within the current line after the read (optional)
 * @return Number of bytes read
 */
ssize_t session_read_expect_output(session_t *session, char *buffer, size_t size,
                                   session_line_no_t start_line, size_t start_offset,
                                   session_line_no_t *next_line, size_t *next_offset);

void session_clear_raw_buffer(session_t *session);

/**
 * Get scrollback buffer size
 * @param session Session
 * @return Number of scrollback lines configured
 */
int session_get_scrollback_size(const session_t *session);

/**

/**
 * Read a range of lines from session output log
 * Works across rotated files to provide seamless access to historical output
 * @param session Session
 * @param start_line Starting line number (1-based, inclusive)
 * @param end_line Ending line number (1-based, inclusive)
 * @param out_len Output: number of bytes in returned buffer
 * @return Allocated buffer containing the lines, or NULL on error. Caller must free().
 */
char* session_read_line_range(session_t *session,
                               session_line_no_t start_line,
                               session_line_no_t end_line,
                               size_t *out_len);

/**
 * Get total number of lines written to session output log
 * @param session Session
 * @return Total lines written (global counter, never resets)
 */
session_line_no_t session_get_total_lines(const session_t *session);

/**
 * Get scrollback buffer used lines
 * @param session Session
 * @return Number of lines currently in scrollback buffer
 */
int session_get_scrollback_used(const session_t *session);

/**
 * Get scrollback line
 * @param session Session
 * @param line_index Line index (0 = oldest, scrollback_used-1 = newest)
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of characters written, or -1 if index out of bounds
 */
ssize_t session_get_scrollback_line(const session_t *session, int line_index, char *buffer, size_t size);



void session_get_cursor_position(const session_t *session, int *row, int *col);

#endif /* PSHELL_SESSION_H */
