#ifndef PSHELL_SESSION_H
#define PSHELL_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

/* Room for a session command (up to 255 chars) with every character JSON-escaped */
#define SESSION_COMMAND_JSON_MAX (255 * 6 + 1)

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
 * Bytes session_get_screen_text() needs for the current screen, including
 * the NUL (the screen can change meanwhile; add some slack)
 */
size_t session_get_screen_text_size(const session_t *session);

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

/* Expect window: the most recent PTY output kept while expects are attached
 * (oldest half dropped when full) and the number of concurrent expects per session */
#define SESSION_EXPECT_WINDOW (256 * 1024)
#define SESSION_MAX_EXPECTS 16

/**
 * Attach an expect operation to the session: the expect window is allocated
 * on first use. Several expects can be attached at once.
 * @param session Session
 * @param start_pos Output stream position of the end of the output so far
 *        (where an incoming-only expect starts reading)
 * @return 0 on success, -1 on error (errno EBUSY: SESSION_MAX_EXPECTS reached)
 */
int session_expect_attach(session_t *session, uint64_t *start_pos);

/**
 * Detach an expect operation; the window is freed with the last one.
 */
void session_expect_detach(session_t *session);

/**
 * Check whether an expect operation is currently active on this session
 * (thread-safe)
 */
bool session_expect_active(session_t *session);

/**
 * Number of expect operations attached to the session (thread-safe)
 */
int session_expect_count(session_t *session);

/**
 * Get the current monotonic line counter for the session.
 * The counter starts at 1 and increments whenever a newline is read from the PTY.
 * @param session Session
 * @return Current session line number
 */
session_line_no_t session_get_current_line_no(const session_t *session);

/**
 * Read from the expect window (non-destructive peek)
 * @param session Session
 * @param pos In/out stream position to read from; advanced by the bytes read.
 *        A position older than the window is moved to its start.
 * @param buffer Output buffer
 * @param size Maximum bytes to read
 * @return Bytes read (0 = nothing new), -1 if no expect is attached
 */
ssize_t session_expect_read(session_t *session, uint64_t *pos, char *buffer, size_t size);

void session_clear_raw_buffer(session_t *session);

/* Interactive terminals attachable to one session: default and upper bound
 * (the daemon's select()-based multiplexer tops out near 1024 descriptors) */
#define SESSION_DEFAULT_MAX_ATTACH 16
#define SESSION_MAX_ATTACH_LIMIT 256

/**
 * Set how many terminals may attach to each session. Call once at startup,
 * before sessions are created; sessions created earlier keep their limit.
 * @return false if max_attach is outside 1..SESSION_MAX_ATTACH_LIMIT
 */
bool session_manager_set_max_attach(session_manager_t *mgr, int max_attach);

/**
 * Attach an interactive client to a session. On success the session owns
 * fd: bytes arriving on it are written to the PTY, PTY output is copied to
 * it, and it is closed when the client disconnects or the session ends.
 * The current screen is sent first so the client starts with a redraw.
 * @param session Session
 * @param fd Connected stream socket (set non-blocking by this call)
 * @return true on success (fd now owned by the session); false leaves fd to the caller
 */
bool session_attach_client(session_t *session, int fd);

/**
 * Send a redraw of the current screen (clear, text, cursor position) to an
 * attached client. Call after the handshake response has been written.
 * @param session Session
 * @param fd The fd passed to session_attach_client
 */
void session_attach_redraw(session_t *session, int fd);

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

/**
 * Scrollback lines as the inside of a JSON array ("a","b",...), malloc'd.
 * Lines are not limited in length; the output stops once it would exceed
 * max_bytes (at least one line is always returned).
 * @param out_count Number of lines included (out)
 * @return String to free, or NULL on error / no line available
 */
char *session_get_scrollback_json(const session_t *session, int start_line, int count,
                                  size_t max_bytes, int *out_count);

/* Byte budget of one get_scrollback response (clients read 64 KB) */
#define SESSION_SCROLLBACK_MAX_BYTES (48 * 1024)



void session_get_cursor_position(const session_t *session, int *row, int *col);

/**
 * Cursor position as of the last PTY output, safe to call from a thread other
 * than the event loop (expect workers).
 */
void session_get_expect_cursor(session_t *session, int *row, int *col);

#endif /* PSHELL_SESSION_H */
