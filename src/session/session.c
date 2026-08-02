#include "pshell/session.h"
#include "pshell/pty_posix.h"
#include "pshell/process_spawn.h"
#include "pshell/uuid_simple.h"
#include "pshell/logger.h"
#include "pshell/vt100_parser.h"
#include "pshell/screen_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_SESSIONS 100
#define RAW_BUFFER_SIZE (64 * 1024)  /* 64KB circular buffer for raw PTY output */

/* Forward declarations */
static void session_pty_callback(int fd, io_event_type_t events, void *user_data);
static void session_flush_output_log(session_t *session);

/* Session structure */
struct session {
    char id[64];
    char command[256];
    int rows;
    int cols;
    session_state_t state;
    pid_t pid;
    pty_t *pty;
    time_t created;
    time_t last_activity;
    bool active;
    
    /* Terminal emulation */
    screen_buffer_t *screen;
    vt100_parser_t parser;
    
    /* Raw PTY output buffer (circular) */
    char *raw_buffer;
    size_t raw_buffer_size;
    size_t raw_write_pos;
    size_t raw_read_pos;
    size_t raw_available;
    
    /* Expect buffer (active only during expect operations) */
    char *expect_buffer;
    size_t expect_buffer_size;
    size_t expect_buffer_used;
    bool expect_active;
    
    /* Output logging */
    char output_file[512];
    int output_fd;
    char *output_buffer;
    size_t output_buffer_used;
    size_t output_buffer_size;
    
    /* Output rotation */
    int rotate_count;        /* Number of rotation files (0 = no rotation) */
    int rotate_lines;        /* Max lines per file before rotation */
    int current_line_count;  /* Current line count in active file */
};

/* Session manager structure */
struct session_manager {
    session_t sessions[MAX_SESSIONS];
    int max_sessions;
    int session_count;
    io_multiplexer_t *mux;  /* I/O multiplexer for monitoring PTY FDs */
};

session_manager_t* session_manager_create(int max_sessions, io_multiplexer_t *mux) {
    if (max_sessions <= 0 || max_sessions > MAX_SESSIONS) {
        log_error("Invalid max_sessions: %d", max_sessions);
        return NULL;
    }
    
    if (!mux) {
        log_error("Invalid I/O multiplexer");
        return NULL;
    }
    
    session_manager_t *mgr = calloc(1, sizeof(session_manager_t));
    if (!mgr) {
        log_error("Failed to allocate session manager");
        return NULL;
    }
    
    mgr->max_sessions = max_sessions;
    mgr->session_count = 0;
    mgr->mux = mux;
    
    /* Initialize all sessions as inactive */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        mgr->sessions[i].active = false;
        mgr->sessions[i].pty = NULL;
    }
    
    log_info("Created session manager (max_sessions=%d)", max_sessions);
    return mgr;
}

void session_manager_destroy(session_manager_t *mgr) {
    if (!mgr) return;
    
    /* Destroy all active sessions */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (mgr->sessions[i].active) {
            session_t *session = &mgr->sessions[i];
            
            /* Kill process */
            if (session->pid > 0) {
                kill(session->pid, SIGTERM);
                waitpid(session->pid, NULL, WNOHANG);
            }
            
            /* Destroy PTY */
            if (session->pty) {
                pty_destroy(session->pty);
                session->pty = NULL;
            }
        }
    }
    
    free(mgr);
    log_info("Destroyed session manager");
}

/* Output logging helper functions */
#define OUTPUT_BUFFER_SIZE 8192

