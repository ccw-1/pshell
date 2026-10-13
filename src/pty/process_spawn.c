#include "pshell/process_spawn.h"
#include "pshell/logger.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>

#ifndef _GNU_SOURCE
extern int execvpe(const char *file, char *const argv[], char *const envp[]);
#endif

extern char **environ;

/* Hard ceiling on the FD scan range when closing inherited descriptors in
 * the child before exec. */
#define MAX_INHERITED_FD 4096

/* Async-signal-safe child-side error report. Between fork() and exec*() the
 * child must not call malloc()-backed functions: log_error() takes the
 * logger mutex and allocates, which can deadlock (the mutex may be held by a
 * thread that was interrupted mid-log) or corrupt the heap. write() to an
 * already-open fd is safe. stderr is redirected to the PTY slave, so the
 * message lands in the session output where it is actually visible. */
static void child_fatal(const char *msg) {
    if (msg) {
        ssize_t ignored = write(STDERR_FILENO, msg, strlen(msg));
        (void)ignored;
    }
    _exit(1);
}

/* Helper: Drop privileges to specified user/group */
static bool drop_privileges(const process_credentials_t *creds) {
    if (!creds || (creds->uid == 0 && creds->gid == 0)) {
        return true;  /* No privilege separation requested */
    }
    
    /* Drop group privileges first */
    if (creds->gid != 0) {
        if (setgid(creds->gid) < 0) {
            log_error("setgid(%d) failed: %s", creds->gid, strerror(errno));
            return false;
        }
        
        /* Clear supplementary groups */
        if (setgroups(0, NULL) < 0) {
            log_error("setgroups failed: %s", strerror(errno));
            return false;
        }
        
        log_debug("Dropped group privileges to GID %d", creds->gid);
    }
    
    /* Drop user privileges */
    if (creds->uid != 0) {
        if (setuid(creds->uid) < 0) {
            log_error("setuid(%d) failed: %s", creds->uid, strerror(errno));
            return false;
        }
        
        log_debug("Dropped user privileges to UID %d", creds->uid);
    }
    
    /* Verify we can't regain privileges */
    if (creds->uid != 0 && setuid(0) == 0) {
        log_error("Failed to permanently drop privileges - can still become root!");
        return false;
    }
    
    if (creds->username) {
        log_info("Running as user: %s (UID=%d, GID=%d)", 
                 creds->username, creds->uid, creds->gid);
    }
    
    return true;
}

int process_parse_command(const char *command, char **argv, int max_args) {
    if (!command || !argv || max_args <= 0) return 0;
    
    char *cmd = strdup(command);
    if (!cmd) return 0;
    
    int argc = 0;
    char *p = cmd;
    bool in_quote = false;
    char quote_char = '\0';
    char *token_start = NULL;
    
    while (*p && argc < max_args - 1) {
        // Skip leading whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        
        token_start = p;
        
        // Handle quoted strings
        if (*p == '"' || *p == '\'') {
            quote_char = *p;
            in_quote = true;
            token_start = ++p;
            
            while (*p && *p != quote_char) p++;
            
            if (*p == quote_char) {
                *p = '\0';
                argv[argc++] = strdup(token_start);
                p++;
            }
            in_quote = false;
        } else {
            // Regular token
            while (*p && !isspace((unsigned char)*p)) p++;
            
            char saved = *p;
            *p = '\0';
            argv[argc++] = strdup(token_start);
            
            if (saved) {
                *p = saved;
                p++;
            }
        }
    }
    
    argv[argc] = NULL;
    free(cmd);
    
    return argc;
}

void process_free_argv(char **argv, int count) {
    if (!argv) return;
    
    for (int i = 0; i < count; i++) {
        free(argv[i]);
    }
}

