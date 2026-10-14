#include "pshell/io_multiplexer.h"
#include "pshell/logger.h"
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <errno.h>
#include <unistd.h>

typedef struct io_handler {
    int fd;
    io_event_type_t events;
    io_callback_t callback;
    void *user_data;
    bool active;
} io_handler_t;

struct io_multiplexer {
    io_handler_t handlers[MAX_FDS];
    int count;
    bool running;
    int max_fd;
    
    /* Deferred handler removals/additions during callback dispatch */
    struct {
        int remove_fds[MAX_FDS];
        int remove_count;
        int add_fd;
        io_event_type_t add_events;
        io_callback_t add_callback;
        void *add_user_data;
        bool add_pending;
    } deferred;
};

io_multiplexer_t* io_multiplexer_create(void) {
    io_multiplexer_t *mux = calloc(1, sizeof(io_multiplexer_t));
    if (!mux) {
        log_error("Failed to allocate I/O multiplexer");
        return NULL;
    }
    
    mux->count = 0;
    mux->running = false;
    mux->max_fd = -1;
    mux->deferred.remove_count = 0;
    mux->deferred.add_pending = false;
    
    for (int i = 0; i < MAX_FDS; i++) {
        mux->handlers[i].fd = -1;
        mux->handlers[i].active = false;
    }
    
    log_debug("Created I/O multiplexer");
    return mux;
}

void io_multiplexer_destroy(io_multiplexer_t *mux) {
    if (!mux) return;
    
    log_debug("Destroying I/O multiplexer");
    free(mux);
}

bool io_multiplexer_add(io_multiplexer_t *mux, int fd, io_event_type_t events,
                        io_callback_t callback, void *user_data) {
    if (!mux || fd < 0 || fd >= FD_SETSIZE || !callback) {
        log_error("Invalid arguments to io_multiplexer_add (fd=%d)", fd);
        return false;
    }
    
    if (mux->count >= MAX_FDS) {
        log_error("I/O multiplexer is full");
        return false;
    }
    
    // Check if FD already exists
    for (int i = 0; i < MAX_FDS; i++) {
        if (mux->handlers[i].active && mux->handlers[i].fd == fd) {
            log_warn("FD %d already registered", fd);
            return false;
        }
    }
    
    // Find free slot
    for (int i = 0; i < MAX_FDS; i++) {
        if (!mux->handlers[i].active) {
            mux->handlers[i].fd = fd;
            mux->handlers[i].events = events;
            mux->handlers[i].callback = callback;
            mux->handlers[i].user_data = user_data;
            mux->handlers[i].active = true;
            mux->count++;
            
            if (fd > mux->max_fd) {
                mux->max_fd = fd;
            }
            
            log_debug("Added FD %d to multiplexer (events=%d)", fd, events);
            return true;
        }
    }
    
    return false;
}

/* Internal: immediate removal (not from callback) */
static bool io_multiplexer_remove_immediate(io_multiplexer_t *mux, int fd) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (mux->handlers[i].active && mux->handlers[i].fd == fd) {
            mux->handlers[i].active = false;
            mux->handlers[i].fd = -1;
            mux->count--;
            
            if (fd == mux->max_fd) {
                mux->max_fd = -1;
                for (int j = 0; j < MAX_FDS; j++) {
                    if (mux->handlers[j].active && mux->handlers[j].fd > mux->max_fd) {
                        mux->max_fd = mux->handlers[j].fd;
                    }
                }
            }
            
            log_debug("Removed FD %d from multiplexer", fd);
            return true;
        }
    }
    
    return false;
}

/* Public: removal with deferred support for callback safety */
bool io_multiplexer_remove(io_multiplexer_t *mux, int fd) {
    if (!mux || fd < 0) return false;
    
    /* If called from callback context (mux->running), defer the removal */
    if (mux->running && mux->deferred.remove_count < MAX_FDS) {
        mux->deferred.remove_fds[mux->deferred.remove_count++] = fd;
        log_debug("Deferred removal of FD %d", fd);
        return true;
    }
    
    return io_multiplexer_remove_immediate(mux, fd);
}