static void session_rotate_output_files(session_t *session) {
    if (!session || session->rotate_count <= 0) {
        return;
    }
    
    char old_path[600];
    char new_path[600];
    
    /* Remove oldest file (e.g., output.3 if rotate_count is 4) */
    snprintf(old_path, sizeof(old_path), "%s.%d", session->output_file, session->rotate_count - 1);
    unlink(old_path);  /* Ignore errors if file doesn't exist */
    
    /* Rotate files: .2 -> .3, .1 -> .2, .0 -> .1 */
    for (int i = session->rotate_count - 2; i >= 0; i--) {
        snprintf(old_path, sizeof(old_path), "%s.%d", session->output_file, i);
        snprintf(new_path, sizeof(new_path), "%s.%d", session->output_file, i + 1);
        rename(old_path, new_path);  /* Ignore errors if file doesn't exist */
    }
    
    /* Close current file */
    if (session->output_fd >= 0) {
        session_flush_output_log(session);
        close(session->output_fd);
        session->output_fd = -1;
    }
    
    /* Rename current file to .0 */
    snprintf(new_path, sizeof(new_path), "%s.0", session->output_file);
    rename(session->output_file, new_path);
    
    /* Open new file */
    session->output_fd = open(session->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (session->output_fd < 0) {
        log_error("Failed to open rotated output file '%s': %s", session->output_file, strerror(errno));
    } else {
        log_info("Rotated output file for session %s (lines: %d)", session->id, session->current_line_count);
    }
    
    /* Reset line counter */
    session->current_line_count = 0;
}

static bool session_open_output_file(session_t *session, const char *output_file) {
    if (!session || !output_file || !output_file[0]) {
        return false;
    }
    
    /* Open file in append mode */
    session->output_fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (session->output_fd < 0) {
        log_error("Failed to open output file '%s': %s", output_file, strerror(errno));
        return false;
    }
    
    /* Allocate write buffer */
    session->output_buffer = malloc(OUTPUT_BUFFER_SIZE);
    if (!session->output_buffer) {
        log_error("Failed to allocate output buffer");
        close(session->output_fd);
        session->output_fd = -1;
        return false;
    }
    
    session->output_buffer_size = OUTPUT_BUFFER_SIZE;
    session->output_buffer_used = 0;
    strncpy(session->output_file, output_file, sizeof(session->output_file) - 1);
    session->output_file[sizeof(session->output_file) - 1] = '\0';
    
    log_info("Opened output file for session %s: %s", session->id, output_file);
    return true;
}

static void session_write_output_log(session_t *session, const char *data, size_t len) {
    if (!session || session->output_fd < 0 || !data || len == 0) {
        return;
    }
    
    /* Write to buffer and count newlines for rotation */
    for (size_t i = 0; i < len; i++) {
        session->output_buffer[session->output_buffer_used++] = data[i];
        
        /* Count newlines for rotation */
        if (session->rotate_count > 0 && data[i] == '\n') {
            session->current_line_count++;
            
            /* Check if rotation needed */
            if (session->current_line_count >= session->rotate_lines) {
                /* Flush current buffer first */
                if (session->output_buffer_used > 0) {
                    ssize_t written = write(session->output_fd, session->output_buffer, session->output_buffer_used);
                    if (written < 0) {
                        log_error("Failed to write to output file: %s", strerror(errno));
                    }
                    session->output_buffer_used = 0;
                }
                
                /* Rotate files */
                session_rotate_output_files(session);
            }
        }
        
        /* Flush if buffer full */
        if (session->output_buffer_used >= session->output_buffer_size) {
            ssize_t written = write(session->output_fd, session->output_buffer, session->output_buffer_used);
            if (written < 0) {
                log_error("Failed to write to output file: %s", strerror(errno));
            }
            session->output_buffer_used = 0;
        }
    }
}

static void session_flush_output_log(session_t *session) {
    if (!session || session->output_fd < 0 || session->output_buffer_used == 0) {
        return;
    }
    
    ssize_t written = write(session->output_fd, session->output_buffer, session->output_buffer_used);
    if (written < 0) {
        log_error("Failed to flush output file: %s", strerror(errno));
    }
    session->output_buffer_used = 0;
}

static void session_close_output_file(session_t *session) {
    if (!session) return;
    
    /* Flush remaining data */
    session_flush_output_log(session);
    
    /* Close file */
    if (session->output_fd >= 0) {
        close(session->output_fd);
        session->output_fd = -1;
        log_info("Closed output file for session %s", session->id);
    }
    
    /* Free buffer */
    if (session->output_buffer) {
        free(session->output_buffer);
        session->output_buffer = NULL;
        session->output_buffer_size = 0;
        session->output_buffer_used = 0;
    }
    
    session->output_file[0] = '\0';
}

/* PTY callback - called when PTY has data to read */
static void session_pty_callback(int fd, io_event_type_t events, void *user_data) {
    (void)fd;  /* Unused parameter */
    session_t *session = (session_t *)user_data;
    
    if (!session || !session->active || !session->pty) {
        log_warn("PTY callback for inactive session");
        return;
    }
    
    if (events & IO_EVENT_READ) {
        char buffer[4096];
        ssize_t n = pty_read(session->pty, buffer, sizeof(buffer));
        
        if (n > 0) {
            session->last_activity = time(NULL);
            
            /* Write to output log file if enabled */
            if (session->output_fd >= 0) {
                session_write_output_log(session, buffer, n);
            }
            
            /* Store raw output in circular buffer */
            if (session->raw_buffer) {
                for (ssize_t i = 0; i < n; i++) {
                    session->raw_buffer[session->raw_write_pos] = buffer[i];
                    session->raw_write_pos = (session->raw_write_pos + 1) % session->raw_buffer_size;
                    
                    /* Update available count, handle overflow */
                    if (session->raw_available < session->raw_buffer_size) {
                        session->raw_available++;
                    } else {
                        /* Buffer full, advance read position (drop oldest data) */
                        session->raw_read_pos = (session->raw_read_pos + 1) % session->raw_buffer_size;
                    }
                }
            }
            
            /* Also write to expect buffer if expect is active */
            if (session->expect_active && session->expect_buffer) {
                for (ssize_t i = 0; i < n; i++) {
                    if (session->expect_buffer_used < session->expect_buffer_size) {
                        session->expect_buffer[session->expect_buffer_used++] = buffer[i];
                    }
                }
            }
            
            /* Parse VT100 sequences and update screen buffer */
            if (session->screen) {
                vt100_sequence_t seq;
                for (ssize_t i = 0; i < n; i++) {
                    if (vt100_parser_feed(&session->parser, buffer[i], &seq)) {
                        screen_buffer_process_sequence(session->screen, &seq);
                    }
                }
            }
            
            log_debug("Read %zd bytes from PTY for session %s", n, session->id);
        } else if (n == 0) {
            /* EOF - process has exited */
            log_info("PTY EOF for session %s - process exited", session->id);
            session->state = SESSION_STATE_DEAD;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            log_debug("PTY read error for session %s: %s", session->id, strerror(errno));
        }
    }
    
    if (events & IO_EVENT_ERROR) {
        log_debug("PTY error event for session %s", session->id);
        session->state = SESSION_STATE_DEAD;
    }
}

const char* session_manager_create_session_as_user(session_manager_t *mgr,
                                                    const char *command,
                                                    int rows, int cols,
                                                    uid_t uid, gid_t gid,
                                                    const char *username,
                                                    int scrollback_lines,
                                                    const char *output_file,
                                                    int rotate_count,
                                                    int rotate_lines) {
    if (!mgr || !command) {
        log_error("Invalid parameters");
        return NULL;
    }
    
    if (mgr->session_count >= mgr->max_sessions) {
        log_error("Maximum sessions reached (%d)", mgr->max_sessions);
        return NULL;
    }
    
    /* Find free slot */
    session_t *session = NULL;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!mgr->sessions[i].active) {
            session = &mgr->sessions[i];
            break;
        }
    }
    
    if (!session) {
        log_error("No free session slots");
        return NULL;
    }
    
    /* Generate session ID */
    char uuid[UUID_STRING_LENGTH];
    uuid_generate(uuid);
    strncpy(session->id, uuid, sizeof(session->id) - 1);
    
    /* Copy command */
    strncpy(session->command, command, sizeof(session->command) - 1);
    session->command[sizeof(session->command) - 1] = '\0';
    
    /* Set terminal size */
    session->rows = rows > 0 ? rows : 24;
    session->cols = cols > 0 ? cols : 80;
    
    /* Create PTY */
    session->pty = pty_create(session->rows, session->cols);
    if (!session->pty) {
        log_error("Failed to create PTY");
        return NULL;
    }
    
    /* Setup credentials for privilege separation */
    process_credentials_t creds = {0};
    if (uid != 0 || gid != 0) {
        creds.uid = uid;
        creds.gid = gid;
        creds.username = username;
    }
    
    /* Spawn process with privilege separation */
    session->pid = process_spawn_with_creds(session->pty, session->command, NULL, 
                                           (uid != 0 || gid != 0) ? &creds : NULL);
    if (session->pid < 0) {
        log_error("Failed to spawn process");
        pty_destroy(session->pty);
        session->pty = NULL;
        return NULL;
    }
    
    /* Set child PID in PTY */
    pty_set_child_pid(session->pty, session->pid);
    
    /* Initialize terminal emulation */
    int scrollback = (scrollback_lines > 0) ? scrollback_lines : 1000;
    session->screen = screen_buffer_create(session->rows, session->cols, scrollback);
    if (!session->screen) {
        log_error("Failed to create screen buffer");
        kill(session->pid, SIGTERM);
        waitpid(session->pid, NULL, WNOHANG);
        pty_destroy(session->pty);
        session->pty = NULL;
        return NULL;
    }
    vt100_parser_init(&session->parser);
    
    /* Initialize raw PTY output buffer */
    session->raw_buffer = malloc(RAW_BUFFER_SIZE);
    if (!session->raw_buffer) {
        log_error("Failed to allocate raw buffer");
        screen_buffer_destroy(session->screen);
        kill(session->pid, SIGTERM);
        waitpid(session->pid, NULL, WNOHANG);
        pty_destroy(session->pty);
        session->pty = NULL;
        session->screen = NULL;
        return NULL;
    }
    session->raw_buffer_size = RAW_BUFFER_SIZE;
    session->raw_write_pos = 0;
    session->raw_read_pos = 0;
    session->raw_available = 0;
    
    /* Initialize output logging (disabled by default) */
    session->output_file[0] = '\0';
    session->output_fd = -1;
    session->output_buffer = NULL;
    session->output_buffer_used = 0;
    session->output_buffer_size = 0;
    
    /* Initialize output rotation (disabled by default) */
    session->rotate_count = 0;
    session->rotate_lines = 0;
    session->current_line_count = 0;
    
    /* Initialize session state */
    session->state = SESSION_STATE_ACTIVE;
    session->created = time(NULL);
    session->last_activity = session->created;
    session->active = true;
    
    /* Add PTY master FD to multiplexer for monitoring */
    int pty_fd = pty_get_master_fd(session->pty);
    if (pty_fd >= 0 && mgr->mux) {
        if (!io_multiplexer_add(mgr->mux, pty_fd, IO_EVENT_READ, session_pty_callback, session)) {
            log_error("Failed to add PTY FD to multiplexer");
            screen_buffer_destroy(session->screen);
            kill(session->pid, SIGTERM);
            waitpid(session->pid, NULL, WNOHANG);
            pty_destroy(session->pty);
            session->pty = NULL;
            session->screen = NULL;
            return NULL;
        }
        log_debug("Added PTY FD %d to multiplexer for session %s", pty_fd, session->id);
    }
    
    mgr->session_count++;
    
    /* Open output log file if specified */
    if (output_file && output_file[0]) {
        /* Set rotation parameters before opening file */
        session->rotate_count = (rotate_count > 0) ? rotate_count : 0;
        session->rotate_lines = (rotate_lines > 0) ? rotate_lines : 0;
        session->current_line_count = 0;
        
        if (!session_open_output_file(session, output_file)) {
            log_warn("Failed to open output file, continuing without logging");
        }
    }
    
    if (username) {
        log_info("Created session: id=%s, command=%s, pid=%d, rows=%d, cols=%d, user=%s%s",
                 session->id, session->command, session->pid, session->rows, session->cols, username,
                 (session->output_fd >= 0) ? ", output_file=enabled" : "");
    } else {
        log_info("Created session: id=%s, command=%s, pid=%d, rows=%d, cols=%d%s",
                 session->id, session->command, session->pid, session->rows, session->cols,
                 (session->output_fd >= 0) ? ", output_file=enabled" : "");
    }
    
    return session->id;
}

