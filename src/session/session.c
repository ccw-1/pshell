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
#include <sys/wait.h>
#include <errno.h>

#define MAX_SESSIONS 100

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
};

/* Session manager structure */
struct session_manager {
    session_t sessions[MAX_SESSIONS];
    int max_sessions;
    int session_count;
};

session_manager_t* session_manager_create(int max_sessions) {
    if (max_sessions <= 0 || max_sessions > MAX_SESSIONS) {
        log_error("Invalid max_sessions: %d", max_sessions);
        return NULL;
    }
    
    session_manager_t *mgr = calloc(1, sizeof(session_manager_t));
    if (!mgr) {
        log_error("Failed to allocate session manager");
        return NULL;
    }
    
    mgr->max_sessions = max_sessions;
    mgr->session_count = 0;
    
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

const char* session_manager_create_session(session_manager_t *mgr,
                                           const char *command,
                                           int rows, int cols) {
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
    
    /* Spawn process */
    session->pid = process_spawn(session->pty, session->command, NULL);
    if (session->pid < 0) {
        log_error("Failed to spawn process");
        pty_destroy(session->pty);
        session->pty = NULL;
        return NULL;
    }
    
    /* Set child PID in PTY */
    pty_set_child_pid(session->pty, session->pid);
    
    /* Initialize terminal emulation */
    session->screen = screen_buffer_create(session->rows, session->cols, 1000);
    if (!session->screen) {
        log_error("Failed to create screen buffer");
        kill(session->pid, SIGTERM);
        waitpid(session->pid, NULL, WNOHANG);
        pty_destroy(session->pty);
        session->pty = NULL;
        return NULL;
    }
    vt100_parser_init(&session->parser);
    
    /* Initialize session state */
    session->state = SESSION_STATE_ACTIVE;
    session->created = time(NULL);
    session->last_activity = session->created;
    session->active = true;
    
    mgr->session_count++;
    
    log_info("Created session: id=%s, command=%s, pid=%d, rows=%d, cols=%d",
             session->id, session->command, session->pid, session->rows, session->cols);
    
    return session->id;
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

int session_manager_cleanup(session_manager_t *mgr) {
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
    
    if (!session->pty) {
        log_error("Session PTY not open");
        return -1;
    }
    
    ssize_t n = pty_read(session->pty, buffer, size);
    
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_error("Failed to read from PTY: %s", strerror(errno));
        }
        return -1;
    }
    
    if (n > 0) {
        session_update_activity(session);
        
        /* Parse VT100 sequences and update screen buffer */
        if (session->screen) {
            vt100_sequence_t seq;
            for (ssize_t i = 0; i < n; i++) {
                if (vt100_parser_feed(&session->parser, buffer[i], &seq)) {
                    screen_buffer_process_sequence(session->screen, &seq);
                }
            }
        }
    }
    
    return n;
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