/* Process deferred removals after callback dispatch */
static void io_multiplexer_process_deferred(io_multiplexer_t *mux) {
    for (int i = 0; i < mux->deferred.remove_count; i++) {
        int fd = mux->deferred.remove_fds[i];
        io_multiplexer_remove_immediate(mux, fd);
    }
    mux->deferred.remove_count = 0;
    
    if (mux->deferred.add_pending) {
        int fd = mux->deferred.add_fd;
        io_event_type_t events = mux->deferred.add_events;
        io_callback_t callback = mux->deferred.add_callback;
        void *user_data = mux->deferred.add_user_data;
        
        io_multiplexer_add(mux, fd, events, callback, user_data);
        mux->deferred.add_pending = false;
    }
}

bool io_multiplexer_modify(io_multiplexer_t *mux, int fd, io_event_type_t events) {
    if (!mux || fd < 0) return false;
    
    for (int i = 0; i < MAX_FDS; i++) {
        if (mux->handlers[i].active && mux->handlers[i].fd == fd) {
            mux->handlers[i].events = events;
            log_debug("Modified FD %d events to %d", fd, events);
            return true;
        }
    }
    
    return false;
}

int io_multiplexer_run(io_multiplexer_t *mux, int timeout_ms) {
    if (!mux) return -1;
    
    if (mux->count == 0) {
        log_warn("No file descriptors to monitor");
        return 0;
    }
    
    fd_set read_fds, write_fds, error_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);
    
    // Build fd_sets
    for (int i = 0; i < MAX_FDS; i++) {
        if (!mux->handlers[i].active) continue;
        
        int fd = mux->handlers[i].fd;

        if (fd < 0 || fd >= FD_SETSIZE) continue;

        if (mux->handlers[i].events & IO_EVENT_READ) {
            FD_SET(fd, &read_fds);
        }

        if (mux->handlers[i].events & IO_EVENT_WRITE) {
            FD_SET(fd, &write_fds);
        }

        FD_SET(fd, &error_fds);
    }
    
    // Set timeout
    struct timeval tv;
    struct timeval *tvp = NULL;
    
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    
    // Wait for events
    int ret = select(mux->max_fd + 1, &read_fds, &write_fds, &error_fds, tvp);
    
    if (ret < 0) {
        if (errno == EINTR) {
            return 0;  // Interrupted, not an error
        }
        log_error("select failed: %s", strerror(errno));
        return -1;
    }
    
    if (ret == 0) {
        return 0;  // Timeout
    }
    
    /* Mark as running to enable deferred operations */
    mux->running = true;
    
    // Process events
    int events_processed = 0;
    
    for (int i = 0; i < MAX_FDS; i++) {
        if (!mux->handlers[i].active) continue;
        
        int fd = mux->handlers[i].fd;
        io_event_type_t events = 0;
        
        if (FD_ISSET(fd, &read_fds)) {
            events |= IO_EVENT_READ;
        }
        
        if (FD_ISSET(fd, &write_fds)) {
            events |= IO_EVENT_WRITE;
        }
        
        if (FD_ISSET(fd, &error_fds)) {
            events |= IO_EVENT_ERROR;
        }
        
        if (events) {
            mux->handlers[i].callback(fd, events, mux->handlers[i].user_data);
            events_processed++;
        }
    }
    
    mux->running = false;
    
    /* Process deferred removals/additions after callbacks */
    io_multiplexer_process_deferred(mux);
    
    return events_processed;
}

void io_multiplexer_stop(io_multiplexer_t *mux) {
    if (mux) {
        mux->running = false;
        log_debug("Stopped I/O multiplexer");
    }
}

bool io_multiplexer_is_running(const io_multiplexer_t *mux) {
    return mux ? mux->running : false;
}

int io_multiplexer_count(const io_multiplexer_t *mux) {
    return mux ? mux->count : 0;
}