const char* session_manager_create_session(session_manager_t *mgr,
                                           const char *command,
                                           int rows, int cols,
                                           int scrollback_lines,
                                           const char *output_file,
                                           int rotate_count,
                                           int rotate_lines) {
    return session_manager_create_session_as_user(mgr, command, rows, cols, 0, 0, NULL, scrollback_lines, output_file, rotate_count, rotate_lines);
}

session_t* session_manager_get(session_manager_t *mgr, const char *session_id) {
    if (!mgr || !session_id) return NULL;
    
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (mgr->sessions[i].active &&
            strcmp(mgr->sessions[i].id, session_id) == 0) {
            return &mgr->sessions[i];
        }
    }
    
    return NULL;
}

bool session_manager_destroy_session(session_manager_t *mgr, const char *session_id) {
    if (!mgr || !session_id) return false;
    
    session_t *session = session_manager_get(mgr, session_id);
    if (!session) {
        log_error("Session not found: %s", session_id);
        return false;
    }
    
    /* Kill process */
    if (session->pid > 0) {
        log_debug("Killing process: pid=%d", session->pid);
        kill(session->pid, SIGTERM);
        
        /* Wait briefly for process to exit */
        for (int i = 0; i < 10; i++) {
            int status;
            pid_t result = waitpid(session->pid, &status, WNOHANG);
            if (result > 0) break;
            usleep(100000);  /* 100ms */
        }
        
        /* Force kill if still alive */
        kill(session->pid, SIGKILL);
        waitpid(session->pid, NULL, WNOHANG);
    }
    
    /* Close output log file if open */
    session_close_output_file(session);
    
    /* Remove PTY FD from multiplexer */
    if (session->pty && mgr->mux) {
        int pty_fd = pty_get_master_fd(session->pty);
        if (pty_fd >= 0) {
            io_multiplexer_remove(mgr->mux, pty_fd);
            log_debug("Removed PTY FD %d from multiplexer for session %s", pty_fd, session->id);
        }
    }
    
    /* Destroy PTY */
    if (session->pty) {
        pty_destroy(session->pty);
        session->pty = NULL;
    }
    
    /* Destroy terminal emulation */
    if (session->screen) {
        screen_buffer_destroy(session->screen);
        session->screen = NULL;
    }
    
    /* Free raw buffer */
    if (session->raw_buffer) {
        free(session->raw_buffer);
        session->raw_buffer = NULL;
    }
    
    /* Mark as inactive */
    session->active = false;
    mgr->session_count--;
    
    log_info("Destroyed session: id=%s", session_id);
    
    return true;
}

