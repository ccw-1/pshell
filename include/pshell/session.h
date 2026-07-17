#ifndef PSHELL_SESSION_H
#define PSHELL_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

/**
 * Session management for pshell
 * Manages terminal sessions with PTY integration
 */

typedef struct session session_t;
typedef struct session_manager session_manager_t;

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
 * @return Manager instance or NULL on error
 */
session_manager_t* session_manager_create(int max_sessions);

/**
 * Destroy session manager
 * @param mgr Manager to destroy
 */
void session_manager_destroy(session_manager_t *mgr);

/**
 * Create new session
 * @param mgr Session manager
 * @param command Command to execute
 * @param rows Terminal rows
 * @param cols Terminal columns
 * @return Session ID or NULL on error
 */
const char* session_manager_create_session(session_manager_t *mgr,
                                           const char *command,
                                           int rows, int cols);

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
void session_get_cursor_position(const session_t *session, int *row, int *col);

#endif /* PSHELL_SESSION_H */
