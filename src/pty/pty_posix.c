#include "pshell/pty_posix.h"
#include "pshell/logger.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>

#ifdef __APPLE__
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#elif defined(__sun)
#include <stropts.h>
#endif

/* Provide cfmakeraw if not available */
#ifndef __APPLE__
#ifndef __linux__
static void cfmakeraw(struct termios *termios_p) {
    termios_p->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                            | INLCR | IGNCR | ICRNL | IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termios_p->c_cflag &= ~(CSIZE | PARENB);
    termios_p->c_cflag |= CS8;
}
#endif
#endif

pty_t* pty_create(int rows, int cols) {
    pty_t *pty = calloc(1, sizeof(pty_t));
    if (!pty) {
        log_error("Failed to allocate PTY structure");
        return NULL;
    }
    
    pty->rows = rows;
    pty->cols = cols;
    pty->child_pid = -1;
    
    // Open master PTY
#if defined(__APPLE__) || defined(__linux__)
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };
    
    if (openpty(&pty->master_fd, &pty->slave_fd, pty->slave_name, NULL, &ws) < 0) {
        log_error("openpty failed: %s", strerror(errno));
        free(pty);
        return NULL;
    }
#else
    // POSIX fallback
    pty->master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty->master_fd < 0) {
        log_error("posix_openpt failed: %s", strerror(errno));
        free(pty);
        return NULL;
    }
    
    if (grantpt(pty->master_fd) < 0) {
        log_error("grantpt failed: %s", strerror(errno));
        close(pty->master_fd);
        free(pty);
        return NULL;
    }
    
    if (unlockpt(pty->master_fd) < 0) {
        log_error("unlockpt failed: %s", strerror(errno));
        close(pty->master_fd);
        free(pty);
        return NULL;
    }
    
    char *slave_name = ptsname(pty->master_fd);
    if (!slave_name) {
        log_error("ptsname failed: %s", strerror(errno));
        close(pty->master_fd);
        free(pty);
        return NULL;
    }
    
    strncpy(pty->slave_name, slave_name, sizeof(pty->slave_name) - 1);
    
    pty->slave_fd = open(pty->slave_name, O_RDWR | O_NOCTTY);
    if (pty->slave_fd < 0) {
        log_error("Failed to open slave PTY: %s", strerror(errno));
        close(pty->master_fd);
        free(pty);
        return NULL;
    }
    
    // Set window size
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };
    
    if (ioctl(pty->slave_fd, TIOCSWINSZ, &ws) < 0) {
        log_warn("Failed to set window size: %s", strerror(errno));
    }
#endif
    
    // Set non-blocking mode on master
    int flags = fcntl(pty->master_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pty->master_fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    // Save original terminal settings
    if (tcgetattr(pty->master_fd, &pty->orig_termios) < 0) {
        log_warn("tcgetattr failed on master fd %d: %s", pty->master_fd, strerror(errno));
        /* Continue - pty_restore_mode will be a no-op if orig_termios is zeroed */
        memset(&pty->orig_termios, 0, sizeof(pty->orig_termios));
    }
    
    log_debug("Created PTY: master_fd=%d, slave=%s, size=%dx%d",
              pty->master_fd, pty->slave_name, rows, cols);
    
    return pty;
}

void pty_destroy(pty_t *pty) {
    if (!pty) return;
    
    log_debug("Destroying PTY: master_fd=%d", pty->master_fd);
    
    if (pty->master_fd >= 0) {
        close(pty->master_fd);
    }
    
    if (pty->slave_fd >= 0) {
        close(pty->slave_fd);
    }
    
    free(pty);
}

bool pty_resize(pty_t *pty, int rows, int cols) {
    if (!pty || pty->master_fd < 0) return false;
    
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };
    
    if (ioctl(pty->master_fd, TIOCSWINSZ, &ws) < 0) {
        log_error("Failed to resize PTY: %s", strerror(errno));
        return false;
    }
    
    pty->rows = rows;
    pty->cols = cols;
    
    log_debug("Resized PTY to %dx%d", rows, cols);
    return true;
}

int pty_get_master_fd(const pty_t *pty) {
    return pty ? pty->master_fd : -1;
}

int pty_get_slave_fd(const pty_t *pty) {
    return pty ? pty->slave_fd : -1;
}

const char* pty_get_slave_name(const pty_t *pty) {
    return pty ? pty->slave_name : NULL;
}

bool pty_set_raw_mode(pty_t *pty) {
    if (!pty || pty->master_fd < 0) return false;
    
    struct termios raw;
    if (tcgetattr(pty->master_fd, &raw) < 0) {
        log_error("Failed to get terminal attributes: %s", strerror(errno));
        return false;
    }
    
    // Set raw mode
    cfmakeraw(&raw);
    
    if (tcsetattr(pty->master_fd, TCSANOW, &raw) < 0) {
        log_error("Failed to set raw mode: %s", strerror(errno));
        return false;
    }
    
    log_debug("Set PTY to raw mode");
    return true;
}

bool pty_restore_mode(pty_t *pty) {
    if (!pty || pty->master_fd < 0) return false;
    
    if (tcsetattr(pty->master_fd, TCSANOW, &pty->orig_termios) < 0) {
        log_error("Failed to restore terminal mode: %s", strerror(errno));
        return false;
    }
    
    log_debug("Restored PTY terminal mode");
    return true;
}

ssize_t pty_read(pty_t *pty, void *buf, size_t size) {
    if (!pty || pty->master_fd < 0 || !buf) return -1;
    
    ssize_t n = read(pty->master_fd, buf, size);
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        log_error("PTY read error: %s", strerror(errno));
    }
    
    return n;
}

ssize_t pty_write(pty_t *pty, const void *buf, size_t size) {
    if (!pty || pty->master_fd < 0 || !buf) return -1;

    /* Handle partial writes */
    size_t total = 0;
    const char *p = (const char *)buf;
    while (total < size) {
        ssize_t n = write(pty->master_fd, p + total, size - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            log_error("PTY write error: %s", strerror(errno));
            return -1;
        }
        total += (size_t)n;
    }

    return (ssize_t)total;
}

bool pty_has_data(pty_t *pty, int timeout_ms) {
    if (!pty || pty->master_fd < 0) return false;
    
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(pty->master_fd, &read_fds);
    
    struct timeval tv;
    struct timeval *tvp = NULL;
    
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    
    int ret = select(pty->master_fd + 1, &read_fds, NULL, NULL, tvp);
    
    return ret > 0 && FD_ISSET(pty->master_fd, &read_fds);
}

void pty_set_child_pid(pty_t *pty, pid_t pid) {
    if (pty) {
        pty->child_pid = pid;
        log_debug("Set PTY child PID: %d", pid);
    }
}

pid_t pty_get_child_pid(const pty_t *pty) {
    return pty ? pty->child_pid : -1;
}