int session_manager_list(session_manager_t *mgr, session_info_t *infos, int max_count) {
    if (!mgr || !infos || max_count <= 0) return 0;
    
    int count = 0;
    for (int i = 0; i < MAX_SESSIONS && count < max_count; i++) {
        if (mgr->sessions[i].active) {
            session_get_info(&mgr->sessions[i], &infos[count]);
            count++;
        }
    }
    
    return count;
}

int session_manager_count(const session_manager_t *mgr) {
    return mgr ? mgr->session_count : 0;
}

int session_manager_cleanup_dead_sessions(session_manager_t *mgr) {
    if (!mgr) return 0;
    
    int cleaned = 0;
    
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!mgr->sessions[i].active) continue;
        
        session_t *session = &mgr->sessions[i];
        
        /* Check if process is dead */
        if (session->pid > 0) {
            int status;
            pid_t result = waitpid(session->pid, &status, WNOHANG);
            
            if (result > 0) {
                /* Process exited */
                log_info("Session process exited: id=%s, pid=%d", session->id, session->pid);
                session->state = SESSION_STATE_DEAD;
                session_manager_destroy_session(mgr, session->id);
                cleaned++;
            } else if (result < 0 && errno == ECHILD) {
                /* Process doesn't exist */
                log_warn("Session process not found: id=%s, pid=%d", session->id, session->pid);
                session->state = SESSION_STATE_DEAD;
                session_manager_destroy_session(mgr, session->id);
                cleaned++;
            }
        }
    }
    
    if (cleaned > 0) {
        log_debug("Cleaned up %d dead sessions", cleaned);
    }
    
    return cleaned;
}

