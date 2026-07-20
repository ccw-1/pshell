#include "pshell/process_spawn.h"
#include "pshell/logger.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#ifndef _GNU_SOURCE
extern int execvpe(const char *file, char *const argv[], char *const envp[]);
#endif

extern char **environ;

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
        // Child process
        
        // Create new session
        if (setsid() < 0) {
            log_error("setsid failed: %s", strerror(errno));
            _exit(1);
        }
        
        // Set controlling terminal
        int slave_fd = pty_get_slave_fd(pty);
        if (slave_fd < 0) {
            log_error("Failed to get slave FD");
            _exit(1);
        }
        
        // Configure slave terminal to raw mode BEFORE redirecting
        struct termios tios;
        if (tcgetattr(slave_fd, &tios) == 0) {
            cfmakeraw(&tios);
            tcsetattr(slave_fd, TCSANOW, &tios);
        }
        
        // Redirect stdin, stdout, stderr to slave PTY
        if (dup2(slave_fd, STDIN_FILENO) < 0 ||
            dup2(slave_fd, STDOUT_FILENO) < 0 ||
            dup2(slave_fd, STDERR_FILENO) < 0) {
            log_error("dup2 failed: %s", strerror(errno));
            _exit(1);
        }
        
        // Close master and slave FDs
        close(pty_get_master_fd(pty));
        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }
        
        // Drop privileges if credentials provided
        if (creds && !drop_privileges(creds)) {
            log_error("Failed to drop privileges");
            _exit(1);
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
        log_error("exec failed: %s", strerror(errno));
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