pid_t process_spawn_argv_with_creds(pty_t *pty, char **argv, char **env, const process_credentials_t *creds) {
    if (!pty || !argv || !argv[0]) {
        log_error("Invalid arguments to process_spawn_argv_with_creds");
        return -1;
    }
    
    pid_t pid = fork();
    
    if (pid < 0) {
        log_error("fork failed: %s", strerror(errno));
        return -1;
    }
    
    if (pid == 0) {
        // Child process. From here until exec*() we must only use
        // async-signal-safe calls: no malloc(), no log_error()/log_debug()
        // (they take the logger mutex and allocate, which can deadlock if the
        // mutex is held by an interrupted thread). Report errors via
        // child_fatal(), which write()s to the now-redirected stderr.
        
        // Create new session. setsid() makes this child a session AND process
        // group leader, so its PGID == its PID. That lets the daemon later
        // signal the whole group (kill(-pid)) to reap grandchildren such as
        // ssh that hold the PTY slave.
        if (setsid() < 0) {
            child_fatal("pshell: setsid failed\n");
        }
        
        // Set controlling terminal
        int slave_fd = pty_get_slave_fd(pty);
        if (slave_fd < 0) {
            child_fatal("pshell: failed to get slave FD\n");
        }
        
        // Make this PTY the controlling terminal. After setsid() the child has
        // no ctty, so without TIOCSCTTY terminal-generated signals (Ctrl-C ->
        // SIGINT, Ctrl-\ -> SIGQUIT, Ctrl-Z -> SIGTSTP) are never delivered.
        // Must happen after setsid() and before the slave fd is closed.
        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
            child_fatal("pshell: TIOCSCTTY failed\n");
        }
        
        // Redirect stdin, stdout, stderr to slave PTY
        if (dup2(slave_fd, STDIN_FILENO) < 0 ||
            dup2(slave_fd, STDOUT_FILENO) < 0 ||
            dup2(slave_fd, STDERR_FILENO) < 0) {
            child_fatal("pshell: dup2 failed\n");
        }
        
        // Close master and slave FDs
        int master_fd = pty_get_master_fd(pty);
        if (master_fd > STDERR_FILENO) {
            close(master_fd);
        }
        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }
        
        // Close every other inherited descriptor before exec. The daemon holds
        // the HTTP listener, the unix socket listener, PTY masters for other
        // sessions, log files, and client connections; leaking those into the
        // child keeps ports and sockets bound after the daemon dies (observed:
        // orphaned children holding the HTTP listener) and pins PTYs open. FDs
        // 0/1/2 are the redirected slave and are preserved.
        long max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd < 0 || max_fd > MAX_INHERITED_FD) {
            max_fd = MAX_INHERITED_FD;
        }
        for (int fd = STDERR_FILENO + 1; fd < (int)max_fd; fd++) {
            close(fd);
        }
        
        // Drop privileges if credentials provided
        if (creds && !drop_privileges(creds)) {
            child_fatal("pshell: failed to drop privileges\n");
        }
        
        // Set TERM environment variable
        setenv("TERM", "xterm-256color", 1);
        
        // Execute command
        if (env) {
            execvpe(argv[0], argv, env);
        } else {
            execvp(argv[0], argv);
        }
        
        // If we get here, exec failed
        {
            char buf[128];
            int n = snprintf(buf, sizeof(buf), "pshell: exec %s failed: %s\n",
                             argv[0], strerror(errno));
            if (n > 0) {
                ssize_t ignored = write(STDERR_FILENO, buf, (size_t)n);
                (void)ignored;
            }
        }
        _exit(127);
    }
    
    // Parent process
    pty_set_child_pid(pty, pid);
    
    if (creds && creds->username) {
        log_info("Spawned process: pid=%d, command=%s, user=%s", pid, argv[0], creds->username);
    } else {
        log_info("Spawned process: pid=%d, command=%s", pid, argv[0]);
    }
    
    return pid;
}

pid_t process_spawn_argv(pty_t *pty, char **argv, char **env) {
    return process_spawn_argv_with_creds(pty, argv, env, NULL);
}

pid_t process_spawn_with_creds(pty_t *pty, const char *command, char **env, const process_credentials_t *creds) {
    if (!pty || !command) {
        log_error("Invalid arguments to process_spawn_with_creds");
        return -1;
    }
    
    char *argv[256];
    int argc = process_parse_command(command, argv, 256);
    
    if (argc == 0) {
        log_error("Failed to parse command: %s", command);
        return -1;
    }
    
    pid_t pid = process_spawn_argv_with_creds(pty, argv, env, creds);
    
    process_free_argv(argv, argc);
    
    return pid;
}

pid_t process_spawn(pty_t *pty, const char *command, char **env) {
    return process_spawn_with_creds(pty, command, env, NULL);
}

bool process_wait(pid_t pid, int *status, int timeout_ms) {
    if (pid <= 0) return false;
    
    if (timeout_ms < 0) {
        // Blocking wait
        int stat;
        pid_t result = waitpid(pid, &stat, 0);
        
        if (result == pid) {
            if (status) *status = stat;
            return true;
        }
        
        return false;
    }
    
    // Non-blocking wait with timeout
    int elapsed = 0;
    int interval = 100; // 100ms intervals
    
    while (elapsed < timeout_ms) {
        int stat;
        pid_t result = waitpid(pid, &stat, WNOHANG);
        
        if (result == pid) {
            if (status) *status = stat;
            return true;
        }
        
        if (result < 0) {
            log_error("waitpid failed: %s", strerror(errno));
            return false;
        }
        
        usleep(interval * 1000);
        elapsed += interval;
    }
    
    return false;
}

bool process_is_running(pid_t pid) {
    if (pid <= 0) return false;
    
    // Send signal 0 to check if process exists
    if (kill(pid, 0) == 0) {
        return true;
    }
    
    // Check if error is ESRCH (no such process)
    return errno != ESRCH;
}

bool process_kill(pid_t pid, int sig) {
    if (pid <= 0) return false;
    
    if (kill(pid, sig) < 0) {
        log_error("Failed to send signal %d to process %d: %s",
                  sig, pid, strerror(errno));
        return false;
    }
    
    log_debug("Sent signal %d to process %d", sig, pid);
    return true;
}