bool session_get_info(const session_t *session, session_info_t *info) {
    if (!session || !info) return false;
    
    strncpy(info->id, session->id, sizeof(info->id) - 1);
    strncpy(info->command, session->command, sizeof(info->command) - 1);
    info->rows = session->rows;
    info->cols = session->cols;
    info->state = session->state;
    info->pid = session->pid;
    info->created = session->created;
    info->last_activity = session->last_activity;
    
    return true;
}

const char* session_get_id(const session_t *session) {
    return session ? session->id : NULL;
}

int session_get_pty_fd(const session_t *session) {
    return (session && session->pty) ? pty_get_master_fd(session->pty) : -1;
}

ssize_t session_send_input(session_t *session, const char *data, size_t len) {
    if (!session || !data || len == 0) return -1;
    
    if (!session->pty) {
        log_error("Session PTY not open");
        return -1;
    }
    
    ssize_t written = pty_write(session->pty, data, len);
    
    if (written < 0) {
        log_error("Failed to write to PTY: %s", strerror(errno));
        return -1;
    }
    
    session_update_activity(session);
    
    return written;
}

ssize_t session_read_output(session_t *session, char *buffer, size_t size) {
    if (!session || !buffer || size == 0) return -1;
    
    /* If expect is active, read from expect buffer instead */
    if (session->expect_active) {
        return session_read_expect_output(session, buffer, size);
    }
    
    if (!session->raw_buffer) {
        log_error("Session raw buffer not available");
        return -1;
    }
    
    /* Read from raw circular buffer (streaming PTY output) */
    size_t n = session_read_raw_output(session, buffer, size);
    
    if (n == 0) {
        /* No data available - set errno for expect engine */
        errno = EAGAIN;
        return -1;
    }
    
    if (n > 0) {
        session_update_activity(session);
    }
    
    return (ssize_t)n;
}

bool session_resize(session_t *session, int rows, int cols) {
    if (!session || rows <= 0 || cols <= 0) return false;
    
    if (!pty_resize(session->pty, rows, cols)) {
        log_error("Failed to resize PTY");
        return false;
    }
    
    /* Resize screen buffer */
    if (session->screen) {
        if (screen_buffer_resize(session->screen, rows, cols) < 0) {
            log_error("Failed to resize screen buffer");
            return false;
        }
    }
    
    session->rows = rows;
    session->cols = cols;
    session_update_activity(session);
    
    log_debug("Resized session: id=%s, rows=%d, cols=%d", session->id, rows, cols);
    
    return true;
}

bool session_is_alive(const session_t *session) {
    if (!session || !session->active) return false;
    
    if (session->state == SESSION_STATE_DEAD) return false;
    
    if (session->pid <= 0) return false;
    
    /* Check if process exists */
    if (kill(session->pid, 0) < 0) {
        return false;
    }
    
    return true;
}

session_state_t session_get_state(const session_t *session) {
    return session ? session->state : SESSION_STATE_DEAD;
}

void session_update_activity(session_t *session) {
    if (session) {
        session->last_activity = time(NULL);
    }
}



size_t session_get_screen_text(const session_t *session, char *buffer, size_t size) {
    if (!session || !buffer || size == 0) return 0;
    
    if (!session->screen) {
        buffer[0] = '\0';
        return 0;
    }
    
    return screen_buffer_get_text(session->screen, buffer, size);
}

size_t session_get_screen_line(const session_t *session, int row, char *buffer, size_t size) {
    if (!session || !buffer || size == 0) return 0;
    
    if (!session->screen) {
        buffer[0] = '\0';
        return 0;
    }
    
    return screen_buffer_get_line(session->screen, row, buffer, size);
}

void session_get_cursor_position(const session_t *session, int *row, int *col) {
    if (!session || !session->screen) {
        if (row) *row = 0;
        if (col) *col = 0;
        return;
    }
    
    screen_buffer_get_cursor(session->screen, row, col);
}

int session_get_fd(session_t *session) {
    if (!session || !session->pty) {
        return -1;
    }
    
    return pty_get_master_fd(session->pty);
}



/**
 * Get available raw PTY output data size
 * @param session Session
 * @return Number of bytes available in raw buffer
 */
size_t session_get_raw_available(const session_t *session) {
    if (!session || !session->raw_buffer) {
        return 0;
    }
    return session->raw_available;
}

/**
 * Read raw PTY output from circular buffer
 * @param session Session
 * @param buffer Output buffer
 * @param size Maximum bytes to read
 * @return Number of bytes read
 */
size_t session_read_raw_output(session_t *session, char *buffer, size_t size) {
    if (!session || !session->raw_buffer || !buffer || size == 0) {
        return 0;
    }
    
    size_t to_read = (size < session->raw_available) ? size : session->raw_available;
    size_t bytes_read = 0;
    
    while (bytes_read < to_read) {
        buffer[bytes_read] = session->raw_buffer[session->raw_read_pos];
        session->raw_read_pos = (session->raw_read_pos + 1) % session->raw_buffer_size;
        bytes_read++;
    }
    
    session->raw_available -= bytes_read;
    return bytes_read;
}

/**
 * Peek at raw PTY output without consuming it
 * @param session Session
 * @param buffer Output buffer
 * @param size Maximum bytes to peek
 * @return Number of bytes peeked
 */
size_t session_peek_raw_output(const session_t *session, char *buffer, size_t size) {
    if (!session || !session->raw_buffer || !buffer || size == 0) {
        return 0;
    }
    
    size_t to_read = (size < session->raw_available) ? size : session->raw_available;
    size_t bytes_read = 0;
    size_t read_pos = session->raw_read_pos;
    
    while (bytes_read < to_read) {
        buffer[bytes_read] = session->raw_buffer[read_pos];
        read_pos = (read_pos + 1) % session->raw_buffer_size;
        bytes_read++;
    }
    
    return bytes_read;
}

/**
 * Start expect operation - allocate and activate expect buffer
 * @param session Session
 * @return 0 on success, -1 on error
 */
int session_start_expect(session_t *session) {
    if (!session) {
        return -1;
    }
    
    /* Already active */
    if (session->expect_active) {
        return 0;
    }
    
    /* Allocate expect buffer if not already allocated */
    if (!session->expect_buffer) {
        session->expect_buffer_size = RAW_BUFFER_SIZE;  /* Same size as raw buffer */
        session->expect_buffer = malloc(session->expect_buffer_size);
        if (!session->expect_buffer) {
            log_error("Failed to allocate expect buffer");
            return -1;
        }
    }
    
    /* Seed expect buffer with screen buffer content instead of raw buffer */
    session->expect_buffer_used = 0;
    if (session->screen) {
        char screen_text[65536];
        size_t screen_len = screen_buffer_get_text(session->screen, screen_text, sizeof(screen_text) - 1);
        
        if (screen_len > 0) {
            size_t copy_size = screen_len;
            if (copy_size > session->expect_buffer_size) {
                copy_size = session->expect_buffer_size;
            }
            
            memcpy(session->expect_buffer, screen_text, copy_size);
            session->expect_buffer[copy_size] = '\0';
            session->expect_buffer_used = copy_size;
            log_debug("Seeded expect buffer with %zu bytes from screen buffer", copy_size);
        } else {
            log_debug("Screen buffer empty, expect buffer not seeded");
        }
    }
    
    session->expect_active = true;
    
    log_debug("Started expect for session %s", session->id);
    return 0;
}

/**
 * Stop expect operation - deactivate expect buffer
 * @param session Session
 */
void session_stop_expect(session_t *session) {
    if (!session) {
        return;
    }
    
    session->expect_active = false;
    session->expect_buffer_used = 0;
    
    log_debug("Stopped expect for session %s", session->id);
}

/**
 * Read from expect buffer (non-destructive peek)
 * @param session Session
 * @param buffer Output buffer
 * @param size Maximum bytes to read
 * @return Number of bytes read
 */
ssize_t session_read_expect_output(session_t *session, char *buffer, size_t size) {
    if (!session || !buffer || size == 0) {
        return -1;
    }
    
    if (!session->expect_active || !session->expect_buffer) {
        log_debug("session_read_expect_output: expect not active or buffer null");
        errno = EINVAL;
        return -1;
    }
    
    /* Return all accumulated data */
    size_t to_copy = (size < session->expect_buffer_used) ? size : session->expect_buffer_used;
    
    log_debug("session_read_expect_output: returning %zu bytes (buffer_used=%zu)", 
              to_copy, session->expect_buffer_used);
    
    if (to_copy > 0) {
        memcpy(buffer, session->expect_buffer, to_copy);
    }
    
    return (ssize_t)to_copy;
}

void session_clear_raw_buffer(session_t *session) {
    if (!session || !session->raw_buffer) {
        return;
    }
    
    session->raw_read_pos = session->raw_write_pos;
}

int session_get_scrollback_size(const session_t *session) {
    if (!session || !session->screen) return 0;
    return session->screen->scrollback_size;
}

int session_get_scrollback_used(const session_t *session) {
    if (!session || !session->screen) return 0;
    return session->screen->scrollback_used;
}

ssize_t session_get_scrollback_line(const session_t *session, int line_index, char *buffer, size_t size) {
    if (!session || !session->screen || !buffer || size == 0) return -1;
    
    const char_cell_t *line = screen_buffer_get_scrollback_line(session->screen, line_index);
    if (!line) return -1;
    
    /* Convert cells to text */
    size_t pos = 0;
    for (int col = 0; col < session->screen->cols && pos < size - 1; col++) {
        char ch = line[col].ch;
        if (ch >= 32 || ch == '\t') {
            buffer[pos++] = ch;
        }
    }
    buffer[pos] = '\0';
    
    return pos;
}
